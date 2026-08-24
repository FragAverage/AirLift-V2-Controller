#include "mfl.h"

#include "defs.h"
#include "globals.h"

namespace {

// Set true if the signal reaches pinMflSignal through an inverting stage —
// this board's optocoupler inverts (car HIGH -> LED on -> phototransistor on
// -> GPIO pulled LOW), same as the bench build in MFL-FINDINGS.md.
constexpr bool kSignalInverted = true;

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

void IRAM_ATTR mflIsr() {
  s_isrEdgeCount++;

  const uint32_t now = micros();
  bool isMark = digitalRead(pinMflSignal) == HIGH;
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
