#pragma once

#include "airlift_espnow.h"

// ---------------------------------------------------------------------------
// Receive-only ESP-NOW link. We never need the master's MAC: ESP-NOW delivers
// broadcast (and unicast-to-us) frames to the receive callback regardless of
// whether the sender is a registered peer.
// ---------------------------------------------------------------------------
namespace espnow {

// STA mode + fixed channel + esp_now_init + recv callback. Halts on failure.
void begin();

// Returns true (once) when a fresh packet has arrived, copying it into `out`.
// Clears the new-data flag.
bool take(AirLiftData& out);

// millis() of the last valid packet, or 0 if nothing has ever been received.
uint32_t lastPacketMs();

// True while a packet has been seen inside LINK_TIMEOUT_MS.
bool alive();

}  // namespace espnow
