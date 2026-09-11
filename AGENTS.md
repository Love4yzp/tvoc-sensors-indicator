# Developer Guide

Firmware for the **Seeed Studio SenseCAP Indicator** (project `indicator_ha`), an environmental monitoring panel centered on the Sensirion SEN54 air-quality sensor.

---

## 1. System Topology

```
┌────────────────────────────────────────────────────────────────────────┐
│                               ESP32-S3                                 │
│  (480x480 RGB Display + Touch, Wi-Fi, MQTT, NVS, Business Logic)       │
│                                                                        │
│   ┌────────────────────────┐  view_event  ┌────────────────────────┐   │
│   │     Model Layer        │◄───handle───►│       View Layer       │   │
│   │  (State, NVS, MQTT)    │ (non-block)  │  (LVGL 9, Modal Views) │   │
│   └───────────┬────────────┘              └────────────────────────┘   │
│               │ (COBS UART link, 1 Hz)                                 │
└───────────────┼────────────────────────────────────────────────────────┘
                │
┌───────────────┼────────────────────────────────────────────────────────┐
│               ▼                RP2040                                  │
│  (Sensor Coprocessor: Earle Philhower Arduino Core, PlatformIO)        │
│                                                                        │
│   ┌────────────────────────┐              ┌────────────────────────┐   │
│   │   Sensirion SEN54      │──I2C Bus────►│   Legacy Sensor Stubs  │   │
│   │ (PM1/2.5/4/10, T, RH,  │              │    (behind build guard)│   │
│   │  VOC Index via I2C)    │              │                        │   │
│   └────────────────────────┘              └────────────────────────┘   │
└────────────────────────────────────────────────────────────────────────┘
```

- **ESP32-S3**: ESP-IDF v5.5.x, LVGL 9.5 (via ESP Component Manager), FreeRTOS.
- **RP2040**: PlatformIO in `rp2040/`. Streams SEN54 readings over UART.

---

## 2. Tooling & Core Commands

```bash
# Environment setup (ESP-IDF v5.5.x)
alias get_idf='source ~/esp/esp-idf-v5.5.4/export.sh'  # or export IDF_PATH=...

# Common Workflows
./dev build            # clean + build (wipes build/ by default)
./dev build --no-clean # incremental build
./dev flash            # auto-detects port & flashes firmware
./dev monitor          # serial monitor (Ctrl-] to exit)
./dev test --no-host   # fast static guards & protocol tests (run after any change)
./dev test             # full test suite (including host Linux unit tests)
./dev rp2040 build     # coprocessor build (PlatformIO)
```

---

## 3. Core Design Constraints

1. **Hardware Boundary**: ESP32-S3 never talks to Grove sensors directly.
   - **Do not touch**: `main/lv_port.c`, `components/bsp/`, and `managed_components/`.
2. **Vertical Slices & Event Bus**: Model and View never call each other directly. All cross-domain communication goes through `view_event_handle`. Model files never own LVGL objects.
3. **UI Threading**: Never perform a blocking post (`portMAX_DELAY`) to `view_event_handle` from UI/view code (causes immediate freeze). View code must use `ui_event_post()`.
4. **Navigation**: Single-tile dashboard (`NAV_TILE_SEN5X`). All settings/forms are modals on `lv_layer_top()`.
5. **MQTT & Warm-up**: Data topics (`<prefix>/<device>/data`) publish 8 metrics every 5 s (NTP-gated). First 15 minutes after boot is locked to `WARMING_UP` (`voc_alert=0`).

---

## 4. High-Frequency Gotchas

- **CMake GLOB Cache**: When adding, moving, or deleting source files in `main/`, run `touch main/CMakeLists.txt` before building.
- **`sdkconfig.defaults`**: Kconfig re-merges defaults on every build; manual edits to `sdkconfig` can be overwritten. Put permanent config in `sdkconfig.defaults`.
- **Serial Port Collision**: Stop any running `./dev monitor` before flashing.

---

## 5. Domain Documentation Index

| Topic | Primary Document |
|---|---|
| Deep Architecture & Boot Sequence & Crash Decoding | [`main/ARCHITECTURE.md`](main/ARCHITECTURE.md) |
| UI Recipes (Adding Modals, Assets, Common Build Errors) | [`main/AGENTS.md`](main/AGENTS.md) |
| UI Threading Model, `ui_defer`, & LVGL 9.5 Memory/Rendering | [`main/ui/README.md`](main/ui/README.md) |
| SEN54 MQTT Topic Specs, Payload, & VOC State Machine | [`main/sen5x/README.md`](main/sen5x/README.md) |
| Sensor Cache & RP2040 Ingress | [`main/sensor/README.md`](main/sensor/README.md) |
| Wi-Fi State Machine & Modals | [`main/wifi/README.md`](main/wifi/README.md) |
| Display Backlight & Sleep Timer | [`main/display/README.md`](main/display/README.md) |
| RP2040 PlatformIO Build | [`rp2040/README.md`](rp2040/README.md) |
| Standalone Packaging Scaffold (`click_deploy`) | [`click_deploy/AGENTS.md`](click_deploy/AGENTS.md) |
