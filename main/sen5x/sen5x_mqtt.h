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

/* Warming-up suppression: 60 minutes in microseconds (esp_timer unit) */
#define SEN5X_WARMING_US  (60ULL * 60ULL * 1000000ULL)

/* DDATA publish interval */
#define SEN5X_DDATA_INTERVAL_US  (5ULL * 1000000ULL)  /* 5 seconds */

/* NVS */
#define SEN5X_NVS_NS        "sen5x"
#define SEN5X_NVS_WARM_KEY  "warm_done"

void sen5x_mqtt_init(void);
void sen5x_mqtt_on_connect(esp_mqtt_client_handle_t client);
void sen5x_mqtt_on_disconnect(void);

#ifdef __cplusplus
}
#endif

#endif /* SEN5X_MQTT_H */
