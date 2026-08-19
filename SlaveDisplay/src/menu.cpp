#include "menu.h"

#include "airlift_espnow.h"
#include "config.h"
#include "display.h"
#include "espnow_link.h"

namespace menu {
namespace {

Mode    s_mode   = Mode::GAUGE;
uint8_t s_cursor = 0;

// Last-seen button bitmask, for edge detection — AirLiftButtons is a state
// broadcast, not discrete press/release events (see airlift_espnow.h), so
// this end has to derive "just pressed" itself.
uint8_t s_lastButtons = 0;

// Live-adjustable, not persisted (no NVS use on the slave today) — resets to
// BACKLIGHT_PCT on reboot.
uint8_t s_backlightPct = BACKLIGHT_PCT;

const char* kTopItems[]      = {"PRESETS", "SETTINGS"};
const char* kSettingsItems[] = {"BACKLIGHT"};

void enterTop() {
  s_mode   = Mode::TOP;
  s_cursor = 0;
}

void enterGauge() {
  s_mode   = Mode::GAUGE;
  s_cursor = 0;
}

void moveCursor(int8_t delta, uint8_t count) {
  if (count == 0) return;
  s_cursor = (uint8_t)((s_cursor + count + delta) % count);
}

void onPress(uint8_t bit) {
  switch (s_mode) {
    case Mode::GAUGE:
      if (bit == MFL_BIT_IO) enterTop();
      break;

    case Mode::TOP:
      if (bit == MFL_BIT_PLUS)       moveCursor(-1, 2);
      else if (bit == MFL_BIT_MINUS) moveCursor(1, 2);
      else if (bit == MFL_BIT_IO)    { s_mode = (s_cursor == 0) ? Mode::PRESETS : Mode::SETTINGS; s_cursor = 0; }
      else if (bit == MFL_BIT_SET)   enterGauge();
      break;

    case Mode::PRESETS:
      if (bit == MFL_BIT_PLUS)       moveCursor(-1, 8);
      else if (bit == MFL_BIT_MINUS) moveCursor(1, 8);
      else if (bit == MFL_BIT_IO)    { espnow::sendCommand(CMD_SELECT_PRESET, s_cursor); enterGauge(); }
      else if (bit == MFL_BIT_SET)   enterTop();
      break;

    case Mode::SETTINGS:
      // Only one item in v1 — PLUS/MINUS adjust it directly rather than
      // moving a cursor that has nowhere else to go.
      if (bit == MFL_BIT_PLUS) {
        s_backlightPct = (uint8_t)min(100, s_backlightPct + 10);
        display::setBacklight(s_backlightPct);
      } else if (bit == MFL_BIT_MINUS) {
        s_backlightPct = (uint8_t)max(10, s_backlightPct - 10);
        display::setBacklight(s_backlightPct);
      } else if (bit == MFL_BIT_IO || bit == MFL_BIT_SET) {
        enterTop();
      }
      break;
  }
}

}  // namespace

void poll() {
  AirLiftButtons b;
  if (!espnow::takeButtons(b)) return;

  const uint8_t pressedNow = b.buttons & ~s_lastButtons;
  s_lastButtons = b.buttons;

  // Only ever act on exactly one newly-pressed bit — mfl.cpp on the master
  // already guarantees at most one bit is set at a time, but a dropped/
  // reordered broadcast could in principle show two edges in one diff, and
  // acting on more than one per frame would double-navigate.
  if (pressedNow == MFL_BIT_PLUS || pressedNow == MFL_BIT_MINUS ||
      pressedNow == MFL_BIT_SET  || pressedNow == MFL_BIT_IO) {
    onPress(pressedNow);
  }
}

bool active() { return s_mode != Mode::GAUGE; }

View currentView() {
  View v;
  v.mode   = s_mode;
  v.cursor = s_cursor;

  switch (s_mode) {
    case Mode::GAUGE:
      break;

    case Mode::TOP:
      v.title     = "MENU";
      v.itemCount = 2;
      for (uint8_t i = 0; i < 2; i++) v.items[i] = kTopItems[i];
      break;

    case Mode::PRESETS:
      v.title     = "PRESETS";
      v.itemCount = 8;
      for (uint8_t i = 0; i < 8; i++) v.items[i] = presetName(i + 1);
      break;

    case Mode::SETTINGS: {
      v.title     = "SETTINGS";
      v.itemCount = 1;
      static char buf[16];
      snprintf(buf, sizeof(buf), "%s %u%%", kSettingsItems[0], s_backlightPct);
      v.items[0] = buf;
      break;
    }
  }
  return v;
}

}  // namespace menu
