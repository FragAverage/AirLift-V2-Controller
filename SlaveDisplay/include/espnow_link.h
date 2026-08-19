#pragma once

#include "airlift_espnow.h"

// ---------------------------------------------------------------------------
// ESP-NOW link, mostly receive. We never need the master's MAC to receive:
// ESP-NOW delivers broadcast (and unicast-to-us) frames to the receive
// callback regardless of whether the sender is a registered peer. Sending
// (sendCommand(), for confirmed menu actions) broadcasts the same way the
// master does — a peer for the broadcast MAC is registered in begin().
// ---------------------------------------------------------------------------
namespace espnow {

// STA mode + fixed channel + esp_now_init + recv callback + broadcast peer
// (for sendCommand()). Halts on failure.
void begin();

// Returns true (once) when a fresh AirLiftData packet has arrived, copying it
// into `out`. Clears the new-data flag.
bool take(AirLiftData& out);

// Returns true (once) when a fresh AirLiftButtons packet has arrived, copying
// it into `out`. Clears the new-data flag. Separate from take()/AirLiftData —
// this rides its own, faster broadcast (see PlatformIO/src/espnow_tx.cpp).
bool takeButtons(AirLiftButtons& out);

// Broadcast a confirmed menu action to the master (e.g. CMD_SELECT_PRESET).
void sendCommand(uint8_t cmd, uint8_t param);

// millis() of the last valid AirLiftData packet, or 0 if nothing has ever
// been received.
uint32_t lastPacketMs();

// True while an AirLiftData packet has been seen inside LINK_TIMEOUT_MS.
bool alive();

}  // namespace espnow
