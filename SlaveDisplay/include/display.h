#pragma once

#include "airlift_espnow.h"
#include "menu.h"

namespace display {

// Panel init, backlight on, landscape rotation, black fill, sprite alloc.
void begin();

// Backlight brightness, 0-100. Driven by PWM, so this can later be tied to the
// car's dip-beam/illumination signal for a night setting.
void setBacklight(uint8_t percent);

#ifdef BOARD_OLED13
// SH1106 pixel-swap invert, confirmed visible on real hardware -- OLED13
// only, exposed as a Settings toggle alongside BACKLIGHT (see menu.cpp).
void setInvert(bool on);
#endif

// Boot splash — handle, product name, firmware version — with the backlight
// faded up over it. Blocks for SPLASH_HOLD_MS. Call between begin() and
// drawStaticLayout().
void splash();

// Everything that never changes: dividers, corner labels, TANK / PRESET
// captions. Called once from setup() — update() only ever touches value boxes.
void drawStaticLayout();

// Repaints the values that actually changed since the last call.
// `signalOk` false renders the held pressures dimmed and forces the status bar
// to NO SIGNAL regardless of what the last packet said.
void update(const AirLiftData& d, bool signalOk);

// Renders the MFL menu (see menu.h) instead of the gauge. Called in place of
// update() whenever menu::active() is true. Diffs against its own
// last-rendered state the same way update() does — EXCEPT `force`, which
// must be true on the first call after the gauge was showing (the physical
// screen holds gauge pixels the diff cache doesn't know about, so a view
// that happens to match the cache would otherwise wrongly skip the redraw
// and leave stale gauge content on screen).
void drawMenu(const menu::View& view, bool force);

}  // namespace display
