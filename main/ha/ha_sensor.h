#ifndef HA_SENSOR_H
#define HA_SENSOR_H

#include "mqtt_client.h"

#ifdef __cplusplus
extern "C" {
#endif

void ha_sensor_init(void);
void ha_sensor_subscribe(esp_mqtt_client_handle_t client);
int  ha_sensor_on_mqtt_data(const char *topic, int topic_len,
                             const char *data,  int data_len);

#ifdef __cplusplus
}
#endif

#endif /* HA_SENSOR_H */
