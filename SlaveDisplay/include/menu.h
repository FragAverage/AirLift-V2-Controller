#pragma once

#include <Arduino.h>

// ---------------------------------------------------------------------------
// MFL-button-driven on-screen menu. Board-agnostic — no TFT_eSPI/Arduino_GFX
// dependency, compiled the same on all three envs (like main.cpp). Reads live
// button state via espnow::takeButtons() and does its own edge detection
// (state is broadcast, not events — see airlift_espnow.h), so this is the one
// place in the codebase that turns "PLUS is currently held" into "PLUS was
// just pressed".
//
// Mapping: IO = enter / select, PLUS = up, MINUS = down, SET/ON = back.
// ---------------------------------------------------------------------------
namespace menu {

enum class Mode : uint8_t { GAUGE, TOP, PRESETS, SETTINGS };

// What display::drawMenu() should render this frame. `items`/`itemCount`
// unused when mode == GAUGE.
struct View {
  Mode        mode      = Mode::GAUGE;
  const char* title     = "";
  const char* items[8]  = {};
  uint8_t     itemCount = 0;
  uint8_t     cursor    = 0;
};

// Reads espnow::takeButtons(), edge-detects against the last-seen state, and
// advances the menu state machine. Call every loop() iteration — cheap when
// there's nothing new to process.
void poll();

// False while showing the gauge (main.cpp should call display::update()
// instead of display::drawMenu() in that case).
bool active();

// Current view for display::drawMenu(). Only meaningful while active().
View currentView();

}  // namespace menu
