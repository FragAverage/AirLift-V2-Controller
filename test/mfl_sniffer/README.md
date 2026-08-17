# MFL cruise-control sniffer (bench test)

Throwaway rig to confirm the BMW MFL cruise-control signal exists on your car
and matches the known protocol, **before** wiring it into anything permanent
or committing to a PCB layout. Not part of the AirLift firmware.

## What you're tapping

On E46/E39-era BMWs the steering-wheel cruise buttons (RES/+, SET/-, ON/OFF)
do **not** go over CAN or K-Line — they go over one dedicated wire straight
from the steering wheel to the DME:

- Wheel-side connector: pin 8, "Datalink MFL"
- DME-side: connector `X60004` pin 27, same label
- Route: `DME → X60531 → X6011 → clockspring on the steering column`

Pin numbers above came from an M3/MS S54-era wiring reference — cross-check
against your actual chassis/engine wiring diagram before you cut into
anything, connector pinouts shift a bit by year/harness.

## Before connecting anything: measure it

With a multimeter, key on, probe the Datalink MFL wire to chassis ground:

1. Idle voltage (no buttons pressed).
2. Voltage while holding a button.

Write both down.

**Measured on this car: idle ~10.0V, dips to ~9.5-9.7V on IO/SET while held,
barely moves on PLUS/MINUS.** That's expected, not a measurement error — a
multimeter averages, and this signal is high almost all the time with only
brief (~150-350us) low pulses during each ~10ms message. Different buttons
encode different bit patterns, so the DC average dips by different (small)
amounts per button. This is actually consistent with the protocol, not
evidence against it. Don't try to identify buttons from the multimeter —
that's what the sniffer's raw pulse dump is for.

The important number here is the **idle level, ~10V** — this is riding on
the car's 12V-class rail, not 3.3V/5V logic. That rules out a plain
diode-clamp approach: the signal sits high almost continuously, so a clamp
diode would be conducting nearly all the time, holding the pin at a
sustained out-of-spec voltage rather than just catching brief transients.
It also means the exact voltage will drift with engine state (resting
battery vs. alternator running, roughly 9-15V depending on conditions) —
so the circuit needs to tolerate a range, not one measured number.

## Protection circuit: 4N35 optocoupler (what this bench build actually uses)

Fully decouples the ESP32 from the car's voltage domain — no shared ground
required between the two sides, which is stronger isolation than a resistor
divider or a plain transistor level-shifter:

```
Car MFL wire ──/\/\/\── Pin 1 (Anode)
              R1 (510Ω)
                        [4N35]
Car GND ──────────────── Pin 2 (Cathode)


ESP32 3V3 ── R2 (510Ω, pull-up) ──┬── Pin 5 (Collector)
                                    │
                               ESP32 GPIO (this build: D18)

ESP32 GND ─────────────────────────── Pin 4 (Emitter)
```

DIP-6 pinout: 1 Anode, 2 Cathode, 3 NC, 4 Emitter, 5 Collector, 6 Base (leave
6 and 3 unconnected). Check your own part's pin-1 dot before inserting —
DIP orientation isn't universal; this build's part lands pin 1 at top-right
when straddling the breadboard's center gap, not top-left.

- R1 (LED side, car-facing) and R2 (phototransistor side, ESP32-facing) only
  need to be roughly 1kΩ–4.7kΩ — not picky. This build used 510Ω for both
  (green-brown-brown-gold), which is a bit more current than ideal but well
  within safe limits for the 4N35 and the ESP32.
- Pins 1-2 (car side) and pins 4-5 (ESP32 side) share **no electrical node**
  — only light crosses between them. Tie pin 2 to car chassis ground and
  pin 4 to ESP32 ground; these can be two genuinely separate references.
- **This inverts the signal**: car HIGH → LED on → phototransistor on → GPIO
  pulled LOW. `src/main.cpp` has `kSignalInverted = true` set for this.
- Keep car-side wiring (R1, pins 1/2) physically separate from ESP32-side
  wiring (R2, pins 4/5, GPIO, 3V3) while building — nothing should bridge
  directly between them except the 4N35 itself.

### Alternatives, if you don't have a 4N35

**NPN transistor level-shifter** (any 2N3904/2N2222/BC547 + two ~10k
resistors): base through a resistor to the car wire, emitter to GND,
collector to the GPIO with a pull-up to 3V3. Also inverting, but shares
ground between the two sides rather than isolating them.

**Divider + zener clamp**, if you have a 3.3V zener (e.g. BZX55C3V3) but no
transistor or optocoupler: a divider (try 6.8kΩ from car wire to node, 3.3kΩ
from node to GND) with the zener across the 3.3kΩ leg. Non-inverting — leave
`kSignalInverted = false`. A plain divider with **no** clamp is not safe
here: satisfying both "valid logic-high at ~9-10V" and "under the ESP32's
3.6V abs max at ~14-15V" isn't possible with fixed resistors alone over that
range.

`kMflPin` in `main.cpp` selects the GPIO — set it to whichever pin you
actually wired.

## Running it

```
cd test/mfl_sniffer
pio run -t upload
pio device monitor
```

(Needs [PlatformIO](https://platformio.org/); `platformio.ini` targets a
generic `esp32dev` board like the rest of this repo.)

## Reading the output

Each line is one decoded message:

```
[172,340,168,335,170,169,338,4820] ON=0 IO=0 PLUS=1 MINUS=0   <-- CONFIRMED CHANGE
```

- The `[...]` is the 8 raw pulse widths in microseconds, in order — this is
  what you actually want to look at first. Confirm you're seeing something
  in the ~150-350us range for the first 7, roughly every ~10ms between
  frames. If the numbers look nothing like that (e.g. everything near 0, or
  wildly inconsistent), the tap point, wiring, or ground reference is wrong
  — don't trust the decoded columns until the raw numbers look sane.
- `ON/IO/PLUS/MINUS` are the decoded button states from the pazi88 logic.
  `CONFIRMED CHANGE` prints once a decoded state has repeated for 2
  consecutive frames (debounce against a single corrupted read).
- If PLUS/MINUS never trip while you're clearly pressing RES/SET, the bit
  threshold (`kBitThresholdUs = 250`) may need nudging based on what the raw
  widths actually show on your car — edit and reflash.

Once this confirms clean, consistent decodes for the buttons you care about,
that's your green light to design the real input stage into a PCB.
