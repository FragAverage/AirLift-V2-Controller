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

// Queue a manual button (BTN_MANUAL_* from defs.h) as held for `windowMs`.
// Repeated calls (a "press" heartbeat) extend the window rather than
// stacking — same mechanism transformHandheldFrame() already rewrites
// handheld polls through. Shared by the web UI (/api/intercept/manual's
// press/hold/tap actions) and the MFL ESP-NOW command handler.
void queueManualButton(uint8_t code, uint32_t windowMs);

// Stop emitting the held button and queue one explicit button-up rewrite.
// Shared by the same two callers as queueManualButton().
void releaseManualButton();

// Queue a stored frame for injection on the manifold UART.
// holdMs > 0 ⇒ repeatedly inject the frame at ~50 ms intervals for that
// duration (mimics a press-and-hold button such as air-out).
