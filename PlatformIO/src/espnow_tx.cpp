#include "espnow_tx.h"

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#include "airlift_espnow.h"
#include "defs.h"
#include "globals.h"
#include "mfl.h"
#include "tasks.h"

namespace {

// Broadcast: the display needs no pairing and we need no knowledge of its MAC.
const uint8_t kBroadcastMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

uint32_t s_lastButtonSendMs      = 0;
uint8_t  s_buttonSeq             = 0;
uint32_t s_lastPresetNamesSendMs = 0;

uint8_t buttonsToBitmask(const MflButtons& b) {
  uint8_t mask = 0;
  if (b.plus)  mask |= MFL_BIT_PLUS;
  if (b.minus) mask |= MFL_BIT_MINUS;
  if (b.set)   mask |= MFL_BIT_SET;
  if (b.io)    mask |= MFL_BIT_IO;
  return mask;
}

// Arduino-ESP32 3.x (IDF 5) changed the recv callback signature; support both,
// same as the display's own receiver (SlaveDisplay/src/espnow_link.cpp).
void handleCommand(const uint8_t* data, int len) {
  if (len != (int)sizeof(AirLiftCommand)) return;
  AirLiftCommand cmd;
  memcpy(&cmd, data, sizeof(cmd));

  // Tracks the manual code already written to the event log, so the ~20 Hz
  // CMD_MANUAL_PRESS heartbeat below logs once on the edge into a new press
  // rather than once per frame for as long as the button stays held.
  static uint8_t s_loggedManualCode = 0;

  switch (cmd.cmd) {
    case CMD_SELECT_PRESET:
      queuePresetByIndex(cmd.param, kInterceptPresetHoldMsDefault, "MFL menu");
      break;
    case CMD_MANUAL_PRESS:
      // Same heartbeat pattern as the web UI's "press" action: this call is
      // expected to repeat (~20 Hz, once per AirLiftButtons frame) for as
      // long as the physical MFL button stays held, each call extending the
      // window. If the display stops sending (link drop, button released),
      // the window simply expires — no explicit timeout logic needed here.
      if (cmd.param != s_loggedManualCode) {
        logLine("MFL menu: manual 0x%02X press", (unsigned)cmd.param);
        s_loggedManualCode = cmd.param;
      }
      queueManualButton(cmd.param, kInterceptManualPressWindowMs);
      break;
    case CMD_MANUAL_RELEASE:
      if (s_loggedManualCode != 0) {
        logLine("MFL menu: manual release");
        s_loggedManualCode = 0;
      }
      releaseManualButton();
      break;
    default:
      break;
  }
}

#if ESP_ARDUINO_VERSION_MAJOR >= 3
void onRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
  (void)info;
  handleCommand(data, len);
}
#else
void onRecv(const uint8_t* mac, const uint8_t* data, int len) {
  (void)mac;
  handleCommand(data, len);
}
#endif

bool     s_running    = false;
uint32_t s_lastSendMs = 0;

// init/stop are called from the web-server task (settings toggle) and the power
// manager task (radio about to go down), while the send happens on the ignition
// task. Without this a deinit could land between the running check and the
// send, so every entry point takes it.
SemaphoreHandle_t s_lock = nullptr;

struct Lock {
  bool held;
  Lock() : held(s_lock && xSemaphoreTake(s_lock, portMAX_DELAY) == pdTRUE) {}
  ~Lock() { if (held) xSemaphoreGive(s_lock); }
};

// --- Direction detection ---------------------------------------------------
// The wire carries no "inflating / deflating" flag: the compressor bit only
// says the pump is running, which is also true while it is refilling the tank
// with the bags untouched. So direction comes from whichever of two sources is
// available — a button that is being held right now, or the trend of the
// corner pressures themselves (which also covers preset moves, where the
// manifold closed-loops to a target with no button held at all).
uint8_t  s_trendDir      = AIRLIFT_IDLE;
uint16_t s_trendRefRaw   = 0;     // baseline: mean of the four corners, raw (PSI*2)
uint32_t s_trendSampleMs = 0;
uint32_t s_lastMoveMs    = 0;
bool     s_trendPrimed   = false;

uint16_t cornerMeanRaw() {
  return (uint16_t)(((uint16_t)pressureFL + pressureFR + pressureRL + pressureRR) / 4);
}

// Sampled, not called-per-loop: a fixed window is what makes the threshold mean
// "psi per kEspNowTrendWindowMs", not "psi per however often we happened to run".
void tickTrend(uint32_t now) {
  if (s_trendPrimed && (uint32_t)(now - s_trendSampleMs) < kEspNowTrendWindowMs) return;
  s_trendSampleMs = now;

  const uint16_t cur = cornerMeanRaw();
  if (!s_trendPrimed) {
    s_trendPrimed = true;
    s_trendRefRaw = cur;
    s_lastMoveMs  = now;
    return;
  }

  const int32_t delta = (int32_t)cur - (int32_t)s_trendRefRaw;
  if (delta >= kEspNowTrendRawThresh) {
    s_trendDir    = AIRLIFT_RAISING;
    s_trendRefRaw = cur;
    s_lastMoveMs  = now;
  } else if (delta <= -kEspNowTrendRawThresh) {
    s_trendDir    = AIRLIFT_LOWERING;
    s_trendRefRaw = cur;
    s_lastMoveMs  = now;
  } else if ((uint32_t)(now - s_lastMoveMs) >= kEspNowTrendHoldMs) {
    // Settled. Re-baseline so a slow leak never accumulates into a phantom
    // "LOWERING" hours after the car was parked.
    s_trendDir    = AIRLIFT_IDLE;
    s_trendRefRaw = cur;
  }
}

// 0x5_ = inflate, 0x6_ = deflate (see the BTN_MANUAL_* codes in defs.h).
uint8_t dirFromButtonCode(uint8_t code) {
  switch (code & 0xF0) {
    case 0x50: return AIRLIFT_RAISING;
    case 0x60: return AIRLIFT_LOWERING;
    default:   return AIRLIFT_IDLE;
  }
}

uint8_t deriveStatus(uint32_t now) {
  // No manifold replies = we have nothing true to say about the car.
  if ((uint32_t)(now - lastManifoldFrameMs) >= kEspNowLinStaleMs) return AIRLIFT_NO_SIGNAL;

  // A button we are driving ourselves (web UI / fob / auto-level pulse).
  if (pendingButtonCode != 0 && (int32_t)(pendingButtonRepeatUntilMs - now) > 0) {
    const uint8_t d = dirFromButtonCode(pendingButtonCode);
    if (d != AIRLIFT_IDLE) return d;
  }

  // A button physically held on the handheld right now. Its polls arrive ~5/s,
  // so anything older than kEspNowButtonFreshMs is a released button.
  if (lastButtonB2 == 0x41 && lastButtonB3 != 0 &&
      (uint32_t)(now - lastButtonTimestampMs) < kEspNowButtonFreshMs) {
    const uint8_t d = dirFromButtonCode(lastButtonB3);
    if (d != AIRLIFT_IDLE) return d;
  }

  return s_trendDir;
}

// The wire carries no preset index — only the target pressures the handheld
// broadcast (01 16 47 …). Map those back to a slot via the learned/configured
// preset table, which is exactly how the UI's preset list is populated.
uint8_t derivePreset() {
  if (currentMode != MODE_PRESET) return 0;
  if (lastPresetTargetMs == 0) return 0;   // no target seen since boot
  const uint8_t f = lastPresetTargetFrontPsi;
  const uint8_t r = lastPresetTargetRearPsi;
  for (uint8_t i = 0; i < kPresetCount; i++) {
    if (presetFrontPsi[i] == f && presetRearPsi[i] == r) return (uint8_t)(i + 1);
  }
  return 0;
}

// ESP-NOW must be told which netif to transmit on. We normally run soft-AP
// only, but after a wake the AP may not be back yet — follow whatever is up.
wifi_interface_t txInterface() {
  const wifi_mode_t mode = WiFi.getMode();
  return (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA) ? WIFI_IF_AP : WIFI_IF_STA;
}

// Caller must hold s_lock.
void stopLocked() {
  if (!s_running) return;
  esp_now_del_peer(kBroadcastMac);
  esp_now_deinit();
  s_running = false;
  DEBUG_WIFI("ESP-NOW stopped");
}

}  // namespace

void espnowTxStop() {
  Lock lock;
  stopLocked();
}

void espnowTxInit() {
  // The first call is from setup(), before any task exists to contend for it.
  if (!s_lock) s_lock = xSemaphoreCreateMutex();
  Lock lock;

  stopLocked();
  if (!espnowEnabled) return;

  if (WiFi.getMode() == WIFI_MODE_NULL) {
    DEBUG_WIFI("ESP-NOW not started: radio is off");
    return;
  }

  if (esp_now_init() != ESP_OK) {
    espnowErrors++;
    DEBUG_WIFI("ESP-NOW init FAILED");
    return;
  }

  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, kBroadcastMac, sizeof(kBroadcastMac));
  // channel 0 = "whatever channel the interface is already on". Setting it
  // explicitly would fight the soft-AP, which owns the radio's channel.
  peer.channel = 0;
  peer.ifidx   = txInterface();
  peer.encrypt = false;
  if (esp_now_add_peer(&peer) != ESP_OK) {
    esp_now_deinit();
    espnowErrors++;
    DEBUG_WIFI("ESP-NOW add_peer FAILED");
    return;
  }

  esp_now_register_recv_cb(onRecv);

  s_running               = true;
  s_lastSendMs            = 0;
  s_lastButtonSendMs      = 0;
  s_lastPresetNamesSendMs = 0;

  uint8_t ch = 0;
  wifi_second_chan_t sec = WIFI_SECOND_CHAN_NONE;
  esp_wifi_get_channel(&ch, &sec);
  DEBUG_WIFI("ESP-NOW broadcasting on channel %u (display expects %u)",
             (unsigned)ch, (unsigned)kEspNowChannel);
  if (ch != kEspNowChannel)
    logLine("ESP-NOW: radio on ch %u, display expects ch %u", (unsigned)ch,
            (unsigned)kEspNowChannel);
}

bool espnowTxRunning() { return s_running; }

void espnowTxTick() {
  if (!espnowEnabled || !s_running) return;

  Lock lock;
  if (!s_running) return;   // torn down while we waited

  const uint32_t now = millis();
  tickTrend(now);

  if ((uint32_t)(now - s_lastSendMs) >= kEspNowPeriodMs) {
    s_lastSendMs = now;

    AirLiftData d = {};
    // Corner pressures are stored raw (PSI*2); /2.0f keeps the half-psi step
    // the manifold actually reports. Tank is already stored in PSI.
    d.fl     = pressureFL / 2.0f;
    d.fr     = pressureFR / 2.0f;
    d.rl     = pressureRL / 2.0f;
    d.rr     = pressureRR / 2.0f;
    d.tank   = (float)pressureTank;
    d.preset = derivePreset();
    d.status = deriveStatus(now);

    if (esp_now_send(kBroadcastMac, (const uint8_t*)&d, sizeof(d)) == ESP_OK) {
      espnowSent++;
    } else {
      espnowErrors++;
    }
  }

  if ((uint32_t)(now - s_lastButtonSendMs) >= kEspNowButtonPeriodMs) {
    s_lastButtonSendMs = now;

    AirLiftButtons b = {};
    b.buttons = buttonsToBitmask(mflRead());
    b.seq     = s_buttonSeq++;

    if (esp_now_send(kBroadcastMac, (const uint8_t*)&b, sizeof(b)) == ESP_OK) {
      espnowSent++;
    } else {
      espnowErrors++;
    }
  }

  if ((uint32_t)(now - s_lastPresetNamesSendMs) >= kEspNowPresetNamesPeriodMs) {
    s_lastPresetNamesSendMs = now;

    AirLiftPresetNames pn = {};
    memcpy(pn.names, presetNames, sizeof(pn.names));

    if (esp_now_send(kBroadcastMac, (const uint8_t*)&pn, sizeof(pn)) == ESP_OK) {
      espnowSent++;
    } else {
      espnowErrors++;
    }
  }
}
