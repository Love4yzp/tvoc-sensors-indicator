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

extern instance_mqtt mqtt_ha_instance;

/* Request an MQTT client restart through the mqtt_app loop (e.g. after the
 * broker config changed). Safe to call from the LVGL task or any event loop —
 * the post is non-blocking and a full queue only logs a warning. */
void ha_mqtt_request_restart(void);

int indicator_ha_model_init(void);
int indicator_ha_view_init(void);

#ifdef __cplusplus
}
#endif

#endif /* HA_MQTT_H */
