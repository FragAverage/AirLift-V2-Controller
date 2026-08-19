// ---------------------------------------------------------------------------
// This board (1.3" SH1106 OLED) has no touch hardware at all — every input
// comes from the MFL menu (see menu.h) instead. Stub, unconditionally, so
// main.cpp's touch::begin()/poll() calls stay board-agnostic.
// ---------------------------------------------------------------------------
#include "touch.h"

#include "config.h"

namespace touch {
void begin() { Serial.println("[TOUCH] none on this board"); }
void poll() {}
}  // namespace touch
