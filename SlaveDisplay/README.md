# AirLift V2 — Slave Display

Receive-only dashboard for the AirLift V2 MITM controller, running on an
**ESP32-2432S028R** ("Cheap Yellow Display" / CYD): 2.8" ILI9341 320×240 + XPT2046
resistive touch.

The master broadcasts an `AirLiftData` packet over ESP-NOW; this unit renders it.
It never transmits, never pairs, and holds no state beyond the last packet.

```
  0                160               319
  +--------------------+--------------------+   0
  |  FL                |  FR                |
  |       62.5         |       61.8         |     ← Font 6, white
  +--------------------+--------------------+   84
  |  RL                |  RR                |
  |       78.0         |       77.4         |
  +--------------------+--------------------+  168
  |  TANK              |  PRESET            |
  |    145 PSI         |     DRIVE          |     ← cyan / yellow, Font 4
  +--------------------+--------------------+  218
  |               RAISING                   |     ← colour-coded status bar
  +-----------------------------------------+  240
```

## Build & flash

```bash
pio run              # build
pio run -t upload    # flash over USB
pio device monitor   # 115200 baud
```

All TFT_eSPI configuration is passed as `-D` build flags from `platformio.ini`
(`USER_SETUP_LOADED=1`), so the library folder is never edited and the config
survives a clean build or a `pio pkg update`. `User_Setup_CYD.h` in this folder
is the same configuration as a drop-in `User_Setup.h`, for Arduino IDE builds.

TFT_eSPI emits `#warning TOUCH_CS pin not defined` during the build. That is
expected and correct: the CYD's XPT2046 sits on its own VSPI bus and is driven by
`XPT2046_Touchscreen`, not by TFT_eSPI's built-in touch support. Handing
TFT_eSPI a `TOUCH_CS` would make it talk to the touch chip over the display's
HSPI bus, which is not how the board is wired.

## Build flags worth knowing

| Flag | Default | Purpose |
| --- | --- | --- |
| `ESPNOW_WIFI_CHANNEL` | `1` | **Must match the master's channel.** |
| `ENABLE_TOUCH` | `0` | Compiles in the tap zones. Off because the unit is cluster-mounted. |
| `DEMO_MODE` | `0` | Animates fake pressures so the panel can be tested with no master present. |

### Wi-Fi channel

ESP-NOW only works between radios parked on the same channel, and this end never
associates with anything, so the channel is pinned explicitly at boot. If the
master is running its SoftAP (web UI) on a channel other than 1, set
`ESPNOW_WIFI_CHANNEL` to match or **no packets will ever arrive** — the display
will sit on `NO SIGNAL` with no other symptom.

## Master-side sender

Implemented in `PlatformIO/src/espnow_tx.cpp` (firmware v2.11+), enabled by
default and switchable in the master's web UI under **Settings → Slave Display
(ESP-NOW)**. It broadcasts at ~10 Hz from the soft-AP interface.

The struct in `include/airlift_espnow.h` must stay byte-identical on both ends
(24 bytes with default packing — there is a `static_assert` guarding it). The
master keeps its own copy at `PlatformIO/include/airlift_espnow.h`; **change one
and you must change the other.**

Broadcast means the display needs no knowledge of the master's MAC. The receiver
sanity-checks the length and drops anything that is not exactly `sizeof(AirLiftData)`.

Two fields are derived master-side rather than read off the LIN wire:

- **`preset`** — the wire carries no preset index, only the target pressures from
  the `01 16 47` broadcast. The master matches those against its configured /
  learned preset table. No match (or not in PRESET mode) sends `0`, which renders
  as `---`.
- **`status`** — `RAISING` / `LOWERING` come from a button being held right now
  or, failing that, from the trend of the corner pressures (which is what covers
  a preset move, where the manifold closed-loops with no button held). The
  compressor bit alone is not direction: it is also set while the tank refills.
  `NO_SIGNAL` is sent when the master itself has lost the manifold, so the
  display distinguishes "master gone" (its own 3 s link timeout) from
  "master fine, car's LIN bus is dead".

The master pins its soft-AP to channel 1 (`kEspNowChannel`) to match this end's
`ESPNOW_WIFI_CHANNEL`, and holds its radio up while the ignition is on so the
display does not go blank when its power-saving would otherwise drop Wi-Fi.

## Behaviour

- **Redraw is diffed, not cleared.** `drawStaticLayout()` paints the dividers and
  captions once in `setup()`; `update()` compares the *rendered text* of each
  value against what is on screen and `fillRect`s only the boxes that actually
  changed. Jittery floats that all format to the same string cost zero SPI traffic.
- **Pressures are whole psi.** Sub-psi resolution is noise on an air suspension
  gauge and churns the last digit on every packet, which reads as a flickering
  display. A value that can't be trusted (NaN, negative) renders as `---`.
- **Link loss.** No packet for 3 s → the status bar goes red `NO SIGNAL` and the
  held pressures are dimmed to dark grey, so a stale reading can't be mistaken
  for a live one.

## Touch (`ENABLE_TOUCH=1`)

Calibrated for raw X/Y 200–3900, remapped to landscape with the axes swapped and
Y mirrored. Tap zones: the four corner quadrants in the top half, and preset
down / preset up on the left / right of the tank-preset strip. Each hit currently
just does a `Serial.printf` — the actual command injection is master-side work
still to be wired up.

If taps land in the mirrored position, swap the endpoints of one `map()` call in
`mapRaw()` (`src/touch.cpp`).

## Layout

| File | Contents |
| --- | --- |
| `include/config.h` | Touch pins, layout geometry, colours, timeouts |
| `include/airlift_espnow.h` | The shared wire struct — **keep in sync with the master** |
| `src/espnow_link.cpp` | STA mode, fixed channel, receive callback, staleness |
| `src/display.cpp` | Static layout + diffed value rendering |
| `src/touch.cpp` | XPT2046 on VSPI, tap zone dispatch |
| `src/main.cpp` | Boot, loop, preset names |
