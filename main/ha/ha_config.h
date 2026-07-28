#ifndef HA_CONFIG_H
#define HA_CONFIG_H

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MQTT_HA_CFG_STORAGE "ha-mqtt"

typedef struct {
    char broker_url[128];
    char client_id[32];
    char username[32];
    char password[64];
    /* v2 fields — absent in legacy 256-byte NVS blobs; ha_cfg_get() migrates */
    char device_name[32];   /* default "indicator-<mac4>", e.g. indicator-3f2a */
    char topic_prefix[64];  /* default "seeed" */
} ha_cfg_interface;

esp_err_t ha_cfg_get(ha_cfg_interface *cfg);
esp_err_t ha_cfg_set(ha_cfg_interface *cfg);

/* Shared validation for the Settings UI and the setmqtt console command.
 * Device name: non-empty, [A-Za-z0-9-_], ≤31 chars.
 * Topic prefix: non-empty, no '+'/'#'/space, no leading/trailing '/', ≤63 chars. */
bool ha_cfg_validate_device_name(const char *name);
bool ha_cfg_validate_topic_prefix(const char *prefix);

void      ha_config_view_init(void);

#ifdef __cplusplus
}
#endif

#endif /* HA_CONFIG_H */
