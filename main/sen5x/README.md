# SEN5x Domain — SEN54 Air-Quality & MQTT Integration

This domain owns the MQTT publishing pipeline, wire payload formatting, and VOC alert state machine for the Sensirion SEN54 sensor.

---

## 1. MQTT Topics & Wire Format

Topics follow `<topic_prefix>/<device_name>/<leaf>`:

- **Topic Prefix**: Default `seeed` (configurable via NVS / Settings UI / `setmqtt -t`).
- **Device Name**: Default `indicator-<mac4>` (e.g. `indicator-3f2a`, configurable via NVS / `setmqtt -n`). Also serves as MQTT `client_id`.

### Published Topics

| Topic Leaf | Rate / Timing | QoS / Retain | Content |
|---|---|---|---|
| `status` | On connect / disconnect | QoS 1, Retained | `online` on connect; broker LWT publishes `offline` |
| `data` | Every 5 s (NTP-gated) | QoS 0, Not retained | JSON payload with 8 metrics + metadata |

### Data Payload Format

```json
{
  "seq": 42,
  "timestamp": 1773000000,
  "device": "indicator-3f2a",
  "sen5x/pm1_0": 12.3,
  "sen5x/pm2_5": 18.5,
  "sen5x/pm4_0": 21.0,
  "sen5x/pm10": 25.1,
  "sen5x/humidity": 55.4,
  "sen5x/temperature": 23.8,
  "sen5x/voc_index": 115,
  "sen5x/voc_alert": 0
}
```

- `seq`: Monotonic counter (0–255, wrapping).
- `timestamp`: UTC epoch seconds (publishes are held back until clock is NTP-synced).
- Floating-point metrics are rounded to sensor resolution (PM 1 dp, Temp/Hum 2 dp) to eliminate float32 noise.
- Protocol wire format is strictly pinned by `scripts/test_sen5x_mqtt_protocol.py`.

---

## 2. VOC Alert State Machine & 15-Minute Warm-up

`sen5x_mqtt.c` maintains a two-state machine:

```
[Boot / Power-on]
       │
       ▼
 ┌───────────┐   15 minutes elapsed   ┌──────────┐
 │WARMING_UP │───────────────────────►│  ACTIVE  │
 │(Alert = 0)│                        │(Alert 0-3│
 └───────────┘                        └──────────┘
```

- **Why 15 minutes on EVERY boot?**
  The SEN54 internal Sensirion Gas Index Algorithm is cold-reset via `deviceReset()` whenever the RP2040 or ESP32-S3 reboots. Post-boot VOC Index values take up to 15 minutes to stabilize. This state is intentionally **not** latched in NVS.
- **Alert Levels (Active State)**:
  - `0` (Normal): VOC Index ≤ 120
  - `1` (Light): VOC Index 121–180
  - `2` (Moderate): VOC Index 181–250
  - `3` (Severe): VOC Index > 250
