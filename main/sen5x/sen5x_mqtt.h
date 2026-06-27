#ifndef SEN5X_MQTT_H
#define SEN5X_MQTT_H

#include "mqtt_client.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Sparkplug B topic components (hardcoded; TODO: make configurable via NVS) */
#define SEN5X_SPBV_GROUP    "seeed"
#define SEN5X_EDGE_NODE_ID  "edge-01"
#define SEN5X_DEVICE_ID     "sen5x"

#define SEN5X_TOPIC_NBIRTH  "spBv1.0/" SEN5X_SPBV_GROUP "/NBIRTH/" SEN5X_EDGE_NODE_ID
#define SEN5X_TOPIC_NDEATH  "spBv1.0/" SEN5X_SPBV_GROUP "/NDEATH/" SEN5X_EDGE_NODE_ID
#define SEN5X_TOPIC_DBIRTH  "spBv1.0/" SEN5X_SPBV_GROUP "/DBIRTH/" SEN5X_EDGE_NODE_ID "/" SEN5X_DEVICE_ID
#define SEN5X_TOPIC_DDATA   "spBv1.0/" SEN5X_SPBV_GROUP "/DDATA/"  SEN5X_EDGE_NODE_ID "/" SEN5X_DEVICE_ID

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

/* DDATA publish interval */
#define SEN5X_DDATA_INTERVAL_US  (5ULL * 1000000ULL)  /* 5 seconds */

void sen5x_mqtt_init(void);
void sen5x_mqtt_on_connect(esp_mqtt_client_handle_t client);
void sen5x_mqtt_on_disconnect(void);

#ifdef __cplusplus
}
#endif

#endif /* SEN5X_MQTT_H */
