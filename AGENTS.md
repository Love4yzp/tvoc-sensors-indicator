# Agent Guide

> **File roles (pinned — do not merge, delete, or "deduplicate" these two files):**
> - `CLAUDE.md` — read by **Claude Code** (it has no native fallback to AGENTS.md).
> - `AGENTS.md` (this file) — read by **other agents** (Codex, Cursor, Kimi Code, etc.); CLAUDE.md is ignored by them.
> Both are entry points on purpose. Keep project knowledge in CLAUDE.md and agent-agnostic operational rules here; each file must stay useful on its own.

This file is the entry point for AI coding agents. It assumes you know nothing about the project. See [CLAUDE.md](CLAUDE.md) for the same content aimed at Claude Code; keep both in sync when you change project knowledge. The rules below are project-specific operational knowledge — follow them, they are not suggestions.

## Project overview

Firmware for the **Seeed Studio SenseCAP Indicator** — a dual-MCU environmental monitoring panel (project name `indicator_ha`, currently focused on the Sensirion SEN54 air-quality sensor; also branded "Seeed Monitor" in `docs/`):

- **ESP32-S3** — main firmware. Owns the 480×480 display (LVGL 9), touch, Wi-Fi, MQTT, NVS storage, and all business logic. Built with **ESP-IDF v5.4.x**; LVGL 9 and `esp_lvgl_port` come from the ESP Component Manager (`main/idf_component.yml`, pinned in `dependencies.lock`).
- **RP2040** — sensor coprocessor. Reads the SEN54 (PM1.0/2.5/4.0/10, humidity, temperature, VOC index) over I2C and streams readings to the ESP32-S3 every ~1 s over a COBS-framed UART link. Built with **PlatformIO** (Arduino core by Earle Philhower) from `rp2040/`. Only needs rebuilding when `rp2040/` changes.

The ESP32-S3 **never** talks to Grove sensors directly — do not add sensor drivers on the ESP32-S3 side.

Key configuration files:

| File | Purpose |
|------|---------|
| `CMakeLists.txt` (root) | ESP-IDF project entry; `IDF_TARGET=esp32s3`, `PROJECT_VER`, adds `components/` |
| `main/CMakeLists.txt` | Collects app sources via `file(GLOB_RECURSE)` over `DIRECTORIES_TO_INCLUDE` |
| `sdkconfig.defaults` | Minimal Kconfig, merged on **every** reconfigure (see gotchas below) |
| `partitions.csv` | Custom partition table; app partition is 7 MB of 8 MB flash |
| `main/idf_component.yml` | Managed component deps (LVGL 9.x, esp_lvgl_port 2.x) |
| `rp2040/platformio.ini` | RP2040 build; SEN5X lib deps, `-DLEGACY_SENSORS` toggle |
| `.clang-format` | Google-based style, 4-space indent, 200 col limit |

## Build and test commands

All tooling goes through the `./dev` shell script at the repo root (Windows: `dev.bat`). It auto-activates ESP-IDF from `$IDF_PATH` if `idf.py` is not on PATH. `./dev` with no args prints help.

### ESP32-S3 (main firmware)

```bash
./dev build            # clean + build (wipes build/ by default)
./dev build --no-clean # incremental build
./dev flash            # auto-detect port and flash (-p PORT, -b BAUD, default 460800)
./dev monitor          # serial monitor (Ctrl-] to exit)
./dev fullclean        # idf.py fullclean
```

Manual `idf.py` works the same way after `. "$IDF_PATH/export.sh"`.

### RP2040 coprocessor

```bash
./dev rp2040 build     # pio run          (alias: ./dev rp build)
./dev rp2040 upload    # autodetects the RP2040 by USB VID:PID — do NOT pin upload_port
./dev rp2040 monitor   # 115200 baud
```

### Tests and verification

Run these after any change before claiming success:

```bash
./dev test                  # fast Python guards + ESP-IDF host unit tests (linux target)
./dev test --no-host        # fast checks only, no ESP-IDF needed
python3 scripts/dev_check.py            # full check including firmware build
python3 scripts/dev_check.py --skip-build  # checks only, no ESP-IDF needed
```

Individual checks:

- `scripts/check_event_post_safety.py` — UI-freeze deadlock guard (event-post discipline)
- `scripts/architecture_scan.py` — domain boundary check (allowlists existing debt; new violations fail)
- `scripts/test_sen5x_mqtt_protocol.py` — MQTT protocol (topics/payload/validation) unit tests
- `scripts/test_ha_switch_protocol.py` — legacy HA switch protocol tests
- `scripts/test_lcd_rgb_config.py`, `scripts/test_ui_geometry.py`, `scripts/test_click_deploy_package.py` — config/UI/packaging guards
- `test/host/` — ESP-IDF `linux`-target unit test running the real `esp_event` loop + POSIX FreeRTOS (no hardware) to prove `ui_event_post()` can never deadlock the UI. Build with `idf.py --preview set-target linux && idf.py build` in `test/host/`, then run `build/host_ui_event_test.elf`. `./dev test` does this for you.

## Architecture

### Boot sequence

Entry point: `main/main.c`

1. `bsp_board_init()`
2. `lv_port_init()`
3. create `view_event_handle` (the ESP event loop)
4. `indicator_view_init()` — nav tileview + all domain view/screen components
5. `indicator_model_init()` — storage, btn, display, rp2040, sensor, cmd, Wi-Fi, MQTT, HA

### Code organization — vertical domain slices

Each domain lives in its own directory under `main/` with a paired `*_model.c` (state, NVS, MQTT, packet parsing — **no LVGL**) and `*_view.c` (LVGL widgets, touch callbacks). Domains: `ha/`, `wifi/`, `sensor/`, `display/`, `rp2040/`, `mqtt/`, `sen5x/`, `storage/`, `cmd/`, `btn/`, `settings/`, `nav/`, `ui/` (UI infrastructure), `util/`, `assets/` (LVGL image/font sources). New domains must be added to `DIRECTORIES_TO_INCLUDE` in `main/CMakeLists.txt` and inited from `indicator_view.c` / `indicator_model.c`.

Model and view **never call each other directly**. All cross-domain communication goes through the event loop:

```c
// post
esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_SENSOR_DATA,
                  &data, sizeof(data), 0);
// subscribe
esp_event_handler_instance_register_with(
    view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_SENSOR_DATA, cb, NULL, NULL);
```

Event IDs live in `main/view_data.h`, payload structs in `main/view_data_types.h` — the manifest comment there lists every event's producer and consumer; update it when adding events.

### UART/COBS packet flow

RP2040 sends typed packets: 1-byte type code + `float` payload. Type codes are defined in both:

- `rp2040/include/indicator_rp2040.hpp` — RP2040 side (C++, canonical)
- `main/rp2040/rp2040.h` — ESP32-S3 side (C, must match)

**Keep these two files in sync whenever adding a new sensor type.** SEN54 codes are `0xC0–0xC6` (PM1.0, PM2.5, PM4.0, PM10, humidity, temperature, VOC index); dynamic registry codes `0xB8/0xB9/0xBA` (`ATTACHED/DETACHED/VALUE`) are still active in `main/rp2040/rp2040.c`.

### MQTT protocol (configurable topics + LWT status)

The device publishes to a broker at `mqtt://seeed-mqtt.lan` (configurable via Settings UI or the `setmqtt` console command; also `haconfig`/`mqtthelp`). Topics follow `<topic_prefix>/<device_name>/<leaf>` with two fixed leaves: `data` (JSON every 5 s: `seq`, `timestamp`, `device`, 8 `sen5x/*` metrics) and `status` (retained `online` on connect; broker LWT publishes retained `offline`). The prefix (default `seeed`) and device name (default MAC-derived `indicator-<mac4>`; also the MQTT client_id — one name everywhere) are NVS-configurable. Data publishes are held back until the clock is NTP-synced; the `online` status is not NTP-gated. Implementation: `main/sen5x/sen5x_mqtt.c` + topic builder `mqtt_topics_build()` in `main/ha/ha_mqtt.c`; full spec in `docs/mqtt-protocol-and-voc-indicator.md`. The legacy three-topic HA switch protocol (`indicator/sensor`, `indicator/switch/set|state`) also still exists. **MQTT topics and payload compatibility are product behavior — do not change them without explicit instruction.**

### VOC alert and warming-up state

`sen5x_mqtt.c` runs a two-state machine (`WARMING_UP` → `ACTIVE`). WARMING_UP lasts 15 min (`SEN5X_WARMING_US`) on **every boot** — the SEN54's VOC Index algorithm is cold-reset by `deviceReset()` on every (re)start, so post-boot VOC readings are unreliable. It is intentionally **not** latched in NVS. During WARMING_UP, `voc_alert` is always 0. Lab VOC thresholds: 0 (≤120), 1 (121–180), 2 (181–250), 3 (>250).

### LVGL thread safety

Any code that touches LVGL widget state outside the LVGL task must hold the semaphore:

```c
lv_port_sem_take();
// lv_obj_* calls
lv_port_sem_give();
```

Background tasks and event handlers must update LVGL through the lock/deferral pattern (`main/ui/ui_defer.c`, `main/ui/ui_event.c`). Model files must not own LVGL objects.

### Navigation

The UI is **single-tile**: the SEN54 dashboard is the only tile (`NAV_TILE_SEN5X`, `NAV_TILE_COUNT 1` in `main/nav/nav.h`). Settings, Wi-Fi, Display and Broker screens are **modals on `lv_layer_top()`** opened from on-screen buttons — there is no horizontal swipe. When adding a modal, follow `main/settings/settings_view.c`. Only add a real swipe tile if the product genuinely needs paged navigation (bump `NAV_TILE_COUNT`, set tile directions in `nav.c`, add the dir to `main/CMakeLists.txt`, init from `indicator_view.c`).

### Key file locations

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
| Sensor data cache/parser | `main/sensor/sensor_model.c` |
| Sensor dashboard UI | `main/sensor/sensor_view.c` |
| MQTT publish logic (topics, payload, status) | `main/sen5x/sen5x_mqtt.c` + `.h` |
| MQTT lifecycle | `main/ha/ha_mqtt.c` |
| MQTT broker defaults | `main/home_assistant_config.h` |
| UI infra (defer, freeze monitor, mem pool) | `main/ui/` |
| COBS codec | `main/util/cobs.c` |
| Dev CLI | `dev`, `scripts/dev.py` |
| Architecture deep-dive | `main/ARCHITECTURE.md` |

## Code style

- C (ESP32-S3, ESP-IDF) and C++ (RP2040, Arduino). Comments and docs are in English (a few design docs under `docs/` are in Chinese).
- `.clang-format`: Google base, 4-space indent, 200-column limit, left pointer alignment, braces on new line after functions only, always-braced `if`.
- Match the surrounding file's naming/comment density; keep refactors incremental — the project must remain buildable after each patch.
- Do not reintroduce legacy compatibility layers; preserve current product behavior unless the task says otherwise.

## Testing strategy

There is no on-device unit test framework. Verification is layered:

1. **Fast static guards** (`architecture_scan.py`, `check_event_post_safety.py`) enforce the model/view boundary and event-post discipline — run on every change.
2. **Pure-Python protocol tests** (`test_sen5x_mqtt_protocol.py`, `test_ha_switch_protocol.py`) validate wire payloads.
3. **Host unit tests** (`test/host/`, ESP-IDF `linux` target) exercise real FreeRTOS/esp_event behavior without hardware.
4. **On-device verification** — `./dev flash` + `./dev monitor` for anything touching hardware, UI, or timing.

`scripts/architecture_scan.py` maintains an explicit allowlist of known debt. Do not add new entries without a written reason.

## Deployment

- Normal flow: `./dev build && ./dev flash` (plus `./dev rp2040 build/upload` when `rp2040/` changed).
- `click_deploy/` is a packaging scaffold for a no-toolchain flashing bundle (ESP32-S3 + RP2040 images for macOS/Linux/Windows). Firmware images and bundled tool binaries are not committed (gitignored). One-command release packaging: `scripts/package_windows_deploy.sh [--build]` — it syncs the firmware via `click_deploy/sync_from_build.sh`, downloads the pinned Windows `esptool.exe` into `click_deploy/tools/`, writes `click_deploy.zip`, then cleans the populated artifacts (`--keep` retains them). The Windows RP2040 script flashes by copying `firmware.uf2` onto the chip's `RPI-RP2` BOOTSEL drive (built-in USB mass-storage driver — do **not** reintroduce picotool on Windows, it needs a Zadig/WinUSB driver there); macOS/Linux still use picotool from PATH. `scripts/test_click_deploy_package.py` guards the package layout and expects the checkout to stay scaffold-only.

## Security considerations

- Wi-Fi credentials, MQTT broker config, and switch state persist in on-device **NVS**, set via the touchscreen or the `setmqtt` serial console command — never hardcode or commit real credentials.
- `main/home_assistant_config.h` holds only compile-time defaults (broker URI etc.); keep it free of real secrets.
- Never commit `$IDF_PATH`-specific paths, generated firmware images, or `.env`-style files.
- The MQTT client supports username/password auth; payloads are plaintext MQTT — the broker is assumed to be on a trusted LAN.

## Build gotchas

- `main/CMakeLists.txt` collects sources via `file(GLOB_RECURSE)`, and the result is cached in `build/`. After adding, moving, or deleting any source file, run `touch main/CMakeLists.txt` before building — otherwise ninja keeps referencing stale paths and fails with `missing and no known rule to make it`.
- Never verify a build through `cmd 2>&1 | tail`: the pipeline exits with `tail`'s status and masks compile failures. Use `set -o pipefail` first (or check `${PIPESTATUS[0]}`).

## Crash & freeze debugging

- Decode watchdog/panic backtraces against the exact elf that was flashed:
  `~/.espressif/tools/xtensa-esp-elf/*/xtensa-esp-elf/bin/xtensa-esp32s3-elf-addr2line -e build/indicator_ha.elf -f -C -a <0x40/0x42 code addrs>`
  The top frames (`esp_crosscore_isr`, `_xt_lowint1`) are the dump mechanism, not the crash site. Two dumps seconds apart stuck in the same function means the task is spinning.
- A freeze with **no** watchdog output means a task is blocked (semaphore/queue/mutex with `portMAX_DELAY`), not spinning — the task WDT cannot catch blocked tasks. This is why `sdkconfig` ships `CONFIG_ESP_TASK_WDT_PANIC=y` (spin → auto-reboot) and `CONFIG_ESP_COREDUMP_ENABLE_TO_UART=y` (panic → every task's stack on serial). Decode saved dumps with `espcoredump.py info_corefile -c <dump> build/indicator_ha.elf`.
- `main/ui/ui_freeze_mon.c` heartbeat-watches taskLVGL (8 s timeout) and aborts on freeze, producing exactly such a coredump plus reboot. After any field UI freeze, capture the full serial log and decode the coredump before theorizing about root causes.

## Serial monitor & flash gotchas

- `idf.py monitor` requires a TTY and fails headless (`Monitor requires standard input to be attached to TTY`); macOS `script -q` also fails (`tcgetattr: Operation not supported on socket`). Working wrapper for background capture: `python3 -c "import pty; pty.spawn(['./dev','monitor'])"` (pty.spawn tolerates a non-TTY stdin).
- Stop any monitor before flashing — the port is exclusive. When multiple USB serial devices are attached (e.g. a DJI mic), port autodetect can pick the wrong one; pass it explicitly: `./dev flash -p /dev/cu.usbserial-XXXX`.

## sdkconfig editing gotchas

- `sdkconfig.defaults` is merged on **every** reconfigure, not just when `sdkconfig` is first created; unknown symbols there are dropped with a warning (visible in `idf.py reconfigure` output). Confirmed dead in LVGL 9.5: `LV_MEM_CUSTOM`, `LV_COLOR_SCREEN_TRANSP`, `LV_SPRINTF_USE_FLOAT`.
- Hand-edits to `sdkconfig` in a non-canonical position can be silently dropped on the next reconfigure. Reliable workflow: put the symbol in `sdkconfig.defaults` (or at its canonical menu position in `sdkconfig`), run `idf.py reconfigure`, then verify it landed in `build/config/sdkconfig.h` before building.
- Do not remove from `sdkconfig.defaults`: `CONFIG_LV_MEM_POOL_EXPAND_SIZE_KILOBYTES=256` (load-bearing for the PSRAM overflow pool below), PSRAM 120 MHz OCT mode, CPU 240 MHz, flash QIO 120 MHz. `CONFIG_LV_MEM_CUSTOM=y` is a kept-for-history no-op under LVGL 9.5.

## LVGL rendering diagnosis (9.5)

- Layer-alloc freeze class: taskLVGL spins in `lv_malloc_core` / `lv_draw_layer_alloc_buf`. Confirm by enabling `CONFIG_LV_USE_LOG=y` + `CONFIG_LV_LOG_LEVEL_INFO=y` + `CONFIG_LV_LOG_PRINTF=y` and looking for the repeating triplet `Allocating layer buffer failed. Try later` / `couldn't allocate memory (N bytes)` / `No memory: WxH, cf: F, stride: S, BByte`. The `WxH` and `cf` (16 = ARGB8888) identify the layer; `Layer memory used: X kB` lines show successful ones. Turn these off again afterwards — INFO level is noisy.
- Known trigger here: the default theme sets `clip_corner=true` on `lv_list` backgrounds (also msgbox/win), which makes LVGL render the top/bottom rounded strips (`width x radius`, ARGB8888, ~12 KB for 420-wide) through offscreen layers on every scroll frame. If the rounded corner is invisible (container bg matches the page bg), kill it: `lv_obj_set_style_clip_corner(obj, false, LV_PART_MAIN)`. Reference: `main/wifi/wifi_list_screen.c`.
- LVGL's builtin TLSF pool is 64 KB internal RAM (`CONFIG_LV_MEM_SIZE_KILOBYTES=64`) and a layer allocation that can never succeed is retried forever = frozen UI. `main/ui/ui_mem_pool.c` adds a 256 KB PSRAM overflow pool via `lv_mem_add_pool()`; two non-obvious requirements: the pool base must be 8-byte aligned (`heap_caps_aligned_alloc(16, ...)` — plain `heap_caps_malloc` only guarantees 4), and any pool is capped at `LV_MEM_SIZE + LV_MEM_POOL_EXPAND_SIZE`, hence `CONFIG_LV_MEM_POOL_EXPAND_SIZE_KILOBYTES=256` in sdkconfig.defaults.
- UI jank measurement: enable `CONFIG_LV_USE_SYSMON=y` + `CONFIG_LV_USE_PERF_MONITOR=y` + `CONFIG_LV_USE_PERF_MONITOR_LOG_MODE=y` to get periodic `sysmon: X FPS ... refr Yms (render Zms | flush Wms), CPU N%` lines on serial — no need to read the on-screen overlay. Reference numbers (480×480 RGB, full refresh): idle render ~0 ms @93 FPS; an on-screen-keyboard keystroke costs ~130 ms render (brief 6-7 FPS dips) — inherent to SW rendering, not a bug.

## UI conventions

- On-screen keyboard must never cover the field being edited: shrink the scrollable form so its bottom edge sits above the keyboard and `lv_obj_scroll_to_view_recursive()` the focused field; restore full height when the keyboard hides. Reference implementation: `_set_keyboard_visible()` in `main/ha/ha_config.c`. Reuse this pattern for any new form with text input.
- SquareLine Studio-generated files in `main/assets/` are treated as assets. Custom logic goes in `*_view.c` files, not in generated screens.

## Legacy sensor code

Old sensors (SCD41/SGP40/SHT41) are preserved behind guards:

- `#ifdef LEGACY_SENSORS` — RP2040 side (`indicator_rp2040.hpp`, `sensors.cpp`, `sensor_model.c`) and `rp2040.h`
- `#ifdef LEGACY_HA_SENSORS` — ESP32-S3 side (`ha_sensor.c`)

Do not define these macros in normal builds. To re-enable legacy hardware, add `-DLEGACY_SENSORS` / `-DLEGACY_HA_SENSORS` to the build.

## Do not touch

| Path | Reason |
|------|--------|
| `main/lv_port.c` | BSP hardware boundary — display/touch port |
| `components/bsp/` | Hardware driver layer |
| `managed_components/` | Managed by ESP Component Manager, never edit manually |
