#pragma once

#include <Arduino.h>

// ---------------------------------------------------------------------------
// Wire format shared with the AirLift V2 master.
//
// *** This struct must stay byte-identical to the master's copy. ***
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
              "AirLiftData layout changed — the master must be updated to match");

// status field values
enum : uint8_t {
  AIRLIFT_IDLE      = 0,
  AIRLIFT_RAISING   = 1,
  AIRLIFT_LOWERING  = 2,
  AIRLIFT_NO_SIGNAL = 3,
};

// ---------------------------------------------------------------------------
// Master -> slave: live MFL steering-wheel button state, broadcast separately
// from (and faster than) AirLiftData so menu navigation stays responsive.
// State, not edges — this end derives presses itself by diffing consecutive
// frames, so a dropped broadcast just gets caught up by the next one instead
// of losing a button press outright.
// ---------------------------------------------------------------------------
typedef struct {
  uint8_t buttons;  // bit0=PLUS bit1=MINUS bit2=SET bit3=IO; 0 = idle/invalid
  uint8_t seq;      // free-running, purely diagnostic
} AirLiftButtons;

static_assert(sizeof(AirLiftButtons) == 2,
              "AirLiftButtons layout changed — the master must be updated to match");

enum : uint8_t {
  MFL_BIT_PLUS  = 0x01,
  MFL_BIT_MINUS = 0x02,
  MFL_BIT_SET   = 0x04,
  MFL_BIT_IO    = 0x08,
};

// ---------------------------------------------------------------------------
// Slave -> master: a confirmed menu action. Broadcast, unencrypted, same
// trust model as AirLiftData/AirLiftButtons — the only thing a command can do
// is select from the vehicle's own pre-configured preset table, no arbitrary
// pressure injection.
// ---------------------------------------------------------------------------
typedef struct {
  uint8_t cmd;
  uint8_t param;
} AirLiftCommand;

static_assert(sizeof(AirLiftCommand) == 2,
              "AirLiftCommand layout changed — the master must be updated to match");

enum : uint8_t {
  CMD_SELECT_PRESET = 1,   // param = preset index, 0-based
};

// Preset 1-8 -> display name. 0 (or anything unknown) -> "---".
const char* presetName(uint8_t preset);
