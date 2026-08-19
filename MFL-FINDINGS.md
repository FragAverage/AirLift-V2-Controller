# MFL Cruise-Button Findings

Summary of everything confirmed while getting BMW E46 steering-wheel MFL
cruise-control buttons (RES/+, SET/-, ON, IO) reading reliably into an
ESP32, ahead of folding this into the main AirLift firmware. Bench
prototype and working code: [`test/mfl_sniffer/`](test/mfl_sniffer/). PCB
requirements derived from this: [`pcb/PCB-REQUIREMENTS.md`](pcb/PCB-REQUIREMENTS.md).

---

## What the signal actually is

E46-era BMW cruise buttons do **not** appear on CAN or K-Line. They run
over one dedicated wire straight from the steering wheel to the DME:

- Wheel-side connector: pin 8, "Datalink MFL"
- DME-side: connector `X60004` pin 27, same label
- Route: `DME → X60531 → X6011 → clockspring on the steering column`

(Pin numbers sourced from an M3/MS S54-era wiring reference — cross-check
against your exact chassis/engine if wiring a different car.)

The wheel has its own internal microcontroller that reads the physical
button switches and re-encodes them onto this single wire as a **PWM pulse
train** — it is not a raw switch/resistor-ladder signal on this wire, and
it is not a standard bus protocol (not LIN, not K-Line, not UART-framed).

## Protocol

Decode logic ported from pazi88's `MFL.ino`/`MFL.h`
([Speeduino-M5x-PCBs](https://github.com/pazi88/Speeduino-M5x-PCBs/blob/master/m52tu-m54_PnP/Serial3toBMWcan/MFL.ino)),
which reads the same signal on M52TU/M54 (E46-era) BMWs for ECU-swap
projects — confirmed working end-to-end on this specific car.

- Idle-high line. A message is **8 pulses**, repeating **~every 10ms**.
- Each pulse's HIGH duration encodes a bit. Measured on this car: ~168µs
  and ~341µs are the two widths (threshold at the midpoint, 250µs).
- Pulse 8 (index 7) is a toggle/counter bit, not part of the decoded value.
- A HIGH duration **> 5ms** marks the idle gap between messages (frame
  sync).
- The first 7 pulses reconstruct a 7-bit value, checked against bitmasks:
  ```
  CRUISE_ON    = (value & 110) == 0
  CRUISE_IO    = (value & 218) == 0
  CRUISE_PLUS  = (value & 182) == 0
  CRUISE_MINUS = (value & 252) == 0
  ```

### Bit polarity — corrected from the original source

pazi88's original hardware (bare GPIO, no inverting stage) treats a **long**
pulse as bit `1`. On this build, with the signal passing through an
inverting isolation stage (see below), the opposite is true: **short pulse
(< threshold) = bit `1`**. This was confirmed by hand-decoding real
captures — long-pulse=1 never produced a valid mask match on any observed
button press; short-pulse=1 produced clean, repeatable matches. If this
logic is ever reused with a *non-inverting* interface, that bit needs
flipping back.

### Idle vs. pressed reading

At rest, the decoded value is `0`, which trivially satisfies all four
bitmasks — naive decoding reads idle as **all four flags `true`
simultaneously** (`ON=1 IO=1 PLUS=1 MINUS=1`). A real button press clears
bits belonging to the *other* three masks while leaving its own mask's bits
untouched, so a press produces a **nonzero** value where exactly one mask
still matches.

**Fixed in `decode()`**: `value == 0` is checked first and returned as "no
button pressed" (all four flags `false`) before the mask checks run, since
every idle capture decoded to exactly `0` and every real press decoded to
something nonzero. Without this check, idle would read as all four buttons
held down at once — clearly wrong, and dangerous if wired to real actions
(triggering air-up and air-down simultaneously, for instance).

**For the AirLift integration**, go one step further than the bench
sniffer does: after excluding `value == 0`, still only trust a decode where
*exactly one* of the four flags is `true`. This is based on four observed
button presses, not a proof from the bitmask structure itself — a
corrupted/noisy frame on a real automotive bus could in principle produce
an unexpected value that matches zero or more than one mask. Since this
will be triggering physical air-up/air-down actions, treat "not exactly one
match" as a discard-this-frame case rather than acting on it.

## Confirmed button mapping (this car, single-press-per-button test)

| Physical button | Flag that stays `1` while the other three drop to `0` |
|---|---|
| + | `PLUS` |
| − | `MINUS` |
| SET | `ON` |
| IO | `IO` |

Three of four line up with pazi88's flag names directly. One real finding:
**this wheel's "SET" button decodes as `CRUISE_ON`, not `CRUISE_MINUS`.**
The flag names are just labels from the original author's car and don't
necessarily match this wheel's silkscreen — when folding into AirLift,
name the resulting firmware fields after the physical button (`mflPlus`,
`mflMinus`, `mflSet`, `mflIo` or similar), not after pazi88's internal
names, to avoid future confusion.

## Electrical

- Idle voltage on this car: **~10.0V** (not 3.3V/5V logic) — rides on the
  vehicle's 12V-class rail, expected to vary ~9-15V with engine/battery
  state.
- **Isolation/protection circuit**: 4N35 optocoupler. Car side: signal
  through R1 (~510Ω, non-critical) into the LED anode, cathode to car
  ground. ESP32 side: 3V3 through R2 (~510Ω) pull-up into the
  phototransistor collector (read by the GPIO), emitter to ESP32 ground.
  Full writeup and wiring diagram history in
  [`test/mfl_sniffer/README.md`](test/mfl_sniffer/README.md).
- This inverts the signal (car HIGH → LED on → phototransistor on → GPIO
  pulled LOW) — accounted for in both the framing code (`kSignalInverted`)
  and the bit-polarity fix above.
- On the bench prototype, GPIO18 was used and works fine. **On the real
  AirLift PCB this conflicts** with the existing LIN transceiver WAKE pin
  — production wiring needs a different, genuinely free GPIO (GPIO34
  recommended; see `pcb/PCB-REQUIREMENTS.md`).

## Next steps (firmware integration)

1. ~~Port the framing/decode logic from `test/mfl_sniffer/src/main.cpp` into
   the main AirLift firmware as its own module, rather than copy-pasting
   into existing files.~~ **Done** — `PlatformIO/include/mfl.h` /
   `src/mfl.cpp`, on the production pin (GPIO34, see point 5 below).
2. ~~Rename the decoded fields to match physical buttons (see mapping
   table), not pazi88's original flag names.~~ **Done** — `MflButtons` in
   `mfl.h` uses `plus`/`minus`/`set`/`io`, with a comment noting this wheel's
   "SET" decodes as pazi88's `on` mask.
3. Button → action mapping: **implemented differently from the original
   plan.** Rather than PLUS/MINUS driving air up/down directly, all four
   buttons now drive an on-screen menu on the Slave Display (`IO` =
   enter/select, `PLUS`/`MINUS` = up/down, `SET` = back), with **preset
   select** as the only action that reaches the manifold so far — see
   `SlaveDisplay/include/menu.h` and `AirLiftButtons`/`AirLiftCommand` in
   `airlift_espnow.h`. Direct air up/down on PLUS/MINUS (bypassing the menu)
   remains an open option if the menu proves too slow for that.
4. Still open: whether an MFL button press should participate in the
   existing power-management wake logic alongside CAN traffic (currently the
   ESP32 never light-sleeps, so this may be moot — revisit if that changes).
5. ~~Move from the bench GPIO (18) to the production pin once the new PCB
   pin map is finalized.~~ **Done** — GPIO34, confirmed on the fabricated
   PCB's net list (`MFL_SIGNAL` → optocoupler → `MFL_IN` → `J_DEVKIT_L` pin
   4) and set as `pinMflSignal` in `PlatformIO/include/defs.h`.
