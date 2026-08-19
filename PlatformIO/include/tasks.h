#pragma once

#include "globals.h"

void startTasks();

// Queue preset `index` (0-based) as the desired mode/target, held for
// `holdMs` before settling back to the heartbeat. Shared by the web UI
// (/api/intercept/preset), the fob/air-out triggers, and the MFL ESP-NOW
// command handler — one canonical place that touches
// desiredMode/pendingPreset*/presetEnterUntilMs under airliftMux, so all
// three behave identically. No-op if index is out of range. Callers that need
// a passThroughMode gate (as the fob/air-out path does) still check it
// themselves before calling — this function is exactly the preset-queueing
// body, nothing more, so it doesn't change behaviour for existing callers
// that never had that gate (e.g. /api/intercept/preset).
void queuePresetByIndex(uint8_t index, uint32_t holdMs, const char* source);

// Queue a stored frame for injection on the manifold UART.
// holdMs > 0 ⇒ repeatedly inject the frame at ~50 ms intervals for that
// duration (mimics a press-and-hold button such as air-out).
