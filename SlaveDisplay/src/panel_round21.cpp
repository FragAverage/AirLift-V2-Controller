// ---------------------------------------------------------------------------
// ST7701 480x480 round panel over the ESP32-S3's RGB-parallel LCD peripheral
// (round21 board only) — shared bring-up for both the Arduino_GFX
// (display_round21.cpp) and LVGL (display_round21_lvgl.cpp) backends.
//
// The init sequence (st7701Init()) and RGB timing config (rgbPanelInit())
// are ported byte-for-byte from Waveshare's own Display_ST7701.cpp demo for
// this board — these are panel-specific gamma/voltage/timing registers, do
// not "clean up" or reorder them.
// ---------------------------------------------------------------------------
#include "panel_round21.h"

#include <Wire.h>
#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_rgb.h>

#include "config.h"
#include "tca9554.h"

namespace panel_round21 {
namespace {

spi_device_handle_t    spiHandle   = nullptr;
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
}

esp_lcd_panel_handle_t handle() { return panelHandle; }

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

}  // namespace panel_round21
