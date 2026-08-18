#include "touch.h"

#include "config.h"

#if ENABLE_TOUCH

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

void dispatch(int16_t x, int16_t y) {
  if (y < GRID_H) {
    const bool right  = x >= CELL_W;
    const bool bottom = y >= CELL_H;
    const char* corner = bottom ? (right ? "RR" : "RL")
                                : (right ? "FR" : "FL");
    Serial.printf("[TOUCH] corner %s (%d,%d)\n", corner, x, y);
    return;
  }

  if (y < STATUS_Y) {
    if (x < CELL_W) Serial.printf("[TOUCH] preset DOWN (%d,%d)\n", x, y);
    else            Serial.printf("[TOUCH] preset UP (%d,%d)\n", x, y);
    return;
  }

  Serial.printf("[TOUCH] status bar (%d,%d)\n", x, y);
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

#else  // ENABLE_TOUCH

namespace touch {
void begin() { Serial.println("[TOUCH] disabled (ENABLE_TOUCH=0)"); }
void poll() {}
}  // namespace touch

#endif
