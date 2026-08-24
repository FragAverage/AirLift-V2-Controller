#pragma once

#include <Arduino.h>

#include "airlift_espnow.h"

// ---------------------------------------------------------------------------
// MFL-button-driven on-screen menu. Board-agnostic — no TFT_eSPI/Arduino_GFX
// dependency, compiled the same on all four envs (like main.cpp). Reads live
// button state via espnow::takeButtons() and does its own edge detection
// (state is broadcast, not events — see airlift_espnow.h), so this is the one
// place in the codebase that turns "PLUS is currently held" into "PLUS was
// just pressed" — except in MANUAL_ACTIVE, which is deliberately
// level-sensitive on PLUS/MINUS instead (see menu.cpp), so holding the
// physical button airs a corner up/down continuously rather than one tap per
// press, matching how the real handheld's manual buttons already work.
//
// Mapping: IO = enter / select, PLUS = up, MINUS = down, SET/ON = back.
// ---------------------------------------------------------------------------
namespace menu {

// Loads persisted settings (currently just ROTATE 180 -- see rotate180()
// below) from NVS via the ESP32 Preferences library. Call once from
// main.cpp's setup(), before display::begin(), so the initial rotation is
// already known when each board applies it at panel init.
void begin();

// SETTINGS_BACKLIGHT: OLED13-only edit submode entered from the SETTINGS
// list when BACKLIGHT is selected (see menu.cpp) -- PLUS/MINUS adjust the
// value live, SET/IO returns to the SETTINGS list. Unreachable on the other
// boards, where SETTINGS itself is still the single-item direct-adjust
// screen it always was.
enum class Mode : uint8_t { GAUGE, TOP, PRESETS, SETTINGS, SETTINGS_BACKLIGHT, ABOUT, MANUAL, MANUAL_ACTIVE };

// What display::drawMenu() should render this frame. `items`/`itemCount`
// unused when mode == GAUGE.
struct View {
  Mode        mode      = Mode::GAUGE;
  const char* title     = "";
  const char* items[8]  = {};
  uint8_t     itemCount = 0;
  uint8_t     cursor    = 0;

  // MANUAL_ACTIVE only: live pressures for the axle being adjusted (already
  // formatted the same way the gauge view does — whole PSI, "--" for an
  // untrustworthy reading), so the display can show real numbers moving in
  // real time instead of just a static "HOLD +/-". `direction` mirrors the
  // AIRLIFT_RAISING/LOWERING/IDLE values in airlift_espnow.h.
  bool        hasLivePressures = false;
  char        leftPsi[6]       = {};   // FL (front) or RL (rear)
  char        rightPsi[6]      = {};   // FR (front) or RR (rear)
  uint8_t     direction        = AIRLIFT_IDLE;
};

// Reads espnow::takeButtons(), edge-detects against the last-seen state, and
// advances the menu state machine. Call every loop() iteration — cheap when
// there's nothing new to process.
void poll();

// False while showing the gauge (main.cpp should call display::update()
// instead of display::drawMenu() in that case).
bool active();

// Current view for display::drawMenu(). Only meaningful while active().
// `liveData` populates View::leftPsi/rightPsi/direction in MANUAL_ACTIVE —
// pass whatever main.cpp's latest AirLiftData is, ignored in every other
// mode.
View currentView(const AirLiftData& liveData);

// The Settings screen's live-adjusted brightness (0-100, starts at
// BACKLIGHT_PCT). display::drawStaticLayout() restores this — not the
// hardcoded BACKLIGHT_PCT default — when redrawing the gauge after the menu
// closes, so a brightness change made in Settings survives leaving the menu
// instead of silently reverting to default.
uint8_t currentBacklightPct();

// The Settings screen's ROTATE 180 toggle, persisted in NVS (unlike
// BACKLIGHT/INVERT above) so a physically upside-down mount survives a power
// cycle. begin() loads the saved value; each board's display::begin() reads
// this once at boot to set its initial orientation, and menu.cpp calls
// display::setRotate180() immediately when the toggle changes so the screen
// flips live without needing a reboot.
bool rotate180();

}  // namespace menu
