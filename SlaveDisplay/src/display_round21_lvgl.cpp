// ---------------------------------------------------------------------------
// round21_lvgl env: same ST7701 480x480 RGB-parallel panel as env:round21
// (panel_round21.cpp does the shared bring-up), rendered with LVGL widgets
// instead of Arduino_GFX draw calls. Same on-screen layout as
// display_round21.cpp on purpose -- see platformio.ini's round21_lvgl
// comment and README.md's "The round21 board" section.
//
// main.cpp has no idea LVGL exists (it's identical across every board env),
// so this file is responsible for driving LVGL's own timing itself --
// pumpLvgl() is called at the top of every update()/drawMenu(), the only two
// entry points main.cpp's loop() calls at its own ~10ms cadence.
// ---------------------------------------------------------------------------
#include "display.h"

#include <esp_lcd_panel_ops.h>
#include <lvgl.h>

#include "config.h"
#include "dseg7_font.h"
#include "panel_round21.h"

namespace display {
namespace {

// --- LVGL <-> panel plumbing ------------------------------------------------

// Partial-render draw buffer height, in scanlines. 40 lines * 480px *
// 2 bytes/px * 2 buffers ~= 77KB of plain heap (not PSRAM -- this is a
// short-lived render scratchpad LVGL fills and this file immediately copies
// out via flushCb, not the panel's own framebuffer, which stays in PSRAM per
// panel_round21.cpp/rgbPanelInit()'s fb_in_psram=true).
constexpr int kDrawBufLines = 40;

lv_display_t* lvDisp = nullptr;

// Settings -> ROTATE 180 (see menu.cpp), applied entirely in flushCb() below
// rather than via any panel/peripheral rotation feature. Unlike TFT_eSPI's
// MADCTL-backed setRotation() (display.cpp, display_lcd147_lvgl.cpp) or
// Arduino_GFX's pure-software one (display_round21.cpp), the ESP32-S3's RGB
// LCD peripheral this board's panel_round21.cpp drives has no
// software-selectable scan-direction register of its own to lean on
// (esp_lcd_panel_mirror()/swap_xy() are built for panel *controller* chips
// addressed like ST7789's MADCTL, not a fixed-timing parallel scanout), and
// re-sending the ST7701's own MADCTL over its one-time init-only link at
// runtime isn't worth risking against this codebase's own "do not hand-edit
// the register values" rule for that byte-for-byte-ported sequence (see
// panel_round21.cpp). So this flips the rect and reverses the pixel buffer
// ourselves, entirely in software, before it ever reaches the panel.
bool s_rotate180 = false;

void lvLogCb(lv_log_level_t level, const char* buf) {
  (void)level;
  Serial.print("[LVGL] ");
  Serial.println(buf);
}

// LVGL hands flushCb a raw RGB565 rect (LV_COLOR_DEPTH=16 in lv_conf.h) --
// the same pixel format and the same esp_lcd_panel_draw_bitmap() call the
// Arduino_GFX backend already uses successfully, just one whole flushed
// rect at a time instead of Arduino_GFX's per-pixel/per-line calls.
void flushCb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
  if (!s_rotate180) {
    esp_lcd_panel_draw_bitmap(panel_round21::handle(), area->x1, area->y1,
                               area->x2 + 1, area->y2 + 1, px_map);
    lv_display_flush_ready(disp);
    return;
  }

  // A 180-degree rotation of a rectangular RGB565 buffer is just "reverse
  // the whole flat pixel array" -- the pixel that belongs at local (row r,
  // col c) after rotating ends up at (h-1-r, w-1-c), and row-major index
  // (h-1-r)*w + (w-1-c) is exactly (w*h-1) - (r*w+c). Reversing in place is
  // safe: px_map is LVGL's own draw buffer, already handed to us to flush,
  // and nothing else reads it again before the next render pass refills it.
  const int32_t w = area->x2 - area->x1 + 1;
  const int32_t h = area->y2 - area->y1 + 1;
  uint16_t*     px = (uint16_t*)px_map;
  for (int32_t i = 0, j = w * h - 1; i < j; i++, j--) {
    const uint16_t tmp = px[i];
    px[i] = px[j];
    px[j] = tmp;
  }

  esp_lcd_panel_draw_bitmap(panel_round21::handle(),
                             SCREEN_W - area->x2 - 1, SCREEN_H - area->y2 - 1,
                             SCREEN_W - area->x1,     SCREEN_H - area->y1,
                             px_map);
  lv_display_flush_ready(disp);
}

// main.cpp's loop() calls update()/drawMenu() every ~10ms with no idea LVGL
// exists underneath -- this is what actually advances LVGL's tick and lets
// its timer/animation/redraw machinery run each pass.
void pumpLvgl() {
  static uint32_t lastMs = millis();
  const uint32_t now = millis();
  lv_tick_inc(now - lastMs);
  lastMs = now;
  lv_timer_handler();
}

// COL_* in config.h are raw RGB565 (shared with the Arduino_GFX backend,
// which has no lv_color_t concept at all) -- expand each channel to 8 bits
// and hand it to lv_color_make() rather than poking lv_color_t's internal
// field layout directly, so this doesn't depend on LVGL's struct packing.
lv_color_t lvColor(uint16_t rgb565) {
  const uint8_t r5 = (rgb565 >> 11) & 0x1F;
  const uint8_t g6 = (rgb565 >> 5) & 0x3F;
  const uint8_t b5 = rgb565 & 0x1F;
  return lv_color_make((uint8_t)((r5 * 527 + 23) >> 6),
                        (uint8_t)((g6 * 259 + 33) >> 6),
                        (uint8_t)((b5 * 527 + 23) >> 6));
}

// Maps config.h's *_FONT_PX_* macros to the nearest LVGL bundled font
// enabled in lv_conf.h (LV_FONT_MONTSERRAT_20/28/48) -- LVGL fonts are fixed
// sizes, not a scale factor like Arduino_GFX's setTextSize().
const lv_font_t* fontPx(int px) {
  if (px >= 48) return &lv_font_montserrat_48;
  if (px >= 28) return &lv_font_montserrat_28;
  return &lv_font_montserrat_20;
}

// --- widget helpers ----------------------------------------------------

lv_obj_t* mkPlainObj(lv_obj_t* parent) {
  lv_obj_t* o = lv_obj_create(parent);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(o, 0, 0);
  lv_obj_set_style_radius(o, 0, 0);
  lv_obj_set_style_pad_all(o, 0, 0);
  return o;
}

lv_obj_t* mkDivider(lv_obj_t* parent, int16_t x, int16_t y, int16_t w, int16_t h) {
  lv_obj_t* o = mkPlainObj(parent);
  lv_obj_set_pos(o, x, y);
  lv_obj_set_size(o, w, h);
  lv_obj_set_style_bg_color(o, lvColor(COL_DIVIDER), 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  return o;
}

// The LVGL equivalent of drawValue()'s "fillRect the box, then centre the
// text in it" -- Adafruit_GFX/Arduino_GFX has no vertical-centre text datum
// either, flex-centering a label inside a fixed-size box is LVGL's version
// of the same trick.
lv_obj_t* mkCell(lv_obj_t* parent, int16_t x, int16_t y, int16_t w, int16_t h) {
  lv_obj_t* o = mkPlainObj(parent);
  lv_obj_set_pos(o, x, y);
  lv_obj_set_size(o, w, h);
  lv_obj_set_style_bg_color(o, lvColor(COL_BG), 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  lv_obj_set_flex_flow(o, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(o, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  // Small gap for cells that hold more than one label side by side (the
  // tank digit + "PSI" unit label) -- a no-op for every other cell, which
  // only ever has one child.
  lv_obj_set_style_pad_column(o, 4, 0);
  return o;
}

// Letter-spacing bump on every label -- old VDO/BMW-style dash legends and
// OBC digit readouts are printed with generous tracking, not set tight like
// a modern UI font; this is a cheap, uniform way to lean that direction
// without a second non-numeric font.
lv_obj_t* mkLabel(lv_obj_t* cell, const lv_font_t* font, uint16_t colour) {
  lv_obj_t* l = lv_label_create(cell);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, lvColor(colour), 0);
  lv_obj_set_style_text_letter_space(l, 1, 0);
  lv_label_set_text(l, "");
  return l;
}

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

// --- gauge screen, built once, reused for the life of the program ----------
bool     gaugeBuilt = false;
lv_obj_t* gaugeScreen = nullptr;
lv_obj_t* cornerValueLabel[4] = {};
lv_obj_t* tankValueLabel   = nullptr;
lv_obj_t* tankUnitLabel    = nullptr;
lv_obj_t* presetValueLabel = nullptr;
lv_obj_t* statusLabel      = nullptr;

// --- diff cache: only touch a label whose rendered text actually changed --
// (LVGL invalidates/redraws on every lv_label_set_text() call regardless of
// whether the text actually differs, so this still earns its keep here the
// same way it does in display_round21.cpp -- a jittery float that formats
// to the same string costs zero LVGL calls, not just zero SPI traffic.)
bool    primed            = false;
char    lastCorner[4][8]  = {};
char    lastTank[8]       = {};
char    lastPreset[12]    = {};
uint8_t lastStatus         = 0xFF;
bool    lastSignalOk       = true;

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

void buildGaugeScreen() {
  gaugeScreen = lv_obj_create(nullptr);
  lv_obj_remove_flag(gaugeScreen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(gaugeScreen, lvColor(COL_BG), 0);
  lv_obj_set_style_bg_opa(gaugeScreen, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(gaugeScreen, 0, 0);
  lv_obj_set_style_pad_all(gaugeScreen, 0, 0);

  mkDivider(gaugeScreen, GRID_X0 + CELL_W - 1, GRID_Y0, 2, GRID_H);
  mkDivider(gaugeScreen, GRID_X0, GRID_Y0 + CELL_H - 1, GRID_W, 2);
  mkDivider(gaugeScreen, GRID_X0, STRIP_Y - 1, GRID_W, 2);
  mkDivider(gaugeScreen, GRID_X0 + CELL_W - 1, STRIP_Y, 2, STRIP_H);
  mkDivider(gaugeScreen, GRID_X0, STATUS_Y - 1, GRID_W, 2);

  for (uint8_t i = 0; i < 4; i++) {
    const Cell& c = kCells[i];
    lv_obj_t* caption = mkCell(gaugeScreen, c.x, c.y, CELL_W, CELL_VALUE_TOP);
    lv_label_set_text(mkLabel(caption, fontPx(LABEL_FONT_PX), COL_LABEL), c.label);

    // DSEG7 (a real segmented "digital clock/calculator" font, see
    // dseg7_font.h) for the actual pressure digits -- this is the one
    // element every 90s BMW OBC/trip-computer sub-display shares, so it's
    // the biggest lever for the retro look. Everything else (labels, menu,
    // status/preset words) stays on Montserrat, which was never meant to
    // look like a digit display.
    lv_obj_t* value = mkCell(gaugeScreen, c.x, c.y + CELL_VALUE_TOP, CELL_W, CELL_VALUE_H);
    cornerValueLabel[i] = mkLabel(value, &font_dseg7_48, COL_VALUE);
  }

  lv_obj_t* tankCaption = mkCell(gaugeScreen, GRID_X0, STRIP_Y, CELL_W, STRIP_VALUE_TOP - STRIP_Y);
  lv_label_set_text(mkLabel(tankCaption, fontPx(LABEL_FONT_PX), COL_TANK), "TANK");
  // Digits in DSEG7 + a small Montserrat "PSI" unit label side by side in
  // the same flex cell -- mirrors how a real OBC digit module prints its
  // unit as a small silkscreened label next to the segmented digits rather
  // than as part of the digit display itself.
  lv_obj_t* tankCell = mkCell(gaugeScreen, GRID_X0, STRIP_VALUE_TOP, CELL_W, STRIP_VALUE_H);
  tankValueLabel = mkLabel(tankCell, &font_dseg7_28, COL_TANK);
  tankUnitLabel  = mkLabel(tankCell, fontPx(LABEL_FONT_PX), COL_TANK);
  lv_label_set_text(tankUnitLabel, "PSI");

  lv_obj_t* presetCaption = mkCell(gaugeScreen, GRID_X0 + CELL_W, STRIP_Y, CELL_W, STRIP_VALUE_TOP - STRIP_Y);
  lv_label_set_text(mkLabel(presetCaption, fontPx(LABEL_FONT_PX), COL_PRESET), "PRESET");
  lv_obj_t* presetCell = mkCell(gaugeScreen, GRID_X0 + CELL_W, STRIP_VALUE_TOP, CELL_W, STRIP_VALUE_H);
  presetValueLabel = mkLabel(presetCell, fontPx(VALUE_FONT_PX_STRIP), COL_PRESET);

  lv_obj_t* statusCell = mkCell(gaugeScreen, GRID_X0, STATUS_Y, GRID_W, STATUS_H);
  statusLabel = mkLabel(statusCell, fontPx(STATUS_FONT_PX), COL_NOSIGNAL);
}

}  // namespace

void begin() {
  panel_round21::begin();

  // menu::begin() (called before display::begin() in main.cpp) has already
  // loaded the persisted value.
  s_rotate180 = menu::rotate180();

  lv_init();
  lv_log_register_print_cb(lvLogCb);

  lvDisp = lv_display_create(SCREEN_W, SCREEN_H);
  lv_display_set_default(lvDisp);
  lv_display_set_flush_cb(lvDisp, flushCb);

  const size_t bufBytes = (size_t)SCREEN_W * kDrawBufLines * sizeof(uint16_t);
  void* buf1 = malloc(bufBytes);
  void* buf2 = malloc(bufBytes);
  if (!buf1 || !buf2) {
    Serial.println("[LVGL] draw buffer allocation failed");
  }
  lv_display_set_buffers(lvDisp, buf1, buf2, bufBytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
}

void setBacklight(uint8_t percent) { panel_round21::setBacklight(percent); }

void splash() {
  pumpLvgl();

  // One-time screen, never freed -- splash() runs exactly once per boot and
  // drawStaticLayout() immediately switches away from it afterwards, so a
  // handful of small abandoned lv_obj_t's for the rest of the program's life
  // is a non-issue (no large buffers involved, unlike the panel framebuffer).
  lv_obj_t* splashScreen = lv_obj_create(nullptr);
  lv_obj_remove_flag(splashScreen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(splashScreen, lvColor(COL_BG), 0);
  lv_obj_set_style_border_width(splashScreen, 0, 0);
  lv_obj_set_style_pad_all(splashScreen, 0, 0);

  lv_obj_t* handleCell = mkCell(splashScreen, 0, SPLASH_Y_HANDLE - 24, SCREEN_W, 48);
  lv_label_set_text(mkLabel(handleCell, fontPx(TITLE_FONT_PX), COL_VALUE), SPLASH_HANDLE);

  mkDivider(splashScreen, SCREEN_W / 2 - SPLASH_RULE_HALFLEN, SPLASH_Y_RULE,
            SPLASH_RULE_HALFLEN * 2, 2);

  lv_obj_t* productCell = mkCell(splashScreen, 0, SPLASH_Y_PRODUCT - 20, SCREEN_W, 40);
  lv_label_set_text(mkLabel(productCell, fontPx(LABEL_FONT_PX), COL_PRESET), "AIRLIFT V2");

  lv_obj_t* versionCell = mkCell(splashScreen, 0, SPLASH_Y_VERSION - 16, SCREEN_W, 32);
  lv_label_set_text(mkLabel(versionCell, fontPx(LABEL_FONT_PX), COL_LABEL), SLAVE_FW_VERSION);

  lv_screen_load(splashScreen);

  for (uint16_t t = 0; t <= SPLASH_FADE_MS; t += 20) {
    setBacklight((BACKLIGHT_PCT * t) / SPLASH_FADE_MS);
    pumpLvgl();
    delay(20);
  }
  setBacklight(BACKLIGHT_PCT);

  const uint32_t holdStart = millis();
  while (millis() - holdStart < SPLASH_HOLD_MS) {
    pumpLvgl();
    delay(20);
  }
}

void drawStaticLayout() {
  if (!gaugeBuilt) {
    buildGaugeScreen();
    gaugeBuilt = true;
  }
  lv_screen_load(gaugeScreen);

  primed = false;
  // Restores the Settings screen's live brightness (menu.cpp), not the
  // hardcoded default -- this also runs every time the menu closes back to
  // the gauge, and resetting to default there would silently undo a
  // brightness change the moment you left Settings.
  setBacklight(menu::currentBacklightPct());
}

void update(const AirLiftData& d, bool signalOk) {
  pumpLvgl();

  const bool     forceAll    = !primed || (signalOk != lastSignalOk);
  const uint16_t valueColour = signalOk ? COL_VALUE : COL_STALE;
  const uint16_t tankColour  = signalOk ? COL_TANK  : COL_STALE;

  const float corners[4] = {d.fl, d.fr, d.rl, d.rr};
  for (uint8_t i = 0; i < 4; i++) {
    char buf[8];
    formatPsi(corners[i], buf, sizeof(buf));
    if (forceAll || strcmp(buf, lastCorner[i]) != 0) {
      lv_label_set_text(cornerValueLabel[i], buf);
      lv_obj_set_style_text_color(cornerValueLabel[i], lvColor(valueColour), 0);
      strncpy(lastCorner[i], buf, sizeof(lastCorner[i]) - 1);
      lastCorner[i][sizeof(lastCorner[i]) - 1] = '\0';
    }
  }

  char tank[8];
  formatPsi(d.tank, tank, sizeof(tank));
  if (forceAll || strcmp(tank, lastTank) != 0) {
    // Digits and the "PSI" unit are two separate labels (see
    // buildGaugeScreen()) -- only the digits' text ever changes, the unit
    // label's text is fixed at build time, just recoloured to match.
    lv_label_set_text(tankValueLabel, tank);
    lv_obj_set_style_text_color(tankValueLabel, lvColor(tankColour), 0);
    lv_obj_set_style_text_color(tankUnitLabel, lvColor(tankColour), 0);
    strncpy(lastTank, tank, sizeof(lastTank) - 1);
    lastTank[sizeof(lastTank) - 1] = '\0';
  }

  const char* preset = presetName(d.preset);
  if (forceAll || strcmp(preset, lastPreset) != 0) {
    lv_label_set_text(presetValueLabel, preset);
    lv_obj_set_style_text_color(presetValueLabel, lvColor(signalOk ? COL_PRESET : COL_STALE), 0);
    strncpy(lastPreset, preset, sizeof(lastPreset) - 1);
    lastPreset[sizeof(lastPreset) - 1] = '\0';
  }

  const uint8_t status = signalOk ? d.status : AIRLIFT_NO_SIGNAL;
  if (forceAll || status != lastStatus) {
    const char* text;
    uint16_t    colour;
    statusText(status, &text, &colour);
    lv_label_set_text(statusLabel, text);
    lv_obj_set_style_text_color(statusLabel, lvColor(colour), 0);
    lastStatus = status;
  }

  lastSignalOk = signalOk;
  primed       = true;
}

namespace {

// --- menu screen, built once, reused for the life of the program ----------
bool      menuBuilt        = false;
lv_obj_t* menuScreen        = nullptr;
lv_obj_t* menuTitleLabel    = nullptr;
lv_obj_t* menuListContainer = nullptr;
lv_obj_t* menuRowCell[8]    = {};
lv_obj_t* menuRowLabel[8]   = {};
lv_obj_t* manualContainer    = nullptr;
lv_obj_t* manualLeftLabel    = nullptr;
lv_obj_t* manualRightLabel   = nullptr;
lv_obj_t* manualSymbolLabel  = nullptr;

// --- menu diff cache --------------------------------------------------
// The menu only redraws on a button press, not a 10 Hz telemetry stream, so
// (unlike update() above) touching every visible row on every change is
// cheap enough here too. MANUAL_ACTIVE is the exception: it tracks live
// pressures instead of items[], since it updates continuously while a
// button is held rather than only on navigation.
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

void buildMenuScreen() {
  menuScreen = lv_obj_create(nullptr);
  lv_obj_remove_flag(menuScreen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(menuScreen, lvColor(COL_BG), 0);
  lv_obj_set_style_bg_opa(menuScreen, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(menuScreen, 0, 0);
  lv_obj_set_style_pad_all(menuScreen, 0, 0);

  lv_obj_t* titleCell = mkCell(menuScreen, 0, GRID_Y0, SCREEN_W, 40);
  menuTitleLabel = mkLabel(titleCell, fontPx(TITLE_FONT_PX), COL_PRESET);

  // Item rows: a pool of 8 pre-created cells, repositioned/resized each
  // drawMenu() call to split the available height evenly across however
  // many items the current view actually has (mirrors display_round21.cpp's
  // per-call rowH calculation) -- cheap since this only runs on a button
  // press, not a telemetry stream.
  menuListContainer = mkPlainObj(menuScreen);
  lv_obj_set_style_bg_opa(menuListContainer, LV_OPA_TRANSP, 0);
  lv_obj_set_pos(menuListContainer, 0, 0);
  lv_obj_set_size(menuListContainer, SCREEN_W, SCREEN_H);
  for (uint8_t i = 0; i < 8; i++) {
    menuRowCell[i]  = mkCell(menuListContainer, GRID_X0, GRID_Y0, GRID_W, 10);
    menuRowLabel[i] = mkLabel(menuRowCell[i], fontPx(LABEL_FONT_PX), COL_LABEL);
  }

  // FRONT/REAR live manual-adjust view: two big pressure numbers either
  // side of a direction glyph, colour-coded (green raising / amber lowering
  // / grey idle) -- swaps display_round21.cpp's hand-drawn filled triangle
  // for LVGL's bundled LV_SYMBOL_UP/DOWN/MINUS glyphs (no cheap
  // filled-polygon primitive in LVGL to match the original exactly).
  manualContainer = mkPlainObj(menuScreen);
  lv_obj_set_style_bg_opa(manualContainer, LV_OPA_TRANSP, 0);
  lv_obj_set_pos(manualContainer, 0, 0);
  lv_obj_set_size(manualContainer, SCREEN_W, SCREEN_H);
  lv_obj_add_flag(manualContainer, LV_OBJ_FLAG_HIDDEN);

  const int16_t top    = GRID_Y0 + 48;
  const int16_t bottom = STATUS_Y + STATUS_H;
  const int16_t midY   = (top + bottom) / 2;
  const int16_t cx     = GRID_X0 + CELL_W;

  // Same DSEG7 digit font as the gauge's corner values -- this is the same
  // "live pressure readout" concept, just on the manual-adjust screen.
  lv_obj_t* leftCell = mkCell(manualContainer, GRID_X0, midY - CELL_VALUE_H / 2,
                              CELL_W, CELL_VALUE_H);
  manualLeftLabel = mkLabel(leftCell, &font_dseg7_48, COL_LABEL);

  lv_obj_t* rightCell = mkCell(manualContainer, GRID_X0 + CELL_W, midY - CELL_VALUE_H / 2,
                               CELL_W, CELL_VALUE_H);
  manualRightLabel = mkLabel(rightCell, &font_dseg7_48, COL_LABEL);

  lv_obj_t* symbolCell = mkCell(manualContainer, cx - 32, midY - 32, 64, 64);
  manualSymbolLabel = mkLabel(symbolCell, fontPx(VALUE_FONT_PX_CORNER), COL_LABEL);

  lv_obj_t* hintCell = mkCell(manualContainer, 0, bottom - 40, SCREEN_W, 40);
  lv_label_set_text(mkLabel(hintCell, fontPx(LABEL_FONT_PX), COL_LABEL), "HOLD +/-");
}

void updateManualActive(const menu::View& view) {
  const uint16_t valueColour = (view.direction == AIRLIFT_RAISING)  ? COL_RAISING
                              : (view.direction == AIRLIFT_LOWERING) ? COL_LOWERING
                                                                       : COL_LABEL;
  lv_label_set_text(manualLeftLabel, view.leftPsi);
  lv_label_set_text(manualRightLabel, view.rightPsi);
  lv_obj_set_style_text_color(manualLeftLabel, lvColor(valueColour), 0);
  lv_obj_set_style_text_color(manualRightLabel, lvColor(valueColour), 0);

  const char* symbol = (view.direction == AIRLIFT_RAISING)  ? LV_SYMBOL_UP
                      : (view.direction == AIRLIFT_LOWERING) ? LV_SYMBOL_DOWN
                                                                : LV_SYMBOL_MINUS;
  lv_label_set_text(manualSymbolLabel, symbol);
  lv_obj_set_style_text_color(manualSymbolLabel, lvColor(valueColour), 0);
}

}  // namespace

void setRotate180(bool on) {
  s_rotate180 = on;
  // Whatever's already on the glass is now upside down relative to the new
  // orientation -- force both diff caches to repaint in full next time,
  // same trick main.cpp's GAUGE<->MENU transition already relies on.
  primed     = false;
  menuPrimed = false;
}

void drawMenu(const menu::View& view, bool force) {
  pumpLvgl();

  if (!menuBuilt) {
    buildMenuScreen();
    menuBuilt = true;
  }
  if (force) lv_screen_load(menuScreen);

  if (!force && !menuViewChanged(view)) return;
  cacheMenuView(view);

  lv_label_set_text(menuTitleLabel, view.title);

  if (view.mode == menu::Mode::MANUAL_ACTIVE) {
    lv_obj_add_flag(menuListContainer, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(manualContainer, LV_OBJ_FLAG_HIDDEN);
    updateManualActive(view);
    return;
  }

  lv_obj_add_flag(manualContainer, LV_OBJ_FLAG_HIDDEN);
  lv_obj_remove_flag(menuListContainer, LV_OBJ_FLAG_HIDDEN);

  const int16_t top    = GRID_Y0 + 48;
  const int16_t bottom = STATUS_Y + STATUS_H;
  const uint8_t rows   = view.itemCount ? view.itemCount : 1;
  const int16_t rowH   = (bottom - top) / rows;

  for (uint8_t i = 0; i < 8; i++) {
    if (i >= view.itemCount) {
      lv_obj_add_flag(menuRowCell[i], LV_OBJ_FLAG_HIDDEN);
      continue;
    }
    lv_obj_remove_flag(menuRowCell[i], LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(menuRowCell[i], GRID_X0, top + i * rowH);
    lv_obj_set_size(menuRowCell[i], GRID_W, rowH);

    const bool selected = (i == view.cursor);
    lv_obj_set_style_bg_color(menuRowCell[i], lvColor(selected ? COL_DIVIDER : COL_BG), 0);
    lv_label_set_text(menuRowLabel[i], view.items[i]);
    lv_obj_set_style_text_color(menuRowLabel[i], lvColor(selected ? COL_VALUE : COL_LABEL), 0);
  }
}

}  // namespace display
