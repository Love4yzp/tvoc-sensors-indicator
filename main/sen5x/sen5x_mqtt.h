#ifndef SEN5X_MQTT_H
#define SEN5X_MQTT_H

#include "mqtt_client.h"

#ifdef __cplusplus
extern "C" {
#endif

/* VOC alert thresholds — lab-specific (normal office: 150/250) */
#define SEN5X_VOC_THR_LIGHT     120.0f
#define SEN5X_VOC_THR_MODERATE  180.0f
#define SEN5X_VOC_THR_SEVERE    250.0f

/* Warming-up suppression window, applied on EVERY boot.
 *
 * The SEN54's VOC Index algorithm runs inside the sensor and is reset to a cold
 * baseline by deviceReset() on every RP2040/SEN54 (re)start — which happens on
 * every power cycle AND on every ESP32 reboot (the ESP32 sends CMD_POWER_ON,
 * re-running sensor_sen54_init() → deviceReset()). So the cold-start VOC garbage
 * is identical after any boot, not just the first one. We therefore re-run this
 * window on every boot rather than latching "done" in NVS. 15 min covers the
 * worst initial drift; full convergence takes ~12 h regardless, so a longer wait
 * buys little. */
#define SEN5X_WARMING_US  (15ULL * 60ULL * 1000000ULL)  /* 15 minutes */

/* Data publish interval */
#define SEN5X_DATA_INTERVAL_US  (5ULL * 1000000ULL)  /* 5 seconds */

void sen5x_mqtt_init(void);
void sen5x_mqtt_on_connect(esp_mqtt_client_handle_t client);
void sen5x_mqtt_on_disconnect(void);

#ifdef __cplusplus
}
#endif

#endif /* SEN5X_MQTT_H */
