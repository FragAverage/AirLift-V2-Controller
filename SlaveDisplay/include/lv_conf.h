#pragma once

// ---------------------------------------------------------------------------
// LVGL config for round21_lvgl (see platformio.ini's -D LV_CONF_INCLUDE_SIMPLE=1
// and lv_conf_internal.h's #include "lv_conf.h" under that flag). Same
// "config lives in build flags/this header, library folder never edited"
// convention as this project's TFT_eSPI USER_SETUP_LOADED flags.
//
// Everything not set here falls back to LVGL's own default (every setting in
// lv_conf_internal.h is `#ifndef X ... #endif`-guarded), so this only lists
// the handful of settings this app actually cares about, not a copy of the
// full ~1500-line lv_conf_template.h.
// ---------------------------------------------------------------------------
#define LV_CONF_H

// RGB565 -- matches the byte order the RGB LCD peripheral is already
// configured for in panel_round21.cpp (cfg.bits_per_pixel = 16, 16 data
// lines wired B0..B4/G0..G5/R0..R4) and what the existing Arduino_GFX
// backend already pushes through esp_lcd_panel_draw_bitmap() successfully --
// no LV_COLOR_16_SWAP needed since nothing swaps bytes on that path either.
#define LV_COLOR_DEPTH 16

// Plain C malloc/free/snprintf rather than LVGL's internal fixed-size pool
// allocator -- ESP32 Arduino's libc heap is already this app's memory model
// everywhere else (see e.g. display_round21.cpp's Arduino_ST7701 class,
// which mallocs its own scratch line/rect buffers), so this avoids having
// to size a separate LV_MEM_SIZE pool up front.
#define LV_USE_STDLIB_MALLOC  LV_STDLIB_CLIB
#define LV_USE_STDLIB_STRING  LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_CLIB

// No RTOS integration: main.cpp's loop() drives lv_timer_handler() directly
// (single-threaded access to LVGL, same as every other display:: backend in
// this codebase), it doesn't hand control to LVGL's own task/thread model.
#define LV_USE_OS LV_OS_NONE

// Logging routed to Serial via lv_log_register_print_cb() in
// display_round21_lvgl.cpp's begin() -- this codebase leans on serial
// diagnostics heavily for board bring-up (see MFL-FINDINGS.md, oled13's
// I2C-scan-on-failure), and this is genuinely new, unverified-on-glass
// bring-up.
#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF 0

// Union of every LVGL env's *_FONT_PX_* macros in config.h (round21_lvgl:
// VALUE_FONT_PX_STRIP/STATUS_FONT_PX/TITLE_FONT_PX=28, LABEL_FONT_PX=20;
// lcd147_lvgl: STATUS_FONT_PX=16, TITLE_FONT_PX=20) -- this file is shared
// project-wide, not per-env, so it's every size any LVGL backend needs, not
// just one board's. 14 stays on (LVGL's own default) since it's
// LV_FONT_DEFAULT. The 48px corner font (round21_lvgl only) and the
// 22px/16px ones (lcd147_lvgl only) are DSEG7, not Montserrat -- see
// lib/dseg7_font/dseg7_font.h, no lv_conf.h entry needed for those.
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_48 1

// lib/dseg7_font/'s generated fonts use lv_font_conv's default RLE bitmap
// compression -- without this, LVGL silently fails to decode every glyph
// (logged as "Couldn't get the bitmap of a glyph" at LV_USE_LOG above,
// found by actually flashing and checking the serial log rather than
// assuming the font "just works" once it links).
#define LV_USE_FONT_COMPRESSED 1
