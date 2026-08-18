#pragma once

#include "airlift_espnow.h"

namespace display {

// Panel init, backlight on, landscape rotation, black fill, sprite alloc.
void begin();

// Backlight brightness, 0-100. Driven by PWM, so this can later be tied to the
// car's dip-beam/illumination signal for a night setting.
void setBacklight(uint8_t percent);

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

}  // namespace display
