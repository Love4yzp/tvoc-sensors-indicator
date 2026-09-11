#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "ha.h"
#include "home_assistant_config.h"
#include "mqtt.h"
#include "sen5x_mqtt.h"
#include "esp_log.h"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ha-model";

instance_mqtt mqtt_ha_instance;
static instance_mqtt_t instance_ptr = &mqtt_ha_instance;

static void _mqtt_ha_start(instance_mqtt *instance);
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);

void mqtt_topics_build(const ha_cfg_interface *cfg,
                       char *data, size_t data_sz,
                       char *status, size_t status_sz)
{
    snprintf(data, data_sz, "%s/%s/data", cfg->topic_prefix, cfg->device_name);
    snprintf(status, status_sz, "%s/%s/status", cfg->topic_prefix, cfg->device_name);
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;

    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED: {
            /* Read the broker from persistent config. mqtt_cfg->broker.address.uri
             * points into the stack frame of _mqtt_ha_start() — esp-mqtt copies the
             * config strings into its own state at init, so that pointer is dangling
             * by the time this connect event fires and logging it prints garbage. */
            ha_cfg_interface cfg;
            ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED — broker=%s",
                     (ha_cfg_get(&cfg) == ESP_OK) ? cfg.broker_url : "?");
            instance_ptr->mqtt_connected_flag = true;
            sen5x_mqtt_on_connect(client);
            /* LEGACY_HA: ha_sensor_subscribe(client); ha_switch_subscribe(client); */
            break;
        }
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
            instance_ptr->mqtt_connected_flag = false;
            sen5x_mqtt_on_disconnect();
            break;
        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_DATA:
            /* Device is publisher only — no incoming data processing needed.
             * LEGACY_HA: ha_sensor_on_mqtt_data(...); ha_switch_on_mqtt_data(...); */
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT_EVENT_ERROR");
            if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
                log_error_if_nonzero("reported from esp-tls", event->error_handle->esp_tls_last_esp_err);
                log_error_if_nonzero("reported from tls stack", event->error_handle->esp_tls_stack_err);
                log_error_if_nonzero("captured as transport's socket errno", event->error_handle->esp_transport_sock_errno);
                ESP_LOGE(TAG, "Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));
            } else if (event->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
                /* Broker rejected the CONNECT — bad credentials/client-id/protocol. */
                ESP_LOGE(TAG, "Connection refused by broker, return_code=%d",
                         event->error_handle->connect_return_code);
            }
            break;
        default:
            ESP_LOGW(TAG, "Other event id:%d", event->event_id);
            ESP_LOGW(TAG,
                     "If you are always here, please check that your broker is "
                     "accessible.");
            break;
    }
}

void ha_mqtt_request_restart(void)
{
    /* Restart through the MQTT app loop: it recreates the client from the NVS
     * config via _mqtt_ha_start(), which is NULL-safe and also picks up
     * credential changes. Calling esp_mqtt_client_set_uri() directly crashes
     * when the client was never created (e.g. no network at confirm time) and
     * silently ignores new credentials. Non-blocking: producers may run on the
     * LVGL task or the view_event loop, where a portMAX_DELAY post on a full
     * queue freezes the UI. */
    esp_err_t err = esp_event_post_to(mqtt_app_event_handle, MQTT_APP_EVENT_BASE,
                                      MQTT_APP_RESTART, &instance_ptr,
                                      sizeof(instance_mqtt_t), 0);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "MQTT_APP_RESTART post failed: %s", esp_err_to_name(err));
    }
}

static void _mqtt_ha_start(instance_mqtt *instance)
{
    /* No network gating: esp-mqtt's built-in auto-reconnect (see
     * reconnect_timeout_ms below) is the single reconnection mechanism, so
     * the client is created even with the link down and keeps retrying until
     * the broker is reachable. */
    if (instance->mqtt_client != NULL) {
        /* esp-mqtt's stop path does not dispatch MQTT_EVENT_DISCONNECTED, so
         * sen5x would keep its stale s_client and run its 5 s publish timer
         * against a freed client — clear it BEFORE the client is destroyed. */
        sen5x_mqtt_on_disconnect();
        esp_mqtt_client_stop(instance->mqtt_client);
        esp_mqtt_client_destroy(instance->mqtt_client);
        instance->mqtt_client = NULL;
    }

    if (instance->mqtt_cfg != NULL) {
        free(instance->mqtt_cfg);
        instance->mqtt_cfg = NULL;
    }

    ha_cfg_interface hf_cfg;
    if (ha_cfg_get(&hf_cfg) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get HA configuration");
        return;
    }

    /* Topics live in static storage: hf_cfg is a stack local and the LWT topic
     * pointer handed to esp-mqtt must not dangle. (esp-mqtt copies the config
     * strings at init, but keeping these static also lets sen5x publish to the
     * same buffers' layout via mqtt_topics_build.) */
    static char s_data_topic[MQTT_TOPIC_MAX_LEN];
    static char s_status_topic[MQTT_TOPIC_MAX_LEN];
    mqtt_topics_build(&hf_cfg, s_data_topic, sizeof(s_data_topic),
                      s_status_topic, sizeof(s_status_topic));

    instance->mqtt_cfg = (esp_mqtt_client_config_t *)malloc(sizeof(esp_mqtt_client_config_t));
    *instance->mqtt_cfg = (esp_mqtt_client_config_t){
        .broker.address.uri = hf_cfg.broker_url,
        .credentials.client_id = hf_cfg.client_id,
        .credentials.username = hf_cfg.username,
        .credentials.authentication.password = hf_cfg.password,
        /* Auto-reconnect is the ONLY reconnection mechanism (no WiFi-event
         * driven restart anywhere). Set the backoff explicitly so the retry
         * cadence does not depend on esp-mqtt defaults. */
        .network.reconnect_timeout_ms = 10000,
        /* Broker publishes retained "offline" if the device drops
         * unexpectedly; on connect sen5x_mqtt publishes retained "online" to
         * the same status topic. */
        .session.last_will = {
            .topic   = s_status_topic,
            .msg     = "offline",
            .msg_len = 7,
            .qos     = 0,
            .retain  = true,
        },
    };

    ESP_LOGI(TAG, "| Broker Address               | %-40s |", hf_cfg.broker_url);
    ESP_LOGI(TAG, "| Client ID                    | %-40s |", hf_cfg.client_id);
    ESP_LOGI(TAG, "| username                     | %-40s |", hf_cfg.username);
    ESP_LOGI(TAG, "| Data topic                   | %-40s |", s_data_topic);
    ESP_LOGI(TAG, "| Status topic (LWT)           | %-40s |", s_status_topic);

    instance->mqtt_client = esp_mqtt_client_init(instance->mqtt_cfg);
    if (instance->mqtt_client == NULL) {
        ESP_LOGE(TAG, "Failed to initialize MQTT client");
        return;
    }
    esp_mqtt_client_register_event(instance->mqtt_client, ESP_EVENT_ANY_ID, instance->mqtt_event_handler, NULL);
    esp_err_t start_result = esp_mqtt_client_start(instance->mqtt_client);
    if (start_result != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start MQTT client: %s", esp_err_to_name(start_result));
    } else {
        ESP_LOGI(TAG, "MQTT client started successfully");
    }
}

int indicator_ha_model_init(void)
{
    sen5x_mqtt_init();
    /* LEGACY_HA: ha_sensor_init(); ha_switch_init(); */

    ESP_LOGI(TAG, "mqtt_ha_init");

    mqtt_ha_instance = (instance_mqtt){
        .mqtt_name = "ha-model",
        .mqtt_connected_flag = false,
        .mqtt_client = NULL,
        .mqtt_cfg = NULL,
        .mqtt_event_handler = mqtt_event_handler,
        .mqtt_starter = _mqtt_ha_start,
        .is_using = true,
    };

    /* Kick the initial start through the MQTT app loop so the client is
     * created on the mqtt_event_task, same execution context as later
     * restarts. No WiFi gating: auto-reconnect handles the link being down. */
    esp_err_t err = esp_event_post_to(mqtt_app_event_handle, MQTT_APP_EVENT_BASE,
                                      MQTT_APP_START, &instance_ptr,
                                      sizeof(instance_mqtt_t), 0);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "initial MQTT_APP_START post failed: %s", esp_err_to_name(err));
    }
    esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_HA_ADDR_DISPLAY, NULL, 0, portMAX_DELAY);
    return ESP_OK;
}

int indicator_ha_view_init(void)
{
    ha_config_view_init();
    /* LEGACY_HA: ha_switch_screen_t *screen = ha_switch_screen_create();
     *            ha_switch_attach_screen(screen); */
    return ESP_OK;
}
