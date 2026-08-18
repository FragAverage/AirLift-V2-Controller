#pragma once

#include <Arduino.h>

// ---------------------------------------------------------------------------
// XPT2046 tap zones. The production unit lives inside the instrument cluster
// and is not reachable, so this is compiled out unless ENABLE_TOUCH=1.
//
// Zones (landscape):
//   top half   -> the four corner quadrants (FL / FR / RL / RR)
//   bottom strip -> left half = preset down, right half = preset up
//
// For now every hit is just a Serial.println; the actual command injection
// happens master-side over ESP-NOW later.
// ---------------------------------------------------------------------------
namespace touch {

void begin();
void poll();

}  // namespace touch
