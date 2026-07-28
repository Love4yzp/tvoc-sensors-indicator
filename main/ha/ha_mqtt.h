#ifndef HA_MQTT_H
#define HA_MQTT_H

#include <stddef.h>

#include "esp_event.h"
#include "mqtt.h"
#include "ha_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Room for "<prefix 63>/<name 31>/<leaf 6>" + NUL with margin. */
#define MQTT_TOPIC_MAX_LEN 128

/* Build the device's two protocol topics from the NVS config:
 *   data:   "<topic_prefix>/<device_name>/data"
 *   status: "<topic_prefix>/<device_name>/status"  (LWT + retained online) */
void mqtt_topics_build(const ha_cfg_interface *cfg,
                       char *data, size_t data_sz,
                       char *status, size_t status_sz);

ESP_EVENT_DECLARE_BASE(HA_CFG_EVENT_BASE);
extern esp_event_loop_handle_t ha_cfg_event_handle;
extern instance_mqtt           mqtt_ha_instance;

enum HA_CFG_EVENT {
    HA_CFG_SET,
    HA_CFG_BROKER_CHANGED,
    HA_CFG_EVENT_ALL,
};

int indicator_ha_model_init(void);
int indicator_ha_view_init(void);

#ifdef __cplusplus
}
#endif

#endif /* HA_MQTT_H */
