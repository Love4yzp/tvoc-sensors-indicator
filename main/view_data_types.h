#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Screen / navigation ──────────────────────────────────────────────────── */

enum start_screen {
    SCREEN_SENSECAP_LOG = 0,
    SCREEN_WIFI_CONFIG = 1,
    SCREEN_DISPLAY_MODAL = 2,
    SCREEN_BROKER_MODAL = 3,
};

/* ── WiFi ─────────────────────────────────────────────────────────────────── */

#define WIFI_SCAN_LIST_SIZE 15

struct view_data_wifi_st {
    bool    is_connected;
    bool    is_connecting;
    bool    is_network;
    char    ssid[32];
    int8_t  rssi;
};

struct view_data_wifi_config {
    char    ssid[32];
    uint8_t password[64];
    bool    have_password;
};

struct view_data_wifi_item {
    char    ssid[32];
    bool    auth_mode;
    int8_t  rssi;
};

struct view_data_wifi_list {
    bool                      is_connect;
    struct view_data_wifi_item connect;
    uint16_t                  cnt;
    struct view_data_wifi_item aps[WIFI_SCAN_LIST_SIZE];
};

struct view_data_wifi_connet_ret_msg {
    uint8_t ret; /* 0 = success, 1 = failure */
    char    msg[64];
};

/* ── Display ──────────────────────────────────────────────────────────────── */

struct view_data_display {
    int  brightness;        /* 0–100 */
    bool sleep_mode_en;
    int  sleep_mode_time_min;
};

/* ── Time ─────────────────────────────────────────────────────────────────── */

struct view_data_time_cfg {
    bool   time_format_24;
    bool   auto_update;
    time_t time;
    bool   set_time;
    bool   auto_update_zone;
    int8_t zone;
    bool   daylight;
} __attribute__((packed));

/* ── Sensors ──────────────────────────────────────────────────────────────── */

struct sensor_data_average {
    float  data;
    time_t timestamp;
    bool   valid;
};

struct sensor_data_minmax {
    float  max;
    float  min;
    time_t timestamp;
    bool   valid;
};

/* Legacy sensors — disabled, replaced by SEN54.
 * Re-enable with -DLEGACY_SENSORS if reverting hardware.
 * #define SENSOR_TYPE_LIST                           \
 *     X(SCD41_SENSOR_CO2,      "SCD41_CO2")         \
 *     X(SGP40_SENSOR_TVOC,     "SGP40_TVOC")        \
 *     X(SHT41_SENSOR_TEMP,     "SHT41_TEMP")        \
 *     X(SHT41_SENSOR_HUMIDITY, "SHT41_HUMIDITY")
 */

/* SEN54: metric names match Sparkplug B payload field names */
#define SENSOR_TYPE_LIST                                      \
    X(SEN54_SENSOR_PM1_0,    "sen5x/pm1_0")                  \
    X(SEN54_SENSOR_PM2_5,    "sen5x/pm2_5")                  \
    X(SEN54_SENSOR_PM4_0,    "sen5x/pm4_0")                  \
    X(SEN54_SENSOR_PM10,     "sen5x/pm10")                   \
    X(SEN54_SENSOR_HUMIDITY, "sen5x/humidity")               \
    X(SEN54_SENSOR_TEMP,     "sen5x/temperature")            \
    X(SEN54_SENSOR_VOC_IDX,  "sen5x/voc_index")

#define X(type, str) type,
enum sensor_data_type {
    SENSOR_TYPE_LIST ENUM_SENSOR_ALL
};
#undef X

struct view_data_sensor_data {
    enum sensor_data_type sensor_type;
    float                 value;
};

struct view_data_sensor_history_data {
    enum sensor_data_type     sensor_type;
    struct sensor_data_average data_day[24];
    struct sensor_data_minmax  data_week[7];
    uint8_t                   resolution;
    float                     day_min;
    float                     day_max;
    float                     week_min;
    float                     week_max;
};

/* ── SEN5X status ─────────────────────────────────────────────────────────── */

/* voc_alert levels (lab-specific thresholds):
 *   0 = normal   (voc_index <= 120)
 *   1 = light    (120 < voc_index <= 180)
 *   2 = moderate (180 < voc_index <= 250)
 *   3 = severe   (voc_index > 250)
 * During WARMING_UP, voc_alert is always 0.
 */
struct view_data_sen5x_status {
    bool warming_up;
    int  voc_alert;  /* 0-3 */
};

/* ── Home Assistant ───────────────────────────────────────────────────────── */

struct view_data_ha_sensor_data {
    uint8_t index;
    char    value[32];
};

struct view_data_ha_switch_data {
    uint8_t index;
    int     value;
};

/* ── Event IDs (VIEW_EVENT_BASE) ─────────────────────────────────────────── */

/*
 * Event manifest for view_event_handle (VIEW_EVENT_BASE).
 *
 * Each entry documents:
 *   P  — producer(s)
 *   C  — consumer(s)
 *   Payload — data type posted with the event (NULL = no data)
 *   LVGL    — "lock required" = consumer must hold lv_port semaphore
 *
 * Bus: view_event_handle (main.c). Other buses: mqtt_app_event_handle,
 * ha_cfg_event_handle, cmd_cfg_event_handle.
 */
enum {
    /* P: wifi/wifi_model.c  C: wifi/wifi_view.c  Payload: uint8_t (enum start_screen) */
    VIEW_EVENT_SCREEN_START = 0,

    /* P: display/display_model.c  C: display/display_view.c  Payload: bool */
    VIEW_EVENT_TIME,

    /* P: wifi/wifi_model.c  C: mqtt/mqtt.c, wifi/wifi_view.c, ha/ha_mqtt.c */
    VIEW_EVENT_WIFI_ST,             /* struct view_data_wifi_st */

    /* P/C: TODO  Payload: char city[32] */
    VIEW_EVENT_CITY,

    /* P: rp2040/rp2040.c  C: sensor/sensor_view.c, ha/ha_sensor.c */
    VIEW_EVENT_SENSOR_DATA,         /* struct view_data_sensor_data */

    /* Trigger-only events (NULL payload) */
    VIEW_EVENT_SENSOR_TEMP_HISTORY,
    VIEW_EVENT_SENSOR_HUMIDITY_HISTORY,
    VIEW_EVENT_SENSOR_TVOC_HISTORY,
    VIEW_EVENT_SENSOR_CO2_HISTORY,

    /* P/C: TODO  Payload: struct view_data_sensor_history_data */
    VIEW_EVENT_SENSOR_DATA_HISTORY,

    /* P: wifi/wifi_model.c  C: wifi/wifi_view.c  Payload: struct view_data_wifi_list */
    VIEW_EVENT_WIFI_LIST,

    VIEW_EVENT_WIFI_LIST_START,     /* NULL — scan started */
    VIEW_EVENT_WIFI_LIST_REQ,       /* NULL — user requests scan */

    /* P: wifi/wifi_view.c  C: wifi/wifi_model.c  Payload: struct view_data_wifi_config */
    VIEW_EVENT_WIFI_CONNECT,

    /* P: wifi/wifi_model.c  C: wifi/wifi_view.c  Payload: struct view_data_wifi_connet_ret_msg */
    VIEW_EVENT_WIFI_CONNECT_RET,

    VIEW_EVENT_WIFI_CFG_DELETE,     /* NULL */

    /* P/C: TODO  Payload: struct view_data_time_cfg */
    VIEW_EVENT_TIME_CFG_UPDATE,
    VIEW_EVENT_TIME_CFG_APPLY,

    /* P: display/display_model.c, display/display_view.c  Payload: struct view_data_display */
    VIEW_EVENT_DISPLAY_CFG,

    VIEW_EVENT_BRIGHTNESS_UPDATE,   /* uint8_t brightness (0–100) */

    /* C: display/display_model.c  Payload: struct view_data_display */
    VIEW_EVENT_DISPLAY_CFG_APPLY,

    /* P: btn/btn.c  C: wifi/wifi_model.c, rp2040/rp2040.c  Payload: NULL */
    VIEW_EVENT_SHUTDOWN,

    /* C: wifi/wifi_model.c  Payload: NULL */
    VIEW_EVENT_FACTORY_RESET,

    /* P: btn/btn.c, display/display_model.c  Payload: bool (0=off, 1=on) */
    VIEW_EVENT_SCREEN_CTRL,

    /* P: ha/ha_config.c  C: ha/ha_config.c  Payload: NULL (reads textarea directly) */
    VIEW_EVENT_MQTT_ADDR_CHANGED,

    /* P: ha/ha_mqtt.c  C: ha/ha_config.c  Payload: NULL */
    VIEW_EVENT_HA_ADDR_DISPLAY,

    /* P: ha/ha_sensor.c  C: *** NO CONSUMER — not yet wired up ***
     * Payload: struct view_data_ha_sensor_data */
    VIEW_EVENT_HA_SENSOR,

    /* P: ha/ha_switch_screen.c  C: ha/ha_switch.c  Payload: struct view_data_ha_switch_data */
    VIEW_EVENT_HA_SWITCH_ST,

    /* P: ha/ha_switch.c  C: ha/ha_switch.c (internal loop)  Payload: struct view_data_ha_switch_data */
    VIEW_EVENT_HA_SWITCH_SET,

    /* P: sen5x/sen5x_mqtt.c  C: sensor/sensor_view.c
     * Payload: struct view_data_sen5x_status
     * Posted on WARMING_UP→ACTIVE transition and each DDATA publish. */
    VIEW_EVENT_SEN5X_STATUS,

    VIEW_EVENT_ALL,
};

#ifdef __cplusplus
}
#endif
