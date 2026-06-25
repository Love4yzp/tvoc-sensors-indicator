# Sensor Domain

The Sensor domain is a vertical slice for built-in sensor data received from the
RP2040 side of the SenseCAP Indicator.

## Files

| File | Responsibility |
| --- | --- |
| `sensor.h` | Umbrella header for the Sensor domain. Its include guard is `SENSOR_H` for compatibility with existing feature checks. |
| `sensor_model.h` | Sensor types, status/error definitions, RP2040 parser API, and current-value cache API. |
| `sensor_model.c` | Parses RP2040 sensor packets, maintains the latest `SensorData` cache, and posts `VIEW_EVENT_SENSOR_DATA`. |
| `sensor_view.h` | Sensor view initialization API. |
| `sensor_view.c` | Subscribes to `VIEW_EVENT_SENSOR_DATA` and updates LVGL labels. |

## Init Sequence

`main/main.c` calls `indicator_view_init()` before `indicator_model_init()`.
The Sensor view subscribes to display events during view initialization, and the
Sensor model initializes the cache and mutex during model initialization.

## Data Flow

RP2040 UART data is dispatched to `_sensor_data_parse_handle()`.
The parser maps packet types to sensor IDs and calls `update_sensor_data()`.
The model updates the cached value and posts `VIEW_EVENT_SENSOR_DATA`.
`sensor_view.c` handles that event and updates the matching LVGL labels while
holding the LVGL port semaphore.

## UI Containment

Only `sensor_view.c` may own LVGL label references. Model files must remain
UI-free.
