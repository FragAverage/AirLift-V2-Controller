// ---------------------------------------------------------------------------
// CST820 capacitive touch (round21 board only), I2C, reset on the TCA9554 IO
// expander. Register map and read sequence ported from Waveshare's own
// Touch_CST820.cpp/.h demo for this board.
// ---------------------------------------------------------------------------
#include "touch.h"

#include "config.h"

#if ENABLE_TOUCH

#include <Wire.h>

#include "tca9554.h"

namespace touch {
namespace {

constexpr uint8_t kRegGestureId    = 0x01;  // 6-byte read: gesture, points, x_hi, x_lo, y_hi, y_lo
constexpr uint8_t kRegDisAutoSleep = 0xFE;

uint32_t lastTapMs = 0;

void writeReg(uint8_t reg, uint8_t data) {
  Wire.beginTransmission(TOUCH_I2C_ADDR);
  Wire.write(reg);
  Wire.write(data);
  Wire.endTransmission();
}

// Zone geometry is the same 2x2-grid layout as the other boards' dispatch().
void dispatch(int16_t x, int16_t y) {
  if (y >= GRID_Y0 && y < GRID_Y0 + GRID_H) {
    const bool right  = x >= GRID_X0 + CELL_W;
    const bool bottom = y >= GRID_Y0 + CELL_H;
    const char* corner = bottom ? (right ? "RR" : "RL")
                                : (right ? "FR" : "FL");
    Serial.printf("[TOUCH] corner %s (%d,%d)\n", corner, x, y);
    return;
  }

  if (y >= STRIP_Y && y < STATUS_Y) {
    if (x < GRID_X0 + CELL_W) Serial.printf("[TOUCH] preset DOWN (%d,%d)\n", x, y);
    else                      Serial.printf("[TOUCH] preset UP (%d,%d)\n", x, y);
    return;
  }

  Serial.printf("[TOUCH] status bar (%d,%d)\n", x, y);
}

}  // namespace

void begin() {
  pinMode(TOUCH_IRQ, INPUT_PULLUP);

  // Reset via the IO expander, then disable auto-sleep so a stationary
  // finger doesn't drop out mid-hold.
  tca9554::setPin(TOUCH_RESET_EXIO, false);
  delay(10);
  tca9554::setPin(TOUCH_RESET_EXIO, true);
  delay(50);
  writeReg(kRegDisAutoSleep, 0xFF);

  Serial.println("[TOUCH] CST820 ready on I2C");
}

void poll() {
  uint8_t buf[6];
  Wire.beginTransmission(TOUCH_I2C_ADDR);
  Wire.write(kRegGestureId);
  if (Wire.endTransmission(true) != 0) return;
  if (Wire.requestFrom((int)TOUCH_I2C_ADDR, 6) != 6) return;
  for (uint8_t& b : buf) b = Wire.read();

  const uint8_t points = buf[1];
  if (points == 0) return;

  const uint32_t now = millis();
  if (now - lastTapMs < TP_DEBOUNCE_MS) return;
  lastTapMs = now;

  const int16_t x = ((buf[2] & 0x0F) << 8) | buf[3];
  const int16_t y = ((buf[4] & 0x0F) << 8) | buf[5];
  dispatch(x, y);
}

}  // namespace touch

#else  // ENABLE_TOUCH

namespace touch {
void begin() { Serial.println("[TOUCH] disabled (ENABLE_TOUCH=0)"); }
void poll() {}
}  // namespace touch

#endif
