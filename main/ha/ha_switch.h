#ifndef HA_SWITCH_H
#define HA_SWITCH_H

#include "mqtt_client.h"
#include "ha_switch_screen.h"

#ifdef __cplusplus
extern "C" {
#endif

void ha_switch_init(void);
void ha_switch_subscribe(esp_mqtt_client_handle_t client);
int  ha_switch_on_mqtt_data(const char *topic, int topic_len,
                             const char *data,  int data_len);
void ha_switch_attach_screen(ha_switch_screen_t *screen);

#ifdef __cplusplus
}
#endif

#endif /* HA_SWITCH_H */
