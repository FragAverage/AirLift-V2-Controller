// ---------------------------------------------------------------------------
// 1.3" SH1106 128x64 monochrome OLED, I2C (production screen size).
//
// Genuinely different constraints from the other three boards: no colour (so
// no COL_* usage here — status is conveyed by text alone), and a fraction of
// their pixel budget, so this is a compact text table rather than a scaled-
// down version of the 2x2 grid. Adafruit_SH110X is a buffered driver — unlike
// the RGB-panel board, drawing here only updates RAM; oled.display() has to
// be called to actually push the buffer over I2C, so every function below
// does its diffed drawing first and calls display() once at the end only if
// something actually changed.
// ---------------------------------------------------------------------------
#include "display.h"

#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <Wire.h>

#include "config.h"

namespace display {
namespace {

Adafruit_SH1106G oled(SCREEN_W, SCREEN_H, &Wire, -1);

// --- burn-in mitigation --------------------------------------------------
// The gauge view is on-screen almost all the time (menus auto-return after
// MENU_IDLE_TIMEOUT_MS — see menu.cpp), so its static layout would otherwise
// keep the exact same pixels lit indefinitely on this OLED. Every rotation
// period, nudge the whole gauge layout by up to 1px in X/Y and force a full
// repaint at the new position — small enough to be imperceptible, but no
// pixel stays lit at the same physical spot forever. Since several elements
// (right-hand values, bottom text row) already reach the panel's edge with
// zero margin, a nonzero offset can clip a single edge pixel column/row on
// those elements for part of the rotation — an intentional, minor trade for
// real burn-in protection rather than a bug.
constexpr int16_t  kBurnInOffsets[][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
constexpr uint8_t  kBurnInOffsetCount  = 4;
constexpr uint32_t kBurnInRotateMs     = 5UL * 60UL * 1000UL;  // 5 minutes
uint8_t  s_burnInIndex        = 0;
uint32_t s_burnInLastRotateMs = 0;
int16_t  s_offX = 0;
int16_t  s_offY = 0;

int16_t ox(int16_t x) { return (int16_t)(x + s_offX); }
int16_t oy(int16_t y) { return (int16_t)(y + s_offY); }

void formatPsi(float psi, char* out, size_t n) {
  if (isnan(psi) || psi < -0.5f) {
    snprintf(out, n, "--");
  } else {
    snprintf(out, n, "%.0f", psi);
  }
}

// One glyph doubling as a status icon — no proper arrow glyphs in the
// default GFX font, but '^'/'v' read fine as up/down at 6x8px, and idle
// (0) means "don't draw anything" rather than a stale-looking blank icon.
char statusChar(uint8_t status) {
  switch (status) {
    case AIRLIFT_RAISING:  return STATUS_CHAR_RAISING;
    case AIRLIFT_LOWERING: return STATUS_CHAR_LOWERING;
    case AIRLIFT_IDLE:     return 0;
    default:                return STATUS_CHAR_NOSIGNAL;
  }
}

void drawRowCentered(int16_t y, const char* text, uint8_t size) {
  oled.fillRect(ox(0), oy(y), SCREEN_W, size * 8, SH110X_BLACK);
  oled.setTextSize(size);
  oled.setTextColor(SH110X_WHITE);
  int16_t  x1, y1;
  uint16_t w, h;
  oled.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  oled.setCursor(ox((SCREEN_W - (int16_t)w) / 2 - x1), oy(y));
  oled.print(text);
}

// Corner values live either side of a centred axle-label column (see
// config.h's AXLE_LABEL_COL_W comment) — same driver/passenger left-right
// convention the other boards' 2x2 grid uses, with the axle letter sitting
// where that grid's centre divider would be.
constexpr int16_t kHalfW    = (SCREEN_W - AXLE_LABEL_COL_W) / 2;
constexpr int16_t kAxleColX = kHalfW;   // start of the centred label column

void drawBigValueHalf(int16_t y, bool leftHalf, const char* text) {
  const int16_t xStart = leftHalf ? 0 : (kAxleColX + AXLE_LABEL_COL_W);
  oled.fillRect(ox(xStart), oy(y), kHalfW, GAUGE_TEXTSIZE * 8, SH110X_BLACK);
  oled.setTextSize(GAUGE_TEXTSIZE);
  oled.setTextColor(SH110X_WHITE);
  int16_t  x1, y1;
  uint16_t w, h;
  oled.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  oled.setCursor(ox(xStart + (kHalfW - (int16_t)w) / 2 - x1), oy(y));
  oled.print(text);
}

// Drawn once from drawStaticLayout() — these never change, so update()
// never touches them.
void drawAxleLabels() {
  oled.setTextSize(1);
  oled.setTextColor(SH110X_WHITE);
  const int16_t labelX = kAxleColX + (AXLE_LABEL_COL_W - 6) / 2;  // centre the 6px glyph
  oled.setCursor(ox(labelX), oy(ROW_FRONT_Y + (GAUGE_TEXTSIZE * 8 - 8) / 2));
  oled.print('F');
  oled.setCursor(ox(labelX), oy(ROW_REAR_Y + (GAUGE_TEXTSIZE * 8 - 8) / 2));
  oled.print('R');
}

void drawStatusIcon(char c) {
  oled.fillRect(ox(STATUS_ICON_X), oy(STATUS_ICON_Y), 6, 8, SH110X_BLACK);
  if (!c) return;
  oled.setTextSize(1);
  oled.setTextColor(SH110X_WHITE);
  oled.setCursor(ox(STATUS_ICON_X), oy(STATUS_ICON_Y));
  oled.print(c);
}

// MANUAL_ACTIVE's centre glyph: a real filled triangle rather than a text
// character — this screen is meant to read at a glance while your thumb is
// still on the button, so an unmistakable shape beats a 6px '^'/'v'. Idle
// (no button held) draws a short dash instead of nothing, so the box never
// looks broken/blank between presses.
void drawDirectionGlyph(int16_t y, uint8_t direction) {
  const int16_t cx = kAxleColX + AXLE_LABEL_COL_W / 2;
  const int16_t cy = y + (GAUGE_TEXTSIZE * 8) / 2;
  oled.fillRect(kAxleColX, y, AXLE_LABEL_COL_W, GAUGE_TEXTSIZE * 8, SH110X_BLACK);
  if (direction == AIRLIFT_RAISING) {
    oled.fillTriangle(cx, cy - 6, cx - 6, cy + 5, cx + 6, cy + 5, SH110X_WHITE);
  } else if (direction == AIRLIFT_LOWERING) {
    oled.fillTriangle(cx, cy + 6, cx - 6, cy - 5, cx + 6, cy - 5, SH110X_WHITE);
  } else {
    oled.drawFastHLine(cx - 5, cy, 10, SH110X_WHITE);
  }
}

// --- gauge diff cache --------------------------------------------------
bool primed       = false;
char lastFL[6]    = {};
char lastFR[6]    = {};
char lastRL[6]    = {};
char lastRR[6]    = {};
char lastTank[6]  = {};
char lastPreset[16] = {};
int16_t lastStatusChar = -1;   // not a valid statusChar() return, forces first draw
bool lastSignalOk = true;

// If it's time to rotate the burn-in offset, do so, clear the panel, and
// redraw the static axle labels at the new position -- called from update()
// so it only fires while the gauge (the screen that's actually on all the
// time) is showing. Also resets `primed`, forcing every value in the rest
// of update() to repaint at the new offset in the same frame.
void burnInTick() {
  const uint32_t now = millis();
  if (s_burnInLastRotateMs != 0 &&
      now - s_burnInLastRotateMs < kBurnInRotateMs) {
    return;
  }
  s_burnInLastRotateMs = now;
  s_burnInIndex = (uint8_t)((s_burnInIndex + 1) % kBurnInOffsetCount);
  s_offX        = kBurnInOffsets[s_burnInIndex][0];
  s_offY        = kBurnInOffsets[s_burnInIndex][1];
  Serial.printf("[OLED] burn-in rotate: index=%u offset=(%d,%d)\n",
                s_burnInIndex, s_offX, s_offY);
  oled.clearDisplay();
  drawAxleLabels();
  primed = false;
}

}  // namespace

void begin() {
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);

  // OLED_I2C_ADDR (config.h) is a guess at 0x3C vs 0x3D — try both, and if
  // neither ACKs, scan the whole bus so the log says what (if anything) IS
  // actually there instead of just "failed".
  bool ok = oled.begin(OLED_I2C_ADDR, true);
  if (!ok) {
    const uint8_t altAddr = (OLED_I2C_ADDR == 0x3C) ? 0x3D : 0x3C;
    Serial.printf("[OLED] begin(0x%02X) failed, trying 0x%02X...\n",
                  OLED_I2C_ADDR, altAddr);
    ok = oled.begin(altAddr, true);
  }

  if (!ok) {
    Serial.println("[OLED] begin() failed at 0x3C and 0x3D. I2C bus scan:");
    uint8_t found = 0;
    for (uint8_t addr = 1; addr < 127; addr++) {
      Wire.beginTransmission(addr);
      if (Wire.endTransmission() == 0) {
        Serial.printf("[OLED]   found device at 0x%02X\n", addr);
        found++;
      }
    }
    if (found == 0) {
      Serial.println("[OLED]   nothing responded on the bus at all — check "
                      "SDA/SCL wiring (config.h I2C_SDA_PIN/I2C_SCL_PIN), "
                      "power, and ground.");
    }
  } else {
    Serial.println("[OLED] init OK");
  }

  oled.clearDisplay();
  oled.setContrast(0);
  oled.display();
}

void setBacklight(uint8_t percent) {
  if (percent > 100) percent = 100;
  // SH1106 contrast register is 0-0x7F (127), not 0-255. Confirmed visible
  // on real hardware via a full-screen min/max flash test -- the change is
  // subtle on small text/icons, which is why it looked like it did nothing
  // before that test.
  const uint8_t contrast = (uint8_t)((uint16_t)percent * 127 / 100);
  oled.setContrast(contrast);
}

void setInvert(bool on) {
  oled.invertDisplay(on);
}

void splash() {
  oled.clearDisplay();
  drawRowCentered(SPLASH_Y_HANDLE, SPLASH_HANDLE, 1);
  drawRowCentered(SPLASH_Y_PRODUCT, "AIRLIFT V2", 1);
  drawRowCentered(SPLASH_Y_VERSION, SLAVE_FW_VERSION, 1);
  oled.display();

  for (uint16_t t = 0; t <= SPLASH_FADE_MS; t += 20) {
    setBacklight((BACKLIGHT_PCT * t) / SPLASH_FADE_MS);
    delay(20);
  }
  setBacklight(BACKLIGHT_PCT);

  delay(SPLASH_HOLD_MS);
}

void drawStaticLayout() {
  oled.clearDisplay();
  drawAxleLabels();
  oled.display();
  primed = false;   // force every value to paint on the next update()
  // Baseline for the burn-in rotation timer (see burnInTick()) -- the panel
  // was just fully redrawn, so the next rotation is a full kBurnInRotateMs
  // away rather than firing immediately on the first update() after this.
  s_burnInLastRotateMs = millis();
  // Restores the Settings screen's live brightness (menu.cpp), not the
  // hardcoded default — this also runs every time the menu closes back to
  // the gauge, and resetting to default there would silently undo a
  // brightness change the moment you left Settings.
  setBacklight(menu::currentBacklightPct());
}

void update(const AirLiftData& d, bool signalOk) {
  burnInTick();   // may reset `primed`, so this must run before forceAll below

  // No dimming here (no colour) — a stale reading is flagged by the status
  // icon alone. Values are still held/shown, same as the other boards, so
  // the driver can see where the car was parked.
  const bool forceAll   = !primed || (signalOk != lastSignalOk);
  bool       anyChanged = forceAll;

  char fl[6], fr[6], rl[6], rr[6];
  formatPsi(d.fl, fl, sizeof(fl));
  formatPsi(d.fr, fr, sizeof(fr));
  formatPsi(d.rl, rl, sizeof(rl));
  formatPsi(d.rr, rr, sizeof(rr));

  if (forceAll || strcmp(fl, lastFL) != 0) {
    drawBigValueHalf(ROW_FRONT_Y, true, fl);
    strncpy(lastFL, fl, sizeof(lastFL) - 1);
    lastFL[sizeof(lastFL) - 1] = '\0';
    anyChanged = true;
  }
  if (forceAll || strcmp(fr, lastFR) != 0) {
    drawBigValueHalf(ROW_FRONT_Y, false, fr);
    strncpy(lastFR, fr, sizeof(lastFR) - 1);
    lastFR[sizeof(lastFR) - 1] = '\0';
    anyChanged = true;
  }
  if (forceAll || strcmp(rl, lastRL) != 0) {
    drawBigValueHalf(ROW_REAR_Y, true, rl);
    strncpy(lastRL, rl, sizeof(lastRL) - 1);
    lastRL[sizeof(lastRL) - 1] = '\0';
    anyChanged = true;
  }
  if (forceAll || strcmp(rr, lastRR) != 0) {
    drawBigValueHalf(ROW_REAR_Y, false, rr);
    strncpy(lastRR, rr, sizeof(lastRR) - 1);
    lastRR[sizeof(lastRR) - 1] = '\0';
    anyChanged = true;
  }

  char tank[6];
  formatPsi(d.tank, tank, sizeof(tank));
  char tankRow[16];
  snprintf(tankRow, sizeof(tankRow), "TANK %s", tank);
  if (forceAll || strcmp(tank, lastTank) != 0) {
    drawRowCentered(ROW_TANK_Y, tankRow, 1);
    strncpy(lastTank, tank, sizeof(lastTank) - 1);
    lastTank[sizeof(lastTank) - 1] = '\0';
    anyChanged = true;
  }

  const char* preset = presetName(d.preset);
  if (forceAll || strcmp(preset, lastPreset) != 0) {
    drawRowCentered(ROW_PRESET_Y, preset, 1);
    strncpy(lastPreset, preset, sizeof(lastPreset) - 1);
    lastPreset[sizeof(lastPreset) - 1] = '\0';
    anyChanged = true;
  }

  const uint8_t status = signalOk ? d.status : AIRLIFT_NO_SIGNAL;
  const char    c       = statusChar(status);
  if (forceAll || c != lastStatusChar) {
    drawStatusIcon(c);
    lastStatusChar = c;
    anyChanged     = true;
  }

  lastSignalOk = signalOk;
  primed       = true;

  if (anyChanged) oled.display();
}

namespace {

// --- menu diff cache -----------------------------------------------------
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
// triangle direction indicator — same visual language as the gauge's F/R
// rows (drawBigValueHalf/kAxleColX), just with a dynamic glyph instead of a
// static axle letter, since the title row already names the axle here.
void drawManualActive(const menu::View& view) {
  oled.clearDisplay();
  drawRowCentered(0, view.title, 1);
  drawBigValueHalf(ROW_FRONT_Y, true, view.leftPsi);
  drawBigValueHalf(ROW_FRONT_Y, false, view.rightPsi);
  drawDirectionGlyph(ROW_FRONT_Y, view.direction);
  drawRowCentered(SCREEN_H - 8, "HOLD +/-", 1);
  oled.display();
}

}  // namespace

void drawMenu(const menu::View& view, bool force) {
  if (!force && !menuViewChanged(view)) return;
  cacheMenuView(view);

  if (view.mode == menu::Mode::MANUAL_ACTIVE) {
    drawManualActive(view);
    return;
  }

  oled.clearDisplay();

  // Title row also carries a "N/total" position hint once the list is too
  // long to show in full — 8 presets can't all fit in MENU_VISIBLE_ROWS, so
  // this is the only indicator of where the (scrolled) cursor sits.
  char title[24];
  if (view.itemCount > MENU_VISIBLE_ROWS) {
    snprintf(title, sizeof(title), "%s %u/%u", view.title,
              (unsigned)(view.cursor + 1), (unsigned)view.itemCount);
  } else {
    snprintf(title, sizeof(title), "%s", view.title);
  }
  oled.setTextSize(MENU_TEXTSIZE);
  oled.setTextColor(SH110X_WHITE);
  oled.setCursor(0, MENU_TITLE_Y);
  oled.print(title);

  // A scrolling window that keeps the cursor centred where possible, clamped
  // at both ends of the list — MENU_VISIBLE_ROWS (3) + the title row is
  // exactly this screen's height, so this is the only way to reach item 8
  // of 8 without shrinking the font past legibility.
  uint8_t scrollStart = 0;
  if (view.itemCount > MENU_VISIBLE_ROWS) {
    int16_t start = (int16_t)view.cursor - (MENU_VISIBLE_ROWS / 2);
    if (start < 0) start = 0;
    if (start > view.itemCount - MENU_VISIBLE_ROWS) {
      start = view.itemCount - MENU_VISIBLE_ROWS;
    }
    scrollStart = (uint8_t)start;
  }

  const uint8_t visible = (view.itemCount < MENU_VISIBLE_ROWS)
                               ? view.itemCount
                               : MENU_VISIBLE_ROWS;
  for (uint8_t row = 0; row < visible; row++) {
    const uint8_t  i        = scrollStart + row;
    const bool     selected = (i == view.cursor);
    const int16_t  y        = MENU_ITEMS_Y + row * MENU_ROW_H;

    if (selected) {
      oled.fillRect(0, y, SCREEN_W, MENU_ROW_H, SH110X_WHITE);
      oled.setTextColor(SH110X_BLACK);
    } else {
      oled.setTextColor(SH110X_WHITE);
    }
    oled.setTextSize(MENU_TEXTSIZE);
    oled.setCursor(4, y + (MENU_ROW_H - 8) / 2);
    oled.print(view.items[i]);
  }

  oled.display();
}

}  // namespace display
