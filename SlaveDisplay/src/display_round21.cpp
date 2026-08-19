// ---------------------------------------------------------------------------
// ST7701 480x480 round panel over the ESP32-S3's RGB-parallel LCD peripheral
// (round21 board only). TFT_eSPI has no working ST7701-RGB support, so this
// uses Arduino_GFX as a thin drawing-API wrapper around the raw ESP-IDF
// esp_lcd_panel_rgb driver.
//
// The init sequence (st7701Init()) and RGB timing config (rgbPanelInit())
// are ported byte-for-byte from Waveshare's own Display_ST7701.cpp demo for
// this board — these are panel-specific gamma/voltage/timing registers, do
// not "clean up" or reorder them.
// ---------------------------------------------------------------------------
#include "display.h"

#include <Arduino_GFX_Library.h>
#include <Wire.h>
#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_rgb.h>

#include "config.h"
#include "tca9554.h"

namespace display {
namespace {

spi_device_handle_t   spiHandle   = nullptr;
esp_lcd_panel_handle_t panelHandle = nullptr;

void st7701WriteCommand(uint8_t cmd) {
  spi_transaction_t t = {};
  t.cmd  = 0;
  t.addr = cmd;
  spi_device_transmit(spiHandle, &t);
}

void st7701WriteData(uint8_t data) {
  spi_transaction_t t = {};
  t.cmd  = 1;
  t.addr = data;
  spi_device_transmit(spiHandle, &t);
}

// Register init over the panel's init-only "3-wire SPI" link (1-bit command
// + 8-bit address encodes cmd/data, no MISO) — CS is gated by the IO
// expander (LCD_CS_EXIO), not a real SPI CS pin.
void st7701Init() {
  spi_bus_config_t buscfg = {};
  buscfg.mosi_io_num     = LCD_SPI_MOSI_PIN;
  buscfg.miso_io_num     = -1;
  buscfg.sclk_io_num     = LCD_SPI_CLK_PIN;
  buscfg.quadwp_io_num   = -1;
  buscfg.quadhd_io_num   = -1;
  buscfg.max_transfer_sz = 64;
  spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);

  spi_device_interface_config_t devcfg = {};
  devcfg.command_bits   = 1;
  devcfg.address_bits   = 8;
  devcfg.mode           = SPI_MODE0;
  devcfg.clock_speed_hz = 40000000;
  devcfg.spics_io_num   = -1;
  devcfg.queue_size     = 1;
  spi_bus_add_device(SPI2_HOST, &devcfg, &spiHandle);

  tca9554::setPin(LCD_CS_EXIO, false);
  delay(10);

  st7701WriteCommand(0xFF);
  st7701WriteData(0x77); st7701WriteData(0x01); st7701WriteData(0x00);
  st7701WriteData(0x00); st7701WriteData(0x10);

  st7701WriteCommand(0xC0);
  st7701WriteData(0x3B); st7701WriteData(0x00);

  st7701WriteCommand(0xC1);
  st7701WriteData(0x0B); st7701WriteData(0x02);

  st7701WriteCommand(0xC2);
  st7701WriteData(0x07); st7701WriteData(0x02);

  st7701WriteCommand(0xCC);
  st7701WriteData(0x10);

  st7701WriteCommand(0xCD);  // RGB format
  st7701WriteData(0x08);

  st7701WriteCommand(0xB0);  // positive gamma
  st7701WriteData(0x00); st7701WriteData(0x11); st7701WriteData(0x16); st7701WriteData(0x0e);
  st7701WriteData(0x11); st7701WriteData(0x06); st7701WriteData(0x05); st7701WriteData(0x09);
  st7701WriteData(0x08); st7701WriteData(0x21); st7701WriteData(0x06); st7701WriteData(0x13);
  st7701WriteData(0x10); st7701WriteData(0x29); st7701WriteData(0x31); st7701WriteData(0x18);

  st7701WriteCommand(0xB1);  // negative gamma
  st7701WriteData(0x00); st7701WriteData(0x11); st7701WriteData(0x16); st7701WriteData(0x0e);
  st7701WriteData(0x11); st7701WriteData(0x07); st7701WriteData(0x05); st7701WriteData(0x09);
  st7701WriteData(0x09); st7701WriteData(0x21); st7701WriteData(0x05); st7701WriteData(0x13);
  st7701WriteData(0x11); st7701WriteData(0x2a); st7701WriteData(0x31); st7701WriteData(0x18);

  st7701WriteCommand(0xFF);
  st7701WriteData(0x77); st7701WriteData(0x01); st7701WriteData(0x00);
  st7701WriteData(0x00); st7701WriteData(0x11);

  st7701WriteCommand(0xB0);  // VOP
  st7701WriteData(0x6d);

  st7701WriteCommand(0xB1);  // VCOM amplitude
  st7701WriteData(0x37);

  st7701WriteCommand(0xB2);  // VGH, 12V
  st7701WriteData(0x81);

  st7701WriteCommand(0xB3);
  st7701WriteData(0x80);

  st7701WriteCommand(0xB5);  // VGL, -8.3V
  st7701WriteData(0x43);

  st7701WriteCommand(0xB7);
  st7701WriteData(0x85);

  st7701WriteCommand(0xB8);
  st7701WriteData(0x20);

  st7701WriteCommand(0xC1);
  st7701WriteData(0x78);

  st7701WriteCommand(0xC2);
  st7701WriteData(0x78);

  st7701WriteCommand(0xD0);
  st7701WriteData(0x88);

  st7701WriteCommand(0xE0);
  st7701WriteData(0x00); st7701WriteData(0x00); st7701WriteData(0x02);

  st7701WriteCommand(0xE1);
  st7701WriteData(0x03); st7701WriteData(0xA0); st7701WriteData(0x00); st7701WriteData(0x00);
  st7701WriteData(0x04); st7701WriteData(0xA0); st7701WriteData(0x00); st7701WriteData(0x00);
  st7701WriteData(0x00); st7701WriteData(0x20); st7701WriteData(0x20);

  st7701WriteCommand(0xE2);
  for (int i = 0; i < 13; i++) st7701WriteData(0x00);

  st7701WriteCommand(0xE3);
  st7701WriteData(0x00); st7701WriteData(0x00); st7701WriteData(0x11); st7701WriteData(0x00);

  st7701WriteCommand(0xE4);
  st7701WriteData(0x22); st7701WriteData(0x00);

  st7701WriteCommand(0xE5);
  st7701WriteData(0x05); st7701WriteData(0xEC); st7701WriteData(0xA0); st7701WriteData(0xA0);
  st7701WriteData(0x07); st7701WriteData(0xEE); st7701WriteData(0xA0); st7701WriteData(0xA0);
  st7701WriteData(0x00); st7701WriteData(0x00); st7701WriteData(0x00); st7701WriteData(0x00);
  st7701WriteData(0x00); st7701WriteData(0x00); st7701WriteData(0x00); st7701WriteData(0x00);

  st7701WriteCommand(0xE6);
  st7701WriteData(0x00); st7701WriteData(0x00); st7701WriteData(0x11); st7701WriteData(0x00);

  st7701WriteCommand(0xE7);
  st7701WriteData(0x22); st7701WriteData(0x00);

  st7701WriteCommand(0xE8);
  st7701WriteData(0x06); st7701WriteData(0xED); st7701WriteData(0xA0); st7701WriteData(0xA0);
  st7701WriteData(0x08); st7701WriteData(0xEF); st7701WriteData(0xA0); st7701WriteData(0xA0);
  st7701WriteData(0x00); st7701WriteData(0x00); st7701WriteData(0x00); st7701WriteData(0x00);
  st7701WriteData(0x00); st7701WriteData(0x00); st7701WriteData(0x00); st7701WriteData(0x00);

  st7701WriteCommand(0xEB);
  st7701WriteData(0x00); st7701WriteData(0x00); st7701WriteData(0x40); st7701WriteData(0x40);
  st7701WriteData(0x00); st7701WriteData(0x00); st7701WriteData(0x00);

  st7701WriteCommand(0xED);
  st7701WriteData(0xFF); st7701WriteData(0xFF); st7701WriteData(0xFF); st7701WriteData(0xBA);
  st7701WriteData(0x0A); st7701WriteData(0xBF); st7701WriteData(0x45); st7701WriteData(0xFF);
  st7701WriteData(0xFF); st7701WriteData(0x54); st7701WriteData(0xFB); st7701WriteData(0xA0);
  st7701WriteData(0xAB); st7701WriteData(0xFF); st7701WriteData(0xFF); st7701WriteData(0xFF);

  st7701WriteCommand(0xEF);
  st7701WriteData(0x10); st7701WriteData(0x0D); st7701WriteData(0x04); st7701WriteData(0x08);
  st7701WriteData(0x3F); st7701WriteData(0x1F);

  st7701WriteCommand(0xFF);
  st7701WriteData(0x77); st7701WriteData(0x01); st7701WriteData(0x00);
  st7701WriteData(0x00); st7701WriteData(0x13);

  st7701WriteCommand(0xEF);
  st7701WriteData(0x08);

  st7701WriteCommand(0xFF);
  st7701WriteData(0x77); st7701WriteData(0x01); st7701WriteData(0x00);
  st7701WriteData(0x00); st7701WriteData(0x00);

  st7701WriteCommand(0x36);  // memory access control
  st7701WriteData(0x00);

  st7701WriteCommand(0x3A);  // pixel format
  st7701WriteData(0x66);

  st7701WriteCommand(0x11);  // sleep out
  delay(480);

  st7701WriteCommand(0x20);  // display inversion off
  delay(120);
  st7701WriteCommand(0x29);  // display on

  tca9554::setPin(LCD_CS_EXIO, true);
}

void rgbPanelInit() {
  const int dataPins[16] = RGB_DATA_PINS;

  esp_lcd_rgb_panel_config_t cfg = {};
  cfg.clk_src              = LCD_CLK_SRC_DEFAULT;
  cfg.timings.pclk_hz      = 16 * 1000 * 1000;
  cfg.timings.h_res        = SCREEN_W;
  cfg.timings.v_res        = SCREEN_H;
  cfg.timings.hsync_pulse_width  = 8;
  cfg.timings.hsync_back_porch   = 10;
  cfg.timings.hsync_front_porch  = 50;
  cfg.timings.vsync_pulse_width  = 3;
  cfg.timings.vsync_back_porch   = 8;
  cfg.timings.vsync_front_porch  = 8;
  cfg.timings.flags.pclk_active_neg = false;
  cfg.data_width            = 16;
  cfg.bits_per_pixel        = 16;
  cfg.num_fbs               = 2;
  cfg.bounce_buffer_size_px = 10 * SCREEN_W;
  cfg.psram_trans_align     = 64;
  cfg.hsync_gpio_num        = RGB_HSYNC_PIN;
  cfg.vsync_gpio_num        = RGB_VSYNC_PIN;
  cfg.de_gpio_num           = RGB_DE_PIN;
  cfg.pclk_gpio_num         = RGB_PCLK_PIN;
  cfg.disp_gpio_num         = -1;
  for (int i = 0; i < 16; i++) cfg.data_gpio_nums[i] = dataPins[i];
  cfg.flags.fb_in_psram     = true;
  cfg.flags.double_fb       = true;

  esp_lcd_new_rgb_panel(&cfg, &panelHandle);
  esp_lcd_panel_reset(panelHandle);
  esp_lcd_panel_init(panelHandle);
}

// Thin Arduino_GFX subclass: routes the library's drawing primitives to
// esp_lcd_panel_draw_bitmap() against the RGB panel's PSRAM framebuffer.
// (The upstream demo also builds an Arduino_ESP32RGBPanel bus object and
// calls its begin() here — that object is never touched by these overrides,
// panelHandle above is the one actually doing the drawing, so it's dropped
// to avoid configuring the RGB peripheral through two separate paths.)
class Arduino_ST7701 : public Arduino_GFX {
 public:
  Arduino_ST7701(int16_t w, int16_t h) : Arduino_GFX(w, h) {}

  bool begin(int32_t speed = GFX_NOT_DEFINED) override {
    (void)speed;
    return panelHandle != nullptr;
  }

 protected:
  void writePixelPreclipped(int16_t x, int16_t y, uint16_t color) override {
    esp_lcd_panel_draw_bitmap(panelHandle, x, y, x + 1, y + 1, &color);
  }

  void writeFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color) override {
    if (h < 0) { y += h + 1; h = -h; }
    uint16_t* line = (uint16_t*)malloc(h * sizeof(uint16_t));
    if (!line) return;
    for (int16_t i = 0; i < h; i++) line[i] = color;
    esp_lcd_panel_draw_bitmap(panelHandle, x, y, x + 1, y + h, line);
    free(line);
  }

  void writeFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) override {
    if (w < 0) { x += w + 1; w = -w; }
    uint16_t* line = (uint16_t*)malloc(w * sizeof(uint16_t));
    if (!line) return;
    for (int16_t i = 0; i < w; i++) line[i] = color;
    esp_lcd_panel_draw_bitmap(panelHandle, x, y, x + w, y + 1, line);
    free(line);
  }

  void writeFillRectPreclipped(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) override {
    if (w < 0) { x += w + 1; w = -w; }
    if (h < 0) { y += h + 1; h = -h; }
    uint16_t* buf = (uint16_t*)malloc((size_t)w * h * sizeof(uint16_t));
    if (!buf) return;
    for (int32_t i = 0; i < (int32_t)w * h; i++) buf[i] = color;
    esp_lcd_panel_draw_bitmap(panelHandle, x, y, x + w, y + h, buf);
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
  setBacklight(0);

  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  delay(100);
  tca9554::begin();

  tca9554::setPin(LCD_PWR_EXIO, false);  // display power enable is active-low
  delay(10);

  tca9554::setPin(LCD_RESET_EXIO, false);
  delay(10);
  tca9554::setPin(LCD_RESET_EXIO, true);
  delay(50);

  st7701Init();
  rgbPanelInit();

  gfx = new Arduino_ST7701(SCREEN_W, SCREEN_H);
  gfx->begin();
  gfx->setRotation(DISPLAY_ROTATION);
  gfx->fillScreen(COL_BG);
}

void setBacklight(uint8_t percent) {
  if (percent > 100) percent = 100;
  const uint32_t maxDuty = (1u << BACKLIGHT_PWM_BITS) - 1;
  const uint32_t duty    = (maxDuty * percent) / 100;

  static bool attached = false;
  if (!attached) {
    ledcAttach(LCD_BACKLIGHT_PIN, BACKLIGHT_PWM_FREQ, BACKLIGHT_PWM_BITS);
    attached = true;
  }
  ledcWrite(LCD_BACKLIGHT_PIN, duty);
}

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
