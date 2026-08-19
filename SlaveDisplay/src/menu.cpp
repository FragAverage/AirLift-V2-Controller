#include "menu.h"

#include <WiFi.h>

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

// Burn-in protection: any menu screen left open with no button activity for
// this long snaps back to the gauge on its own, rather than sitting static
// (and on OLED, burning in) indefinitely. The gauge view is exempt — its
// values are always changing.
constexpr uint32_t kMenuIdleTimeoutMs = 30000;
uint32_t           s_lastActivityMs  = 0;

// Live-adjustable, not persisted (no NVS use on the slave today) — resets to
// BACKLIGHT_PCT on reboot.
uint8_t s_backlightPct = BACKLIGHT_PCT;

const char*      kTopItems[]     = {"PRESETS", "MANUAL", "SETTINGS"};
constexpr uint8_t kTopItemCount  = 3;

// oled13 additionally exposes an INVERT toggle (confirmed visible on real
// hardware, unlike the DC-DC pump register tried earlier) alongside
// BACKLIGHT -- the other boards keep the plain single-value BACKLIGHT item
// they've always had. ABOUT (firmware version / MAC / link status) is last
// on every board.
#ifdef BOARD_OLED13
const char*      kSettingsItems[]     = {"BACKLIGHT", "INVERT", "ABOUT"};
constexpr uint8_t kSettingsItemCount  = 3;
constexpr uint8_t kSettingsAboutIndex = 2;
bool s_invertOn = false;  // not persisted, resets to OFF on reboot
#else
const char*      kSettingsItems[]     = {"BACKLIGHT", "ABOUT"};
constexpr uint8_t kSettingsItemCount  = 2;
constexpr uint8_t kSettingsAboutIndex = 1;
#endif

// MANUAL axle select (FRONT/REAR move both corners of that axle together,
// ALL moves all four) — order matches the BTN_MANUAL_*_UP/DOWN tables below.
const char*   kManualAxles[]     = {"FRONT", "REAR", "ALL"};
const uint8_t kManualUpCodes[]   = {BTN_MANUAL_FRONT_UP,   BTN_MANUAL_REAR_UP,   BTN_MANUAL_ALL_UP};
const uint8_t kManualDownCodes[] = {BTN_MANUAL_FRONT_DOWN, BTN_MANUAL_REAR_DOWN, BTN_MANUAL_ALL_DOWN};
constexpr uint8_t kManualAxleCount = 3;

// MANUAL_ACTIVE-only state: which axle, which codes to send, and whether
// we're currently mid-press (so poll() knows to send exactly one
// CMD_MANUAL_RELEASE on release rather than one per idle frame).
uint8_t s_manualUpCode    = 0;
uint8_t s_manualDownCode  = 0;
bool    s_manualHeld      = false;
uint8_t s_manualDirection = AIRLIFT_IDLE;   // reuses the gauge's status enum

// Whole psi only, "--" for an untrustworthy reading — same convention as
// each board's own formatPsi() for the gauge view, duplicated here (rather
// than shared via a header) since it's five lines and pulling in a shared
// formatting header across four board-specific files isn't worth it for that.
void formatPsi(float psi, char* out, size_t n) {
  if (isnan(psi) || psi < -0.5f) {
    snprintf(out, n, "--");
  } else {
    snprintf(out, n, "%.0f", psi);
  }
}

void enterTop() {
  s_mode   = Mode::TOP;
  s_cursor = 0;
}

void enterGauge() {
  s_mode   = Mode::GAUGE;
  s_cursor = 0;
}

void enterManualActive() {
  s_manualUpCode    = kManualUpCodes[s_cursor];
  s_manualDownCode  = kManualDownCodes[s_cursor];
  s_manualHeld      = false;
  s_manualDirection = AIRLIFT_IDLE;
  s_mode            = Mode::MANUAL_ACTIVE;
  // s_cursor is left as-is (still the chosen axle, 0-1) so MANUAL_ACTIVE's
  // view can name it, and backing out with SET returns to the same spot in
  // the axle list rather than resetting to FRONT.
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
      if (bit == MFL_BIT_PLUS)       moveCursor(-1, kTopItemCount);
      else if (bit == MFL_BIT_MINUS) moveCursor(1, kTopItemCount);
      else if (bit == MFL_BIT_IO) {
        s_mode = (s_cursor == 0) ? Mode::PRESETS
               : (s_cursor == 1) ? Mode::MANUAL
                                  : Mode::SETTINGS;
        s_cursor = 0;
      } else if (bit == MFL_BIT_SET) {
        enterGauge();
      }
      break;

    case Mode::PRESETS:
      if (bit == MFL_BIT_PLUS)       moveCursor(-1, 8);
      else if (bit == MFL_BIT_MINUS) moveCursor(1, 8);
      else if (bit == MFL_BIT_IO)    { espnow::sendCommand(CMD_SELECT_PRESET, s_cursor); enterGauge(); }
      else if (bit == MFL_BIT_SET)   enterTop();
      break;

    case Mode::SETTINGS:
      // PLUS/MINUS move the cursor like any other list; IO enters
      // BACKLIGHT's edit submode, opens ABOUT, or (OLED13 only) toggles
      // INVERT immediately (a boolean doesn't need its own submode).
      if (bit == MFL_BIT_PLUS)       moveCursor(-1, kSettingsItemCount);
      else if (bit == MFL_BIT_MINUS) moveCursor(1, kSettingsItemCount);
      else if (bit == MFL_BIT_IO) {
        if (s_cursor == 0) {
          s_mode = Mode::SETTINGS_BACKLIGHT;
        } else if (s_cursor == kSettingsAboutIndex) {
          s_mode = Mode::ABOUT;
#ifdef BOARD_OLED13
        } else {
          s_invertOn = !s_invertOn;
          display::setInvert(s_invertOn);
#endif
        }
      } else if (bit == MFL_BIT_SET) {
        enterTop();
      }
      break;

    case Mode::SETTINGS_BACKLIGHT:
      if (bit == MFL_BIT_PLUS) {
        s_backlightPct = (uint8_t)min(100, s_backlightPct + 10);
        display::setBacklight(s_backlightPct);
      } else if (bit == MFL_BIT_MINUS) {
        s_backlightPct = (uint8_t)max(10, s_backlightPct - 10);
        display::setBacklight(s_backlightPct);
      } else if (bit == MFL_BIT_IO || bit == MFL_BIT_SET) {
        s_mode = Mode::SETTINGS;   // back to the list, cursor stays on BACKLIGHT
      }
      break;

    case Mode::ABOUT:
      // Static info screen, nothing to adjust — IO/SET both just back out.
      // s_cursor is left as-is (still ABOUT's own row in the SETTINGS list),
      // same convention enterManualActive() uses, so returning lands back
      // where you left it rather than resetting to the top of the list.
      if (bit == MFL_BIT_IO || bit == MFL_BIT_SET) {
        s_mode = Mode::SETTINGS;
      }
      break;

    case Mode::MANUAL:
      if (bit == MFL_BIT_PLUS)       moveCursor(-1, kManualAxleCount);
      else if (bit == MFL_BIT_MINUS) moveCursor(1, kManualAxleCount);
      else if (bit == MFL_BIT_IO)    enterManualActive();
      else if (bit == MFL_BIT_SET)   enterTop();
      break;

    case Mode::MANUAL_ACTIVE:
      // Handled separately in poll() — PLUS/MINUS are level-sensitive here,
      // not edge-triggered, so they never reach onPress().
      break;
  }
}

// PLUS/MINUS are read as "currently held", not "just pressed" — holding the
// physical MFL button should air a corner up/down continuously, the same
// way the real handheld's manual buttons already behave, not fire one tap
// per press. SET is still an edge (one release + back per press).
void handleManualActive(uint8_t current, uint8_t pressedNow) {
  if (pressedNow & MFL_BIT_SET) {
    if (s_manualHeld) {
      espnow::sendCommand(CMD_MANUAL_RELEASE, 0);
      s_manualHeld = false;
    }
    s_manualDirection = AIRLIFT_IDLE;
    s_mode             = Mode::MANUAL;
    return;
  }

  const bool wantsUp   = current & MFL_BIT_PLUS;
  const bool wantsDown = current & MFL_BIT_MINUS;

  if (wantsUp || wantsDown) {
    // Repeats every AirLiftButtons frame (~20 Hz) for as long as the button
    // stays held — each send extends the master's hold window, same
    // heartbeat the web UI's "press" action already relies on.
    espnow::sendCommand(CMD_MANUAL_PRESS, wantsUp ? s_manualUpCode : s_manualDownCode);
    s_manualHeld      = true;
    s_manualDirection = wantsUp ? AIRLIFT_RAISING : AIRLIFT_LOWERING;
  } else if (s_manualHeld) {
    espnow::sendCommand(CMD_MANUAL_RELEASE, 0);
    s_manualHeld      = false;
    s_manualDirection = AIRLIFT_IDLE;
  }
}

}  // namespace

void poll() {
  AirLiftButtons b;
  if (!espnow::takeButtons(b)) return;

  if (b.buttons != 0) s_lastActivityMs = millis();

  if (s_mode != Mode::GAUGE &&
      millis() - s_lastActivityMs > kMenuIdleTimeoutMs) {
    enterGauge();
    s_lastButtons = b.buttons;   // don't let the stale edge fire on return
    return;
  }

  const uint8_t pressedNow = b.buttons & ~s_lastButtons;
  s_lastButtons = b.buttons;

  if (s_mode == Mode::MANUAL_ACTIVE) {
    handleManualActive(b.buttons, pressedNow);
    return;
  }

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

View currentView(const AirLiftData& liveData) {
  View v;
  v.mode   = s_mode;
  v.cursor = s_cursor;

  switch (s_mode) {
    case Mode::GAUGE:
      break;

    case Mode::TOP:
      v.title     = "MENU";
      v.itemCount = kTopItemCount;
      for (uint8_t i = 0; i < kTopItemCount; i++) v.items[i] = kTopItems[i];
      break;

    case Mode::PRESETS:
      v.title     = "PRESETS";
      v.itemCount = 8;
      for (uint8_t i = 0; i < 8; i++) v.items[i] = presetName(i + 1);
      break;

    case Mode::SETTINGS:
    case Mode::SETTINGS_BACKLIGHT: {
      // Same list rendering for both -- SETTINGS_BACKLIGHT only changes
      // which button presses do (see onPress()), the cursor stays parked
      // on BACKLIGHT the whole time so that row visibly highlights while
      // its value is being live-edited.
      v.title     = "SETTINGS";
      v.itemCount = kSettingsItemCount;
      static char buf0[16];
      snprintf(buf0, sizeof(buf0), "%s %u%%", kSettingsItems[0], s_backlightPct);
      v.items[0] = buf0;
#ifdef BOARD_OLED13
      static char buf1[16];
      snprintf(buf1, sizeof(buf1), "%s %s", kSettingsItems[1], s_invertOn ? "ON" : "OFF");
      v.items[1] = buf1;
#endif
      v.items[kSettingsAboutIndex] = kSettingsItems[kSettingsAboutIndex];
      break;
    }

    case Mode::ABOUT: {
      v.title = "ABOUT";
      static char line0[24];
      static char line1[24];
      static char line2[24];
      snprintf(line0, sizeof(line0), "FW %s", SLAVE_FW_VERSION);
      snprintf(line1, sizeof(line1), "%s", WiFi.macAddress().c_str());
      const uint32_t lastMs = espnow::lastPacketMs();
      if (lastMs == 0) {
        snprintf(line2, sizeof(line2), "LINK: none yet");
      } else {
        const uint32_t ageS = (millis() - lastMs) / 1000;
        snprintf(line2, sizeof(line2), "LINK %s %lus",
                 espnow::alive() ? "OK" : "LOST", (unsigned long)ageS);
      }
      v.itemCount = 3;
      v.items[0]  = line0;
      v.items[1]  = line1;
      v.items[2]  = line2;
      break;
    }

    case Mode::MANUAL:
      v.title     = "MANUAL";
      v.itemCount = kManualAxleCount;
      for (uint8_t i = 0; i < kManualAxleCount; i++) v.items[i] = kManualAxles[i];
      break;

    case Mode::MANUAL_ACTIVE: {
      v.title            = kManualAxles[s_cursor];
      v.hasLivePressures = true;
      v.direction        = s_manualDirection;
      // FRONT (s_cursor==0) -> FL/FR, REAR (1) -> RL/RR. ALL (2) moves all
      // four corners together, which doesn't fit the display's two-value
      // left/right layout — show the driver-side/passenger-side average
      // instead (FL+RL)/2 and (FR+RR)/2, which stays physically meaningful
      // since both axles should move roughly in sync under ALL anyway.
      float left, right;
      if (s_cursor == 0)      { left = liveData.fl; right = liveData.fr; }
      else if (s_cursor == 1) { left = liveData.rl; right = liveData.rr; }
      else                    { left = (liveData.fl + liveData.rl) / 2.0f;
                                 right = (liveData.fr + liveData.rr) / 2.0f; }
      formatPsi(left, v.leftPsi, sizeof(v.leftPsi));
      formatPsi(right, v.rightPsi, sizeof(v.rightPsi));
      break;
    }
  }
  return v;
}

uint8_t currentBacklightPct() { return s_backlightPct; }

}  // namespace menu
