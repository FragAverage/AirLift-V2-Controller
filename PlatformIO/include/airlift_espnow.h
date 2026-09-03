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

// ---------------------------------------------------------------------------
// Master -> slave: live MFL steering-wheel button state, broadcast separately
// from (and faster than) AirLiftData so menu navigation stays responsive.
// State, not edges — the slave derives presses itself by diffing consecutive
// frames, so a dropped broadcast just gets caught up by the next one instead
// of losing a button press outright.
// ---------------------------------------------------------------------------
typedef struct {
  uint8_t buttons;  // bit0=PLUS bit1=MINUS bit2=SET bit3=IO; 0 = idle/invalid
  uint8_t seq;      // free-running, purely diagnostic
} AirLiftButtons;

static_assert(sizeof(AirLiftButtons) == 2,
              "AirLiftButtons layout changed — the slave display must be updated to match");

enum : uint8_t {
  MFL_BIT_PLUS  = 0x01,
  MFL_BIT_MINUS = 0x02,
  MFL_BIT_SET   = 0x04,
  MFL_BIT_IO    = 0x08,
};

// ---------------------------------------------------------------------------
// Master -> slave: the 8 configurable preset display names (web UI ->
// presetNames[] in globals.h), so the display's menu/gauge preset text
// matches what was actually configured instead of a hardcoded guess. Sent at
// a fraction of AirLiftData's rate (see kEspNowPresetNamesPeriodMs) since
// these change rarely — no reason to spend airtime on them every tick.
// Fixed-width, NUL-padded like presetNames[] itself; not necessarily
// NUL-terminated if a name fills all 24 bytes, so the receiver must treat it
// the same defensive way (or just re-terminate defensively on receipt).
// ---------------------------------------------------------------------------
typedef struct {
  char names[8][24];
} AirLiftPresetNames;

static_assert(sizeof(AirLiftPresetNames) == 192,
              "AirLiftPresetNames layout changed — the slave display must be updated to match");

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
  CMD_MANUAL_PRESS   = 2,   // param = one of defs.h's BTN_MANUAL_* codes;
                             // repeated sends extend the hold window (same
                             // heartbeat the web UI's "press" action uses)
  CMD_MANUAL_RELEASE = 3,   // param unused
};

// NOTE: unlike AirLiftData/AirLiftButtons, the BTN_MANUAL_* codes CMD_MANUAL_
// PRESS's param carries are NOT redefined here — they already exist as
// `constexpr uint8_t` in defs.h (this project's canonical copy, used
// throughout the LIN/button logic), and every file here that needs them
// already includes defs.h. Redeclaring the same names as an enum in this
// header would conflict wherever both are included. The display side has no
// defs.h equivalent, so its copy of this file (SlaveDisplay/include/
// airlift_espnow.h) DOES define them — see the comment there.
