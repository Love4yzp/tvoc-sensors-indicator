# WiFi Domain

Vertical slice for all WiFi functionality. Mirrors the pattern established by `main/ha/`.

## File Responsibilities

| File | Owns | LVGL |
|---|---|---|
| `wifi_model.c` | ESP WiFi event handling, reconnect state machine, ping network check, 5 s monitor task | ✗ |
| `wifi_list_screen.c` | AP list widget lifecycle, item creation, show/hide spinner | ✓ |
| `wifi_connect_screen.c` | Connect dialog and AP-details dialog (modal overlays) | ✓ |
| `wifi_view.c` | Event subscriptions, status icon updates, screen navigation, component coordination | ✓ |
| `wifi.h` | Umbrella header (`wifi_model.h` + `wifi_view.h`) | — |

## Init Sequence

```
indicator_wifi_view_init()    ← called from indicator_view_init()
  → creates Wi-Fi status/list/connect widgets on nav/modal parents
  → registers VIEW_EVENT handlers for WIFI_ST / WIFI_LIST / SCREEN_START / CONNECT_RET / etc.

indicator_wifi_model_init()   ← called later from indicator_model_init()
  → ESP netif + WiFi stack init
  → starts _indicator_wifi_task (ping / reconnect loop)
  → registers VIEW_EVENT handlers for WIFI_LIST_REQ / WIFI_CONNECT / CFG_DELETE / SHUTDOWN
  → posts VIEW_EVENT_SCREEN_START if no saved SSID
```

## Network State Flags (`struct view_data_wifi_st`)

Three flags with distinct meanings — do not conflate them:

- `is_connected`: associated to the AP (no IP yet guaranteed).
- `has_ip`: `IP_EVENT_STA_GOT_IP` received — the LAN is usable. UI/status
  semantics only; it deliberately does NOT gate the MQTT client (esp-mqtt
  auto-reconnect owns reconnection, so the client also retries on isolated
  LANs with no internet).
- `is_network`: internet reachable (periodic ping to hardcoded `1.1.1.1`).
  UI/status semantics only; on isolated LANs it stays false forever and must
  never gate local services.

SNTP starts on first GOT_IP with the configured NTP server (NVS `ntp_server`,
default `pool.ntp.org`; Settings → MQTT screen or `setmqtt -s`). On config
change, `_mqtt_ha_start()` (ha_mqtt.c) re-applies the server to the running
SNTP on every MQTT client (re)start.

## Event Flow

```
User taps AP button
  → _on_unconnected_tap() [wifi_view.c]
  → wifi_connect_screen_show(ssid, have_password) [wifi_connect_screen.c]

User taps Join
  → _on_join() [wifi_connect_screen.c]
  → VIEW_EVENT_WIFI_CONNECT posted
  → wifi_connect_screen auto-dismisses

VIEW_EVENT_WIFI_CONNECT
  → wifi_model: starts connection
  → wifi_view: shows spinner (wifi_list_screen_show_spinner)

WIFI_EVENT_STA_CONNECTED (ESP WiFi)
  → wifi_model: posts VIEW_EVENT_WIFI_ST + VIEW_EVENT_WIFI_CONNECT_RET

VIEW_EVENT_WIFI_CONNECT_RET
  → wifi_view: posts VIEW_EVENT_WIFI_LIST_REQ, shows toast

VIEW_EVENT_WIFI_LIST (scan result)
  → wifi_view: wifi_list_screen_update()
```

## Reconnect State Machine

Single mechanism, split across the WiFi event handler (fast path) and
`_indicator_wifi_task` (slow path). There is no second driver-restart layer
and `wifi_sta_config_t.failure_retry_cnt` is intentionally unused: the
"retrying" state must be broadcast to the UI, and `failure_retry_cnt`
suppresses the intermediate DISCONNECTED events that make that possible
(the UI would keep showing a stale "connected").

```
WIFI_EVENT_STA_DISCONNECTED (wifi_model.c)
  ├─ _g_shutting_down or !is_cfg → state=disconnected, broadcast, no retry
  ├─ retry_num < retry_max (3)   → retry_num++, state={connected=false,
  │                                 network=false, connecting=true},
  │                                 broadcast VIEW_EVENT_WIFI_ST, esp_wifi_connect()
  └─ budget spent                → state=disconnected, broadcast,
                                   VIEW_EVENT_WIFI_CONNECT_RET(failure)

_indicator_wifi_task (5 s tick)
  └─ is_cfg && !connected && !connecting for > 5 ticks (~30 s)
       → re-arm retry_num=0, state=connecting, broadcast, esp_wifi_connect()
```

- Every state transition updates `_g_wifi_model.st` AND broadcasts
  `VIEW_EVENT_WIFI_ST` — no stale "connected" while retrying.
- `is_cfg` / `retry_num` / `retry_max` / `idle_ticks` all live in
  `_g_wifi_model` under `_g_data_mutex` (same critical section as `st`).
- `_g_shutting_down` (atomic, one-way) is latched by `_wifi_shutdown()` so
  the DISCONNECTED from the shutdown `esp_wifi_stop()` is not retried; the
  blocking `esp_wifi_stop()` itself runs on `_wifi_cmd_task`
  (`WIFI_CMD_SHUTDOWN`), never on the view_event loop.
- `IP_EVENT_STA_GOT_IP` re-arms the burst budget (`retry_num = idle_ticks = 0`).

## LVGL Thread Safety

All LVGL widget mutations in `wifi_view.c` event handlers are wrapped with
`lv_port_sem_take()` / `lv_port_sem_give()`.

Screen component functions (`wifi_list_screen_*`, `wifi_connect_screen_*`)
are always called from within an already-held semaphore in `wifi_view.c`.

## LVGL Containment Rule

Only view/screen files in this domain may own LVGL widgets. `wifi_model.c`
must remain UI-free. Screen component files use `LV_IMAGE_DECLARE` /
`LV_FONT_DECLARE` for the specific assets they need and accept parent widget
pointers as parameters.
