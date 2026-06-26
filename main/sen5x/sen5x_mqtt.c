#include "sen5x_mqtt.h"

#include <string.h>
#include <time.h>

#include "cJSON.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "sensor_model.h"
#include "view_data.h"

static const char *TAG = "sen5x-mqtt";

/* ── State ──────────────────────────────────────────────────────────────── */

typedef enum {
    SEN5X_STATE_WARMING_UP,
    SEN5X_STATE_ACTIVE,
} sen5x_state_t;

static volatile sen5x_state_t s_state       = SEN5X_STATE_WARMING_UP;
static esp_mqtt_client_handle_t s_client    = NULL;
static uint32_t                 s_seq       = 0;

static esp_timer_handle_t s_warming_timer;
static esp_timer_handle_t s_ddata_timer;

/* ── NVS helpers ─────────────────────────────────────────────────────────── */

static bool _nvs_load_warming_done(void)
{
    nvs_handle_t h;
    uint8_t val = 0;
    if (nvs_open(SEN5X_NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u8(h, SEN5X_NVS_WARM_KEY, &val);
        nvs_close(h);
    }
    return val == 1;
}

static void _nvs_save_warming_done(void)
{
    nvs_handle_t h;
    if (nvs_open(SEN5X_NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, SEN5X_NVS_WARM_KEY, 1);
        nvs_commit(h);
        nvs_close(h);
    }
}

/* ── VOC alert ───────────────────────────────────────────────────────────── */

static int _voc_alert(float voc_index)
{
    if (s_state == SEN5X_STATE_WARMING_UP) {
        return 0;
    }
    if (voc_index > SEN5X_VOC_THR_SEVERE)   return 3;
    if (voc_index > SEN5X_VOC_THR_MODERATE) return 2;
    if (voc_index > SEN5X_VOC_THR_LIGHT)    return 1;
    return 0;
}

/* ── Payload builders ────────────────────────────────────────────────────── */

static uint64_t _timestamp_ms(void)
{
    time_t now = time(NULL);
    if (now > 1000000000L) {
        return (uint64_t)now * 1000ULL;  /* NTP-synced Unix ms */
    }
    return (uint64_t)(esp_timer_get_time() / 1000);  /* fallback: ms since boot */
}

static cJSON *_make_metric(const char *name, float value, bool include_type)
{
    cJSON *m = cJSON_CreateObject();
    cJSON_AddStringToObject(m, "name", name);
    if (include_type) {
        cJSON_AddStringToObject(m, "type", "float");
    }
    cJSON_AddNumberToObject(m, "value", (double)value);
    return m;
}

static char *_build_payload(bool is_birth)
{
    float pm1_0    = get_sensor_float_value(SEN54_SENSOR_PM1_0);
    float pm2_5    = get_sensor_float_value(SEN54_SENSOR_PM2_5);
    float pm4_0    = get_sensor_float_value(SEN54_SENSOR_PM4_0);
    float pm10     = get_sensor_float_value(SEN54_SENSOR_PM10);
    float humidity = get_sensor_float_value(SEN54_SENSOR_HUMIDITY);
    float temp     = get_sensor_float_value(SEN54_SENSOR_TEMP);
    float voc_idx  = get_sensor_float_value(SEN54_SENSOR_VOC_IDX);
    float alert    = (float)_voc_alert(voc_idx);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "seq", (double)s_seq);
    cJSON_AddNumberToObject(root, "timestamp", (double)_timestamp_ms());

    cJSON *metrics = cJSON_AddArrayToObject(root, "metrics");
    cJSON_AddItemToArray(metrics, _make_metric("sen5x/pm1_0",      pm1_0,    is_birth));
    cJSON_AddItemToArray(metrics, _make_metric("sen5x/pm2_5",      pm2_5,    is_birth));
    cJSON_AddItemToArray(metrics, _make_metric("sen5x/pm4_0",      pm4_0,    is_birth));
    cJSON_AddItemToArray(metrics, _make_metric("sen5x/pm10",       pm10,     is_birth));
    cJSON_AddItemToArray(metrics, _make_metric("sen5x/humidity",   humidity, is_birth));
    cJSON_AddItemToArray(metrics, _make_metric("sen5x/temperature",temp,     is_birth));
    cJSON_AddItemToArray(metrics, _make_metric("sen5x/voc_index",  voc_idx,  is_birth));
    cJSON_AddItemToArray(metrics, _make_metric("sen5x/voc_alert",  alert,    is_birth));

    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;  /* caller must free() */
}

/* ── Publish helpers ─────────────────────────────────────────────────────── */

static void _publish_nbirth(void)
{
    if (!s_client) return;
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "seq", 0);
    cJSON_AddNumberToObject(root, "timestamp", (double)_timestamp_ms());
    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (payload) {
        esp_mqtt_client_publish(s_client, SEN5X_TOPIC_NBIRTH, payload, 0, 0, 0);
        free(payload);
    }
}

static void _publish_dbirth(void)
{
    if (!s_client) return;
    s_seq = 0;
    char *payload = _build_payload(true);
    if (payload) {
        esp_mqtt_client_publish(s_client, SEN5X_TOPIC_DBIRTH, payload, 0, 0, 0);
        ESP_LOGI(TAG, "DBIRTH published");
        free(payload);
    }
    s_seq++;
}

static void _publish_ddata(void)
{
    if (!s_client) return;
    char *payload = _build_payload(false);
    if (!payload) return;

    esp_mqtt_client_publish(s_client, SEN5X_TOPIC_DDATA, payload, 0, 0, 0);
    free(payload);
    s_seq++;

    /* Notify UI of current alert status */
    float voc_idx = get_sensor_float_value(SEN54_SENSOR_VOC_IDX);
    struct view_data_sen5x_status status = {
        .warming_up = (s_state == SEN5X_STATE_WARMING_UP),
        .voc_alert  = _voc_alert(voc_idx),
    };
    esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_SEN5X_STATUS,
                      &status, sizeof(status), 0);
}

/* ── Timer callbacks ─────────────────────────────────────────────────────── */

static void _warming_timer_cb(void *arg)
{
    s_state = SEN5X_STATE_ACTIVE;
    _nvs_save_warming_done();
    ESP_LOGI(TAG, "Warming up complete — VOC alerts now active");

    struct view_data_sen5x_status status = {.warming_up = false, .voc_alert = 0};
    esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_SEN5X_STATUS,
                      &status, sizeof(status), 0);
}

static void _ddata_timer_cb(void *arg)
{
    _publish_ddata();
}

/* ── Public API ──────────────────────────────────────────────────────────── */

void sen5x_mqtt_init(void)
{
    /* Determine initial state from NVS */
    if (_nvs_load_warming_done()) {
        s_state = SEN5X_STATE_ACTIVE;
        ESP_LOGI(TAG, "NVS: warming already done — starting ACTIVE");
    } else {
        s_state = SEN5X_STATE_WARMING_UP;
        ESP_LOGI(TAG, "NVS: first boot — starting WARMING_UP (60 min)");
    }

    /* One-shot warming timer (only needed if warming up) */
    esp_timer_create_args_t warming_args = {
        .callback = _warming_timer_cb,
        .name     = "sen5x_warm",
    };
    ESP_ERROR_CHECK(esp_timer_create(&warming_args, &s_warming_timer));
    if (s_state == SEN5X_STATE_WARMING_UP) {
        ESP_ERROR_CHECK(esp_timer_start_once(s_warming_timer, SEN5X_WARMING_US));
    }

    /* Periodic DDATA timer — starts after first MQTT connect */
    esp_timer_create_args_t ddata_args = {
        .callback = _ddata_timer_cb,
        .name     = "sen5x_ddata",
    };
    ESP_ERROR_CHECK(esp_timer_create(&ddata_args, &s_ddata_timer));
}

void sen5x_mqtt_on_connect(esp_mqtt_client_handle_t client)
{
    s_client = client;
    _publish_nbirth();
    _publish_dbirth();

    if (!esp_timer_is_active(s_ddata_timer)) {
        ESP_ERROR_CHECK(esp_timer_start_periodic(s_ddata_timer, SEN5X_DDATA_INTERVAL_US));
    }
    ESP_LOGI(TAG, "MQTT connected — NBIRTH+DBIRTH sent, DDATA timer running");
}

void sen5x_mqtt_on_disconnect(void)
{
    s_client = NULL;
    if (esp_timer_is_active(s_ddata_timer)) {
        esp_timer_stop(s_ddata_timer);
    }
}
