# HA Domain — Vertical Slice Architecture

Home Assistant integration. This domain is the pilot for the project's target architecture:
each feature slice owns its full stack (data model + MQTT protocol + UI component).

---

## Files and Responsibilities

| File | Lines | Owns |
|------|-------|------|
| `ha.h` | 17 | Public API for the entire domain. Include this, not individual slice headers. |
| `ha_mqtt.c` | ~200 | MQTT client lifecycle: start/restart. Owns `mqtt_ha_instance` and `ha_mqtt_request_restart()`. Reconnection is delegated to esp-mqtt auto-reconnect. |
| `ha_config.c` | ~860 | Broker NVS config (`ha_cfg_get`/`ha_cfg_set`) + broker config modal view. |
| `ha_sensor.c` | ~170 | Sensor entity metadata, subscribes to sensor MQTT topics, publishes built-in sensor readings, routes incoming sensor JSON to `VIEW_EVENT_HA_SENSOR`. |
| `ha_switch.c` | ~190 | Switch entity metadata, NVS state persistence, MQTT publish/subscribe, posts `VIEW_EVENT_HA_SWITCH_SET`. Holds `ha_switch_screen_t *` — does NOT touch LVGL directly. |
| `ha_switch_screen.c` | ~530 | Builds HA switch widgets on the nav tile. Owns widget handles for all 8 switch widgets. Dispatch table maps index → widget + updater function. |
| `ha_switch_screen.h` | 18 | `create` / `update` / `destroy` interface. |

---

## Init Sequence

```
indicator_ha_view_init()    (called from indicator_view.c)
  ha_config_view_init()     register VIEW_EVENT_MQTT_ADDR_CHANGED + VIEW_EVENT_HA_ADDR_DISPLAY
  ha_switch_screen_create() builds switch widgets on the dashboard tile
                            (NAV_TILE_HA_CTRL / NAV_TILE_HA_MIX are legacy aliases for NAV_TILE_SEN5X — the UI is single-tile now)
  ha_switch_attach_screen() gives ha_switch.c the screen handle

indicator_ha_model_init()   (called later from indicator_model.c)
  ha_sensor_init()          register VIEW_EVENT_SENSOR_DATA handler
  ha_switch_init()          init entities, restore NVS state,
                            register VIEW_EVENT_HA_SWITCH_ST + VIEW_EVENT_HA_SWITCH_SET handlers
  init mqtt_ha_instance
  post initial MQTT_APP_START to mqtt_app_event_handle (client is created
                            on the mqtt_event_task; no WiFi gating —
                            esp-mqtt auto-reconnect handles a down link)
```

---

## Event Flows

```
User toggles switch widget
  → ha_switch_screen.c: LV_EVENT_VALUE_CHANGED
  → post VIEW_EVENT_HA_SWITCH_ST {index, value}
  → ha_switch.c: publish to MQTT topic_state, save NVS

MQTT broker sends switch command
  → ha_mqtt.c: MQTT_EVENT_DATA
  → ha_switch_on_mqtt_data()
  → post VIEW_EVENT_HA_SWITCH_SET {index, value}
  → ha_switch.c: ha_switch_screen_update() [LVGL lock]

Boot
  → indicator_ha_model_init(): post MQTT_APP_START to mqtt_app_event_handle
  → mqtt/mqtt.c: calls mqtt_ha_instance.mqtt_starter()
  → ha_mqtt.c: _mqtt_ha_start() reads NVS config, creates and starts the MQTT
    client (even with the link down — see Reconnection below)

MQTT connects
  → MQTT_EVENT_CONNECTED
  → sen5x_mqtt_on_connect() — publishes retained "online", starts 5 s data timer
  → ha_sensor_subscribe() — subscribe to sensor topics (LEGACY_HA)
  → ha_switch_subscribe() — subscribe to switch topics (LEGACY_HA)

Reconnection (single mechanism: esp-mqtt built-in auto-reconnect)
  → ha_mqtt.c configures .network.reconnect_timeout_ms = 10000 explicitly
  → link loss or broker unreachable: esp-mqtt fires MQTT_EVENT_DISCONNECTED
    (sen5x drops s_client, stops the 5 s timer), then retries with backoff
    until the broker is reachable — no WiFi-event-driven restart anywhere
  → reconnect: MQTT_EVENT_CONNECTED → retained "online" + timer restart
  → LWT retained "offline" still covers an ungraceful drop

User changes broker config (modal confirm or console `setmqtt`)
  → ha_config.c / cmd.c: validate, save NVS (ha_cfg_set)
  → ha_mqtt_request_restart()        direct call, non-blocking post of
                                     MQTT_APP_RESTART to mqtt_app_event_handle
  → mqtt/mqtt.c: _app_event_handler → mqtt_ha_instance.mqtt_starter()
  → ha_mqtt.c: _mqtt_ha_start() — sen5x_mqtt_on_disconnect() FIRST (esp-mqtt's
    stop path never fires MQTT_EVENT_DISCONNECTED, so sen5x must drop its stale
    s_client and stop the 5 s publish timer here), then stop/destroy the old
    client, rebuild it from the NVS config and start it
```

---

## Known Issues

- `VIEW_EVENT_HA_SENSOR` is posted by `ha_sensor.c` but **has no consumer**. The HA sensor data display screen is not yet wired up. See the manifest comment in `view_data.h`.

---

## Adding a New Switch Widget Type

1. Add a new `_update_xxx()` function in `ha_switch_screen.c`
2. Add a slot entry in `ha_switch_screen_create()` pointing to the locally-created widget and updater
3. Extend `CONFIG_HA_SWITCH_ENTITY_NUM` in `home_assistant_config.h` if adding a new entity

## Extending to a New Feature Slice

Follow this pattern:
1. `ha_yyy.c` — data + MQTT logic, registers event handlers in `ha_yyy_init()`
2. `ha_yyy_screen.c` — LVGL component, `create`/`update`/`destroy` interface
3. Declare cross-module functions in `ha.h`
4. Call `ha_yyy_init()` from `indicator_ha_model_init()` in `ha_mqtt.c`
5. Call `ha_yyy_screen_create()` + attach from `indicator_ha_view_init()` in `ha_mqtt.c`
