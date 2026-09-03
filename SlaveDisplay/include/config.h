#pragma once

#include <Arduino.h>

// ---------------------------------------------------------------------------
// Five boards (nine envs -- cyd, round128, round21, and lcd147 each have two
// rendering-backend variants) share this firmware:
//
//   - ESP32-2432S028R "Cheap Yellow Display" (default) — ILI9341 320x240
//     landscape + XPT2046 resistive touch on its own SPI bus. Two rendering
//     backends: env:cyd (raw TFT_eSPI draw calls — src/display.cpp, shared
//     with round128/lcd147) and env:cyd_lvgl (LVGL — src/display_cyd_lvgl.cpp,
//     include/lv_conf.h), both using src/touch.cpp unchanged.
//   - Waveshare ESP32-S3-LCD-1.28 (build flag DISPLAY_ROUND=1) — plain,
//     non-touch, CNC metal case variant. GC9A01 240x240 round panel, no
//     touch hardware on this unit (CST816T support stays compiled in behind
//     ENABLE_TOUCH=0 for boards that do have it). Two rendering backends:
//     env:round128 (raw TFT_eSPI draw calls — src/display.cpp, shared with
//     cyd/lcd147) and env:round128_lvgl (LVGL —
//     src/display_round128_lvgl.cpp, include/lv_conf.h), both using
//     src/touch.cpp (ENABLE_TOUCH=0 stub) unchanged.
//   - Waveshare ESP32-S3-Touch-LCD-2.1 (build flag BOARD_ROUND21=1) —
//     ST7701 480x480 round panel driven over the ESP32-S3's RGB-parallel LCD
//     peripheral (not SPI), reset/CS/power gated through a TCA9554 I2C IO
//     expander, CST820 capacitive touch on I2C. TFT_eSPI has no working
//     ST7701-RGB support. Panel bring-up (src/panel_round21.cpp) is shared
//     by two rendering backends: env:round21 (Arduino_GFX --
//     src/display_round21.cpp) and env:round21_lvgl (LVGL --
//     src/display_round21_lvgl.cpp, include/lv_conf.h), both using
//     src/touch_round21.cpp and src/tca9554.cpp unchanged.
//   - Generic ESP32-S3 devkit + 1.3" SH1106 128x64 monochrome OLED, I2C
//     (build flag BOARD_OLED13=1) — the intended production screen size.
//     Adafruit_SH110X, own implementation: src/display_oled13.cpp,
//     src/touch_oled13.cpp (this board has no touch hardware at all, so
//     that file is just a stub). No backlight — "brightness" maps to the
//     SH1106's contrast register instead of PWM.
//   - Waveshare ESP32-S3-LCD-1.47B (build flag BOARD_LCD147=1) — ST7789
//     172x320 SPI panel, rotated to a 320x172 landscape canvas, no touch
//     hardware. Two rendering backends, both using this board's ST7789
//     TFT_eSPI config as-is (unlike round21, TFT_eSPI already has working
//     support for this panel, so there was no reason to bypass it for the
//     LVGL backend): env:lcd147 (raw TFT_eSPI draw calls --
//     src/display.cpp, shared with cyd/round128) and env:lcd147_lvgl (LVGL
//     -- src/display_lcd147_lvgl.cpp, include/lv_conf.h). src/touch.cpp
//     (ENABLE_TOUCH=0 makes it compile to a no-op stub) is shared unchanged
//     by both.
//
// platformio.ini's build_src_filter keeps each env compiling only its own
// display/touch files. Panel SPI pins for the two TFT_eSPI boards live in
// platformio.ini as TFT_eSPI build flags. Everything else board-specific —
// touch pins, panel pins/timing, I2C pins, and layout geometry — lives here.
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

#elif defined(BOARD_OLED13)

// No touch hardware on this board at all — every button input comes from
// the MFL menu instead (see menu.h). touch_oled13.cpp is a stub; nothing
// here actually needs TP_DEBOUNCE_MS, kept only so config.h's shape doesn't
// surprise a reader expecting every board branch to define it.
#define TP_DEBOUNCE_MS 250

// --- I2C bus (OLED only — no other I2C device on this board) ---------------
#define I2C_SDA_PIN 16
#define I2C_SCL_PIN 17
#define OLED_I2C_ADDR 0x3C   // 0x3D on genuine Adafruit modules; 0x3C on most
                             // generic/eBay SH1106 breakouts — check yours if
                             // the screen stays blank with no I2C errors.

#elif defined(BOARD_LCD147)

// No touch hardware on this board at all — every button input comes from
// the MFL menu instead (see menu.h), same as BOARD_OLED13. Nothing here
// actually needs TP_DEBOUNCE_MS, kept only so config.h's shape doesn't
// surprise a reader expecting every board branch to define it.
#define TP_DEBOUNCE_MS 250

// Onboard WS2812-style addressable "RGB light bead" per Waveshare's pin
// table. Unused by this firmware, and powers up showing a colour until
// explicitly written to (undefined power-on state, same as any addressable
// LED) -- display::begin() sends it an off frame via neopixelWrite().
#define RGB_LED_PIN 38

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

#endif  // DISPLAY_ROUND / BOARD_ROUND21 / BOARD_OLED13 / BOARD_LCD147

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

// Same sizing intent as VALUE_FONT_CORNER/VALUE_FONT_STRIP above, in px
// instead of a TFT_eSPI font ID -- round128_lvgl (display_round128_lvgl.cpp)
// maps these to DSEG7 (corner/strip digits) or the nearest enabled
// lv_font_montserrat_* (everything else). This board's cells are the exact
// same height as BOARD_LCD147's (CELL_VALUE_H=28, STRIP_VALUE_H=22 both) --
// lcd147's numbers were copied from here in the first place, so these are
// identical rather than coincidentally matching. Unused, harmless macros in
// the non-LVGL round128 build.
#define VALUE_FONT_PX_CORNER 22
#define VALUE_FONT_PX_STRIP  16
#define LABEL_FONT_PX        14
#define STATUS_FONT_PX        16
#define TITLE_FONT_PX          20

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

// Same sizing intent as the VALUE_TEXTSIZE_*/LABEL_TEXTSIZE/STATUS_TEXTSIZE
// macros above, expressed in px instead of an Arduino_GFX integer scale --
// LVGL's bundled fonts (display_round21_lvgl.cpp, the round21_lvgl env only)
// are fixed sizes, not a scale factor, so that backend maps each of these to
// the nearest enabled lv_font_montserrat_*. Unused by the Arduino_GFX
// round21 build; kept here rather than in a backend-specific header since
// this is geometry, same as every other constant in this file.
#define VALUE_FONT_PX_CORNER 48
#define VALUE_FONT_PX_STRIP  28
#define LABEL_FONT_PX        20
#define STATUS_FONT_PX        28
#define TITLE_FONT_PX          28

#define SPLASH_HANDLE_TEXTSIZE 4
#define SPLASH_Y_HANDLE        200
#define SPLASH_Y_RULE          248
#define SPLASH_RULE_HALFLEN    110
#define SPLASH_Y_PRODUCT       272
#define SPLASH_Y_VERSION       312

#elif defined(BOARD_OLED13)

// 128x64 monochrome, no colour, a fraction of the other boards' pixel
// budget — this is a genuinely different, tighter layout (a mini 2x2-style
// grid in text, not the full boxed grid the other three boards share). No
// GRID_X0/CELL_W-style constants here since there's no grid.
#define SCREEN_W 128
#define SCREEN_H 64

// Corner pressures are the primary readout, so they get the big font
// (size-2, 16px tall) — big enough that a "FL:" style label wouldn't fit
// alongside the value, so a single-letter axle label (F/R) sits in a narrow
// column centred in the middle of the row instead, drawn once in
// drawStaticLayout() (it never changes), with the axle's two corner values
// flanking it left/right — same left/right = driver/passenger-side spatial
// convention the other three boards' 2x2 grid uses, just without the box
// round it, and with the axle letter itself right where the grid's centre
// divider would be.
#define GAUGE_TEXTSIZE     2
#define AXLE_LABEL_COL_W   16   // centred column width for F/R
#define ROW_FRONT_Y        10   // big FL | F | big FR
#define ROW_REAR_Y         28   // big RL | R | big RR

// Status used to be its own text row; now it's a single glyph in the top
// right, out of the corner-values' way, so the freed space goes to the
// bigger corner text and to tank/preset (moved lower, each its own row).
// No dedicated "no data yet" glyph — nothing to draw beats a stale-looking
// blank icon.
#define STATUS_ICON_Y      0
// 2px margin from the right edge (not SCREEN_W - 6, which touches it exactly)
// so the burn-in pixel-shift (display_oled13.cpp) never clips it.
#define STATUS_ICON_X      (SCREEN_W - 8)   // one 6px-wide char, size 1
#define STATUS_CHAR_RAISING  '^'
#define STATUS_CHAR_LOWERING 'v'
#define STATUS_CHAR_NOSIGNAL 'X'

#define ROW_TANK_Y         46
#define ROW_PRESET_Y       56

// Menu: title row + up to 3 item rows (title + 3*16 = 64, exactly the
// screen height) — an 8-item Presets list can't all fit at once here, so
// display_oled13.cpp's drawMenu() scrolls a 3-row window that keeps the
// cursor centred, rather than trying to shrink text further.
#define MENU_TEXTSIZE     1
#define MENU_TITLE_Y      0
#define MENU_ITEMS_Y      16
#define MENU_ROW_H        16
#define MENU_VISIBLE_ROWS 3

#define SPLASH_Y_HANDLE   16
#define SPLASH_Y_PRODUCT  32
#define SPLASH_Y_VERSION  48

#elif defined(BOARD_LCD147)

// Native panel is 172x320 portrait; rotated to a 320x172 landscape canvas,
// same convention as the CYD's rotation below. Untested on real hardware —
// if the image comes up mirrored/rotated wrong, try 3 instead of 1 before
// touching anything else.
#define TFT_ROTATION 1

#define SCREEN_W 320
#define SCREEN_H 172

#define GRID_X0  0
#define GRID_Y0  0
#define GRID_W   SCREEN_W
#define GRID_H   108                 // total height of the 2x2 corner grid
#define CELL_W   (GRID_W / 2)        // 160
#define CELL_H   (GRID_H / 2)        // 54

#define STRIP_Y  (GRID_Y0 + GRID_H)  // 108 — tank / preset strip
// The strip holds a static label (font 2, drawn once in drawStaticLayout())
// AND a live value (font 4) stacked below it -- STRIP_H/STRIP_VALUE_TOP/H
// reuse round128's exact proven numbers rather than being scaled from this
// panel's height, since font pixel sizes are fixed and don't scale with the
// screen. Getting this wrong doesn't fail to compile -- it silently paints
// the live value's background square over the static label every update()
// (see display.cpp's drawValue()), erasing "TANK"/"PRESET" a moment after
// drawStaticLayout() draws them.
#define STRIP_H  42                  // 108..150

#define STATUS_Y (STRIP_Y + STRIP_H)  // 150
#define STATUS_H (SCREEN_H - STATUS_Y)  // 22

#define CELL_VALUE_TOP    18   // relative to the cell origin -- round128's numbers
#define CELL_VALUE_H      28
#define STRIP_VALUE_TOP   (STRIP_Y + 16)  // round128's numbers -- starts after
#define STRIP_VALUE_H     22              // the STRIP_Y+4 label, doesn't overlap it

// Same reasoning as round128: this cell is too short for Font 6 (48px,
// digits-only) — Font 4 (26px) is the proven fit at a similar cell height.
#define VALUE_FONT_CORNER 4
#define VALUE_FONT_STRIP  4

// Same sizing intent as VALUE_FONT_CORNER/VALUE_FONT_STRIP above, in px
// instead of a TFT_eSPI font ID -- lcd147_lvgl (display_lcd147_lvgl.cpp)
// maps these to DSEG7 (corner/strip digits) or the nearest enabled
// lv_font_montserrat_* (everything else). Sized to this board's much
// shorter cells than round21's (CELL_VALUE_H=28, STRIP_VALUE_H=22 here vs.
// 56/44 there) -- unused, harmless macros in the non-LVGL lcd147 build.
#define VALUE_FONT_PX_CORNER 22
#define VALUE_FONT_PX_STRIP  16
#define LABEL_FONT_PX        14
#define STATUS_FONT_PX        16
#define TITLE_FONT_PX          20

// Splash text, compact for this panel's short 172px canvas. Y values are
// MC_DATUM centres (see display.cpp's splash()) -- vertically centred so
// the block's top/bottom margins come out roughly equal (~43px each) within
// SCREEN_H=172.
#define SPLASH_HANDLE_FONT 4
#define SPLASH_HANDLE_SIZE 1
#define SPLASH_Y_HANDLE       56
#define SPLASH_Y_RULE         78
#define SPLASH_RULE_HALFLEN   50
#define SPLASH_Y_PRODUCT      98
#define SPLASH_Y_VERSION      120

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

// Same sizing intent as VALUE_FONT_CORNER/VALUE_FONT_STRIP above, in px
// instead of a TFT_eSPI font ID -- env:cyd_lvgl (display_cyd_lvgl.cpp) maps
// these to DSEG7 (corner/strip digits) or the nearest enabled
// lv_font_montserrat_* (everything else). CELL_VALUE_H=58 here is closest to
// BOARD_ROUND21's 56 (-> the same 48px DSEG7 asset); STRIP_VALUE_H=28 sits
// between lcd147's 22 and round21's 44, so 22px leaves a few px of headroom
// rather than nearly filling the cell. Unused, harmless macros in the
// non-LVGL cyd build.
#define VALUE_FONT_PX_CORNER 48
#define VALUE_FONT_PX_STRIP  22
#define LABEL_FONT_PX        16
#define STATUS_FONT_PX        16
#define TITLE_FONT_PX          20

#define SPLASH_HANDLE_FONT 4
#define SPLASH_HANDLE_SIZE 2
#define SPLASH_Y_HANDLE       96
#define SPLASH_Y_RULE         132
#define SPLASH_RULE_HALFLEN   70
#define SPLASH_Y_PRODUCT      156
#define SPLASH_Y_VERSION      186

#endif  // DISPLAY_ROUND / BOARD_ROUND21 / BOARD_OLED13 / BOARD_LCD147 (see the OLED branch above)

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
// Early-2000s BMW/VDO cluster look: black background, amber/orange numerals
// and labels (the colour those clusters actually backlight in), rather than
// white/grey/teal. RAISING stays a genuine green and NOSIGNAL stays a
// genuine red -- those two are real at-a-glance status signals (direction
// while driving, and a fault condition), not just theming, so they're kept
// distinct from the orange family on purpose rather than folded into it.
// Raw RGB565 (not e.g. TFT_eSPI's TFT_BLACK) since this header is shared
// with the round21 build's Arduino_GFX code, which doesn't define
// TFT_eSPI's colour macros.
#define COL_BG        0x0000    // black
#define COL_VALUE     0xFC60    // vivid orange — corner pressures
#define COL_STALE     0x69A1    // dim brownish amber — pressures held over from a lost link
#define COL_LABEL     0x8A40    // dim amber — FL / FR / RL / RR
#define COL_DIVIDER   0x38E0    // near-black amber-brown — static rules
#define COL_TANK      0xFDA5    // warm amber-yellow — distinct from the corner values' orange
#define COL_PRESET    0xFC60    // same vivid orange as COL_VALUE
#define COL_RAISING   0x96E7    // warm green — kept distinct, this is a real status signal
#define COL_LOWERING  0xFAC0    // warm orange-red
#define COL_IDLE      0x5A26    // dim amber-grey
#define COL_NOSIGNAL  0xD8A2    // red — kept distinct, this is a real fault signal
