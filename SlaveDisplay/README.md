# AirLift V2 — Slave Display

Gauge-cluster dashboard for the AirLift V2 MITM controller, with an
MFL-button-driven on-screen menu (see "MFL menu" below). Four boards share
this firmware as separate PlatformIO envs:

- **`cyd`** (default) — **ESP32-2432S028R** ("Cheap Yellow Display"): 2.8"
  ILI9341 320×240 + XPT2046 resistive touch. TFT_eSPI.
- **`round128`** — **Waveshare ESP32-S3-Touch-LCD-1.28**: 1.28" GC9A01
  240×240 round panel + CST816T capacitive touch. TFT_eSPI.
- **`round21`** — **Waveshare ESP32-S3-Touch-LCD-2.1**: 2.1" ST7701 480×480
  round panel driven over the ESP32-S3's RGB-parallel LCD peripheral (not
  SPI) + CST820 capacitive touch, both gated through a TCA9554 I2C IO
  expander. Arduino_GFX — see "The round21 board" below for why.
- **`oled13`** — generic ESP32-S3 devkit + 1.3" SH1106 128×64 monochrome
  OLED, I2C. The intended **production screen size** — no touch hardware at
  all, every input is the MFL menu. Adafruit_SH110X — see "The oled13 board"
  below.

The master broadcasts `AirLiftData` (pressures/preset/status) and
`AirLiftButtons` (live MFL button state) over ESP-NOW; the unit renders them
and holds no state beyond the last packet of each. It also broadcasts back —
`AirLiftCommand`, only when the on-screen menu confirms an action — but
still needs no pairing and no knowledge of the master's MAC either
direction, same broadcast model throughout.

`display.cpp`/`touch.cpp` are identical between `cyd` and `round128` — every
pixel position, font size, and pin comes from `include/config.h`, gated on
the `DISPLAY_ROUND` build flag. `round128`'s grid is the same 2x2-corner /
tank-preset-strip / status-bar layout as the CYD, just scaled down and inset
so every box sits inside the circle the physical bezel actually shows — see
"Round panel geometry" below before touching the numbers.

`round21` can't share those files (TFT_eSPI has no working driver for its
ST7701 RGB-parallel panel — see "The round21 board" below), so it gets its
own `display_round21.cpp`/`touch_round21.cpp`/`tca9554.cpp`, selected per env
by `build_src_filter` in `platformio.ini`. It follows the *same* 2x2-grid /
strip / status-bar layout and the same `display::`/`touch::` interface as the
other two, just against a different rendering backend — `main.cpp` and the
ESP-NOW receive layer (`espnow_link.cpp`) don't know or care which board
they're running on.

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
*(`cyd` — 320×240 landscape, edge-to-edge)*

## Build & flash

```bash
pio run                       # build the default env (cyd)
pio run -e round128           # build the 1.28" round panel
pio run -e round21            # build the 2.1" round panel
pio run -e oled13             # build the 1.3" OLED (production size)
pio run -e round128 -t upload # flash over USB
pio device monitor -e round128 --baud 115200
```

All TFT_eSPI configuration is passed as `-D` build flags from `platformio.ini`
(`USER_SETUP_LOADED=1`), so the library folder is never edited and the config
survives a clean build or a `pio pkg update`. `User_Setup_CYD.h` in this folder
is the same configuration as a drop-in `User_Setup.h`, for Arduino IDE builds.

TFT_eSPI emits `#warning TOUCH_CS pin not defined` during the build, on the
two TFT_eSPI envs. That is expected and correct: neither board's touch
controller is driven by TFT_eSPI's built-in (resistive, SPI) touch support —
the CYD's XPT2046 sits on its own VSPI bus via `XPT2046_Touchscreen`, and
`round128`'s CST816T is a capacitive I2C part via `CST816S`, which TFT_eSPI
has no concept of at all. A `TOUCH_CS` would (on the CYD) make TFT_eSPI talk
to the touch chip over the display's own bus, which is not how the board is
wired. `round21` doesn't use TFT_eSPI at all, so this doesn't apply to it.

## Build flags worth knowing

| Flag | Default | Purpose |
| --- | --- | --- |
| `ESPNOW_WIFI_CHANNEL` | `1` | **Must match the master's channel.** |
| `ENABLE_TOUCH` | `0` | Compiles in the tap zones. Off because the unit is cluster-mounted. |
| `DEMO_MODE` | `0` | Animates fake pressures so the panel can be tested with no master present. |
| `DISPLAY_ROUND` | *(unset on `cyd`, `1` on `round128`)* | Selects `round128`'s pins/geometry/touch driver in `config.h`. Not something you set by hand — it's baked into that env's `build_flags`. |
| `BOARD_ROUND21` | *(unset elsewhere, `1` on `round21`)* | Selects `round21`'s pins/geometry/touch driver in `config.h`. Also baked into that env's `build_flags`. |

## Round panel (`round128`)

Waveshare ESP32-S3-Touch-LCD-1.28: ESP32-S3R2, 16MB flash, GC9A01 240×240
round panel on SPI, CST816T capacitive touch + QMI8658 IMU sharing one I2C
bus (SDA 6 / SCL 7 — IMU is present on the board but this firmware doesn't
use it). Flashes over the onboard CH343P USB-UART bridge, not the S3's native
USB, so it's a plain serial upload like the CYD.

Pin sourcing: the GC9A01 SPI pins come from three independent sources that
all agree — TFT_eSPI's own bundled
`User_Setups/Setup302_Waveshare_ESP32S3_GC9A01.h`, and two community
Waveshare pinout writeups. The CST816T touch pins (`TOUCH_SDA/SCL/RST/IRQ` in
`config.h`) come from a single community-verified PlatformIO project for this
exact board — lower confidence than the panel pins, since Waveshare sells a
plain (non-touch) `ESP32-S3-LCD-1.28` and a different `DualEye-1.28` board
with similar names and different wiring. **Verify against your unit's
schematic before relying on touch**, the same way this repo's other
hardware docs ask you to re-check MFL/LIN pin assignments against your own
car.

### Round panel geometry

The panel's framebuffer is a normal 240×240 rectangle — the round glass just
physically masks whatever gets drawn outside its visible circle. Rather than
give the round build its own layout code, its grid is the CYD's grid,
inset so every box's farthest corner stays a few px inside a conservative
118px-radius safe circle (measured from centre (120,120), not from any
physical test — a 240-diameter circle's true radius is 120, and a couple of
those px are typically lost to the bezel/lens, so this stays back from that
edge on purpose). The math is inline as a comment in `config.h` next to
`GRID_X0`/`GRID_Y0`: if your unit's visible circle is smaller and the grid's
corners get clipped by the bezel, shrink those two constants (and grow
`STATUS_H` to keep the bottom edge symmetric) until it clears.

Consequently the corner/strip fonts on `round128` are smaller than the CYD's
(Font 4, 26px, vs Font 6, 48px) — the round panel's cells are physically
smaller, so the CYD's big digits-only Font 6 no longer fits.

## The round21 board (`round21`)

Waveshare ESP32-S3-Touch-LCD-2.1: ESP32-S3-WROOM-1-N16R8 (16MB Quad flash,
8MB Octal PSRAM), ST7701 480×480 round panel, CST820 capacitive touch,
TCA9554 I2C IO expander. This is a materially different board from the other
two, not just a different pinout:

- The panel isn't SPI. It's driven over the ESP32-S3's dedicated RGB-parallel
  LCD peripheral (16 data lines + HSYNC/VSYNC/DE/PCLK) — the same peripheral
  used for the RGB parallel LCDs on things like the ESP32-S3-BOX. A short
  "3-wire SPI"-style link (CLK + MOSI only, no MISO, no real CS pin) is used
  *only* to send the ST7701's one-time gamma/voltage/timing register init;
  actual pixel data never touches that link.
- **TFT_eSPI has no working ST7701-RGB support** (this is an open, unresolved
  upstream issue, not something fixable from this repo), so this board can't
  reuse `display.cpp`/`touch.cpp` the way `round128` does. It uses
  [Arduino_GFX](https://github.com/moononournation/Arduino_GFX) ("GFX Library
  for Arduino") instead, via a thin custom `Arduino_GFX` subclass
  (`Arduino_ST7701` in `display_round21.cpp`) that routes the library's
  drawing calls to `esp_lcd_panel_draw_bitmap()` against the panel's
  PSRAM framebuffer.
- The panel's reset and init-SPI "CS" aren't real GPIOs — they're bits on a
  **TCA9554** I2C GPIO expander (`tca9554.cpp`), which also gates the CST820
  touch controller's reset and the panel's power-enable line.

**Pin/init sourcing and confidence:** the RGB data-pin mapping, TCA9554
usage, and the full ST7701 register-init sequence (gamma curves, VOP/VCOM/
VGH/VGL, timing) are ported **byte-for-byte** from a community-posted copy of
what is, by its file names (`Display_ST7701.cpp`, `TCA9554PWR.cpp`,
`Touch_CST820.cpp`, `I2C_Driver.cpp`), Waveshare's own Arduino demo for this
board. Do not hand-edit the register values in `st7701Init()` — they're
panel-specific gamma/voltage/timing, not arbitrary numbers, and this hasn't
been tested against real glass. One deliberate deviation from that source:
the demo also constructs an `Arduino_ESP32RGBPanel` bus object and calls its
`begin()`, but nothing in its actual drawing path uses it (only the directly-
created `esp_lcd_panel_handle_t` does) — reproducing that would mean
configuring the RGB peripheral through two separate, uncoordinated paths, so
it's dropped here as vestigial.

**PSRAM matters on this board** in a way it doesn't on the other two: the RGB
panel's two framebuffers live in PSRAM (`fb_in_psram = true`, `double_fb =
true` in `rgbPanelInit()`), so `platformio.ini`'s `qio_opi` memory-type
config (Quad flash + Octal PSRAM, matching the N16R8 module) has to be
correct or the panel simply won't come up. If your unit uses a different
ESP32-S3 module variant, check its flash/PSRAM mode before flashing.

**round21 layout** follows the same inscribed-square reasoning as
`round128`'s (see above) — this panel is 4x the pixel area with a much more
generous safe circle, so the numbers in `config.h`'s `BOARD_ROUND21` block
have more headroom, but they're equally unverified against real glass.
Arduino_GFX's built-in font is a fixed 6x8px bitmap glyph scaled by an
integer `setTextSize()` — there's no font-ID system like TFT_eSPI's, hence
the `VALUE_TEXTSIZE_CORNER`/`LABEL_TEXTSIZE`/etc. macros instead of
`VALUE_FONT_CORNER`.

Unlike the two TFT_eSPI boards, `round21`'s value redraws skip the
sprite-composition trick entirely (`drawValue()` just does a plain
`fillRect` + centred `print()`). That trick exists on the other boards to
hide the visible time an SPI transfer takes; here the "display" is a PSRAM
framebuffer the RGB peripheral scans out continuously in hardware, so writing
into it is just a RAM store with no transfer-time flicker to hide. The
diffed-redraw cache (skip repainting a box whose text didn't change) is kept
anyway, purely to save CPU at the ~10 Hz update rate.

## The oled13 board (`oled13`)

Generic ESP32-S3 devkit + 1.3" SH1106 128×64 monochrome OLED, I2C — the
**intended production screen size**, unlike the other three boards which are
all off-the-shelf dev-board bring-up rigs. No touch hardware exists on this
board at all (`touch_oled13.cpp` is an unconditional stub); every input is
the MFL menu instead.

Wiring is whatever you actually used — this is a bare devkit + a separate
OLED breakout, not a fixed-pinout product like the other three boards, so
`config.h`'s `I2C_SDA_PIN`/`I2C_SCL_PIN`/`OLED_I2C_ADDR` under `BOARD_OLED13`
need to match your build. `display_oled13.cpp`'s `begin()` tries both common
addresses (0x3C, 0x3D) and, if neither ACKs, scans the whole I2C bus and logs
what it finds — check the serial log first if the screen stays blank.

**Layout is a genuine redesign, not a shrunk version of the other boards'
2x2 grid** — 128×64 monochrome is a different enough form factor (no colour,
a fraction of the pixel budget) that it gets its own compact layout:

- Corner pressures are the primary readout, big (`GAUGE_TEXTSIZE` 2, 16px),
  arranged as two rows — "F" (front: FL left, FR right) and "R" (rear: RL
  left, RR right) — with a narrow single-letter axle label instead of a
  `FL:`/`FR:` prefix, which wouldn't fit at this size. Same left/right =
  driver/passenger-side convention the other boards' 2x2 grid uses.
- Status has no room for its own text row here, so it's a single glyph in
  the top-right corner instead — `^`/`v` for raising/lowering, `X` for no
  signal, nothing drawn for idle (a blank icon reads as "no data" more than
  "nothing wrong"). See `STATUS_CHAR_*` in `config.h`.
- Tank and preset get their own small rows lower on the screen, freed up by
  moving status out of a full row.
- The MFL menu's 8-item Presets list can't fit in one screen at this
  resolution (title + 3 rows is already the full 64px height), so
  `drawMenu()` scrolls a `MENU_VISIBLE_ROWS`-tall window that keeps the
  cursor centred, with a "N/total" hint in the title row — see `config.h`'s
  `MENU_VISIBLE_ROWS` comment.

No PSRAM needed (the whole SH1106 framebuffer is 128×64/8 = 1KB), so unlike
`round21` this env doesn't touch `qio_opi`/PSRAM config at all — just the
same native-USB `ARDUINO_USB_CDC_ON_BOOT` requirement `round21` has (see that
section above).

## Wi-Fi channel

Applies to all four boards. ESP-NOW only works between radios parked on the
same channel, and this end never associates with anything, so the channel is
pinned explicitly at boot. If the
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

## MFL menu

The steering wheel's MFL cruise-control buttons are wired into the **master**
(GPIO34, opto-isolated — see `MFL-FINDINGS.md`), not this display. To let
those buttons drive an on-screen menu here, ESP-NOW is bidirectional between
the two ends for the first time:

- **Master → slave**, `AirLiftButtons` (`airlift_espnow.h`): live button
  *state* (not press/release events), broadcast at ~20 Hz — faster than the
  10 Hz `AirLiftData` telemetry, so menu navigation feels responsive.
  State rather than events matters here: ESP-NOW broadcast has no delivery
  guarantee, so a lost state frame just gets caught up by the next one,
  where a lost discrete "button pressed" event would be a permanently
  missed input. `menu.cpp` does its own rising-edge detection by diffing
  each new frame against the last one it saw.
- **Slave → master**, `AirLiftCommand`: sent once, when the menu confirms an
  action (currently only `CMD_SELECT_PRESET`). Same broadcast/unencrypted/
  no-pairing trust model as everything else this firmware sends or
  receives — the master can only be told to select from its own
  pre-configured preset table, never handed an arbitrary pressure target.

**Mapping** (`menu.cpp`): `IO` = enter the menu / select the highlighted
item, `PLUS`/`MINUS` = move the cursor up/down, `SET`/`ON` = back one level
(and back out of the top level returns to the gauge view). Two top-level
screens: **Presets** (select one — sends `CMD_SELECT_PRESET` and returns to
the gauge) and **Settings** (currently just backlight, adjusted live via
`display::setBacklight()`; not persisted — resets to `BACKLIGHT_PCT` on
reboot).

The menu is rendered by the same per-board `display::drawMenu()` each env
already has for the gauge (`display.cpp` for `cyd`/`round128`,
`display_round21.cpp` for `round21`) — board-agnostic `menu.cpp` only ever
deals in a `menu::View` (title + item list + cursor), never a pixel. Redraws
are whole-screen rather than diffed per element (unlike the gauge's
`update()`): the menu only changes on a button press, not a 10 Hz stream, so
there's no meaningful SPI/redraw cost to save by diffing it.

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

Applies to `cyd`/`round128`/`round21` only — `oled13` has no touch hardware
at all, `touch_oled13.cpp` is an unconditional stub, and every input on that
board is the MFL menu instead. Tap-zone logic is the same shape on the other
three boards (`dispatch()`, in whichever `touch*.cpp` the env compiles): the
four corner quadrants, and preset down / preset up on the left / right of
the tank-preset strip, driven by that env's `GRID_X0/Y0`/`CELL_W`/`CELL_H`
geometry. Each hit currently just does a `Serial.printf` — the actual
command injection is master-side work still to be wired up. Their raw-read
front ends differ:

- **`cyd`** — XPT2046 resistive, its own SPI bus. Calibrated for raw X/Y
  200–3900, remapped to landscape with the axes swapped and Y mirrored in
  `mapRaw()`. If taps land in the mirrored position, swap the endpoints of one
  `map()` call there.
- **`round128`** — CST816T capacitive, I2C (`fbiego/CST816S`). Reports
  `touch.data.x/y` directly in panel space at rotation 0 — no raw-range
  calibration or axis remap needed. If your mounting uses a different
  `TFT_ROTATION`, the touch coordinates will need the same remap treatment
  `mapRaw()` gives the CYD.
- **`round21`** — CST820 capacitive, I2C, reset via the TCA9554 expander
  (`touch_round21.cpp`, register map ported from Waveshare's demo — see "The
  round21 board" above). Also reports coordinates directly in panel space at
  rotation 0, same caveat as `round128` if you change `DISPLAY_ROTATION`.

## Layout

| File | Contents |
| --- | --- |
| `include/config.h` | Per-board pins + layout geometry (`DISPLAY_ROUND`/`BOARD_ROUND21`/`BOARD_OLED13`-gated), colours, timeouts |
| `include/airlift_espnow.h` | The shared wire structs (`AirLiftData`, `AirLiftButtons`, `AirLiftCommand`) — **keep in sync with the master** |
| `src/espnow_link.cpp` | STA mode, fixed channel, receive callback (`AirLiftData` + `AirLiftButtons`), `sendCommand()` — shared, board-agnostic |
| `include/menu.h` / `src/menu.cpp` | MFL-button-driven menu state machine (edge detection, screens, cursor) — shared, board-agnostic, rendering-agnostic |
| `src/display.cpp` | `cyd`/`round128` (TFT_eSPI): static layout + diffed value rendering + `drawMenu()`, driven entirely by `config.h` |
| `src/touch.cpp` | `cyd`/`round128`: tap-zone dispatch; XPT2046 (CYD, VSPI) or CST816T (round128, I2C) front end |
| `src/display_round21.cpp` | `round21` (Arduino_GFX): ST7701 init, RGB panel bring-up, `Arduino_ST7701` GFX subclass, layout + diffed rendering + `drawMenu()` |
| `src/touch_round21.cpp` | `round21`: tap-zone dispatch + CST820 front end |
| `src/tca9554.cpp` | `round21`: TCA9554 I2C IO-expander driver (panel reset/CS/power, touch reset) |
| `src/display_oled13.cpp` | `oled13` (Adafruit_SH110X): I2C bring-up (with address fallback + bus scan on failure), compact big-value layout + `drawMenu()` |
| `src/touch_oled13.cpp` | `oled13`: unconditional no-op stub — no touch hardware on this board |
| `src/main.cpp` | Boot, loop (menu vs. gauge branch), preset names — shared, board-agnostic |

Each env's `build_src_filter` in `platformio.ini` picks the right pair of
`display*`/`touch*` files and excludes `tca9554.cpp` on the boards that
don't have one.
