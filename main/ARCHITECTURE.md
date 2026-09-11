# Firmware Architecture

SenseCAP Indicator firmware for ESP32-S3 + RP2040. ESP-IDF v5.5, FreeRTOS, LVGL 9 via ESP Component Manager, esp-mqtt.

---

## Directory Map

```
main/
  main.c                  Entry point. Creates view_event_handle, then calls view init and model init.
  indicator_model.c       Model init orchestrator for storage, button, display, RP2040, sensor, command, Wi-Fi, MQTT, and HA.
  indicator_view.c        View init orchestrator. Creates nav tileview, then domain views/components.
  indicator_enabler.h     Aggregates all module headers; define-guards drive conditional init.
  view_data.h             Shared event bus contract and VIEW_EVENT_* definitions.
  view_data_types.h       Pure data types used by the event contract.
  lv_port.c               LVGL display/touch port. Owns lv_port_sem_take/give for LVGL thread safety.

  nav/                    lv_tileview navigation. Single-tile today: the SEN54 dashboard is the only tile; settings-style screens are modals on lv_layer_top().
  assets/                 LVGL 9 image/font descriptors used by handwritten screen components.

  sen5x/                  SEN5x domain: MQTT publish logic (topics, payload, LWT status, VOC alert state machine).
  ha/                     Home Assistant domain: broker config, MQTT lifecycle, sensors, legacy switch protocol, screen widgets.
  wifi/                   Wi-Fi domain: scanning, connection state, list/connect modals, status icon.
  sensor/                 SEN54 data cache/parser (from RP2040 packets) and the sensor dashboard view.
  display/                LCD backlight, sleep mode, and display settings view.
  settings/               Settings entry modal (gear button).
  rp2040/                 UART/COBS ingress from the RP2040 co-processor.
  btn/                    Physical button handling.
  mqtt/                   Shared MQTT client lifecycle controller.
  storage/                NVS helpers.
  cmd/                    Serial command interface.
  ui/                     UI infrastructure: non-blocking event posts (ui_event), LVGL-task deferral (ui_defer), freeze monitor (ui_freeze_mon), PSRAM memory pool (ui_mem_pool).

  util/
    cobs.*                COBS encode/decode for RP2040 UART framing.
    indicator_util.*      IP address helpers.
```

---

## Boot Sequence

```
app_main()
  1. bsp_board_init()
  2. lv_port_init()
  3. esp_event_loop_create(&view_event_handle)
  4. indicator_view_init()
       nav_init()
       → indicator_display_view_init
       → view_sensor_init
       → indicator_wifi_view_init
       → indicator_ha_view_init
  5. indicator_model_init()
       indicator_nvs_init → indicator_btn_init → indicator_display_init
       → esp32_rp2040_init → indicator_sensor_init
       → indicator_cmd_init
       → indicator_wifi_model_init → indicator_mqtt_init → indicator_ha_model_init
```

View initialization runs before model initialization. Screen components must tolerate initial empty state and update when model events arrive.

---

## Event Loops

| Handle | Base | Created in | Purpose |
|--------|------|------------|---------|
| `view_event_handle` | `VIEW_EVENT_BASE` | `main.c` | Main UI/data bus for cross-domain data flow |
| `mqtt_app_event_handle` | `MQTT_APP_EVENT_BASE` | `mqtt/mqtt.c` | MQTT client lifecycle commands |
| `ha_cfg_event_handle` | `HA_CFG_EVENT_BASE` | `ha/ha_mqtt.c` | HA broker config changes |
| `cmd_cfg_event_handle` | `CMD_CFG_EVENT_BASE` | `cmd/cmd.c` | Serial command events |
| default event loop | `WIFI_EVENT`, `IP_EVENT` | `wifi/wifi_model.c` | ESP-IDF Wi-Fi driver events |

---

## Architectural Patterns

- **Vertical Domain Slices**: Each domain owns its model, view, and screen components behind a small public header (`ha.h`, `wifi.h`, `sensor.h`, etc.).
- **Boundary Rule**: Cross-domain communication passes strictly through `view_event_handle`. Model files never own LVGL objects.
- **Screen Ownership**:
  - `create()` or `*_init()` builds widgets under a passed parent or `nav_get_tile()`.
  - `update()` applies state; callers hold the LVGL lock unless documented otherwise.
  - `destroy()` is only needed for dynamic modal lifetime.

---

## Crash & Freeze Debugging

- **Watchdog / Panic Backtrace Decoding**:
  ```bash
  ~/.espressif/tools/xtensa-esp-elf/*/xtensa-esp-elf/bin/xtensa-esp32s3-elf-addr2line \
    -e build/indicator_ha.elf -f -C -a <0x40/0x42 addresses>
  ```
  *(Top frames `esp_crosscore_isr` / `_xt_lowint1` are dump mechanisms, not crash sites).*
- **Blocked Freezes vs Spinning**:
  - A freeze with **no** serial watchdog output means a task is blocked on a mutex/queue with `portMAX_DELAY` (Task WDT cannot catch blocked tasks).
  - `CONFIG_ESP_TASK_WDT_PANIC=y` and `CONFIG_ESP_COREDUMP_ENABLE_TO_UART=y` ensure spinning loops dump stack to UART.
- **Coredump Analysis**:
  ```bash
  espcoredump.py info_corefile -c <saved_dump_file> build/indicator_ha.elf
  ```
- **UI Freeze Monitor**: `main/ui/ui_freeze_mon.c` heartbeat-watches `taskLVGL` (8 s timeout) and aborts on deadlock to produce a full serial dump and auto-reboot.

---

## Development Guidelines

Before modifying an event, inspect `main/view_data_types.h` manifest comments for all producers and consumers.

Blast radius reference:

| File/area | Blast radius |
|-----------|-------------|
| `view_data.h`, `view_data_types.h` | Cross-domain event contract |
| `indicator_model.c`, `indicator_view.c` | Boot/init ordering |
| `lv_port.c`, `components/bsp/` | Display/touch hardware behavior |
| `nav/nav.c` | Main page container ownership |
| `main/assets/` | Shared image/font descriptors |

Verification commands:

```bash
./dev test --no-host                          # fast python checks & protocol unit tests
./dev test                                    # full host unit test suite
./dev build                                   # full firmware build
```
