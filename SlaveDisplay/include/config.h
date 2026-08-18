#pragma once

#include <Arduino.h>

// ---------------------------------------------------------------------------
// Board: ESP32-2432S028R ("Cheap Yellow Display") — ILI9341 320x240 + XPT2046
//
// Display pins / SPI config live in platformio.ini as TFT_eSPI build flags.
// Only the touch controller pins are declared here: they must NOT be handed to
// TFT_eSPI (defining TOUCH_CS in its setup makes it drive the touch chip on the
// display's own bus, which is not how this board is wired).
// ---------------------------------------------------------------------------

// --- Touch (XPT2046 on VSPI, separate bus from the panel) ------------------
#define TP_CS    33
#define TP_IRQ   36
#define TP_MOSI  32
#define TP_MISO  39
#define TP_CLK   25

// Calibrated raw ranges for this panel.
#define TP_RAW_MIN 200
#define TP_RAW_MAX 3900

// Ignore repeat taps inside this window.
#define TP_DEBOUNCE_MS 250

// --- Serial ----------------------------------------------------------------
#define SERIAL_BAUD 115200

// --- Link ------------------------------------------------------------------
// No packet for this long -> NO SIGNAL.
#define LINK_TIMEOUT_MS 3000

#ifndef ESPNOW_WIFI_CHANNEL
#define ESPNOW_WIFI_CHANNEL 1
#endif
#ifndef ENABLE_TOUCH
#define ENABLE_TOUCH 0
#endif
#ifndef DEMO_MODE
#define DEMO_MODE 0
#endif

// ---------------------------------------------------------------------------
// Layout — landscape (setRotation(1)), 320 x 240
//
//   0                160               319
//   +--------------------+--------------------+   0
//   |        FL          |        FR          |
//   +--------------------+--------------------+   84
//   |        RL          |        RR          |
//   +--------------------+--------------------+  168
//   |   TANK  145 PSI    |  PRESET   DRIVE    |
//   +-------------------------------------------  218
//   |                 RAISING                 |
//   +-------------------------------------------  240
// ---------------------------------------------------------------------------
#define SCREEN_W 320
#define SCREEN_H 240

#define GRID_H   168               // total height of the 2x2 corner grid
#define CELL_W   (SCREEN_W / 2)    // 160
#define CELL_H   (GRID_H / 2)      // 84

#define STRIP_Y  GRID_H            // 168 — tank / preset strip
#define STRIP_H  50                // 168..217

#define STATUS_Y (STRIP_Y + STRIP_H)  // 218
#define STATUS_H (SCREEN_H - STATUS_Y)  // 22

// Value boxes are cleared with fillRect before redrawing to avoid full-screen
// clears (and the flicker that comes with them).
#define CELL_VALUE_TOP    22   // relative to the cell origin
#define CELL_VALUE_H      58
#define STRIP_VALUE_TOP   (STRIP_Y + 22)
#define STRIP_VALUE_H     28

// --- Splash ----------------------------------------------------------------
#define SPLASH_HANDLE   "@jd_drift"
#define SLAVE_FW_VERSION "v1.0.0"
#define SPLASH_HOLD_MS  1800   // how long the splash stays up
#define SPLASH_FADE_MS  400    // backlight fade-in over the splash

// --- Backlight -------------------------------------------------------------
// Driven by PWM rather than a hard digitalWrite: full brightness is painful at
// night in a cluster. 0-100.
#define BACKLIGHT_PCT 70
#define BACKLIGHT_PWM_CH   0
#define BACKLIGHT_PWM_FREQ 5000
#define BACKLIGHT_PWM_BITS 8

// --- Colours ---------------------------------------------------------------
// Deliberately desaturated. Pure white / pure TFT_CYAN / pure TFT_RED on black
// glow badly behind cluster glazing at night; these are pulled back towards the
// warm greys an OEM cluster actually uses while keeping the status colours
// unambiguous at a glance.
#define COL_BG        TFT_BLACK
#define COL_VALUE     0xDEB9    // warm off-white — corner pressures
#define COL_STALE     0x4208    // dim grey — pressures held over from a lost link
#define COL_LABEL     0x8410    // mid grey — FL / FR / RL / RR
#define COL_DIVIDER   0x39E7    // dark grey — static rules
#define COL_TANK      0x6E5A    // muted teal
#define COL_PRESET    0xE5AB    // muted amber
#define COL_RAISING   0x7E4F    // soft green
#define COL_LOWERING  0xE5AB    // muted amber
#define COL_IDLE      0x738E    // grey
#define COL_NOSIGNAL  0xD9E7    // soft red
