# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

All tooling goes through the `./dev` shell script at the repo root. It auto-activates ESP-IDF if `idf.py` is not on PATH (reads `$IDF_PATH`).

### ESP32-S3 (main firmware — ESP-IDF v5.4.x)

```bash
./dev build          # clean + build (deletes build/ by default)
./dev build --no-clean   # incremental build
./dev flash          # auto-detect port and flash
./dev monitor        # serial monitor (Ctrl-] to exit)
./dev fullclean      # wipe build directory
```

### RP2040 coprocessor (PlatformIO / Arduino)

Only needs rebuilding when `rp2040/` changes.

```bash
./dev rp2040 build
./dev rp2040 upload      # device must appear as /dev/cu.usbmodem*
./dev rp2040 monitor     # 115200 baud
```

### macOS SDL2 simulator (no hardware needed)

Preview the LVGL UI on the host machine. Does not require ESP-IDF or physical hardware.

```bash
./dev sim build                    # CMake build only (sim/build/)
./dev sim screenshot               # headless render → sim/sim.png
./dev sim screenshot /tmp/out.png  # headless render → custom path
./dev sim run                      # interactive SDL2 window
```

After any `*_view.c` or `*_screen.c` change, run `./dev sim screenshot` and read
`sim/sim.png` to verify the UI before claiming success.

### Verification scripts

Run these after any change before claiming success:

```bash
python3 scripts/architecture_scan.py          # domain boundary check
python3 scripts/dev_check.py --skip-build     # all checks except firmware build
python3 scripts/dev_check.py                  # full check including build

# Protocol-specific tests
python3 scripts/test_sen5x_mqtt_protocol.py   # SEN54/Sparkplug B payloads
python3 scripts/test_ha_switch_protocol.py    # legacy HA switch protocol
```

---

## Architecture

### Boot sequence

Entry point: `main/main.c`

1. `bsp_board_init()`
2. `lv_port_init()`
3. create `view_event_handle`
4. `indicator_view_init()` — nav tileview + all domain view/screen components
5. `indicator_model_init()` — storage, btn, display, rp2040, sensor, cmd, Wi-Fi, MQTT, HA

### Architecture rules

- Preserve current product behavior unless a task explicitly says otherwise.
- ESP32-S3 receives sensor data from RP2040. Do not add direct Grove drivers on ESP32-S3.
- Domains live in vertical slices (`main/ha/`, `main/wifi/`, `main/sensor/`, `main/display/`, `main/rp2040/`, `main/mqtt/`, `main/storage/`, `main/cmd/`, `main/btn/`). Do not reintroduce legacy compatibility layers.
- View/screen files may call LVGL APIs. Model files must remain UI-free and must not own LVGL objects.
- Background tasks and event handlers must update LVGL through the lock/deferral pattern (`lv_port_sem_take/give`).
- MQTT topics and Home Assistant payload compatibility are product behavior — do not change them without explicit instruction.
- Keep refactors incremental; the project must remain buildable after each patch.

### Dual-MCU overview

- **RP2040** — reads the SEN54 sensor (PM1.0/2.5/4.0/10, humidity, temperature, VOC index) via I2C; sends readings over COBS-framed UART to the ESP32-S3 every ~1 s.
- **ESP32-S3** — owns display (480×480, LVGL 9), touch, Wi-Fi, MQTT, NVS, and all business logic.

Stack: ESP-IDF 5.4.x, LVGL 9 (managed by ESP Component Manager). Runtime UI is handwritten in domain view/screen components under `main/`.

### UART/COBS packet flow

RP2040 sends typed packets. Each packet is a 1-byte type code followed by a `float` payload. Type codes are defined in both:
- `rp2040/include/indicator_rp2040.hpp` — RP2040 side (C++, canonical)
- `main/rp2040/rp2040.h` — ESP32-S3 side (C, must match)

**Keep these two files in sync whenever adding a new sensor type.**

Current SEN54 type codes (`0xC0–0xC6`):

| Code | Name | Unit |
|------|------|------|
| 0xC0 | `PKT_TYPE_SENSOR_SEN54_PM1_0` | µg/m³ |
| 0xC1 | `PKT_TYPE_SENSOR_SEN54_PM2_5` | µg/m³ |
| 0xC2 | `PKT_TYPE_SENSOR_SEN54_PM4_0` | µg/m³ |
| 0xC3 | `PKT_TYPE_SENSOR_SEN54_PM10` | µg/m³ |
| 0xC4 | `PKT_TYPE_SENSOR_SEN54_HUMIDITY` | %RH |
| 0xC5 | `PKT_TYPE_SENSOR_SEN54_TEMPERATURE` | °C |
| 0xC6 | `PKT_TYPE_SENSOR_SEN54_VOC_INDEX` | 1–500 |

Dynamic registry codes `0xB8/0xB9/0xBA` (`ATTACHED/DETACHED/VALUE`) are still active and used by `rp2040.c`.

### Model / View pattern

Each domain has paired `*_model.c` + `*_view.c`. They **never call each other's functions directly**. All cross-domain communication goes through:

```c
// post
esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_SENSOR_DATA,
                  &data, sizeof(data), 0);
// subscribe
esp_event_handler_instance_register_with(
    view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_SENSOR_DATA, cb, NULL, NULL);
```

Event IDs and payload types are defined in `main/view_data.h` (IDs) and `main/view_data_types.h` (structs). The manifest comment in `view_data_types.h` lists every event's producer and consumer — update it when adding events.

### MQTT protocol (Sparkplug B)

The device publishes to a broker at `mqtt://seeed-mqtt.lan` (configurable via Settings UI or `setmqtt` console command). Topics follow Sparkplug B:

```
spBv1.0/seeed/NBIRTH/edge-01          — node birth
spBv1.0/seeed/NDEATH/edge-01          — node death (LWT)
spBv1.0/seeed/DBIRTH/edge-01/sen5x   — device birth (with "type":"float" per metric)
spBv1.0/seeed/DDATA/edge-01/sen5x    — data (no "type" field, every 5 s)
```

DDATA carries 8 metrics: `sen5x/pm1_0`, `sen5x/pm2_5`, `sen5x/pm4_0`, `sen5x/pm10`, `sen5x/humidity`, `sen5x/temperature`, `sen5x/voc_index`, `sen5x/voc_alert`.

Implementation lives in `main/sen5x/sen5x_mqtt.c`. It is wired into `main/ha/ha_mqtt.c` (`indicator_ha_model_init` calls `sen5x_mqtt_init`).

### VOC alert and warming-up state

`sen5x_mqtt.c` maintains a two-state machine (`WARMING_UP` → `ACTIVE`). The first 60 minutes after first boot is WARMING_UP; this is persisted via NVS (namespace `"sen5x"`, key `"warm_done"`) so controlled restarts skip the wait. During WARMING_UP, `voc_alert` is always 0.

Lab VOC thresholds: 0 (≤120), 1 (121–180), 2 (181–250), 3 (>250).

### LVGL thread safety

Any code that touches LVGL widget state outside the LVGL task must hold the semaphore:

```c
lv_port_sem_take();
// lv_obj_* calls
lv_port_sem_give();
```

Model files must not own LVGL objects.

### Navigation

`main/nav/nav.h` defines tile indices. Current tiles:

```c
#define NAV_TILE_SEN5X    0   // SEN54 sensor dashboard
#define NAV_TILE_SETTINGS 1   // Settings
#define NAV_TILE_COUNT    2   // MUST equal the number of tiles above
```

When adding a tile: bump `NAV_TILE_COUNT`, add the directory to `DIRECTORIES_TO_INCLUDE` in `main/CMakeLists.txt`, create `*_view.c`, and call its init from `indicator_view.c`.

---

## Key File Locations

| Task | File(s) |
|------|---------|
| Boot / init orchestration | `main/main.c`, `main/indicator_model.c`, `main/indicator_view.c` |
| Tile navigation | `main/nav/nav.h`, `main/nav/nav.c` |
| Shared event IDs and payload manifest | `main/view_data.h` + `main/view_data_types.h` |
| LVGL port and semaphore | `main/lv_port.h`, `main/lv_port.c` |
| LVGL image/font assets | `main/assets/` |
| PKT type codes (RP2040 side) | `rp2040/include/indicator_rp2040.hpp` |
| PKT type codes (ESP32-S3 side) | `main/rp2040/rp2040.h` |
| RP2040 UART ingress | `main/rp2040/rp2040.c` |
| Sensor enum and SENSOR_TYPE_LIST | `main/view_data_types.h` |
| Sensor data cache/parser | `main/sensor/sensor_model.c` |
| Sensor dashboard UI | `main/sensor/sensor_view.c` |
| Sparkplug B MQTT logic | `main/sen5x/sen5x_mqtt.c` + `.h` |
| MQTT lifecycle (ESP32-S3) | `main/ha/ha_mqtt.c` |
| MQTT broker + credentials | `main/home_assistant_config.h` |
| Home Assistant model/view | `main/ha/` |
| Wi-Fi model/view | `main/wifi/` |
| Display settings | `main/display/` |
| Local development checks | `scripts/dev_check.py`, `scripts/architecture_scan.py` |

---

## Legacy Sensor Code

Old sensors (SCD41/SGP40/SHT41) are preserved behind guards:

- `#ifdef LEGACY_SENSORS` — RP2040 side (`indicator_rp2040.hpp`, `sensors.cpp`, `sensor_model.c`) and `rp2040.h`
- `#ifdef LEGACY_HA_SENSORS` — ESP32-S3 side (`ha_sensor.c`)

Do not define these macros in normal builds. To re-enable legacy hardware, add `-DLEGACY_SENSORS` / `-DLEGACY_HA_SENSORS` to the build.

---

## Critical sdkconfig Settings

Do not remove from `sdkconfig.defaults`:

- `CONFIG_LV_MEM_CUSTOM=y` — required; removing causes LVGL to freeze
- PSRAM: 120 MHz OCT mode
- CPU: 240 MHz, flash: QIO 120 MHz

App partition is 7 MB (`partitions.csv`).

---

## Do Not Touch

| Path | Reason |
|------|--------|
| `main/lv_port.c` | BSP hardware boundary — display/touch port |
| `components/bsp/` | Hardware driver layer |
| `managed_components/` | Managed by ESP Component Manager, never edit manually |
