// Standalone bench test: sweep a 28BYJ-48 stepper (via its ULN2003AN driver
// board) back and forth between 0deg and 180deg, dwelling 5s at each end.
// Not part of the AirLift firmware -- throwaway rig to confirm the motor,
// driver board, and wiring are good before this goes into anything real.
//
// Driver board has 4 inputs (IN1-IN4) that want digital HIGH/LOW from the
// MCU; the ULN2003AN itself does the current sinking for the coils.
//
// Pin choice: the ask was GPIO35-38, but on an ESP32-S3-WROOM-1 those pins
// are wired internally to Octal PSRAM on modules that have it (N8R8/N16R8
// etc.) -- driving them as plain GPIO on such a module corrupts memory
// access. Since the exact module variant wasn't known, this uses GPIO4-7
// instead, which are safe general-purpose pins on every S3-WROOM-1 variant
// (not strapping pins, not flash/PSRAM, not USB D+/D-). Rewire IN1-IN4 to
// these if you built for pins 35-38 already.
#include <Arduino.h>

constexpr int kPinIn1 = 4;
constexpr int kPinIn2 = 5;
constexpr int kPinIn3 = 6;
constexpr int kPinIn4 = 7;
constexpr int kCoilPins[4] = {kPinIn1, kPinIn2, kPinIn3, kPinIn4};

// 8-step half-step drive sequence for a 28BYJ-48 through a ULN2003 board.
// Half-stepping (vs. 4-step full-step) roughly doubles resolution and runs
// noticeably smoother/quieter at the same speed.
constexpr uint8_t kHalfStepSeq[8][4] = {
  {1, 0, 0, 0},
  {1, 1, 0, 0},
  {0, 1, 0, 0},
  {0, 1, 1, 0},
  {0, 0, 1, 0},
  {0, 0, 1, 1},
  {0, 0, 0, 1},
  {1, 0, 0, 1},
};

// 28BYJ-48 output-shaft steps per revolution in half-step mode. The gearbox
// ratio (63.68395:1) isn't a whole number, so this is the commonly-used
// rounded figure -- good enough for a bench sweep test, not precision
// positioning.
constexpr uint32_t kStepsPerRev = 4096;
constexpr uint32_t kStepsPerHalfRev = kStepsPerRev / 2;  // 180 degrees

// Delay between half-steps. 28BYJ-48 stalls if driven too fast; 1ms/step
// roughly doubles the original 2ms speed (~2s for a 180deg sweep) and is
// still comfortably inside typical half-step limits with adequate power.
// Push lower (down toward ~0.6-0.8ms) if it still tracks cleanly with the
// separate 5V supply; back off if it starts stalling/skipping again.
constexpr uint32_t kStepDelayMs = 1;

constexpr uint32_t kDwellMs = 5000;

static uint8_t s_seqIndex = 0;

static void applyStep(uint8_t seqIndex)
{
  for (int i = 0; i < 4; ++i) {
    digitalWrite(kCoilPins[i], kHalfStepSeq[seqIndex][i]);
  }
}

// direction: +1 = forward (index increases), -1 = reverse
static void stepMotor(uint32_t steps, int8_t direction)
{
  for (uint32_t i = 0; i < steps; ++i) {
    s_seqIndex = (s_seqIndex + direction + 8) % 8;
    applyStep(s_seqIndex);
    delay(kStepDelayMs);
  }
}

// De-energize all coils. Holding a half-step energized between moves just
// wastes current and heats the driver/motor for no benefit on a bench test.
static void coilsOff()
{
  for (int pin : kCoilPins) digitalWrite(pin, LOW);
}

void setup()
{
  Serial.begin(115200);
  delay(200);
  for (int pin : kCoilPins) pinMode(pin, OUTPUT);
  coilsOff();
  Serial.println();
  Serial.println("Stepper sweep test up. IN1-IN4 on GPIO 4,5,6,7.");
}

void loop()
{
  Serial.println("-> 180deg");
  stepMotor(kStepsPerHalfRev, +1);
  coilsOff();
  Serial.println("holding 5s");
  delay(kDwellMs);

  Serial.println("-> 0deg");
  stepMotor(kStepsPerHalfRev, -1);
  coilsOff();
  Serial.println("holding 5s");
  delay(kDwellMs);
}
