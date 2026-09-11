# SenseCAP Indicator — SEN54 Air-Quality Panel

Firmware that turns the [Seeed Studio SenseCAP Indicator](https://www.seeedstudio.com/SenseCAP-Indicator-D1-p-5643.html) into a wall-mountable air-quality monitor. The built-in **Sensirion SEN54** sensor (PM1.0/2.5/4.0/10, humidity, temperature, VOC index) is rendered live on the 480×480 touchscreen and published to an MQTT broker for Home Assistant or any other consumer.

---

## Table of Contents

- [Features](#features)
- [Quick Start](#quick-start)
- [Architecture](#architecture)
- [MQTT Protocol](#mqtt-protocol)
- [Configuration](#configuration)
- [Build & Flash](#build--flash)
- [Console Commands](#console-commands)
- [Development](#development)
- [Version](#version)

---

## Features

- [x] SEN54 live dashboard: PM1.0/2.5/4.0/10, humidity, temperature, VOC index
- [x] VOC alert level (0–3) with a 15-minute sensor warm-up guard after every boot
- [x] MQTT publishing with configurable topic prefix/device name and retained LWT online/offline status
- [x] Wi-Fi, MQTT broker, brightness and sleep-mode configuration from the touchscreen
- [x] MQTT broker configuration from the serial console (`setmqtt`, `haconfig`, `mqtthelp`)
- [x] NVS-backed persistence for Wi-Fi credentials, MQTT config, display settings
- [x] Legacy Home Assistant switch protocol (6 binary switches + 2 sliders) still available

---

## Quick Start

**Prerequisites:** ESP-IDF **v5.5.x** installed (see [ESP-IDF setup](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/get-started/)). Either export `IDF_PATH` or define a `get_idf` shell alias:

```bash
# ~/.bashrc or ~/.zshrc
alias get_idf='source ~/esp/esp-idf-v5.5.4/export.sh'   # adjust to your install
```

```bash
git clone <this-repo>
cd sensecap-indicator-ha
get_idf                  # or: export IDF_PATH=... — ./dev auto-activates from $IDF_PATH
./dev build && ./dev flash
./dev monitor            # Ctrl-] to exit
```

After flashing, configure Wi-Fi and the MQTT broker on the device (Settings modal) or via the [serial console](#console-commands).

---

## Architecture

The SenseCAP Indicator is a dual-MCU device:

| MCU | Role | Key resources |
|-----|------|---------------|
| **ESP32-S3** | Display, touch, Wi-Fi, MQTT, NVS, all business logic | 8 MB flash, PSRAM @ 120 MHz OCT, 240 MHz CPU |
| **RP2040** | Sensor coprocessor | Reads the SEN54 over I2C, streams readings to the ESP32-S3 over a COBS-framed UART link |

The ESP32-S3 **never** talks to Grove sensors directly.

On the ESP32-S3, each application domain is a vertical slice with a paired `*_model.c` (state, NVS, MQTT — no LVGL) and `*_view.c` (LVGL widgets, touch callbacks). Model and view never call each other directly; all cross-domain communication goes through the `view_event_handle` ESP event loop.

**Developer Documentation:** Module layout, boot sequence, event-bus contract, LVGL thread-safety rules, build gotchas, and verification commands are documented in [`AGENTS.md`](AGENTS.md) and [`main/ARCHITECTURE.md`](main/ARCHITECTURE.md).

---

## MQTT Protocol

Topics follow `<topic_prefix>/<device_name>/<leaf>` (defaults: prefix `seeed`, device name MAC-derived `indicator-<mac4>`, also used as the MQTT client_id — one name everywhere):

```
seeed/indicator-3f2a/data     — sensor data, JSON, every 5 s (held back until NTP-synced)
seeed/indicator-3f2a/status   — retained "online" on connect; broker LWT publishes retained "offline"
```

The data payload carries `seq`, `timestamp` (UTC epoch seconds), `device`, and 8 metrics: `sen5x/pm1_0`, `sen5x/pm2_5`, `sen5x/pm4_0`, `sen5x/pm10`, `sen5x/humidity`, `sen5x/temperature`, `sen5x/voc_index`, `sen5x/voc_alert`.

The legacy three-topic Home Assistant switch protocol (`indicator/sensor`, `indicator/switch/set`, `indicator/switch/state`) is still active in parallel. Payload keys and the full spec are covered by `scripts/test_sen5x_mqtt_protocol.py` / `scripts/test_ha_switch_protocol.py` and the root `AGENTS.md`.

**MQTT topics and payload compatibility are product behavior — do not change them without explicit instruction.**

---

## Configuration

Wi-Fi credentials, MQTT broker (address, credentials, topic prefix, device name), brightness and sleep mode are all configurable on-device via the touchscreen Settings modal, and persist in NVS. Broker settings can also be changed over serial — see below.

To regenerate `sdkconfig` from defaults: delete it and run `./dev build`.

---

## Build & Flash

### ESP32-S3 (main firmware)

```bash
./dev build            # clean + build (wipes build/ by default)
./dev build --no-clean # incremental build
./dev flash            # auto-detects port (-p PORT, -b BAUD, default 460800)
./dev monitor          # Ctrl-] to exit
./dev fullclean        # idf.py fullclean
./dev test             # Python guards + ESP-IDF host unit tests
```

Manual `idf.py` works the same way after `. "$IDF_PATH/export.sh"`.

### RP2040 (sensor coprocessor)

Only needs rebuilding when `rp2040/` changes. Built with PlatformIO (Arduino core by Earle Philhower):

```bash
./dev rp2040 build
./dev rp2040 upload    # autodetects the RP2040 by USB VID:PID
./dev rp2040 monitor   # 115200 baud
```

**Critical `sdkconfig.defaults` settings — do not remove:** `CONFIG_LV_MEM_POOL_EXPAND_SIZE_KILOBYTES=256` (load-bearing for the PSRAM overflow pool in `main/ui/ui_mem_pool.c`), PSRAM 120 MHz OCT mode, CPU 240 MHz, flash QIO 120 MHz. App partition is 7 MB of 8 MB flash (`partitions.csv`).

---

## Console Commands

| Command | Description |
|---------|-------------|
| `mqtthelp` | Print broker, topic, and payload examples |
| `haconfig` | Print the current MQTT/HA configuration |
| `setmqtt -a <addr>` | Set broker address (e.g. `mqtt://192.168.1.10:1883`) |
| `setmqtt -n <name>` / `-t <prefix>` | Set device name / topic prefix |
| `setmqtt -a <addr> -u <user> -p <pass>` | Full broker configuration |

After `setmqtt` succeeds, the configuration is saved to NVS and the MQTT client restarts automatically.

---

## Development

**Code completion.** Install [clangd](https://github.com/clangd/clangd/releases) and the [clangd VS Code extension](https://marketplace.visualstudio.com/items?itemName=llvm-vs-code-extensions.vscode-clangd). After one successful build, `build/compile_commands.json` is generated and clangd uses it automatically.

**Rules and verification.** See [`AGENTS.md`](AGENTS.md) for the model/view boundary, event-post discipline, build gotchas and the check suite (`./dev test`, `scripts/dev_check.py`) that must pass after any change.

---

## Version

| Component | Version |
|-----------|---------|
| Firmware (ESP32-S3) | `v1.1.0` |
| ESP-IDF | `v5.5.x` (verified with 5.5.4) |
| LVGL | `v9.x` (managed component) |
| RP2040 build system | PlatformIO |

<details>
<summary>Changelog</summary>

| Version | Notes |
|---------|-------|
| `v1.1.0` | SEN54 air-quality focus; single-tile dashboard with modal settings; configurable MQTT topics + LWT status; LVGL 9; unified build/flash tooling into `./dev` |
| `v1.0.0` | Initial release (ESP-IDF 5.1 era, LVGL 8, HA switch panel) |

</details>
