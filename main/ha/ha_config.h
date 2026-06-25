#ifndef HA_CONFIG_H
#define HA_CONFIG_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MQTT_HA_CFG_STORAGE "ha-mqtt"

typedef struct {
    char broker_url[128];
    char client_id[16];
    char username[32];
    char password[64];
} ha_cfg_interface;

esp_err_t ha_cfg_get(ha_cfg_interface *cfg);
esp_err_t ha_cfg_set(ha_cfg_interface *cfg);
void      ha_config_view_init(void);

#ifdef __cplusplus
}
#endif

#endif /* HA_CONFIG_H */
