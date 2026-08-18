#pragma once

#include <Arduino.h>

// ---------------------------------------------------------------------------
// Wire format shared with the AirLift V2 slave display (../SlaveDisplay).
//
// *** This struct must stay byte-identical to the display's copy
// *** (SlaveDisplay/include/airlift_espnow.h). ***
// Both ends are built by the same xtensa toolchain with default packing, so
// the layout is 5x float + 2x uint8 + 2 bytes tail padding = 24 bytes.
// If you ever change it, change it on BOTH sides — the receiver can only
// sanity-check the length, not the field layout.
// ---------------------------------------------------------------------------
typedef struct {
  float   fl;      // front left pressure (psi)
  float   fr;      // front right pressure (psi)
  float   rl;      // rear left pressure (psi)
  float   rr;      // rear right pressure (psi)
  float   tank;    // tank pressure (psi)
  uint8_t preset;  // active preset number (1-8, 0 = none)
  uint8_t status;  // 0=idle, 1=raising, 2=lowering, 3=no signal
} AirLiftData;

static_assert(sizeof(AirLiftData) == 24,
              "AirLiftData layout changed — the slave display must be updated to match");

// status field values
enum : uint8_t {
  AIRLIFT_IDLE      = 0,
  AIRLIFT_RAISING   = 1,
  AIRLIFT_LOWERING  = 2,
  AIRLIFT_NO_SIGNAL = 3,
};
