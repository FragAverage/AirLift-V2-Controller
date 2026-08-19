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
// trust model as AirLiftData/AirLiftButtons — every command is one of a
// fixed, small set (a preset slot, or one of the eight discrete manual
// button codes below), never an arbitrary pressure value.
// ---------------------------------------------------------------------------
typedef struct {
  uint8_t cmd;
  uint8_t param;
} AirLiftCommand;

static_assert(sizeof(AirLiftCommand) == 2,
              "AirLiftCommand layout changed — the master must be updated to match");

enum : uint8_t {
  CMD_SELECT_PRESET  = 1,   // param = preset index, 0-based
  CMD_MANUAL_PRESS   = 2,   // param = one of the BTN_MANUAL_* codes below;
                             // repeated sends extend the hold window (same
                             // heartbeat the web UI's "press" action uses)
  CMD_MANUAL_RELEASE = 3,   // param unused
};

// Manual corner button codes — must match PlatformIO/include/defs.h's
// BTN_MANUAL_* exactly. Reused as-is (not re-encoded) as the AirLiftCommand
// param for CMD_MANUAL_PRESS: what travels over ESP-NOW is literally the
// same byte the master rewrites into the handheld's LIN poll (see defs.h's
// "Manual Button Codes" table / the top-level README). The master's own
// copy of this file does NOT redefine these — they already exist as
// `constexpr uint8_t` in its defs.h, and redeclaring the same names here
// too would conflict wherever both are included. This side has no defs.h
// equivalent, so they live here instead.
enum : uint8_t {
  BTN_MANUAL_FL_UP   = 0x51,
  BTN_MANUAL_FL_DOWN = 0x61,
  BTN_MANUAL_FR_UP   = 0x52,
  BTN_MANUAL_FR_DOWN = 0x62,
  BTN_MANUAL_RL_UP   = 0x54,
  BTN_MANUAL_RL_DOWN = 0x64,
  BTN_MANUAL_RR_UP   = 0x58,
  BTN_MANUAL_RR_DOWN = 0x68,
  // Axle-combined (both corners of the axle move together) — see
  // PlatformIO/include/defs.h's BTN_MANUAL_FRONT_*/REAR_* comment:
  // extrapolated from the documented bit-scheme, not verified against a
  // real capture (the physical handheld has no axle-only button).
  BTN_MANUAL_FRONT_UP   = 0x53,
  BTN_MANUAL_FRONT_DOWN = 0x63,
  BTN_MANUAL_REAR_UP    = 0x5C,
  BTN_MANUAL_REAR_DOWN  = 0x6C,
  // All four corners together -- these ARE real handheld buttons (unlike
  // the axle-combined pair above), confirmed in the master's defs.h.
  BTN_MANUAL_ALL_UP     = 0x5F,
  BTN_MANUAL_ALL_DOWN   = 0x6F,
};

// Preset 1-8 -> display name. 0 (or anything unknown) -> "---".
const char* presetName(uint8_t preset);
