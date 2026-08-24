# Toolchain Setup — Machine Handover Reference

Exact tooling/versions from a machine that successfully builds and flashes
every env in this repo (master `PlatformIO/`, `SlaveDisplay/`, and the
`test/*` bench rigs). If a build/flash works here but not on another
machine, compare against this first before assuming a code or hardware
problem.

## The one thing most likely to bite you

Every `platformio.ini` in this repo declares:

```ini
platform = espressif32
```

**unpinned** — no version, no URL. On this machine, that unpinned name
resolves to the **pioarduino community fork**, not the official PlatformIO
registry platform:

```
PLATFORM: Espressif 32 (53.3.10)
  (https://github.com/pioarduino/platform-espressif32/releases/download/53.03.10/platform-espressif32.zip)
```

confirmed identical across every env in every project in this repo (master,
all five `SlaveDisplay` boards, `test/can_sniffer`, `test/mfl_sniffer`).
That's because this fork was explicitly installed at some point and
PlatformIO's resolver picks it as the highest-versioned match for the bare
`espressif32` name — nothing in the repo's `.ini` files forces this.

**On a fresh machine that has never installed the pioarduino fork, `pio run`
will instead pull whatever the official `platformio/espressif32` registry
entry currently is** — a different platform, likely bundling an older
Arduino-ESP32 core. This matters a lot here: native USB-CDC on the ESP32-S3
boards (`round21`, `oled13`, `lcd147`), `ledcAttach()`/`neopixelWrite()`, and
other Arduino-ESP32 3.x APIs used throughout this codebase need that core
version. An older core can fail to build, or build something that can't
enter/complete a flash upload correctly — exactly the "can't flash" symptom
this doc exists to rule out.

**Fix / verify**, before troubleshooting anything else:

```bash
pio pkg install -g -p "https://github.com/pioarduino/platform-espressif32/releases/download/53.03.10/platform-espressif32.zip"
pio run -e <any env> -v 2>&1 | grep '^PLATFORM:'
```

The output must say `Espressif 32 (53.3.10)` with that pioarduino URL, not a
bare `(6.x.x)` official-registry version. If it doesn't match, that's very
likely the root cause of a flash/build failure that only happens on one
machine.

## Exact versions (this machine)

| Tool | Version |
|---|---|
| PlatformIO Core | 6.1.18 |
| Python (pio's venv, `~/.platformio/penv`) | 3.11.7 |
| esptool.py | 4.8.5 (pioarduino's build, bundled with the platform above) |
| espressif32 platform | 53.3.10 (pioarduino fork — see above) |
| framework-arduinoespressif32 (Arduino-ESP32 core) | 3.1.0 |
| framework-arduinoespressif32-libs (ESP-IDF) | 5.3.0+sha.083aad99cf (IDF 5.3) |
| toolchain-xtensa-esp-elf | 13.2.0+20240530 |
| macOS | 26.5.1 (build 25F80) |

These are what every env in this repo actually built against when last
verified (checked via `pio run -e <env> -v`, not just what's installed
globally — this machine has several other espressif32/framework versions
installed too, from other projects; they're irrelevant here as long as the
platform line above matches).

## Per-project library versions (as resolved by `pio run -v`)

**`PlatformIO/` (master)**
| Library | Version |
|---|---|
| AsyncTCP (ESP32Async/AsyncTCP) | 3.5.0 |
| ESPAsyncWebServer (ESP32Async/ESPAsyncWebServer) | 3.12.0 |
| ArduinoJson (bblanchon/ArduinoJson) | 7.4.3 |
| LittleFS / Update / WiFi | 3.1.0 (bundled with the Arduino core) |

**`SlaveDisplay/`** (per env — only the env's own display library, plus the
core's bundled WiFi/SPI/Wire)
| Env | Library | Version |
|---|---|---|
| `cyd` | TFT_eSPI (bodmer) | 2.5.43 |
| `cyd` | XPT2046_Touchscreen (git: PaulStoffregen/XPT2046_Touchscreen) | 1.4.0+sha.f956c5d |
| `round128` | TFT_eSPI (bodmer) | 2.5.43 |
| `round128` | CST816S (fbiego) | 1.3.0 |
| `round21` | GFX Library for Arduino (moononournation) | 1.6.7 |
| `round21_lvgl` | lvgl/lvgl | 9.5.0 |
| `oled13` | Adafruit SH110X | 2.1.15 |
| `oled13` | Adafruit GFX Library | 1.12.6 |
| `oled13` | Adafruit BusIO | 1.17.4 |
| `lcd147` | TFT_eSPI (bodmer) | 2.5.43 |
| `lcd147_lvgl` | TFT_eSPI (bodmer) | 2.5.43 |
| `lcd147_lvgl` | lvgl/lvgl | 9.5.0 |

**`test/*` bench rigs** — `can_sniffer`, `mfl_sniffer`, `stepper_sweep` use
only core libraries (WiFi, esp_now, esp_wifi, driver/twai), no extra
`lib_deps`.

## Board / upload settings quick-reference

| Project / env | `board` | Upload path | `upload_speed` | Flash notes |
|---|---|---|---|---|
| `PlatformIO/` (master) | `esp32dev` | CH340-style USB-UART bridge (port shows as `cu.usbserial-*`) | default (unset) | — |
| `SlaveDisplay` `cyd` | `esp32dev` | CH340-style bridge | **460800** (pinned — this board's CH340 drops the esptool stub handshake at the default 921600) | — |
| `SlaveDisplay` `round128` | `esp32-s3-devkitc-1` | native USB (`cu.usbmodem*`) | 921600 | `board_upload.flash_size=16MB`, `board_build.flash_mode=qio` |
| `SlaveDisplay` `round21` | `esp32-s3-devkitc-1` | native USB | 921600 | qio_opi PSRAM config — see platformio.ini comments |
| `SlaveDisplay` `round21_lvgl` | `esp32-s3-devkitc-1` | native USB | 921600 | Same qio_opi PSRAM config as `round21` — LVGL rendering backend, same board |
| `SlaveDisplay` `oled13` | `esp32-s3-devkitc-1` | native USB | 921600 | — |
| `SlaveDisplay` `lcd147` | `esp32-s3-devkitc-1` | native USB | 921600 | `board_upload.flash_size=16MB`, `board_build.flash_mode=qio` |
| `SlaveDisplay` `lcd147_lvgl` | `esp32-s3-devkitc-1` | native USB | 921600 | Same flash settings as `lcd147` — LVGL rendering backend, same board |
| `test/can_sniffer`, `test/mfl_sniffer` | `esp32dev` | CH340-style bridge | default | — |
| `test/stepper_sweep` | `esp32-s3-devkitc-1` | native USB | 115200 | — |

**Native-USB ESP32-S3 boards** (`round21`, `oled13`, `lcd147`, and
`stepper_sweep`) enumerate as `/dev/cu.usbmodem*` on macOS and need no
driver. **CH340-bridge boards** (master, `cyd`, `can_sniffer`,
`mfl_sniffer`) enumerate as `/dev/cu.usbserial-*` — modern macOS (Ventura+)
has this built in natively; nothing to install.

## macOS auto-reset quirk (if serial capture looks empty)

Opening a fresh serial connection to an already-running native-USB-CDC
ESP32-S3 board does **not** reliably show its live output — only a
connection that also toggles RTS/DTR to trigger a genuine reset reliably
captures anything, and only from that reset point forward. If a live
serial monitor looks silent on a board you know is running, reset it
(physical button, or a monitor tool that pulses RTS) rather than assuming
nothing's happening.

## `pio run -v` sanity check

To confirm a machine matches this doc for a given env:

```bash
cd SlaveDisplay   # or PlatformIO, or test/<rig>
pio run -e <env> -v 2>&1 | grep -E '^PLATFORM:|^PACKAGES:| - '
```

Compare the `PLATFORM:` line and package list against the tables above.
