// ---------------------------------------------------------------------------
// AirLift V2 — Slave Display
//
// ESP32-2432S028R (Cheap Yellow Display). Listens for AirLiftData broadcasts
// from the AirLift V2 MITM master over ESP-NOW and renders them as a four
// corner pressure cluster with tank / preset / status readouts.
//
// This end is receive-only: it never transmits, never pairs, and holds no
// state of its own beyond "what did the last packet say".
// ---------------------------------------------------------------------------
#include <Arduino.h>

#include "airlift_espnow.h"
#include "config.h"
#include "display.h"
#include "espnow_link.h"
#include "menu.h"
#include "touch.h"

namespace {

// Last packet contents. Held (dimmed) on screen when the link drops so the
// driver can still see where the car was parked.
AirLiftData data = {};

#if DEMO_MODE
// Bench test without a master. Ticks at the rate the master will actually send
// at, not at loop() speed, so the panel looks like the real thing — and sweeps
// the corners across the whole 0..160 psi range so the widest value the display
// can ever show gets exercised on glass.
#define DEMO_TICK_MS 150

void demoTick() {
  const float t = millis() / 1000.0f;
  data.fl     = 80.0f + 80.0f * sinf(t * 0.25f);
  data.fr     = 80.0f + 80.0f * sinf(t * 0.25f + 0.3f);
  data.rl     = 80.0f + 80.0f * sinf(t * 0.20f + 1.1f);
  data.rr     = 80.0f + 80.0f * sinf(t * 0.20f + 1.4f);
  data.tank   = 140.0f + 20.0f * sinf(t * 0.2f);
  data.preset = 1 + ((uint8_t)(t / 5.0f) % 8);
  data.status = (uint8_t)(((uint32_t)t / 3) % 3);
}
#endif

}  // namespace

const char* presetName(uint8_t preset) {
  switch (preset) {
    case 1: return "SLAM";
    case 2: return "DRIVE";
    case 3: return "SPORT";
    case 4: return "PRESET 4";
    case 5: return "PRESET 5";
    case 6: return "PRESET 6";
    case 7: return "PRESET 7";
    case 8: return "PRESET 8";
    default: return "---";
  }
}

void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(200);
  Serial.println("\n[SYS] AirLift V2 slave display booting");

  // Loads persisted settings (ROTATE 180) from NVS -- must run before
  // display::begin() so each board's initial panel rotation is already known
  // at init time rather than only taking effect the next time the menu is
  // opened.
  menu::begin();

  display::begin();

  // Bring the radio up before the splash blocks: packets that arrive during
  // those two seconds then count, so the gauge can come up already live
  // instead of sitting on NO SIGNAL while the master is plainly talking.
  espnow::begin();
  touch::begin();

  display::splash();
  display::drawStaticLayout();

  // Whatever arrived during the splash, or NO SIGNAL if nothing did.
  espnow::take(data);
  display::update(data, espnow::alive());
  Serial.println("[SYS] ready");
}

void loop() {
  // Menu state comes from the master's MFL button broadcast regardless of
  // DEMO_MODE (that flag only fakes pressure telemetry).
  menu::poll();

  // Telemetry ingestion runs unconditionally, whether or not the menu is
  // open — the MANUAL_ACTIVE screen shows live pressures while you hold
  // PLUS/MINUS, which needs `data` to actually keep updating in the
  // background rather than freezing at whatever it was when the menu opened.
#if DEMO_MODE
  static uint32_t nextTick = 0;
  bool gotFreshData = false;
  if ((int32_t)(millis() - nextTick) >= 0) {
    nextTick = millis() + DEMO_TICK_MS;
    demoTick();
    gotFreshData = true;
  }
#else
  const bool gotFreshData = espnow::take(data);
  if (gotFreshData) {
    Serial.printf("[NOW] FL %.1f FR %.1f RL %.1f RR %.1f tank %.1f "
                  "preset %u status %u\n",
                  data.fl, data.fr, data.rl, data.rr, data.tank,
                  data.preset, data.status);
  }
#endif

  // A GAUGE<->MENU transition means the screen currently holds pixels from
  // whichever mode isn't running any more — force the entered mode to
  // repaint everything rather than trust either side's diff cache.
  static bool wasMenuActive = false;
  const bool  menuActive    = menu::active();
  if (menuActive != wasMenuActive) {
    if (menuActive) {
      display::drawMenu(menu::currentView(data), true);
    } else {
      display::drawStaticLayout();   // also resets update()'s diff cache
    }
    wasMenuActive = menuActive;
  }

  if (menuActive) {
    display::drawMenu(menu::currentView(data), false);
  } else {
    // update() diffs internally, so calling it every pass is cheap — it only
    // touches the panel when a rendered value actually changed (including
    // the link going stale).
#if DEMO_MODE
    if (gotFreshData) display::update(data, true);
#else
    display::update(data, espnow::alive());
#endif
  }

  touch::poll();
  delay(10);
}
