#include "sen5x_mqtt.h"

#include <math.h>
#include <string.h>
#include <time.h>

#include "cJSON.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "esp_timer.h"

#include "ha_config.h"
#include "ha_mqtt.h"
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

/* Runtime-built protocol topics and device identity, refreshed on every
 * connect from the NVS config. */
static char s_data_topic[MQTT_TOPIC_MAX_LEN];
static char s_status_topic[MQTT_TOPIC_MAX_LEN];
static char s_device_name[32];

static esp_timer_handle_t s_warming_timer;
static esp_timer_handle_t s_data_timer;

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

/* Wall-clock is "synced" once SNTP reports a completed sync (wifi_model.c
 * starts SNTP on first IP). Before that, time(NULL) returns seconds-since-boot,
 * which is not a valid Unix epoch and must never be serialized as a data
 * timestamp. */
static bool _clock_synced(void)
{
    return esp_sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED;
}

static uint64_t _timestamp_s(void)
{
    return (uint64_t)time(NULL);  /* UTC epoch seconds — guard with _clock_synced() */
}

/* Round in double space so float32→double widening noise (e.g. 4.2f becomes
 * 4.19999980926514) never leaks into the JSON, and clamp to the sensor's real
 * resolution. `decimals` of 0 yields a clean integer ("104", not "104.0"). */
static cJSON *_make_metric(const char *name, double value, int decimals)
{
    cJSON *m = cJSON_CreateObject();
    cJSON_AddStringToObject(m, "name", name);
    double scale = pow(10.0, decimals);
    cJSON_AddNumberToObject(m, "value", round(value * scale) / scale);
    return m;
}

static char *_build_payload(void)
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
    cJSON_AddNumberToObject(root, "timestamp", (double)_timestamp_s());
    cJSON_AddStringToObject(root, "device", s_device_name);

    cJSON *metrics = cJSON_AddArrayToObject(root, "metrics");
    cJSON_AddItemToArray(metrics, _make_metric("sen5x/pm1_0",       pm1_0,    1));
    cJSON_AddItemToArray(metrics, _make_metric("sen5x/pm2_5",       pm2_5,    1));
    cJSON_AddItemToArray(metrics, _make_metric("sen5x/pm4_0",       pm4_0,    1));
    cJSON_AddItemToArray(metrics, _make_metric("sen5x/pm10",        pm10,     1));
    cJSON_AddItemToArray(metrics, _make_metric("sen5x/humidity",    humidity, 2));
    cJSON_AddItemToArray(metrics, _make_metric("sen5x/temperature", temp,     2));
    cJSON_AddItemToArray(metrics, _make_metric("sen5x/voc_index",   voc_idx,  0));
    cJSON_AddItemToArray(metrics, _make_metric("sen5x/voc_alert",   alert,    0));

    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;  /* caller must free() */
}

/* ── Publish helpers ─────────────────────────────────────────────────────── */

/* Publish + log uniformly so the serial console shows exactly what the device
 * sends without needing the protocol spec: topic + size + broker msg_id on
 * every publish (QoS 0 never fires MQTT_EVENT_PUBLISHED, so the returned
 * msg_id is the only publish-time signal). Full JSON payload goes to DEBUG
 * level to keep the 5 s data cadence from flooding the default log. */
static int _pub(const char *topic, const char *payload, int retain)
{
    int id = esp_mqtt_client_publish(s_client, topic, payload, 0, 0, retain);
    if (id < 0) {
        ESP_LOGW(TAG, "PUB %s FAILED to enqueue (disconnected?) ret=%d", topic, id);
    } else {
        ESP_LOGI(TAG, "PUB %s (%d bytes)%s msg_id=%d", topic, (int)strlen(payload),
                 retain ? " [retained]" : "", id);
        ESP_LOGD(TAG, "     payload=%s", payload);
    }
    return id;
}

static void _publish_data(void)
{
    if (!s_client) return;

    /* Never publish a fabricated timestamp: a wrong epoch is worse than a
     * missing message. Data publishes are held until NTP has set the clock
     * (the retained "online" status on connect is NOT gated — it carries no
     * timestamp). The UI status update below still runs, so the on-screen VOC
     * alert keeps working on networks without time sync. */
    if (_clock_synced()) {
        char *payload = _build_payload();
        if (payload) {
            _pub(s_data_topic, payload, 0);
            free(payload);
            s_seq = (s_seq + 1) & 0xFFu;  /* seq wraps 0–255 so consumers can spot gaps */
        }
    } else {
        static bool warned = false;
        if (!warned) {
            ESP_LOGW(TAG, "Clock not NTP-synced yet — holding data publishes to avoid bad timestamps");
            warned = true;
        }
    }

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
    ESP_LOGI(TAG, "Warming up complete — VOC alerts now active");

    struct view_data_sen5x_status status = {.warming_up = false, .voc_alert = 0};
    esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_SEN5X_STATUS,
                      &status, sizeof(status), 0);
}

static void _data_timer_cb(void *arg)
{
    _publish_data();
}

/* ── Public API ──────────────────────────────────────────────────────────── */

void sen5x_mqtt_init(void)
{
    /* Always warm up on boot: the SEN54 VOC algorithm is cold-reset by
     * deviceReset() on every (re)start, so the post-boot VOC readings are
     * unreliable regardless of whether this is the first boot. Suppress VOC
     * alerts for SEN5X_WARMING_US, then go ACTIVE. */
    s_state = SEN5X_STATE_WARMING_UP;
    ESP_LOGI(TAG, "Boot — starting WARMING_UP (%llu min)",
             SEN5X_WARMING_US / (60ULL * 1000000ULL));

    /* One-shot warming timer */
    esp_timer_create_args_t warming_args = {
        .callback = _warming_timer_cb,
        .name     = "sen5x_warm",
    };
    ESP_ERROR_CHECK(esp_timer_create(&warming_args, &s_warming_timer));
    ESP_ERROR_CHECK(esp_timer_start_once(s_warming_timer, SEN5X_WARMING_US));

    /* Periodic data timer — starts after first MQTT connect */
    esp_timer_create_args_t data_args = {
        .callback = _data_timer_cb,
        .name     = "sen5x_data",
    };
    ESP_ERROR_CHECK(esp_timer_create(&data_args, &s_data_timer));
}

void sen5x_mqtt_on_connect(esp_mqtt_client_handle_t client)
{
    s_client = client;

    /* Resolve the device identity and topics from the NVS config on every
     * connect so a Settings/console change takes effect on reconnect. The
     * returned struct always carries effective values (defaults filled in by
     * ha_cfg_get), even when the NVS read itself reported an error. */
    ha_cfg_interface cfg;
    esp_err_t err = ha_cfg_get(&cfg);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "ha_cfg_get err:%d — publishing with default identity", err);
    }
    mqtt_topics_build(&cfg, s_data_topic, sizeof(s_data_topic),
                      s_status_topic, sizeof(s_status_topic));
    strlcpy(s_device_name, cfg.device_name, sizeof(s_device_name));

    /* Announce presence immediately: retained "online" on the status topic.
     * Not NTP-gated — it carries no timestamp. The matching "offline" is the
     * broker-published LWT configured in _mqtt_ha_start(). */
    _pub(s_status_topic, "online", 1);

    if (!esp_timer_is_active(s_data_timer)) {
        ESP_ERROR_CHECK(esp_timer_start_periodic(s_data_timer, SEN5X_DATA_INTERVAL_US));
    }
    ESP_LOGI(TAG, "MQTT connected — data %s, status %s (online published)",
             s_data_topic, s_status_topic);
}

void sen5x_mqtt_on_disconnect(void)
{
    s_client = NULL;
    if (esp_timer_is_active(s_data_timer)) {
        esp_timer_stop(s_data_timer);
    }
}
