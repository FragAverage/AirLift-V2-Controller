#pragma once

#include <Arduino.h>

// ---------------------------------------------------------------------------
// ESP-NOW link to the AirLift V2 slave display (../SlaveDisplay).
//
// Broadcasts two packet types to FF:FF:FF:FF:FF:FF, so the display needs no
// pairing and we need no knowledge of its MAC:
//   - AirLiftData (~10 Hz): corner/tank pressures, preset, status.
//   - AirLiftButtons (~20 Hz): live MFL steering-wheel button state, for the
//     display's on-screen menu.
// Also RECEIVES AirLiftCommand from the display (a confirmed menu action,
// e.g. "select preset N") and acts on it via queuePresetByIndex() — the only
// inbound path this firmware has from the display. Same broadcast/no-pairing
// trust model as the outbound data: unencrypted, sanity-checked by length
// only (see airlift_espnow.h).
//
// The radio is shared with the soft-AP: ESP-NOW rides the AP interface on the
// AP's channel, which is pinned to kEspNowChannel so it always matches the
// channel the display was compiled for (ESPNOW_WIFI_CHANNEL).
// ---------------------------------------------------------------------------

// (Re)start ESP-NOW. Safe to call repeatedly; it tears down any previous
// session first. Must be called AFTER the WiFi radio is up (setupWiFi()).
// No-op — and a clean shutdown — when espnowEnabled is false.
void espnowTxInit();

// Tear ESP-NOW down. Called before the power manager kills the radio, so the
// stack is not left holding an interface that no longer exists.
void espnowTxStop();

// Send one packet if enabled, initialised and kEspNowPeriodMs has elapsed.
// Cheap to call at any rate; it rate-limits internally.
void espnowTxTick();

// True while ESP-NOW is initialised and packets are going out.
bool espnowTxRunning();
