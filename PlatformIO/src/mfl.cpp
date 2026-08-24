#include "mfl.h"

#include "defs.h"
#include "globals.h"

namespace {

// Set true if the signal reaches pinMflSignal through an inverting stage.
//
// FALSE on this hardware. The bench build (MFL-FINDINGS.md) fed the signal
// through a 4N35 optocoupler, which inverts. The MFSW board has no such stage:
// the MFL wire comes in on connector pin 7 and reaches GPIO39 through the aux
// light-sense resistive network, which is non-inverting, so the pin follows the
// car line directly.
//
// Confirmed by mflDiagTick()'s phase measurement, which times both levels
// independently of this flag: the HIGH phase carries the >5ms inter-message
// idle gap (measured 10809us) and bottoms out at the ~168us bit width (measured
// 166us), while LOW never exceeds ~508us. Mark = HIGH = not inverted. With this
// set true the framing looked for the gap on LOW, never found one, and
// s_haveSync never armed -- edges climbed but frames stayed at 0.
constexpr bool kSignalInverted = false;

constexpr uint32_t kBitThresholdUs   = 250;   // 0 vs 1 discriminator
constexpr uint32_t kFrameGapUs       = 5000;  // idle gap => new message
constexpr uint8_t  kPulsesPerMessage = 8;
constexpr uint8_t  kConfirmFrames    = 2;     // consecutive matching frames before trusting a change

portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

// ISR-owned state.
volatile uint32_t s_risingAtUs = 0;
volatile uint32_t s_pulseWidths[kPulsesPerMessage];
volatile uint8_t  s_pulseCount = 0;
volatile bool     s_haveSync   = false;
volatile bool     s_frameReady = false;
volatile uint32_t s_frameWidths[kPulsesPerMessage];

// Bring-up diagnostics — see mflDiagTick() / mfl.h.
volatile uint32_t s_isrEdgeCount = 0;
volatile uint32_t s_frameCount   = 0;
volatile int32_t  s_lastRawValue = -1;

// Polarity-independent phase statistics. These measure how long the pin spends
// HIGH and how long it spends LOW using the *raw* level, before kSignalInverted
// is applied, so a capture can answer two questions the edge counter alone
// can't on a front-end whose behaviour isn't known (the MFSW board's aux
// light-sense input):
//   - Which phase carries the >5ms inter-message idle gap? That phase is the
//     "mark", and it fixes what kSignalInverted has to be.
//   - Do the bit pulses survive the front-end at all? Real bits land at ~168us
//     and ~341us; an RC-smothered or noise-only line won't show that split.
// Reset every log interval so they describe the last window, not all of time.
volatile uint32_t s_phaseLastUs  = 0;
volatile bool     s_phaseLastHigh = false;
volatile bool     s_phaseStarted = false;
volatile uint32_t s_highMaxUs = 0;
volatile uint32_t s_highMinUs = UINT32_MAX;
volatile uint32_t s_highCount = 0;
volatile uint32_t s_lowMaxUs  = 0;
volatile uint32_t s_lowMinUs  = UINT32_MAX;
volatile uint32_t s_lowCount  = 0;

void IRAM_ATTR mflIsr() {
  s_isrEdgeCount++;

  const uint32_t now = micros();
  const bool rawHigh = digitalRead(pinMflSignal) == HIGH;

  // Time the phase that just ended (its level is the one held *before* this
  // edge, i.e. the opposite of what we just read).
  if (s_phaseStarted) {
    const uint32_t dur = now - s_phaseLastUs;
    if (s_phaseLastHigh) {
      if (dur > s_highMaxUs) s_highMaxUs = dur;
      if (dur < s_highMinUs) s_highMinUs = dur;
      s_highCount++;
    } else {
      if (dur > s_lowMaxUs) s_lowMaxUs = dur;
      if (dur < s_lowMinUs) s_lowMinUs = dur;
      s_lowCount++;
    }
  }
  s_phaseStarted  = true;
  s_phaseLastUs   = now;
  s_phaseLastHigh = rawHigh;

  bool isMark = rawHigh;
  if (kSignalInverted) isMark = !isMark;

  if (isMark) {
    s_risingAtUs = now;
    return;
  }

  const uint32_t width = now - s_risingAtUs;

  if (width > kFrameGapUs) {
    // Idle gap just ended -> this HIGH period was the inter-message gap
    // itself, not a data bit. Re-sync here, discarding whatever was
    // mid-flight.
    s_pulseCount = 0;
    s_haveSync = true;
    return;
  }

  if (!s_haveSync) return;

  if (s_pulseCount < kPulsesPerMessage) {
    s_pulseWidths[s_pulseCount++] = width;
  }

  if (s_pulseCount >= kPulsesPerMessage && !s_frameReady) {
    portENTER_CRITICAL_ISR(&s_mux);
    for (uint8_t i = 0; i < kPulsesPerMessage; ++i) s_frameWidths[i] = s_pulseWidths[i];
    s_frameReady = true;
    portEXIT_CRITICAL_ISR(&s_mux);
    s_frameCount++;
    s_pulseCount = 0;
    s_haveSync = false;   // require a fresh gap before the next message
  }
}

MflButtons decodeFrame(const uint32_t widths[kPulsesPerMessage]) {
  int value = 0;
  for (uint8_t i = 0; i < 7; ++i) {
    // Short pulse = 1 (inverted relative to pazi88's original — see
    // MFL-FINDINGS.md "Bit polarity" for why).
    if (widths[i] < kBitThresholdUs) value |= 1 << (7 - i);
  }
  s_lastRawValue = value;

  MflButtons s;
  // value == 0 is idle and trivially satisfies all four masks — without this
  // check idle would decode as all four buttons held at once.
  if (value == 0) return s;

  const bool on    = (value & 110) == 0;
  const bool io    = (value & 218) == 0;
  const bool plus  = (value & 182) == 0;
  const bool minus = (value & 252) == 0;

  // Only trust a decode where exactly one mask matched — this wheel's "SET"
  // button decodes as pazi88's "on" mask (see MFL-FINDINGS.md button-mapping
  // table), so it is named `set` here, not `on`.
  const uint8_t matches = (uint8_t)on + io + plus + minus;
  if (matches != 1) return MflButtons{};

  s.set   = on;
  s.io    = io;
  s.plus  = plus;
  s.minus = minus;
  return s;
}

// Debounced across kConfirmFrames consecutive matching decodes before it is
// promoted to the value mflRead() returns.
MflButtons s_confirmed;
MflButtons s_pending;
uint8_t    s_matchCount = 0;

}  // namespace

void mflInit() {
  pinMode(pinMflSignal, INPUT);
  attachInterrupt(digitalPinToInterrupt(pinMflSignal), mflIsr, CHANGE);
  DEBUG_MFL("decoder attached on GPIO%d", pinMflSignal);
}

MflButtons mflRead() {
  if (s_frameReady) {
    uint32_t widths[kPulsesPerMessage];
    portENTER_CRITICAL(&s_mux);
    for (uint8_t i = 0; i < kPulsesPerMessage; ++i) widths[i] = s_frameWidths[i];
    s_frameReady = false;
    portEXIT_CRITICAL(&s_mux);

    const MflButtons decoded = decodeFrame(widths);
    if (decoded == s_pending) {
      if (s_matchCount < 255) s_matchCount++;
    } else {
      s_pending = decoded;
      s_matchCount = 1;
    }

    if (s_matchCount >= kConfirmFrames && s_confirmed != s_pending) {
      s_confirmed = s_pending;
      DEBUG_MFL("button change: plus=%d minus=%d set=%d io=%d",
                s_confirmed.plus, s_confirmed.minus, s_confirmed.set, s_confirmed.io);
    }
  }

  return s_confirmed;
}

void mflDiagTick() {
  const uint32_t now = millis();

  static uint32_t s_lastDebugMs = 0;
  if (now - s_lastDebugMs >= 1000) {
    s_lastDebugMs = now;
    DEBUG_MFL("diag: edges=%lu frames=%lu lastRaw=%ld confirmed(+=%d -=%d set=%d io=%d)",
              (unsigned long)s_isrEdgeCount, (unsigned long)s_frameCount,
              (long)s_lastRawValue, s_confirmed.plus, s_confirmed.minus,
              s_confirmed.set, s_confirmed.io);

    // Snapshot-and-reset the phase stats atomically so the numbers describe
    // the interval just elapsed.
    uint32_t hMax, hMin, hN, lMax, lMin, lN;
    portENTER_CRITICAL(&s_mux);
    hMax = s_highMaxUs; hMin = s_highMinUs; hN = s_highCount;
    lMax = s_lowMaxUs;  lMin = s_lowMinUs;  lN = s_lowCount;
    s_highMaxUs = 0; s_highMinUs = UINT32_MAX; s_highCount = 0;
    s_lowMaxUs  = 0; s_lowMinUs  = UINT32_MAX; s_lowCount  = 0;
    portEXIT_CRITICAL(&s_mux);

    if (hMin == UINT32_MAX) hMin = 0;
    if (lMin == UINT32_MAX) lMin = 0;
    DEBUG_MFL("phase: level=%s HIGH n=%lu %lu..%luus | LOW n=%lu %lu..%luus"
              "  (mark phase should reach >%luus)",
              digitalRead(pinMflSignal) == HIGH ? "HIGH" : "LOW",
              (unsigned long)hN, (unsigned long)hMin, (unsigned long)hMax,
              (unsigned long)lN, (unsigned long)lMin, (unsigned long)lMax,
              (unsigned long)kFrameGapUs);
  }

  // Also mirrored into the persisted event log (visible over the web UI,
  // no serial/USB connection needed) at a slower 5s cadence -- this is
  // bring-up-only instrumentation, and the log ring buffer only holds 64
  // entries (see globals.h's kLogLineCount), so this would otherwise crowd
  // out real events within about a minute.
  static uint32_t s_lastWebLogMs = 0;
  if (now - s_lastWebLogMs >= 5000) {
    s_lastWebLogMs = now;
    logLine("MFL diag: edges=%lu frames=%lu lastRaw=%ld pin=GPIO%d",
            (unsigned long)s_isrEdgeCount, (unsigned long)s_frameCount,
            (long)s_lastRawValue, pinMflSignal);
  }
}
