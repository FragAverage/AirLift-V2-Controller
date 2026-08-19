#pragma once

#include <Arduino.h>

// ---------------------------------------------------------------------------
// BMW MFL steering-wheel cruise-button decode — GPIO34 (pinMflSignal), through
// the optocoupler isolation stage on the AirLift-V2 PCB.
//
// Ported from the bench sniffer (test/mfl_sniffer/src/main.cpp), itself
// ported from pazi88's MFL.ino. See MFL-FINDINGS.md for the full protocol
// write-up and the reasoning behind the two decode rules applied here:
//   - value == 0 is idle, not "all four buttons pressed" (checked first).
//   - only trust a decode where *exactly one* of the four bitmasks matches;
//     anything else is a discarded, not-acted-on frame.
// ---------------------------------------------------------------------------

struct MflButtons {
  bool plus  = false;   // RES/+
  bool minus = false;   // SET/-
  bool set   = false;   // this wheel's "SET" button decodes as pazi88's ON mask
  bool io    = false;   // on/off toggle

  bool operator==(const MflButtons& o) const {
    return plus == o.plus && minus == o.minus && set == o.set && io == o.io;
  }
  bool operator!=(const MflButtons& o) const { return !(*this == o); }

  // True when exactly one button is set — the only state actions should be
  // triggered from. Also true for the all-false idle state.
  uint8_t count() const { return (uint8_t)plus + minus + set + io; }
};

// Attaches the pulse-train ISR on pinMflSignal. Call once from setup().
void mflInit();

// Latest confirmed button state (double-read frames matching, exactly-one-flag
// rule applied) — safe to call from any task.
MflButtons mflRead();
