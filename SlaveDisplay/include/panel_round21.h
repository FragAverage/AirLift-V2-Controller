#pragma once

#include <Arduino.h>
#include <esp_lcd_panel_ops.h>

// ---------------------------------------------------------------------------
// Shared ST7701 480x480 RGB-parallel panel bring-up for the round21 board,
// used by BOTH display backends that target it (Arduino_GFX in
// display_round21.cpp, LVGL in display_round21_lvgl.cpp). Split out of
// display_round21.cpp so the panel-specific gamma/voltage/timing register
// init (st7701Init(), ported byte-for-byte from Waveshare's own demo — see
// display_round21.cpp's original header comment) and the RGB peripheral
// config (rgbPanelInit()) exist exactly once rather than being duplicated
// across backends.
//
// Owns: TCA9554 power/reset sequencing, the ST7701's one-time init-only
// "3-wire SPI" register writes, the ESP32-S3 RGB LCD peripheral bring-up,
// and backlight PWM. Does NOT own pixel drawing — callers draw into the
// framebuffer via esp_lcd_panel_draw_bitmap() against the handle() returned
// below, however they like (Arduino_GFX subclass, LVGL flush callback, etc).
// ---------------------------------------------------------------------------
namespace panel_round21 {

// Full bring-up: I2C/TCA9554, panel power/reset, ST7701 register init, RGB
// peripheral config. Panel is live (accepting esp_lcd_panel_draw_bitmap
// calls) when this returns. Backlight is left off -- call setBacklight()
// once the framebuffer has real content to show.
void begin();

// The RGB panel handle, valid only after begin() returns. Callers draw into
// the panel with esp_lcd_panel_draw_bitmap(handle(), ...).
esp_lcd_panel_handle_t handle();

// Backlight brightness, 0-100, driven by PWM on LCD_BACKLIGHT_PIN.
void setBacklight(uint8_t percent);

}  // namespace panel_round21
