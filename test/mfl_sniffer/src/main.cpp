// Standalone bench sniffer for the BMW MFL cruise-control single-wire signal
// (E46/E39-era "Datalink MFL" line, DME connector pin 27 / wheel connector pin 8).
//
// Not part of the AirLift firmware — this is a throwaway rig to confirm the
// signal exists on a given car and matches the known protocol before wiring
// it into anything permanent. Decode logic ported from pazi88's MFL.ino/.h
// (Speeduino-M5x-PCBs, m52tu-m54_PnP/Serial3toBMWcan), which reads the same
// signal on M52TU/M54 (E46-era) BMWs in production ECU swaps:
//   https://github.com/pazi88/Speeduino-M5x-PCBs/blob/master/m52tu-m54_PnP/Serial3toBMWcan/MFL.ino
//
// Protocol (from that source, unverified against this specific car until you
// run this and check the raw dump):
//   - Idle-high line. A message is 8 pulses, repeating ~every 10 ms.
//   - Each pulse's HIGH duration encodes a bit: ~168us = 0, ~336us = 1
//     (threshold at the midpoint, 250us).
//   - Pulse 8 (index 7) is a toggle/counter bit, not part of the value.
//   - A HIGH duration > 5ms marks the idle gap between messages (frame sync).
//   - The 7-bit value is checked against bitmasks to decode button state:
//       CRUISE_ON    = (value & 110) == 0
//       CRUISE_IO    = (value & 218) == 0   // on/off toggle
//       CRUISE_PLUS  = (value & 182) == 0   // RES/+
//       CRUISE_MINUS = (value & 252) == 0   // SET/-
//
// Wiring: see README.md in this folder for the input-protection circuit.
// Do NOT connect the car wire straight to the ESP32 pin.

#include <Arduino.h>

// Wired to D18 on the bench build — a plain digital GPIO, fine as an input
// here since nothing else on the board claims it in this sketch.
constexpr int kMflPin = 18;

// Set true if the signal reaches this pin through an inverting stage — the
// 4N35 optocoupler on this build inverts (car HIGH -> LED on -> phototransistor
// on -> GPIO pulled LOW), same as the NPN transistor alternative in README.md.
// Leave false only for a non-inverting interface (plain divider/clamp).
constexpr bool kSignalInverted = true;

constexpr uint32_t kBitThresholdUs   = 250;   // 0 vs 1 discriminator
constexpr uint32_t kFrameGapUs       = 5000;  // idle gap => new message
constexpr uint8_t  kPulsesPerMessage = 8;
constexpr uint8_t  kConfirmFrames    = 2;     // consecutive matching frames before we trust a change

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

// ISR-owned state
static volatile uint32_t s_risingAtUs = 0;
static volatile uint32_t s_pulseWidths[kPulsesPerMessage];
static volatile uint8_t  s_pulseCount = 0;
static volatile bool     s_haveSync   = false;
static volatile bool     s_frameReady = false;
static volatile uint32_t s_frameWidths[kPulsesPerMessage];

void IRAM_ATTR mflIsr()
{
  const uint32_t now = micros();
  bool isMark = digitalRead(kMflPin) == HIGH;
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
    s_pulseCount = 0;
    s_haveSync = false;   // require a fresh gap before the next message
  }
}

struct ButtonState {
  bool on = false;
  bool io = false;
  bool plus = false;
  bool minus = false;
  bool operator==(const ButtonState& o) const {
    return on == o.on && io == o.io && plus == o.plus && minus == o.minus;
  }
};

static ButtonState decode(const uint32_t widths[kPulsesPerMessage])
{
  int value = 0;
  for (uint8_t i = 0; i < 7; ++i) {
    // Bit polarity is flipped relative to pazi88's original (short pulse =
    // 1 here), because the 4N35 inverting stage times the complementary
    // phase of each bit cell. Confirmed by hand-decoding real captures on
    // this bench build — long-pulse=1 never produced a valid mask match,
    // short-pulse=1 did, on two independently-captured button presses.
    if (widths[i] < kBitThresholdUs) value |= 1 << (7 - i);
  }
  ButtonState s;
  // value == 0 is idle (confirmed on every no-button capture) and trivially
  // satisfies all four masks (0 & anything == 0) — without this check idle
  // would decode as all four buttons "pressed" simultaneously.
  if (value == 0) return s;
  s.on    = (value & 110) == 0;
  s.io    = (value & 218) == 0;
  s.plus  = (value & 182) == 0;
  s.minus = (value & 252) == 0;
  return s;
}

void setup()
{
  Serial.begin(115200);
  delay(200);
  pinMode(kMflPin, INPUT);
  attachInterrupt(digitalPinToInterrupt(kMflPin), mflIsr, CHANGE);
  Serial.println();
  Serial.println("MFL sniffer up. Waiting for frames on GPIO " + String(kMflPin) + "...");
  Serial.println("Columns: raw pulse widths (us) x8  |  decoded value  |  buttons");
}

void loop()
{
  // Pin-mapping sanity check: prints the raw level every 300ms regardless of
  // whether any framed message has been decoded. Jump the wire in kMflPin's
  // hole straight to 3V3, then to GND, and confirm this actually follows —
  // if it never changes, kMflPin doesn't point at the physical pin you think
  // it does (silkscreen-vs-GPIO mismatch on some board clones).
  static uint32_t lastLevelPrintMs = 0;
  if (millis() - lastLevelPrintMs >= 300) {
    lastLevelPrintMs = millis();
    Serial.printf("[pin check] GPIO%d raw level = %s\r\n", kMflPin, digitalRead(kMflPin) == HIGH ? "HIGH" : "LOW");
  }

  if (!s_frameReady) return;

  uint32_t widths[kPulsesPerMessage];
  portENTER_CRITICAL(&s_mux);
  for (uint8_t i = 0; i < kPulsesPerMessage; ++i) widths[i] = s_frameWidths[i];
  s_frameReady = false;
  portEXIT_CRITICAL(&s_mux);

  Serial.print("[");
  for (uint8_t i = 0; i < kPulsesPerMessage; ++i) {
    Serial.print(widths[i]);
    if (i < kPulsesPerMessage - 1) Serial.print(',');
  }
  Serial.print("] ");

  const ButtonState decoded = decode(widths);

  static ButtonState lastConfirmed;
  static ButtonState pending;
  static uint8_t matchCount = 0;

  if (decoded == pending) {
    if (matchCount < 255) matchCount++;
  } else {
    pending = decoded;
    matchCount = 1;
  }

  Serial.printf("ON=%d IO=%d PLUS=%d MINUS=%d",
                decoded.on, decoded.io, decoded.plus, decoded.minus);

  if (matchCount >= kConfirmFrames && !(pending == lastConfirmed)) {
    lastConfirmed = pending;
    Serial.print("   <-- CONFIRMED CHANGE");
  }
  Serial.println();
}
