// ---------------------------------------------------------------------------
// ST7701 480x480 round panel over the ESP32-S3's RGB-parallel LCD peripheral
// (round21 board only). TFT_eSPI has no working ST7701-RGB support, so this
// uses Arduino_GFX as a thin drawing-API wrapper around the raw ESP-IDF
// esp_lcd_panel_rgb driver.
//
// Panel bring-up (TCA9554 power/reset, the ST7701's byte-for-byte-ported
// init registers, RGB peripheral config) lives in panel_round21.cpp/.h,
// shared with the LVGL backend (display_round21_lvgl.cpp) so that
// unverified-on-glass register/timing code exists exactly once. This file
// only owns the Arduino_GFX drawing wrapper and the gauge/menu rendering.
// ---------------------------------------------------------------------------
#include "display.h"

#include <Arduino_GFX_Library.h>
#include <esp_lcd_panel_ops.h>

#include "config.h"
#include "panel_round21.h"

namespace display {
namespace {

// Thin Arduino_GFX subclass: routes the library's drawing primitives to
// esp_lcd_panel_draw_bitmap() against the RGB panel's PSRAM framebuffer
// (panel_round21::handle(), brought up by panel_round21::begin()).
// (The upstream demo also builds an Arduino_ESP32RGBPanel bus object and
// calls its begin() here — that object is never touched by these overrides,
// the RGB panel handle is the one actually doing the drawing, so it's
// dropped to avoid configuring the RGB peripheral through two separate
// paths.)
class Arduino_ST7701 : public Arduino_GFX {
 public:
  Arduino_ST7701(int16_t w, int16_t h) : Arduino_GFX(w, h) {}

  bool begin(int32_t speed = GFX_NOT_DEFINED) override {
    (void)speed;
    return panel_round21::handle() != nullptr;
  }

 protected:
  void writePixelPreclipped(int16_t x, int16_t y, uint16_t color) override {
    esp_lcd_panel_draw_bitmap(panel_round21::handle(), x, y, x + 1, y + 1, &color);
  }

  void writeFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color) override {
    if (h < 0) { y += h + 1; h = -h; }
    uint16_t* line = (uint16_t*)malloc(h * sizeof(uint16_t));
    if (!line) return;
    for (int16_t i = 0; i < h; i++) line[i] = color;
    esp_lcd_panel_draw_bitmap(panel_round21::handle(), x, y, x + 1, y + h, line);
    free(line);
  }

  void writeFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) override {
    if (w < 0) { x += w + 1; w = -w; }
    uint16_t* line = (uint16_t*)malloc(w * sizeof(uint16_t));
    if (!line) return;
    for (int16_t i = 0; i < w; i++) line[i] = color;
    esp_lcd_panel_draw_bitmap(panel_round21::handle(), x, y, x + w, y + 1, line);
    free(line);
  }

  void writeFillRectPreclipped(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) override {
    if (w < 0) { x += w + 1; w = -w; }
    if (h < 0) { y += h + 1; h = -h; }
    uint16_t* buf = (uint16_t*)malloc((size_t)w * h * sizeof(uint16_t));
    if (!buf) return;
    for (int32_t i = 0; i < (int32_t)w * h; i++) buf[i] = color;
    esp_lcd_panel_draw_bitmap(panel_round21::handle(), x, y, x + w, y + h, buf);
    free(buf);
  }
};

Arduino_ST7701* gfx = nullptr;

// --- corner geometry, shared with touch_round21.cpp's dispatch() zones -----
struct Cell {
  const char* label;
  int16_t     x;
  int16_t     y;
};

const Cell kCells[4] = {
  {"FL", GRID_X0,          GRID_Y0},
  {"FR", GRID_X0 + CELL_W, GRID_Y0},
  {"RL", GRID_X0,          GRID_Y0 + CELL_H},
  {"RR", GRID_X0 + CELL_W, GRID_Y0 + CELL_H},
};

// --- diff cache: only repaint a box whose rendered text actually changed ---
bool    primed       = false;
char    lastCorner[4][8] = {};
char    lastTank[8]      = {};
char    lastPreset[12]   = {};
uint8_t lastStatus        = 0xFF;
bool    lastSignalOk      = true;

void formatPsi(float psi, char* out, size_t n) {
  if (isnan(psi) || psi < -0.5f) {
    snprintf(out, n, "---");
  } else {
    snprintf(out, n, "%.0f", psi);
  }
}

void statusText(uint8_t status, const char** text, uint16_t* colour) {
  switch (status) {
    case AIRLIFT_RAISING:  *text = "RAISING";   *colour = COL_RAISING;  break;
    case AIRLIFT_LOWERING: *text = "LOWERING";  *colour = COL_LOWERING; break;
    case AIRLIFT_IDLE:     *text = "IDLE";      *colour = COL_IDLE;     break;
    default:               *text = "NO SIGNAL"; *colour = COL_NOSIGNAL; break;
  }
}

// Adafruit_GFX has no centred-text datum like TFT_eSPI's MC_DATUM, so this
// measures the string and offsets the cursor by hand.
void drawCentered(int16_t cx, int16_t cy, const char* text, uint8_t size,
                   uint16_t colour, uint16_t bg) {
  gfx->setTextSize(size);
  gfx->setTextColor(colour, bg);
  int16_t  x1, y1;
  uint16_t w, h;
  gfx->getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  gfx->setCursor(cx - (int16_t)(w / 2) - x1, cy - (int16_t)(h / 2) - y1);
  gfx->print(text);
}

// Fill-then-print is fine here (no sprite composition needed): this is a
// PSRAM framebuffer scanned continuously by the RGB peripheral, not an SPI
// panel — writing into it is just RAM stores, not a slow visible transfer.
void drawValue(int16_t x, int16_t y, int16_t w, int16_t h, const char* text,
               uint16_t colour, uint8_t size) {
  gfx->fillRect(x, y, w, h, COL_BG);
  drawCentered(x + w / 2, y + h / 2, text, size, colour, COL_BG);
}

}  // namespace

void begin() {
  panel_round21::begin();

  gfx = new Arduino_ST7701(SCREEN_W, SCREEN_H);
  gfx->begin();
  // Arduino_GFX's setRotation() is a pure software coordinate transform
  // applied in its own high-level draw functions before they ever reach our
  // overridden Preclipped primitives -- it works regardless of whether the
  // RGB peripheral/panel itself has any hardware rotation support (it
  // doesn't matter here either way). menu::begin() (called before
  // display::begin() in main.cpp) has already loaded the persisted value.
  gfx->setRotation(menu::rotate180() ? (DISPLAY_ROTATION ^ 2) : DISPLAY_ROTATION);
  gfx->fillScreen(COL_BG);
}

void setBacklight(uint8_t percent) { panel_round21::setBacklight(percent); }

void splash() {
  gfx->fillScreen(COL_BG);

  drawCentered(SCREEN_W / 2, SPLASH_Y_HANDLE, SPLASH_HANDLE,
               SPLASH_HANDLE_TEXTSIZE, COL_VALUE, COL_BG);

  gfx->drawFastHLine(SCREEN_W / 2 - SPLASH_RULE_HALFLEN, SPLASH_Y_RULE,
                      SPLASH_RULE_HALFLEN * 2, COL_DIVIDER);

  drawCentered(SCREEN_W / 2, SPLASH_Y_PRODUCT, "AIRLIFT V2", 3, COL_PRESET, COL_BG);
  drawCentered(SCREEN_W / 2, SPLASH_Y_VERSION, SLAVE_FW_VERSION, 2, COL_LABEL, COL_BG);

  for (uint16_t t = 0; t <= SPLASH_FADE_MS; t += 20) {
    setBacklight((BACKLIGHT_PCT * t) / SPLASH_FADE_MS);
    delay(20);
  }
  setBacklight(BACKLIGHT_PCT);

  delay(SPLASH_HOLD_MS);
}

void drawStaticLayout() {
  gfx->fillScreen(COL_BG);

  gfx->drawFastVLine(GRID_X0 + CELL_W - 1, GRID_Y0, GRID_H, COL_DIVIDER);
  gfx->drawFastHLine(GRID_X0, GRID_Y0 + CELL_H - 1, GRID_W, COL_DIVIDER);

  gfx->drawFastHLine(GRID_X0, STRIP_Y - 1, GRID_W, COL_DIVIDER);
  gfx->drawFastVLine(GRID_X0 + CELL_W - 1, STRIP_Y, STRIP_H, COL_DIVIDER);
  gfx->drawFastHLine(GRID_X0, STATUS_Y - 1, GRID_W, COL_DIVIDER);

  for (const Cell& c : kCells) {
    drawCentered(c.x + CELL_W / 2, c.y + 20, c.label, LABEL_TEXTSIZE, COL_LABEL, COL_BG);
  }

  drawCentered(GRID_X0 + CELL_W / 2, STRIP_Y + 20, "TANK", LABEL_TEXTSIZE, COL_TANK, COL_BG);
  drawCentered(GRID_X0 + CELL_W + CELL_W / 2, STRIP_Y + 20, "PRESET", LABEL_TEXTSIZE, COL_PRESET, COL_BG);

  primed = false;
  // Restores the Settings screen's live brightness (menu.cpp), not the
  // hardcoded default — this also runs every time the menu closes back to
  // the gauge, and resetting to default there would silently undo a
  // brightness change the moment you left Settings.
  setBacklight(menu::currentBacklightPct());
}

void update(const AirLiftData& d, bool signalOk) {
  const bool     forceAll    = !primed || (signalOk != lastSignalOk);
  const uint16_t valueColour = signalOk ? COL_VALUE : COL_STALE;
  const uint16_t tankColour  = signalOk ? COL_TANK  : COL_STALE;

  const float corners[4] = {d.fl, d.fr, d.rl, d.rr};
  for (uint8_t i = 0; i < 4; i++) {
    char buf[8];
    formatPsi(corners[i], buf, sizeof(buf));
    if (forceAll || strcmp(buf, lastCorner[i]) != 0) {
      drawValue(kCells[i].x, kCells[i].y + CELL_VALUE_TOP, CELL_W, CELL_VALUE_H,
                buf, valueColour, VALUE_TEXTSIZE_CORNER);
      strncpy(lastCorner[i], buf, sizeof(lastCorner[i]) - 1);
      lastCorner[i][sizeof(lastCorner[i]) - 1] = '\0';
    }
  }

  char tank[8];
  formatPsi(d.tank, tank, sizeof(tank));
  if (forceAll || strcmp(tank, lastTank) != 0) {
    char withUnit[12];
    snprintf(withUnit, sizeof(withUnit), "%s PSI", tank);
    drawValue(GRID_X0, STRIP_VALUE_TOP, CELL_W, STRIP_VALUE_H, withUnit,
              tankColour, VALUE_TEXTSIZE_STRIP);
    strncpy(lastTank, tank, sizeof(lastTank) - 1);
    lastTank[sizeof(lastTank) - 1] = '\0';
  }

  const char* preset = presetName(d.preset);
  if (forceAll || strcmp(preset, lastPreset) != 0) {
    drawValue(GRID_X0 + CELL_W, STRIP_VALUE_TOP, CELL_W, STRIP_VALUE_H, preset,
              signalOk ? COL_PRESET : COL_STALE, VALUE_TEXTSIZE_STRIP);
    strncpy(lastPreset, preset, sizeof(lastPreset) - 1);
    lastPreset[sizeof(lastPreset) - 1] = '\0';
  }

  const uint8_t status = signalOk ? d.status : AIRLIFT_NO_SIGNAL;
  if (forceAll || status != lastStatus) {
    const char* text;
    uint16_t    colour;
    statusText(status, &text, &colour);
    drawValue(GRID_X0, STATUS_Y, GRID_W, STATUS_H, text, colour, STATUS_TEXTSIZE);
    lastStatus = status;
  }

  lastSignalOk = signalOk;
  primed       = true;
}

namespace {

// --- menu diff cache ---------------------------------------------------
// The menu only redraws on a button press, not a 10 Hz telemetry stream, so
// (unlike update() above) a whole-screen redraw on every change is cheap
// enough here too. MANUAL_ACTIVE is the exception: it tracks live pressures
// instead of items[], since it updates continuously while a button is held
// rather than only on navigation.
bool       menuPrimed      = false;
menu::Mode lastMenuMode    = menu::Mode::GAUGE;
uint8_t    lastMenuCursor  = 0xFF;
uint8_t    lastMenuCount   = 0xFF;
char       lastMenuItems[8][16] = {};
char       lastLeftPsi[6]  = {};
char       lastRightPsi[6] = {};
uint8_t    lastDirection   = 0xFF;

bool menuViewChanged(const menu::View& v) {
  if (!menuPrimed) return true;
  if (v.mode != lastMenuMode || v.cursor != lastMenuCursor ||
      v.itemCount != lastMenuCount) {
    return true;
  }
  if (v.hasLivePressures) {
    return strcmp(v.leftPsi, lastLeftPsi) != 0 ||
           strcmp(v.rightPsi, lastRightPsi) != 0 ||
           v.direction != lastDirection;
  }
  for (uint8_t i = 0; i < v.itemCount; i++) {
    if (strcmp(v.items[i], lastMenuItems[i]) != 0) return true;
  }
  return false;
}

void cacheMenuView(const menu::View& v) {
  menuPrimed     = true;
  lastMenuMode   = v.mode;
  lastMenuCursor = v.cursor;
  lastMenuCount  = v.itemCount;
  for (uint8_t i = 0; i < v.itemCount && i < 8; i++) {
    strncpy(lastMenuItems[i], v.items[i], sizeof(lastMenuItems[i]) - 1);
    lastMenuItems[i][sizeof(lastMenuItems[i]) - 1] = '\0';
  }
  strncpy(lastLeftPsi, v.leftPsi, sizeof(lastLeftPsi) - 1);
  lastLeftPsi[sizeof(lastLeftPsi) - 1] = '\0';
  strncpy(lastRightPsi, v.rightPsi, sizeof(lastRightPsi) - 1);
  lastRightPsi[sizeof(lastRightPsi) - 1] = '\0';
  lastDirection = v.direction;
}

// FRONT/REAR live control: two big pressure numbers either side of a
// direction triangle, colour-coded (green raising / amber lowering / grey
// idle) — this panel has 4x the other TFT board's pixel budget, so the
// triangle and numbers go correspondingly bigger.
void drawManualActive(const menu::View& view) {
  gfx->fillScreen(COL_BG);
  drawCentered(SCREEN_W / 2, GRID_Y0 + 20, view.title, 3, COL_PRESET, COL_BG);

  const uint16_t valueColour = (view.direction == AIRLIFT_RAISING)  ? COL_RAISING
                              : (view.direction == AIRLIFT_LOWERING) ? COL_LOWERING
                                                                       : COL_LABEL;

  const int16_t top    = GRID_Y0 + 48;
  const int16_t bottom = STATUS_Y + STATUS_H;
  const int16_t midY   = (top + bottom) / 2;

  drawCentered(GRID_X0 + CELL_W / 2, midY, view.leftPsi, VALUE_TEXTSIZE_CORNER,
               valueColour, COL_BG);
  drawCentered(GRID_X0 + CELL_W + CELL_W / 2, midY, view.rightPsi,
               VALUE_TEXTSIZE_CORNER, valueColour, COL_BG);

  const int16_t cx   = GRID_X0 + CELL_W;
  const int16_t triH = 24;
  if (view.direction == AIRLIFT_RAISING) {
    gfx->fillTriangle(cx, midY - triH, cx - triH, midY + triH, cx + triH, midY + triH, COL_RAISING);
  } else if (view.direction == AIRLIFT_LOWERING) {
    gfx->fillTriangle(cx, midY + triH, cx - triH, midY - triH, cx + triH, midY - triH, COL_LOWERING);
  } else {
    gfx->fillRect(cx - triH, midY - 2, triH * 2, 4, COL_LABEL);
  }

  drawCentered(SCREEN_W / 2, bottom - 24, "HOLD +/-", 2, COL_LABEL, COL_BG);
}

}  // namespace

void setRotate180(bool on) {
  gfx->setRotation(on ? (DISPLAY_ROTATION ^ 2) : DISPLAY_ROTATION);
  // Whatever's already on the glass is now upside down relative to the new
  // orientation -- force both diff caches to repaint in full next time,
  // same trick main.cpp's GAUGE<->MENU transition already relies on.
  primed     = false;
  menuPrimed = false;
}

void drawMenu(const menu::View& view, bool force) {
  if (!force && !menuViewChanged(view)) return;
  cacheMenuView(view);

  if (view.mode == menu::Mode::MANUAL_ACTIVE) {
    drawManualActive(view);
    return;
  }

  gfx->fillScreen(COL_BG);
  drawCentered(SCREEN_W / 2, GRID_Y0 + 20, view.title, 3, COL_PRESET, COL_BG);

  const int16_t top    = GRID_Y0 + 48;
  const int16_t bottom = STATUS_Y + STATUS_H;
  const uint8_t rows   = view.itemCount ? view.itemCount : 1;
  const int16_t rowH   = (bottom - top) / rows;

  for (uint8_t i = 0; i < view.itemCount; i++) {
    const bool    selected = (i == view.cursor);
    const int16_t y        = top + i * rowH;
    if (selected) gfx->fillRect(GRID_X0, y, GRID_W, rowH, COL_DIVIDER);
    drawCentered(GRID_X0 + GRID_W / 2, y + rowH / 2, view.items[i], LABEL_TEXTSIZE,
                 selected ? COL_VALUE : COL_LABEL, selected ? COL_DIVIDER : COL_BG);
  }
}

}  // namespace display
