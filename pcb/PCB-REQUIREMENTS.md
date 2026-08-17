# PCB Requirements — AirLift V2 + MFL Cruise Input

Scope: a new PCB that carries everything the current AirLift V2 board does
(LIN MITM, CAN/TWAI, ignition sense, high-side driver) **plus** a new input
that reads the BMW E46 steering wheel's MFL cruise-control buttons (RES/+,
SET/-, ON, IO), repurposed to drive AirLift actions since the car's factory
cruise control doesn't work.

This is a requirements list for schematic capture, not a schematic — chip
choices below are either "must match the existing product" (inherited) or
"validated on a bench prototype, final part TBD at layout" (new).

---

## 1. MCU

- **ESP32-WROOM-32**, same module as the existing AirLift V2 / MFSW
  Controller shared PCB.
- Existing board mounts it as a pluggable DevKit-style module (per current
  `platformio.ini` targeting generic `esp32dev`). Carry this forward unless
  there's a specific reason to move to a bare/castellated module — not
  something this doc is deciding.

---

## 2. Inherited subsystems (carry forward from current AirLift V2 PCB)

These already exist on the current board (`PlatformIO/include/defs.h`) and
must be retained as-is unless a change is explicitly wanted:

| Function | GPIO | Notes |
|---|---|---|
| LIN 1 (Controller) TX | 17 | `Serial1`, 9600 8N1 |
| LIN 1 (Controller) RX | 16 | |
| LIN 2 (Manifold) TX | 23 | `Serial2`, 9600 8N1 |
| LIN 2 (Manifold) RX | 22 | |
| LIN transceiver WAKE | 18 | held HIGH for normal operation (TJA1020) |
| LIN transceiver CS/EN | 19 | HIGH = transceivers enabled |
| CAN/TWAI RX | 13 | |
| CAN/TWAI TX | 14 | |
| Ignition sense (aux) | 39 | input-only pin, 12V level, optional hard-wired ignition detect |
| Controller high-side power | 21 | MOSFET/transistor switch, +12V, 5A max |

Associated chips to retain:
- **LIN transceiver(s)**: TJA1020 (per existing firmware comments), one per
  LIN channel (2 total) — WAKE/CS shared per the pin table above.
- **CAN transceiver**: TJA1050 or SN65HVD230 class, TWAI-compatible,
  listen-only capable (driver-level, not necessarily a hardware pin).
- **High-side driver**: transistor/MOSFET stage capable of switching +12V
  at up to 5A to the handheld controller, for the ignition-off air-out
  sequence.
- **12-way main connector** (MX23A12NF1 on current board) — see §5, pins
  11/12 are currently unused and are the natural home for the new MFL wire.

**Important pin conflict to flag**: GPIO18 is already committed to LIN
transceiver WAKE on this board. The MFL bench prototype used GPIO18 because
it was free on a bare, standalone DevKit with nothing else wired to it —
**that pin is not available on the combined board.** See §3 for the
production pin choice.

---

## 3. New subsystem: MFL cruise-button input

### Signal characteristics (bench-verified, see `test/mfl_sniffer/`)

- Single wire, BMW "Datalink MFL" line (wheel connector pin 8 / DME
  connector pin 27) — not present on CAN or K-Line.
- Idle-high, ~9-15V depending on engine/battery state (not 3.3V/5V logic).
- Message = 8 PWM pulses, repeating ~every 10ms.
- Bit encoding: pulse width (~168µs vs ~336µs) — bit threshold ~250µs.
- Decodes to 4 buttons via bitmask (ON, IO, PLUS/RES, MINUS/SET) — firmware
  side, already implemented and confirmed working end-to-end via optocoupler
  isolation.

### Protection / isolation circuit

- **Required**: the ESP32 must never see the car-side ~9-15V (or automotive
  transients above that) directly. Bench-validated approach: 4N35
  optocoupler, R1 (LED current limit, car side) + R2 (pull-up,
  ESP32/3V3 side), both ~510Ω-4.7kΩ, non-critical values.
- **For production**: evaluate an SMD optocoupler (e.g. PC817-series SMD
  variant, or SMD 4N35 equivalent) for board space — confirm switching
  speed is adequate for ~170µs pulse widths (comfortably true for this
  device class, but verify against the specific part's datasheet).
- **Open item — transient robustness**: bench testing only validated normal
  running-range voltage (~9-15V). This wire runs through the steering
  column/clockspring; if it should also survive automotive load-dump or
  ESD-class transients (ISO 7637-2 style pulses), add a TVS clamp ahead of
  R1, sized to the optocoupler's input rating. Not yet a confirmed
  requirement — flag for decision before layout.
- Car-side ground for this signal is the **same vehicle chassis ground**
  already used by LIN/CAN/ignition-sense elsewhere on this board (unlike
  the bench test's two separate breadboards) — no separate ground domain
  needed on the PCB itself. The optocoupler is still worth keeping for
  voltage protection and noise immunity, just not for avoiding a ground
  loop that doesn't exist on a single board.

### GPIO assignment

- **Do not use GPIO18** (see §2 conflict).
- Recommend an input-only pin, consistent with how `pinIgnitionSense` (39)
  was chosen on the existing board: **GPIO34, 35, or 36** are free, have no
  internal pull, and can't be accidentally driven as an output. GPIO34 is
  the suggested default.
- Must remain edge-interrupt capable (true for any ESP32 GPIO) — no other
  special requirement (not ADC-sampled, not touch-capable, no RTC/wake
  requirement identified yet — see open item below).

### Connector

- Add **one pin** for the MFL signal wire — connector pins 11/12 on the
  current 12-way MX23A12NF1 are unused and unassigned; use one of those.
  No new ground pin needed (reuse pin 2, `GND`).

### Firmware-side note (not a PCB item, but affects pin choice)

- Existing power management never light-sleeps the ESP32 (per README), so
  the MFL GPIO doesn't currently need wake-source capability. **Open
  question**: should a button press also count as a "wake" event the way a
  CAN burst does? If yes in the future, confirm the chosen GPIO supports
  whatever wake mechanism is used before finalizing.

---

## 4. Power

- Board is powered from switched 12V (`PWR_IN`, connector pin 1), same as
  current design. Existing regulation chain (5V/3.3V for ESP32 + logic) is
  inherited as-is — no changes identified for this feature; confirm the
  existing regulator has enough margin for the added optocoupler LED
  current (a few mA, negligible) — not expected to matter but worth a
  one-line sign-off during schematic review.

---

## 5. Mechanical / connector summary

| Pin | Signal | Status |
|---|---|---|
| 1 | `PWR_IN` | existing |
| 2 | `GND` | existing — also serves as MFL signal return |
| 3 | `LIN1` | existing |
| 4 | `LIN2` | existing |
| 5 | `CHASSIS_CANH` | existing |
| 6 | `CHASSIS_CANL` | existing |
| 7 | `ANALOG_LIGHT_IN` | existing (ignition sense) |
| 8 | `RA` | existing, unpopulated |
| 9 | `5V` | existing |
| 10 | `OUTPUT1` | existing (controller high-side) |
| 11 | **`MFL_SIGNAL`** | **new — this feature** |
| 12 | — | still unused |

---

## 6. Open questions before schematic capture

1. TVS/transient clamp on the MFL line — required or not? (§3)
2. Final optocoupler part number (DIP-6 4N35 vs SMD equivalent) and R1/R2
   values for that part.
3. Whether MFL button presses should participate in wake/power-management
   logic alongside CAN traffic.
4. Button → action mapping is a firmware decision, not a PCB one, but
   worth confirming before finalizing: PLUS/MINUS → air-up/air-down (as
   discussed), ON/IO → cycle presets or unassigned.
