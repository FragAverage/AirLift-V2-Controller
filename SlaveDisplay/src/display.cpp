#include "display.h"

#include <TFT_eSPI.h>

#include "config.h"

namespace display {
namespace {

TFT_eSPI tft = TFT_eSPI();

// ---------------------------------------------------------------------------
// Off-screen buffers, one per box geometry.
//
// Painting a value as fillRect-then-drawString puts a blank black box on the
// glass for as long as the text takes to render, which reads as a flicker on
// every update. Composing into a sprite and pushing it means the old value is
// overwritten by the new one in a single SPI burst — no intermediate state.
//
// Allocated once at boot and reused, so nothing churns the heap at runtime.
// Sizes come from config.h (CELL_W/CELL_VALUE_H etc.) and differ per board —
// CYD 16bpp: 156x58 + 156x28 + 320x22 -> ~41k; round: 72x28 + 72x22 + 152x22
// -> ~11k.
// ---------------------------------------------------------------------------
TFT_eSprite sprCorner = TFT_eSprite(&tft);
TFT_eSprite sprStrip  = TFT_eSprite(&tft);
TFT_eSprite sprStatus = TFT_eSprite(&tft);
bool spritesReady = false;

// --- corner geometry -------------------------------------------------------
struct Cell {
  const char* label;
  int16_t     x;
  int16_t     y;
};

// Index order matches the diff cache below: FL, FR, RL, RR.
// Offsets by (GRID_X0, GRID_Y0) so the grid can be inset from the framebuffer
// origin — 0 on the CYD (edge-to-edge), non-zero on the round panel (inset to
// clear the circular bezel; see config.h).
const Cell kCells[4] = {
  {"FL", GRID_X0,          GRID_Y0},
  {"FR", GRID_X0 + CELL_W, GRID_Y0},
  {"RL", GRID_X0,          GRID_Y0 + CELL_H},
  {"RR", GRID_X0 + CELL_W, GRID_Y0 + CELL_H},
};

// --- diff cache ------------------------------------------------------------
// Nothing is repainted unless the rendered *text* would differ, so a stream of
// jittery floats that all format to "138" costs zero SPI traffic.
bool    primed = false;             // false until the first update() paints
char    lastCorner[4][8] = {};
char    lastTank[8]      = {};
char    lastPreset[12]   = {};
uint8_t lastStatus       = 0xFF;
bool    lastSignalOk     = true;

// Whole psi only. Sub-psi resolution is noise on an air suspension gauge and
// churns the last digit on every packet, which reads as a flickering display.
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

// Compose the value centred in its box off-screen, then push the finished box
// to the glass in one go. Falls back to a direct clear-and-draw if the sprite
// could not be allocated — a flickering gauge beats a blank one.
void drawValue(TFT_eSprite& spr, int16_t x, int16_t y, int16_t w, int16_t h,
               const char* text, uint16_t colour, uint8_t font) {
  if (!spritesReady) {
    tft.fillRect(x, y, w, h, COL_BG);
    tft.setTextColor(colour, COL_BG);
    tft.setTextDatum(MC_DATUM);
    tft.drawString(text, x + w / 2, y + h / 2, font);
    return;
  }

  spr.fillSprite(COL_BG);
  spr.setTextColor(colour, COL_BG);
  spr.setTextDatum(MC_DATUM);
  spr.drawString(text, w / 2, h / 2, font);
  spr.pushSprite(x, y);
}

bool createSprites() {
  sprCorner.setColorDepth(16);
  sprStrip.setColorDepth(16);
  sprStatus.setColorDepth(16);

  const bool ok =
      sprCorner.createSprite(CELL_W - 4, CELL_VALUE_H) != nullptr &&
      sprStrip.createSprite(CELL_W - 4, STRIP_VALUE_H) != nullptr &&
      sprStatus.createSprite(GRID_W, STATUS_H) != nullptr;

  if (!ok) {
    sprCorner.deleteSprite();
    sprStrip.deleteSprite();
    sprStatus.deleteSprite();
  }
  return ok;
}

}  // namespace

void begin() {
  // Backlight starts dark so the panel's power-on garbage never reaches the
  // driver's eye — splash() (or drawStaticLayout) brings it up once there is
  // something worth looking at.
  setBacklight(0);

#ifdef BOARD_LCD147
  // Onboard addressable RGB LED powers up showing a colour until explicitly
  // written to; this firmware doesn't use it, so turn it off immediately.
  neopixelWrite(RGB_LED_PIN, 0, 0, 0);
#endif

  tft.init();
  // TFT_eSPI rotation IDs 0-3 come in landscape/portrait pairs 180 degrees
  // apart (0<->2, 1<->3) -- XOR-ing the board's base TFT_ROTATION with 2
  // flips to the other member of its pair, i.e. the same physical mounting
  // orientation upside down. menu::begin() (called before display::begin()
  // in main.cpp) has already loaded the persisted value by this point.
  tft.setRotation(menu::rotate180() ? (TFT_ROTATION ^ 2) : TFT_ROTATION);
  tft.fillScreen(COL_BG);
  tft.setTextDatum(TL_DATUM);

  spritesReady = createSprites();
  if (!spritesReady) {
    Serial.println("[TFT] sprite alloc failed — falling back to direct draw");
  }
}

void setBacklight(uint8_t percent) {
#ifdef BOARD_LCD147
  // GPIO46 (this board's LCD_BL per Waveshare's own pin table) is the one
  // genuinely input-only GPIO on the ESP32-S3 -- it cannot drive PWM or any
  // output at all, confirmed by ledcAttach/digitalWrite both erroring at
  // runtime ("IO 46 is not set as GPIO") despite the panel itself working
  // fine. The backlight is evidently always-on in hardware on this board;
  // nothing to do here.
  (void)percent;
  return;
#else
  if (percent > 100) percent = 100;
  const uint32_t maxDuty = (1u << BACKLIGHT_PWM_BITS) - 1;
  uint32_t duty = (maxDuty * percent) / 100;
  // The panel's BL line is active high on this board; invert if TFT_BACKLIGHT_ON
  // is ever changed to LOW.
#if TFT_BACKLIGHT_ON == LOW
  duty = maxDuty - duty;
#endif

  // Attach on first use only — re-running ledcSetup on every fade step would
  // glitch the output.
  static bool attached = false;
  if (!attached) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcAttach(TFT_BL, BACKLIGHT_PWM_FREQ, BACKLIGHT_PWM_BITS);
#else
    ledcSetup(BACKLIGHT_PWM_CH, BACKLIGHT_PWM_FREQ, BACKLIGHT_PWM_BITS);
    ledcAttachPin(TFT_BL, BACKLIGHT_PWM_CH);
#endif
    attached = true;
  }

#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(TFT_BL, duty);
#else
  ledcWrite(BACKLIGHT_PWM_CH, duty);
#endif
#endif  // BOARD_LCD147
}

void splash() {
  tft.fillScreen(COL_BG);
  tft.setTextDatum(MC_DATUM);

  tft.setTextColor(COL_VALUE, COL_BG);
  tft.setTextSize(SPLASH_HANDLE_SIZE);
  tft.drawString(SPLASH_HANDLE, SCREEN_W / 2, SPLASH_Y_HANDLE, SPLASH_HANDLE_FONT);
  tft.setTextSize(1);

  tft.drawFastHLine(SCREEN_W / 2 - SPLASH_RULE_HALFLEN, SPLASH_Y_RULE,
                     SPLASH_RULE_HALFLEN * 2, COL_DIVIDER);

  tft.setTextColor(COL_PRESET, COL_BG);
  tft.drawString("AIRLIFT V2", SCREEN_W / 2, SPLASH_Y_PRODUCT, 4);

  tft.setTextColor(COL_LABEL, COL_BG);
  tft.drawString(SLAVE_FW_VERSION, SCREEN_W / 2, SPLASH_Y_VERSION, 2);

  // Fade up rather than snapping on — the panel is already painted, so this
  // reveals the splash instead of flashing the driver.
  for (uint16_t t = 0; t <= SPLASH_FADE_MS; t += 20) {
    setBacklight((BACKLIGHT_PCT * t) / SPLASH_FADE_MS);
    delay(20);
  }
  setBacklight(BACKLIGHT_PCT);

  delay(SPLASH_HOLD_MS);
}

void drawStaticLayout() {
  tft.fillScreen(COL_BG);

  // 2x2 grid rules
  tft.drawFastVLine(GRID_X0 + CELL_W - 1, GRID_Y0, GRID_H, COL_DIVIDER);
  tft.drawFastHLine(GRID_X0, GRID_Y0 + CELL_H - 1, GRID_W, COL_DIVIDER);

  // strip + status bar rules
  tft.drawFastHLine(GRID_X0, STRIP_Y - 1, GRID_W, COL_DIVIDER);
  tft.drawFastVLine(GRID_X0 + CELL_W - 1, STRIP_Y, STRIP_H, COL_DIVIDER);
  tft.drawFastHLine(GRID_X0, STATUS_Y - 1, GRID_W, COL_DIVIDER);

  // corner captions
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(COL_LABEL, COL_BG);
  for (const Cell& c : kCells) {
    tft.drawString(c.label, c.x + 6, c.y + 4, 2);
  }

  // tank + preset captions
  tft.setTextColor(COL_TANK, COL_BG);
  tft.drawString("TANK", GRID_X0 + 6, STRIP_Y + 4, 2);
  tft.setTextColor(COL_PRESET, COL_BG);
  tft.drawString("PRESET", GRID_X0 + CELL_W + 6, STRIP_Y + 4, 2);

  primed = false;  // force every value to paint on the next update()

  // Belt and braces: begin() leaves the backlight dark, so make sure it is up
  // even if splash() was skipped. Restores the Settings screen's live
  // brightness (menu.cpp), not the hardcoded default — this also runs every
  // time the menu closes back to the gauge, and resetting to default there
  // would silently undo a brightness change the moment you left Settings.
  setBacklight(menu::currentBacklightPct());
}

void update(const AirLiftData& d, bool signalOk) {
  // A lost/regained link re-colours every pressure, so treat it like a full
  // repaint trigger rather than diffing each value against a stale colour.
  const bool forceAll = !primed || (signalOk != lastSignalOk);
  const uint16_t valueColour = signalOk ? COL_VALUE : COL_STALE;
  const uint16_t tankColour  = signalOk ? COL_TANK  : COL_STALE;

  const float corners[4] = {d.fl, d.fr, d.rl, d.rr};
  for (uint8_t i = 0; i < 4; i++) {
    char buf[8];
    formatPsi(corners[i], buf, sizeof(buf));
    if (forceAll || strcmp(buf, lastCorner[i]) != 0) {
      drawValue(sprCorner, kCells[i].x + 2, kCells[i].y + CELL_VALUE_TOP,
                CELL_W - 4, CELL_VALUE_H, buf, valueColour, VALUE_FONT_CORNER);
      strncpy(lastCorner[i], buf, sizeof(lastCorner[i]) - 1);
      lastCorner[i][sizeof(lastCorner[i]) - 1] = '\0';
    }
  }

  char tank[8];
  formatPsi(d.tank, tank, sizeof(tank));
  if (forceAll || strcmp(tank, lastTank) != 0) {
    char withUnit[12];
    snprintf(withUnit, sizeof(withUnit), "%s PSI", tank);
    drawValue(sprStrip, GRID_X0 + 2, STRIP_VALUE_TOP, CELL_W - 4, STRIP_VALUE_H,
              withUnit, tankColour, VALUE_FONT_STRIP);
    strncpy(lastTank, tank, sizeof(lastTank) - 1);
    lastTank[sizeof(lastTank) - 1] = '\0';
  }

  const char* preset = presetName(d.preset);
  if (forceAll || strcmp(preset, lastPreset) != 0) {
    drawValue(sprStrip, GRID_X0 + CELL_W + 2, STRIP_VALUE_TOP, CELL_W - 4,
              STRIP_VALUE_H, preset, signalOk ? COL_PRESET : COL_STALE,
              VALUE_FONT_STRIP);
    strncpy(lastPreset, preset, sizeof(lastPreset) - 1);
    lastPreset[sizeof(lastPreset) - 1] = '\0';
  }

  const uint8_t status = signalOk ? d.status : AIRLIFT_NO_SIGNAL;
  if (forceAll || status != lastStatus) {
    const char* text;
    uint16_t    colour;
    statusText(status, &text, &colour);
    drawValue(sprStatus, GRID_X0, STATUS_Y, GRID_W, STATUS_H, text, colour, 2);
    lastStatus = status;
  }

  lastSignalOk = signalOk;
  primed       = true;
}

namespace {

// --- menu diff cache ---------------------------------------------------
// The menu only redraws on a button press, not a 10 Hz telemetry stream, so
// (unlike update() above) a whole-screen redraw on every change is cheap
// enough — no per-row sprite diffing needed. MANUAL_ACTIVE is the exception:
// it tracks live pressures instead of items[], since it updates continuously
// while a button is held rather than only on navigation.
bool       menuPrimed     = false;
menu::Mode lastMenuMode   = menu::Mode::GAUGE;
uint8_t    lastMenuCursor = 0xFF;
uint8_t    lastMenuCount  = 0xFF;
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
// idle) the same way the gauge's status bar already colour-codes — the one
// thing this board can do for "fancy" that the monochrome OLED can't.
void drawManualActive(const menu::View& view) {
  tft.fillScreen(COL_BG);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(COL_PRESET, COL_BG);
  tft.drawString(view.title, GRID_X0 + GRID_W / 2, GRID_Y0 + 16, 4);

  const uint16_t valueColour = (view.direction == AIRLIFT_RAISING)  ? COL_RAISING
                              : (view.direction == AIRLIFT_LOWERING) ? COL_LOWERING
                                                                       : COL_LABEL;

  const int16_t top    = GRID_Y0 + 36;
  const int16_t bottom = STATUS_Y + STATUS_H;
  const int16_t midY   = (top + bottom) / 2;

  tft.setTextColor(valueColour, COL_BG);
  tft.drawString(view.leftPsi, GRID_X0 + CELL_W / 2, midY, VALUE_FONT_CORNER);
  tft.drawString(view.rightPsi, GRID_X0 + CELL_W + CELL_W / 2, midY, VALUE_FONT_CORNER);

  const int16_t cx    = GRID_X0 + CELL_W;
  const int16_t triH  = 10;
  if (view.direction == AIRLIFT_RAISING) {
    tft.fillTriangle(cx, midY - triH, cx - triH, midY + triH, cx + triH, midY + triH, COL_RAISING);
  } else if (view.direction == AIRLIFT_LOWERING) {
    tft.fillTriangle(cx, midY + triH, cx - triH, midY - triH, cx + triH, midY - triH, COL_LOWERING);
  } else {
    tft.drawFastHLine(cx - triH, midY, triH * 2, COL_LABEL);
  }

  tft.setTextColor(COL_LABEL, COL_BG);
  tft.drawString("HOLD +/-", GRID_X0 + GRID_W / 2, bottom - 12, 2);
}

}  // namespace

void setRotate180(bool on) {
  tft.setRotation(on ? (TFT_ROTATION ^ 2) : TFT_ROTATION);
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

  tft.fillScreen(COL_BG);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(COL_PRESET, COL_BG);
  tft.drawString(view.title, GRID_X0 + GRID_W / 2, GRID_Y0 + 16, 4);

  const int16_t top    = GRID_Y0 + 36;
  const int16_t bottom = STATUS_Y + STATUS_H;
  const uint8_t rows   = view.itemCount ? view.itemCount : 1;
  const int16_t rowH   = (bottom - top) / rows;

  for (uint8_t i = 0; i < view.itemCount; i++) {
    const bool    selected = (i == view.cursor);
    const int16_t y        = top + i * rowH;
    if (selected) tft.fillRect(GRID_X0, y, GRID_W, rowH, COL_DIVIDER);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(selected ? COL_VALUE : COL_LABEL,
                      selected ? COL_DIVIDER : COL_BG);
    tft.drawString(view.items[i], GRID_X0 + GRID_W / 2, y + rowH / 2, 2);
  }
}

}  // namespace display
