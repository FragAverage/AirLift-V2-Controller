#pragma once

#include <Arduino.h>

#include "airlift_ver.h"   // FW_VERSION (single source of truth; shown in the UI)

// ---------------------------------------------------------------------------
// Serial debug
//
//   * enableDebug is the SINGLE control point. Set it to 0 to turn off every
//     serial debug statement.
//   * With enableDebug = 1, set the flags below to choose specific Serial outputs
// ---------------------------------------------------------------------------
#define enableDebug 1        // ** MASTER ** 0 = silence ALL serial debug

#define debugSys    1        // [SYS]   boot / general / 1 Hz telemetry
#define debugPower  1        // [PWR]   power_manager (reduced power / wake)
#define debugWifi   1        // [WiFi]  soft-AP + web server bring-up
#define debugIO     1        // [IO]    high-side / controller power / UART init
#define debugCAN    1        // [CAN]   TWAI driver + vehicle CAN / fob
#define debugLIN    1        // [LIN]   AirLift LIN MITM (wire / mode / buttons)
#define debugAPI    1        // [API]   REST / settings / OTA
#define debugSavvy  1        // [SAVVY] SavvyCAN GVRET bridge
#define debugMFL    1        // [MFL]   steering-wheel cruise-button decode

#define serialDebugBaud 115200

// --- Master / general (uncategorised) ---
#if enableDebug && debugSys
#define DEBUG(x, ...)  Serial.printf("[SYS] " x "\n", ##__VA_ARGS__)
#define DEBUG_(x, ...) Serial.printf("[SYS] " x, ##__VA_ARGS__)
#else
#define DEBUG(x, ...)
#define DEBUG_(x, ...)
#endif

// --- [PWR] power management ---
#if enableDebug && debugPower
#define DEBUG_PWR(x, ...)  Serial.printf("[PWR] " x "\n", ##__VA_ARGS__)
#define DEBUG_PWR_(x, ...) Serial.printf("[PWR] " x, ##__VA_ARGS__)
#else
#define DEBUG_PWR(x, ...)
#define DEBUG_PWR_(x, ...)
#endif

// --- [WiFi] soft-AP + web server ---
#if enableDebug && debugWifi
#define DEBUG_WIFI(x, ...)  Serial.printf("[WiFi] " x "\n", ##__VA_ARGS__)
#define DEBUG_WIFI_(x, ...) Serial.printf("[WiFi] " x, ##__VA_ARGS__)
#else
#define DEBUG_WIFI(x, ...)
#define DEBUG_WIFI_(x, ...)
#endif

// --- [IO] high-side / controller power / UART init ---
#if enableDebug && debugIO
#define DEBUG_IO(x, ...)  Serial.printf("[IO] " x "\n", ##__VA_ARGS__)
#define DEBUG_IO_(x, ...) Serial.printf("[IO] " x, ##__VA_ARGS__)
#else
#define DEBUG_IO(x, ...)
#define DEBUG_IO_(x, ...)
#endif

// --- [CAN] TWAI driver + vehicle CAN ---
#if enableDebug && debugCAN
#define DEBUG_CAN(x, ...)  Serial.printf("[CAN] " x "\n", ##__VA_ARGS__)
#define DEBUG_CAN_(x, ...) Serial.printf("[CAN] " x, ##__VA_ARGS__)
#else
#define DEBUG_CAN(x, ...)
#define DEBUG_CAN_(x, ...)
#endif

// --- [LIN] AirLift MITM (wire / mode / buttons) ---
#if enableDebug && debugLIN
#define DEBUG_LIN(x, ...)  Serial.printf("[LIN] " x "\n", ##__VA_ARGS__)
#define DEBUG_LIN_(x, ...) Serial.printf("[LIN] " x, ##__VA_ARGS__)
#else
#define DEBUG_LIN(x, ...)
#define DEBUG_LIN_(x, ...)
#endif

// --- [API] REST / settings / OTA ---
#if enableDebug && debugAPI
#define DEBUG_API(x, ...)  Serial.printf("[API] " x "\n", ##__VA_ARGS__)
#define DEBUG_API_(x, ...) Serial.printf("[API] " x, ##__VA_ARGS__)
#else
#define DEBUG_API(x, ...)
#define DEBUG_API_(x, ...)
#endif

// --- [SAVVY] SavvyCAN GVRET bridge ---
#if enableDebug && debugSavvy
#define DEBUG_SAVVY(x, ...)  Serial.printf("[SAVVY] " x "\n", ##__VA_ARGS__)
#define DEBUG_SAVVY_(x, ...) Serial.printf("[SAVVY] " x, ##__VA_ARGS__)
#else
#define DEBUG_SAVVY(x, ...)
#define DEBUG_SAVVY_(x, ...)
#endif

// --- [MFL] steering-wheel cruise-button decode ---
#if enableDebug && debugMFL
#define DEBUG_MFL(x, ...)  Serial.printf("[MFL] " x "\n", ##__VA_ARGS__)
#define DEBUG_MFL_(x, ...) Serial.printf("[MFL] " x, ##__VA_ARGS__)
#else
#define DEBUG_MFL(x, ...)
#define DEBUG_MFL_(x, ...)
#endif

// ---------------------------------------------------------------------------
// Pin assignments — MQB Steering Wheel Controller PCB
// On-wire data is 9600-baud 8N1 UART via LIN transceivers.
// ---------------------------------------------------------------------------
// Controller side — UART1.
// The Controller is the *bus master*: when it is unplugged the
// LIN channel is completely silent (proven by the "controller disconnected" capture).

constexpr int pinTX_Controller = 17;
constexpr int pinRX_Controller = 16;

// MANIFOLD side — UART2.
constexpr int pinTX_Manifold   = 23;
constexpr int pinRX_Manifold   = 22;

// Global Wake and Chip-Select pins for the LIN transceivers
constexpr int pinWake_LIN = 18;   // WAKE on TJA1020 — held HIGH for normal operation (same as MFSW controller)
constexpr int pinCS_LIN   = 19;   // HIGH = enable transceivers (chip select / power enable)

// CAN bus pins (ESP32 TWAI driver)
constexpr int pinCAN_RX = 13;
constexpr int pinCAN_TX = 14;

// High-side MOSFET (5A) to power the controller
constexpr int pinControllerPower = 21;   // HIGH = keep +12 V to handheld during air-out (ignition off)

// MFL steering-wheel cruise-button single-wire input (through the optocoupler
// isolation stage — see pcb/PCB-REQUIREMENTS.md and MFL-FINDINGS.md). Input-
// only pin, no internal pull, can't be accidentally driven as an output.
//
// This is GPIO39, the PCB's former aux-input/ignition-sense line (see the
// KiCad netlist's IGN_SENSE net, J_DEVKIT_L pin 3) — that feature has been
// removed and the physical wire repurposed to carry the MFL PWM signal
// instead. GPIO34 (J_DEVKIT_L pin 4, net MFL_IN) was the original PCB plan
// and is confirmed free/unused, but is NOT what's actually wired on this
// board; do not switch back to it without rewiring.
constexpr int pinMflSignal = 39;

// ---------------------------------------------------------------------------
// Protocol — frame format A: FA-framed polls / responses
// ---------------------------------------------------------------------------
//   FA <payload bytes> F3
//
// From handheld (master):
//   01 00 05                          idle poll
//   01 41 XX YY                       MANUAL-mode button event   (YY = 0xC4 - XX)
//   01 14 XX 00 00 00 YY              PRESET-mode button event
//   01 16 47 TL TR 0F TL TR CHK       preset target-pressure broadcast
//                                     (TL = target front PSI*2, TR = target rear)
// From manifold (slave):
//   10 F1 00 05                       idle reply
//   10 F1 04 01                       reply ack, MANUAL mode
//   10 F1 01 04                       reply ack, PRESET mode
//   10 0E 00 25 ...                   17-byte status frame
//
constexpr uint32_t airliftBaud = 9600; // baud rate of AirLift V2 controller and manifold (both sides of the LIN bus)
constexpr uint8_t  SOF_BYTE    = 0xFA; // start-of-frame byte for FA-framed polls / responses
constexpr uint8_t  EOF_BYTE    = 0xF3; // end-of-frame byte for FA-framed polls / responses

constexpr size_t kMaxFrameLen  = 32; // maximum payload length (excluding FA/F3) for any frame we build or parse

constexpr uint8_t FT_POLL       = 0x01;   // payload[0] from handheld
constexpr uint8_t FT_RESPONSE   = 0x10;   // payload[0] from manifold
constexpr uint8_t ST_CTRL_REPLY = 0xF1;   // payload[1] of manifold reply
constexpr uint8_t ST_PRESSURES  = 0x0E;   // payload[1] of 17-byte status

// Manual-mode button codes — payload[2] of FA 01 41 XX YY F3.
// VERIFIED end-to-end in the 'tank full, up each corner, comp on/off' and
// 'manual,up,down,preset,all 8' captures. Every code below was observed
// multiple times in direct correlation with the physical button pressed.
//
// Layout: upper nibble selects axle (5=front, 6=rear); lower nibble is a
// bit per (side, direction):
//   bit0 = LEFT  UP    bit1 = LEFT  DOWN
//   bit2 = RIGHT UP    bit3 = RIGHT DOWN
// Frame checksum (payload[3]): (0xC4 - code) & 0xFF, i.e. the standard
// sum-to-zero-mod-256 algorithm with SOF 0xFA included.
// Encoding: upper nibble 0x5_ = inflate, 0x6_ = deflate.
//           Lower nibble bit0=FL, bit1=FR, bit2=RL, bit3=RR.
constexpr uint8_t BTN_MANUAL_FL_UP   = 0x51;  // inflate FL
constexpr uint8_t BTN_MANUAL_FL_DOWN = 0x61;  // deflate FL
constexpr uint8_t BTN_MANUAL_FR_UP   = 0x52;  // inflate FR
constexpr uint8_t BTN_MANUAL_FR_DOWN = 0x62;  // deflate FR
constexpr uint8_t BTN_MANUAL_RL_UP   = 0x54;  // inflate RL
constexpr uint8_t BTN_MANUAL_RL_DOWN = 0x64;  // deflate RL
constexpr uint8_t BTN_MANUAL_RR_UP   = 0x58;  // inflate RR
constexpr uint8_t BTN_MANUAL_RR_DOWN = 0x68;  // deflate RR
constexpr uint8_t BTN_MANUAL_ALL_UP   = 0x5F; // inflate all four simultaneously
constexpr uint8_t BTN_MANUAL_ALL_DOWN = 0x6F; // deflate all four simultaneously
// Axle-combined, same encoding: front = FL|FR = bit0|bit1 = 0x3, rear =
// RL|RR = bit2|bit3 = 0xC. Extrapolated from the documented bit-scheme
// above, NOT verified against a real capture the way individual corner
// presses were (the physical handheld has no axle-only button to capture
// from — same caveat BTN_MANUAL_ALL_UP/DOWN above already carries). Confirm
// on your own manifold before relying on this for anything that matters.
constexpr uint8_t BTN_MANUAL_FRONT_UP   = 0x53;  // inflate FL+FR
constexpr uint8_t BTN_MANUAL_FRONT_DOWN = 0x63;  // deflate FL+FR
constexpr uint8_t BTN_MANUAL_REAR_UP    = 0x5C;  // inflate RL+RR
constexpr uint8_t BTN_MANUAL_REAR_DOWN  = 0x6C;  // deflate RL+RR
constexpr uint8_t BTN_MANUAL_RELEASE = 0x00;
constexpr uint8_t BTN_MANUAL_MODE_SWITCH = 0x80;   // press-and-hold 1+5

// Preset selection on the wire is a TWO-frame sequence:
//   1. FA 01 14 [40|43] 00 00 00 CHK F3   — preset-mode poll (state byte,
//      not a preset index: 0x40 on first entry, 0x43 thereafter).
//   2. FA 01 16 47 FL_psi RR_psi 0F FL_psi RR_psi CHK F3   — target PSI
//      broadcast carrying the actual preset content (front-axle PSI and
//      rear-axle PSI, repeated for the rear axle pair).
// The firmware reproduces this via pendingPresetFrontPsi / pendingPresetRearPsi
// and the buildPresetTargets() helper, so individual presets are stored as
// PSI in EEP rather than captured frames.

// Mode-tag bytes in the manifold ack (FA 10 F1 XX YY F3):
//   04 01 -> MANUAL    01 04 -> PRESET
enum AirliftMode : uint8_t {
  MODE_UNKNOWN = 0,
  MODE_MANUAL  = 1,
  MODE_PRESET  = 2,
};

// ---------------------------------------------------------------------------
// Protocol — frame format B: pressure broadcast (idle-terminated, no FA/F3)
// ---------------------------------------------------------------------------
//   00 0A E6 7E   C8 00 4A 00   [FL] [FR] [RL] [RR]   [b12] [b13]
// Pressure bytes are PSI*2.
constexpr uint8_t kBcastPreamble[8] = {0x00, 0x0A, 0xE6, 0x7E, 0xC8, 0x00, 0x4A, 0x00};
constexpr uint8_t kBcastFrameLen   = 14;
constexpr uint8_t kBcastIdxFL      = 8;
constexpr uint8_t kBcastIdxFR      = 9;
constexpr uint8_t kBcastIdxRL      = 10;
constexpr uint8_t kBcastIdxRR      = 11;
constexpr uint8_t kBcastIdxTank    = 0xFF;   // unknown in this format — use format-A

static inline uint8_t psiFromRaw(uint8_t raw) { return raw / 2; }
static inline uint8_t rawFromPsi(uint8_t psi) { return psi * 2; }

// ---------------------------------------------------------------------------
// Protocol — frame format A: 17-byte status frame from manifold
// ---------------------------------------------------------------------------
//   FA 10 0E 00 25  FL  RL  FR  RR  TANK  C8 00 00 00 33 00 CHK F3
//      └─ ST_PRESSURES (payload[1])     ↑      ↑           ↑
//                                       │      │           checksum
//                                       │      └ constant markers
//                                       compressor / mode-state byte
// Verified in 'tank full…' capture:
//   * payload[5..8] = FL,RL,FR,RR raw PSI (PSI*1, NOT PSI*2 like format B).
//   * payload[9]   = tank raw PSI (rises 118→150 under compressor).
//   * payload[3].bit0 (0x01) AND payload[3].bit2 (0x04) are both set iff
//     the on-board compressor is currently running.  Idle frame = 0x20,
//     mode/poll-parity alternates 0x20↔0x30 (bit4), compressor-on adds 0x05.
//   * payload[3].bit5 (0x20) is always set (heartbeat).
//   * payload[16] is a sum-checksum: (0xFA + Σpayload[0..15] + chk) & 0xFF == 0
//
// We therefore read pressures + compressor state from this frame; the
// 14-byte format-B broadcast is kept as a secondary source.
constexpr uint8_t kStatusFrameLen     = 17;
constexpr uint8_t ST_PRESSURES_HDR0   = 0x10;   // payload[0]
constexpr uint8_t ST_PRESSURES_HDR1   = 0x0E;   // payload[1]
constexpr uint8_t kStatusIdxModeFlags = 3;
constexpr uint8_t kStatusIdxFL        = 5;
constexpr uint8_t kStatusIdxFR        = 6;
constexpr uint8_t kStatusIdxRL        = 7;
constexpr uint8_t kStatusIdxRR        = 8;
constexpr uint8_t kStatusIdxTank      = 9;
constexpr uint8_t kStatusIdxChecksum  = 16;
constexpr uint8_t kCompressorOnBits   = 0x05;   // bit0 | bit2 — both set when running
constexpr uint8_t kCompressorOffMask  = (uint8_t)~kCompressorOnBits;

// Status-frame checksum: (SOF + Σpayload[0..15] + chk) mod 256 == 0.
static inline uint8_t statusChecksum(const uint8_t* payload15) {
  uint16_t s = SOF_BYTE;
  for (uint8_t i = 0; i < kStatusIdxChecksum; ++i) s += payload15[i];
  return (uint8_t)(-s);
}

// Inter-byte idle threshold (ms) used to terminate format-B frames.
// At 9600 baud one byte ~1.04 ms; 4 byte-times of silence = ~4 ms.
constexpr uint32_t kFrameIdleMs = 4;

// ---------------------------------------------------------------------------
// Stored button frames (captured live from the handheld).
// ---------------------------------------------------------------------------
enum CornerIndex : uint8_t { CORNER_FL = 0, CORNER_FR, CORNER_RL, CORNER_RR, CORNER_COUNT };
enum CornerDir   : uint8_t { DIR_UP = 0, DIR_DOWN = 1, DIR_COUNT };
constexpr uint8_t kPresetCount      = 8;
constexpr uint8_t kCornerSlotCount  = CORNER_COUNT * DIR_COUNT;

// ---------------------------------------------------------------------------
// Ignition / Power Management
// ---------------------------------------------------------------------------
constexpr uint32_t canPresenceTimeoutMs = 3000;
constexpr uint16_t kCanSilenceSecMin     = 0;
constexpr uint16_t kCanSilenceSecMax     = 60;
constexpr uint16_t kCanSilenceSecDefault = 10;
constexpr uint16_t kCanMinFpsMin         = 0;
constexpr uint16_t kCanMinFpsMax         = 2000;
constexpr uint16_t kCanMinFpsDefault     = 50;
// Controller boot / mode-handshake settle. On a cold wake the AirLift handheld
// takes a few seconds to boot, show pressures, then settle into Preset/Manual
// mode before it will accept commands. Sending an air-up/down preset during
// that handshake trips a comms error, so a fob-triggered air action waits this
// long (measured from controller power-on) before driving the manifold.  Adjustable in UI
constexpr uint16_t kControllerBootDelayMsMin     = 0;
constexpr uint16_t kControllerBootDelayMsMax     = 10000;
constexpr uint16_t kControllerBootDelayMsDefault = 5000;
// ---------------------------------------------------------------------------
// CAN broadcast (firmware -> vehicle bus)
//   8-byte payload:
//     [0] FL raw (PSI*2)   [1] FR raw   [2] RL raw   [3] RR raw   [4] Tank raw
//     [5] Flags: bit0 compOn  bit1 ignOn  bit2 intercept  bit3 passthru
//                bit4..5 mode (0=unknown,1=manual,2=preset)
//     [6] Sequence counter (free-running, rolls every 256 frames)
//     [7] Reserved (0)
// ---------------------------------------------------------------------------
constexpr uint32_t kCanBroadcastIdDefault = 0x520;
constexpr uint32_t kCanBroadcastIdMin     = 0x000;
constexpr uint32_t kCanBroadcastIdMax     = 0x7FF;   // 11-bit standard ID
constexpr uint32_t kCanBroadcastPeriodMs  = 100;     // ~10 Hz
constexpr uint32_t kCanIdDisabled          = 0xFFFFFFFFUL;
// VW Comfort CAN: fob command frame. Captures show 0x4B for unlock and 0x8B
// for lock; this is distinct from the repeating body-state frames generated
// by both the fob and the interior lock/unlock switch.
constexpr uint32_t kComfortFobCanId         = 0x291;
constexpr uint8_t kComfortFobUnlockCode     = 0x4B;
constexpr uint8_t kComfortFobLockCode       = 0x8B;
constexpr uint32_t kComfortFobDebounceMs    = 500;
constexpr uint32_t kComfortFobDoublePressMs = 5000;
constexpr uint32_t kCanTrafficGapMs         = 1500;
constexpr uint32_t kCanTrafficWakeMs        = 10000;
// Minimum number of frames within a FRESH traffic run (bus went quiet for
// > kCanTrafficGapMs, then became active again) that qualifies as a genuine
// wake event to bring WiFi back out of reduced power. A larger value avoids
// waking the AP on a single stray frame.
constexpr uint32_t kCanWakeFrameMin         = 8;
constexpr uint32_t airOutDurationMs     = 10000;
constexpr uint32_t postAirOutDelayMs    = 10000;
constexpr uint32_t kAutoLevelPulseMs    = 300;
constexpr uint32_t kAutoLevelIntervalMs = 600;
constexpr uint32_t kAutoLevelTimeoutMs  = 60000;
constexpr uint8_t  kAutoLevelTolerancePsi = 1;

// ---------------------------------------------------------------------------
// Web-intercept command durations (defaults; each request may still override
// via its own "holdMs" field).
// ---------------------------------------------------------------------------
// Preset: how long the target-PSI frame (01 16 47 …) is asserted before the
// injector settles to the steady preset heartbeat. The manifold closed-loops to
// the target itself, so this only needs to cover the establishment window.
constexpr uint32_t kInterceptPresetHoldMsDefault = 3000;
// Manual button "tap": how long a single corner button is held (valve open).
constexpr uint32_t kInterceptManualTapMsDefault  = 100;
// Manual button "press"/"hold" safety window: how long a held button stays
// active if the UI stops sending heartbeats to extend it.
constexpr uint32_t kInterceptManualPressWindowMs = 1500;

// ---------------------------------------------------------------------------
// ESP-NOW broadcast (firmware -> slave display, see ../SlaveDisplay)
//   24-byte AirLiftData packet (airlift_espnow.h) to FF:FF:FF:FF:FF:FF.
// ---------------------------------------------------------------------------
// The display pins its radio to a fixed channel at boot (its ESPNOW_WIFI_CHANNEL
// build flag) because it never associates with anything. Our soft-AP therefore
// has to sit on that same channel or the packets are never heard.
constexpr uint8_t  kEspNowChannel   = 1;
constexpr uint32_t kEspNowPeriodMs  = 100;   // ~10 Hz
// MFL button-state broadcast (AirLiftButtons) — faster than the pressure
// broadcast so on-screen menu navigation feels responsive.
constexpr uint32_t kEspNowButtonPeriodMs = 50;   // ~20 Hz
// A held button is only "held" while its polls keep arriving (~5/s from the
// handheld); past this it has been released.
constexpr uint32_t kEspNowButtonFreshMs = 400;
// No manifold reply for this long -> report NO SIGNAL rather than stale values.
constexpr uint32_t kEspNowLinStaleMs    = 3000;
// Pressure-trend direction detection (see espnow_tx.cpp).
constexpr uint32_t kEspNowTrendWindowMs  = 400;
constexpr int32_t  kEspNowTrendRawThresh = 2;     // raw counts (PSI*2) = 1 psi
constexpr uint32_t kEspNowTrendHoldMs    = 1200;  // no movement for this long = idle

constexpr uint8_t kZeroPresetManual = 0xFF;

// After waking from reduced power the handheld is re-powered from cold and must
// re-handshake with the manifold over LIN. Bringing the WiFi radio back up in
// that window (RF calibration briefly steals interrupt time) corrupts that
// fragile handshake, so the handheld shows comms errors for a few seconds even
// though we change no frames. The WiFi bringup is therefore DEFERRED until the
// LIN relay is healthy again (the manifold is replying) and a minimum settle
// time has elapsed — or until a hard cap, so it always comes back eventually.
constexpr uint32_t kWifiWakeSettleMs   = 2500;   // min time after wake before WiFi
constexpr uint32_t kWifiWakeLinFreshMs = 800;    // manifold reply is "healthy" if within this
constexpr uint32_t kWifiWakeMaxDeferMs = 12000;  // hard cap: restore WiFi regardless

enum VehicleLockState : uint8_t {
  LOCK_STATE_UNKNOWN = 0,
  LOCK_STATE_LOCKED,
  LOCK_STATE_UNLOCKED,
};

constexpr const char* wifiHostName = "AirLift-V2 Controller";

constexpr uint32_t taskIdleDelayMs = 1;
