#include "touch.h"

#include "config.h"

#if ENABLE_TOUCH

namespace touch {
namespace {

// Zone geometry is board-agnostic (works on the CYD's grid at 0,0 and the
// round panel's inset grid alike, since GRID_X0/Y0 and GRID_W fold in).
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
}  // namespace touch

#if defined(DISPLAY_ROUND)
// ---------------------------------------------------------------------------
// CST816T capacitive touch, I2C, shared bus with the onboard QMI8658 IMU.
// Coordinates come back in panel space already (0,0 top-left, same frame the
// GC9A01 draws into at rotation 0) — no raw-range calibration or axis-swap
// needed the way the CYD's resistive XPT2046 requires.
// ---------------------------------------------------------------------------
#include <CST816S.h>

namespace touch {
namespace {

CST816S ts(TOUCH_SDA, TOUCH_SCL, TOUCH_RST, TOUCH_IRQ);
uint32_t lastTapMs = 0;

}  // namespace

void begin() {
  ts.begin();
  Serial.println("[TOUCH] CST816T ready on I2C");
}

void poll() {
  if (!ts.available()) return;

  const uint32_t now = millis();
  if (now - lastTapMs < TP_DEBOUNCE_MS) return;
  lastTapMs = now;

  dispatch(ts.data.x, ts.data.y);
}

}  // namespace touch

#else  // DISPLAY_ROUND
// ---------------------------------------------------------------------------
// XPT2046 resistive touch, on its own SPI bus, separate from the panel.
// ---------------------------------------------------------------------------
#include <SPI.h>
#include <XPT2046_Touchscreen.h>

namespace touch {
namespace {

// The panel is on HSPI (see USE_HSPI_PORT), so the touch controller gets VSPI
// to itself — no bus sharing, no CS juggling.
SPIClass            touchSpi(VSPI);
XPT2046_Touchscreen ts(TP_CS, TP_IRQ);

uint32_t lastTapMs = 0;

// Raw -> landscape screen coords: axes swapped, Y mirrored.
// If a tap lands in the mirrored zone, swap the endpoints of one map() call.
void mapRaw(uint16_t rawX, uint16_t rawY, int16_t& sx, int16_t& sy) {
  sx = map((int32_t)rawY, TP_RAW_MIN, TP_RAW_MAX, 0, SCREEN_W - 1);
  sy = map((int32_t)rawX, TP_RAW_MIN, TP_RAW_MAX, SCREEN_H - 1, 0);
  sx = constrain(sx, 0, SCREEN_W - 1);
  sy = constrain(sy, 0, SCREEN_H - 1);
}

}  // namespace

void begin() {
  touchSpi.begin(TP_CLK, TP_MISO, TP_MOSI, TP_CS);
  ts.begin(touchSpi);
  ts.setRotation(1);
  Serial.println("[TOUCH] XPT2046 ready on VSPI");
}

void poll() {
  if (!ts.touched()) return;

  const uint32_t now = millis();
  if (now - lastTapMs < TP_DEBOUNCE_MS) return;
  lastTapMs = now;

  TS_Point p = ts.getPoint();
  int16_t  x, y;
  mapRaw(p.x, p.y, x, y);
  dispatch(x, y);
}

}  // namespace touch

#endif  // DISPLAY_ROUND

#else  // ENABLE_TOUCH

namespace touch {
void begin() { Serial.println("[TOUCH] disabled (ENABLE_TOUCH=0)"); }
void poll() {}
}  // namespace touch

#endif
