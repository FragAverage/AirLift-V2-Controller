#pragma once

// ---------------------------------------------------------------------------
// DSEG7 Classic Bold, converted to LVGL's font format at the sizes each LVGL
// display backend actually uses (round21_lvgl: 48px corner/manual-adjust
// pressure digits, 28px tank digits; lcd147_lvgl: 22px corner/manual-adjust,
// 16px tank -- this board's cells are much shorter, see config.h's
// BOARD_LCD147 geometry) -- the segmented "digital clock/calculator" look
// real 90s BMW OBC/trip-computer sub-displays use for their numeric
// readouts, standing in contrast to the plain Montserrat used everywhere
// else (labels, status text, menu, preset names) which was never meant to
// look like a digit display in the first place.
//
// Picked up automatically by whichever LVGL env's display_*_lvgl.cpp
// actually includes this header -- PlatformIO's chain-mode Library
// Dependency Finder only pulls a `lib/` folder into an env's build once
// something in that env #includes it, so this whole folder (all four
// generated sizes) is simply absent from every non-LVGL env's build with no
// build_src_filter entry needed anywhere.
//
// Font: DSEG7 Classic by Keshikan (SIL Open Font License 1.1, see
// DSEG-LICENSE.txt in this folder) -- https://github.com/keshikan/DSEG.
// Character set is deliberately narrow (space, '-', '0'-'9' only, via
// lv_font_conv's --symbols) since these fonts are only ever used for
// formatPsi()'s numeric output, never labels/words.
//
// Regenerate with (from the SlaveDisplay dir, DSEG7Classic-Bold.ttf from a
// https://github.com/keshikan/DSEG release):
//   npx lv_font_conv --font DSEG7Classic-Bold.ttf --symbols ' -0123456789' \
//     --size 48 --bpp 4 --format lvgl --lv-font-name font_dseg7_48 \
//     -o lib/dseg7_font/font_dseg7_48.c
// (swap --size/--lv-font-name/-o for the 28/22/16px files)
// ---------------------------------------------------------------------------
#include <lvgl.h>

extern const lv_font_t font_dseg7_48;  // round21_lvgl: corner + manual-adjust
extern const lv_font_t font_dseg7_28;  // round21_lvgl: tank digits
extern const lv_font_t font_dseg7_22;  // lcd147_lvgl: corner + manual-adjust
extern const lv_font_t font_dseg7_16;  // lcd147_lvgl: tank digits
