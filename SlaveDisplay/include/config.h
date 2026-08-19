#pragma once

#include <Arduino.h>

// ---------------------------------------------------------------------------
// Three boards share this firmware:
//
//   - ESP32-2432S028R "Cheap Yellow Display" (default) — ILI9341 320x240
//     landscape + XPT2046 resistive touch on its own SPI bus. Rendered with
//     TFT_eSPI (src/display.cpp, src/touch.cpp).
//   - Waveshare ESP32-S3-Touch-LCD-1.28 (build flag DISPLAY_ROUND=1) —
//     GC9A01 240x240 round panel + CST816T capacitive touch on I2C. Also
//     TFT_eSPI, same two files.
//   - Waveshare ESP32-S3-Touch-LCD-2.1 (build flag BOARD_ROUND21=1) —
//     ST7701 480x480 round panel driven over the ESP32-S3's RGB-parallel LCD
//     peripheral (not SPI), reset/CS/power gated through a TCA9554 I2C IO
//     expander, CST820 capacitive touch on I2C. TFT_eSPI has no working
//     ST7701-RGB support, so this board uses Arduino_GFX instead and gets its
//     own implementation: src/display_round21.cpp, src/touch_round21.cpp,
//     src/tca9554.cpp. platformio.ini's build_src_filter keeps each env
//     compiling only its own display/touch files.
//
// Panel SPI pins for the two TFT_eSPI boards live in platformio.ini as
// TFT_eSPI build flags. Everything else board-specific — touch pins, panel
// pins/timing for round21, and layout geometry — lives here.
// ---------------------------------------------------------------------------

#if defined(DISPLAY_ROUND)

// --- Touch (CST816T, I2C, shared bus with the onboard QMI8658 IMU) ---------
// Pins per TFT_eSPI's bundled Setup302_Waveshare_ESP32S3_GC9A01.h (panel) and
// a community-verified PlatformIO build for this exact board (touch) —
// re-check against your unit's silkscreen/schematic before relying on it;
// Waveshare sells several similarly-named 1.28" variants (plain LCD-1.28 has
// no touch at all, DualEye-1.28 is a different board) with different pinouts.
#define TOUCH_SDA 6
#define TOUCH_SCL 7
#define TOUCH_RST 13
#define TOUCH_IRQ 5

// Ignore repeat taps inside this window.
#define TP_DEBOUNCE_MS 250

#elif defined(BOARD_ROUND21)

// --- I2C bus (TCA9554 IO expander + CST820 touch share this bus) -----------
// Pins and everything below per Waveshare's own Arduino demo for this board
// (Display_ST7701 / TCA9554PWR / Touch_CST820 / I2C_Driver) — re-verify
// against your unit if you have a different revision.
#define I2C_SDA_PIN 15
#define I2C_SCL_PIN 7

// --- ST7701 panel: init is 3-wire-SPI-style register writes (no MISO, CS
// held by the IO expander, not a real CS pin), then the ESP32-S3's RGB LCD
// peripheral drives actual pixel data. -------------------------------------
#define LCD_SPI_CLK_PIN  2
#define LCD_SPI_MOSI_PIN 1
#define LCD_BACKLIGHT_PIN 6

#define RGB_PCLK_PIN  41
#define RGB_HSYNC_PIN 38
#define RGB_VSYNC_PIN 39
#define RGB_DE_PIN    40
// B0..B4, G0..G5, R0..R4 — order the ESP-IDF RGB panel driver expects.
#define RGB_DATA_PINS {5, 45, 48, 47, 21, 14, 13, 12, 11, 10, 9, 46, 3, 8, 18, 17}

// TCA9554 (addr 0x20) pins: panel reset, panel "CS" (gates the init SPI
// writes), touch reset, and display power enable — none of these are real
// GPIOs, they're bits on the expander (see tca9554.h).
#define LCD_RESET_EXIO   1
#define TOUCH_RESET_EXIO 2
#define LCD_CS_EXIO      3
#define LCD_PWR_EXIO     8

// --- Touch (CST820, I2C, same bus as the IO expander) -----------------------
#define TOUCH_I2C_ADDR 0x15
#define TOUCH_IRQ      16

// Ignore repeat taps inside this window.
#define TP_DEBOUNCE_MS 250

#else  // CYD

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

#endif  // DISPLAY_ROUND / BOARD_ROUND21

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
// Layout
//
// Both panels report a 0,0 top-left framebuffer; the round panel's glass just
// physically masks anything drawn outside its visible circle. So rather than
// branch the drawing code, the round build's grid is the CYD's same 2x2
// corner-grid / tank-preset-strip / status-bar layout, scaled down and inset
// so every value box sits inside the circle inscribed in the 240x240
// framebuffer — display.cpp itself is identical between boards.
// ---------------------------------------------------------------------------
#if defined(DISPLAY_ROUND)

// setRotation() argument. 0 = USB-C tail pointing down. Change if your
// cluster mounts the module in a different orientation.
#define TFT_ROTATION 0

//   Inscribed-square derivation (radius math, not measured on glass):
//   a 240-diameter circle has radius 120; a panel's actual visible circle is
//   a little smaller than that behind the bezel/lens, so this design targets
//   a safe radius of ~118 and keeps every box corner a few px inside it.
//   Grid corner (44,40) relative to centre (120,120) -> sqrt(76^2+80^2) =
//   ~110px, status-bar corner (44,204) -> sqrt(76^2+84^2) = ~113px. If your
//   unit's visible circle is smaller than that, shrink GRID_X0/GRID_Y0 (and
//   grow STATUS_H a little to keep the bottom edge symmetric) until the grid
//   clears the bezel.
#define SCREEN_W 240
#define SCREEN_H 240

#define GRID_X0  44                  // 44..196 inset -> safe circle width
#define GRID_Y0  40
#define GRID_W   (SCREEN_W - 2 * GRID_X0)  // 152
#define GRID_H   100                 // total height of the 2x2 corner grid
#define CELL_W   (GRID_W / 2)        // 76
#define CELL_H   (GRID_H / 2)        // 50

#define STRIP_Y  (GRID_Y0 + GRID_H)  // 140 — tank / preset strip
#define STRIP_H  42                  // 140..181

#define STATUS_Y (STRIP_Y + STRIP_H)  // 182
#define STATUS_H 22                   // 182..203

#define CELL_VALUE_TOP    18   // relative to the cell origin
#define CELL_VALUE_H      28
#define STRIP_VALUE_TOP   (STRIP_Y + 16)
#define STRIP_VALUE_H     22

// Smaller cells than the CYD's -> Font 6 (48px, digits-only) no longer fits;
// Font 4 (26px) does.
#define VALUE_FONT_CORNER 4
#define VALUE_FONT_STRIP  4

// Splash text, tuned to stay inside the safe circle (see grid note above)
// rather than the CYD's wider landscape budget.
#define SPLASH_HANDLE_FONT 4
#define SPLASH_HANDLE_SIZE 1
#define SPLASH_Y_HANDLE       88
#define SPLASH_Y_RULE         108
#define SPLASH_RULE_HALFLEN   50
#define SPLASH_Y_PRODUCT      128
#define SPLASH_Y_VERSION      152

#elif defined(BOARD_ROUND21)

// setRotation() argument passed to Arduino_GFX. 0 = USB-C tail pointing down.
#define DISPLAY_ROTATION 0

// Same 2x2-grid / strip / status-bar layout as the other two boards, scaled
// up for the 480x480 panel. This panel is 4x the CYD's pixel area with a
// generous safe circle, so the inset here is a much smaller fraction of the
// radius than round128 needed (round128's math is the tight case — see its
// comment above) — proportionally similar (~2x round128's numbers) but with
// more headroom, since this hasn't been checked against real glass either.
#define SCREEN_W 480
#define SCREEN_H 480

#define GRID_X0  88
#define GRID_Y0  80
#define GRID_W   (SCREEN_W - 2 * GRID_X0)  // 304
#define GRID_H   200                        // total height of the 2x2 corner grid
#define CELL_W   (GRID_W / 2)               // 152
#define CELL_H   (GRID_H / 2)               // 100

#define STRIP_Y  (GRID_Y0 + GRID_H)  // 280 — tank / preset strip
#define STRIP_H  84                  // 280..363

#define STATUS_Y (STRIP_Y + STRIP_H)  // 364
#define STATUS_H 44                   // 364..407

#define CELL_VALUE_TOP    36   // relative to the cell origin
#define CELL_VALUE_H      56
#define STRIP_VALUE_TOP   (STRIP_Y + 32)
#define STRIP_VALUE_H     44

// Arduino_GFX's built-in font is a fixed 6x8px glyph scaled by an integer
// setTextSize() — there's no font-ID system like TFT_eSPI's here.
#define VALUE_TEXTSIZE_CORNER 6
#define VALUE_TEXTSIZE_STRIP  4
#define LABEL_TEXTSIZE         2
#define STATUS_TEXTSIZE         3

#define SPLASH_HANDLE_TEXTSIZE 4
#define SPLASH_Y_HANDLE        200
#define SPLASH_Y_RULE          248
#define SPLASH_RULE_HALFLEN    110
#define SPLASH_Y_PRODUCT       272
#define SPLASH_Y_VERSION       312

#else  // CYD

#define TFT_ROTATION 1  // landscape, 320x240

#define SCREEN_W 320
#define SCREEN_H 240

#define GRID_X0  0
#define GRID_Y0  0
#define GRID_W   SCREEN_W
#define GRID_H   168               // total height of the 2x2 corner grid
#define CELL_W   (GRID_W / 2)      // 160
#define CELL_H   (GRID_H / 2)      // 84

#define STRIP_Y  (GRID_Y0 + GRID_H)  // 168 — tank / preset strip
#define STRIP_H  50                  // 168..217

#define STATUS_Y (STRIP_Y + STRIP_H)  // 218
#define STATUS_H (SCREEN_H - STATUS_Y)  // 22

#define CELL_VALUE_TOP    22   // relative to the cell origin
#define CELL_VALUE_H      58
#define STRIP_VALUE_TOP   (STRIP_Y + 22)
#define STRIP_VALUE_H     28

#define VALUE_FONT_CORNER 6
#define VALUE_FONT_STRIP  4

#define SPLASH_HANDLE_FONT 4
#define SPLASH_HANDLE_SIZE 2
#define SPLASH_Y_HANDLE       96
#define SPLASH_Y_RULE         132
#define SPLASH_RULE_HALFLEN   70
#define SPLASH_Y_PRODUCT      156
#define SPLASH_Y_VERSION      186

#endif  // DISPLAY_ROUND / BOARD_ROUND21

// --- Splash ------------------------------------------------------------
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
// Deliberately desaturated. Pure white / pure cyan / pure red on black glows
// badly behind cluster glazing at night; these are pulled back towards the
// warm greys an OEM cluster actually uses while keeping the status colours
// unambiguous at a glance. Raw RGB565 (not e.g. TFT_eSPI's TFT_BLACK) since
// this header is shared with the round21 build's Arduino_GFX code, which
// doesn't define TFT_eSPI's colour macros.
#define COL_BG        0x0000    // black
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
