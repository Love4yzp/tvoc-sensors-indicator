#include "sen5x_mqtt.h"

#include <math.h>
#include <string.h>
#include <time.h>

#include "cJSON.h"
#include "esp_log.h"
#include "esp_timer.h"

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
static bool                     s_birth_sent = false;  /* birth announced with a valid timestamp on the current connection */

static esp_timer_handle_t s_warming_timer;
static esp_timer_handle_t s_ddata_timer;

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

/* Wall-clock is "synced" once it is past 2001-09-09, i.e. NTP has set it.
 * Before that, time(NULL) returns seconds-since-boot, which is not a valid
 * Unix epoch and must never be serialized as a Sparkplug timestamp. */
static bool _clock_synced(void)
{
    return time(NULL) > 1000000000L;
}

static uint64_t _timestamp_s(void)
{
    return (uint64_t)time(NULL);  /* UTC epoch seconds — guard with _clock_synced() */
}

/* Round in double space so float32→double widening noise (e.g. 4.2f becomes
 * 4.19999980926514) never leaks into the JSON, and clamp to the sensor's real
 * resolution. `decimals` of 0 yields a clean integer ("104", not "104.0"). */
static cJSON *_make_metric(const char *name, double value, int decimals,
                           const char *type, bool include_type)
{
    cJSON *m = cJSON_CreateObject();
    cJSON_AddStringToObject(m, "name", name);
    if (include_type) {
        cJSON_AddStringToObject(m, "type", type);
    }
    double scale = pow(10.0, decimals);
    cJSON_AddNumberToObject(m, "value", round(value * scale) / scale);
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
    cJSON_AddNumberToObject(root, "timestamp", (double)_timestamp_s());

    cJSON *metrics = cJSON_AddArrayToObject(root, "metrics");
    cJSON_AddItemToArray(metrics, _make_metric("sen5x/pm1_0",       pm1_0,    1, "float", is_birth));
    cJSON_AddItemToArray(metrics, _make_metric("sen5x/pm2_5",       pm2_5,    1, "float", is_birth));
    cJSON_AddItemToArray(metrics, _make_metric("sen5x/pm4_0",       pm4_0,    1, "float", is_birth));
    cJSON_AddItemToArray(metrics, _make_metric("sen5x/pm10",        pm10,     1, "float", is_birth));
    cJSON_AddItemToArray(metrics, _make_metric("sen5x/humidity",    humidity, 2, "float", is_birth));
    cJSON_AddItemToArray(metrics, _make_metric("sen5x/temperature", temp,     2, "float", is_birth));
    cJSON_AddItemToArray(metrics, _make_metric("sen5x/voc_index",   voc_idx,  0, "int",   is_birth));
    cJSON_AddItemToArray(metrics, _make_metric("sen5x/voc_alert",   alert,    0, "int",   is_birth));

    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;  /* caller must free() */
}

/* ── Publish helpers ─────────────────────────────────────────────────────── */

/* Publish + log uniformly so the serial console shows exactly what the device
 * sends without needing the protocol spec: topic + size + broker msg_id on
 * every publish (QoS 0 never fires MQTT_EVENT_PUBLISHED, so the returned
 * msg_id is the only publish-time signal). Full JSON payload goes to DEBUG
 * level to keep the 5 s DDATA cadence from flooding the default log. */
static int _pub(const char *topic, const char *payload)
{
    int id = esp_mqtt_client_publish(s_client, topic, payload, 0, 0, 0);
    if (id < 0) {
        ESP_LOGW(TAG, "PUB %s FAILED to enqueue (disconnected?) ret=%d", topic, id);
    } else {
        ESP_LOGI(TAG, "PUB %s (%d bytes) msg_id=%d", topic, (int)strlen(payload), id);
        ESP_LOGD(TAG, "     payload=%s", payload);
    }
    return id;
}

static void _publish_nbirth(void)
{
    if (!s_client) return;
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "seq", 0);
    cJSON_AddNumberToObject(root, "timestamp", (double)_timestamp_s());
    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (payload) {
        _pub(SEN5X_TOPIC_NBIRTH, payload);
        free(payload);
    }
}

static void _publish_dbirth(void)
{
    if (!s_client) return;
    s_seq = 0;
    char *payload = _build_payload(true);
    if (payload) {
        _pub(SEN5X_TOPIC_DBIRTH, payload);
        free(payload);
    }
    s_seq = (s_seq + 1) & 0xFFu;  /* Sparkplug seq wraps 0–255 */
}

/* NBIRTH+DBIRTH establish the session; both carry a timestamp, so they must
 * wait for NTP just like DDATA. Call only when _clock_synced() is true. */
static void _publish_birth(void)
{
    _publish_nbirth();
    _publish_dbirth();
    s_birth_sent = true;
}

static void _publish_ddata(void)
{
    if (!s_client) return;

    /* Never publish a fabricated timestamp: a wrong epoch is worse than a
     * missing message. Hold the entire birth+data sequence until NTP has set
     * the clock. The UI status update below still runs, so the on-screen VOC
     * alert keeps working on networks without time sync. */
    if (_clock_synced()) {
        /* Announce the session with a valid timestamp before the first sample
         * (handles the case where MQTT connected before the clock synced). */
        if (!s_birth_sent) {
            _publish_birth();
        }
        char *payload = _build_payload(false);
        if (payload) {
            _pub(SEN5X_TOPIC_DDATA, payload);
            free(payload);
            s_seq = (s_seq + 1) & 0xFFu;  /* Sparkplug seq wraps 0–255 */
        }
    } else {
        static bool warned = false;
        if (!warned) {
            ESP_LOGW(TAG, "Clock not NTP-synced yet — holding NBIRTH/DBIRTH/DDATA to avoid bad timestamps");
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

static void _ddata_timer_cb(void *arg)
{
    _publish_ddata();
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

    /* Announce immediately only if the clock is already valid; otherwise the
     * DDATA timer sends NBIRTH+DBIRTH as soon as NTP syncs, so every birth
     * carries a real UTC timestamp instead of a boot-relative one. */
    s_birth_sent = false;
    if (_clock_synced()) {
        _publish_birth();
    }

    if (!esp_timer_is_active(s_ddata_timer)) {
        ESP_ERROR_CHECK(esp_timer_start_periodic(s_ddata_timer, SEN5X_DDATA_INTERVAL_US));
    }
    ESP_LOGI(TAG, "MQTT connected — DDATA timer running%s",
             s_birth_sent ? ", NBIRTH+DBIRTH sent" : " (birth deferred until NTP sync)");
}

void sen5x_mqtt_on_disconnect(void)
{
    s_client = NULL;
    s_birth_sent = false;  /* re-announce on the next connection */
    if (esp_timer_is_active(s_ddata_timer)) {
        esp_timer_stop(s_ddata_timer);
    }
}
