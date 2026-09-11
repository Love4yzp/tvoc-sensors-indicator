#include "mqtt.h"
#include "esp_log.h"

static const char *TAG = "INDICATOR_MQTT";

ESP_EVENT_DEFINE_BASE(MQTT_APP_EVENT_BASE);
esp_event_loop_handle_t mqtt_app_event_handle;

/* Reconnection is owned exclusively by esp-mqtt's built-in auto-reconnect
 * (configured in ha_mqtt.c with an explicit reconnect_timeout_ms). There is
 * deliberately NO WiFi-status gating here: the client starts once at boot and
 * keeps retrying through link outages on its own, so a parallel "start on
 * WiFi up" path would just be a second, conflicting mechanism. This also
 * covers isolated-LAN deployments (local broker, no internet): the client
 * retries until the broker answers, no has_ip/is_network gate needed. This
 * loop now only handles lifecycle commands: initial start and config-change
 * restart. */

static void mqtt_start_interface(const instance_mqtt *instance, enum MQTT_APP_EVENT flag) {
    if (!instance || !instance->mqtt_name || !instance->mqtt_starter) {
        ESP_LOGE(TAG, "Cannot start MQTT: invalid instance");
        return;
    }

    if (flag == MQTT_APP_START && instance->mqtt_connected_flag) {
        ESP_LOGW(TAG, "%s is already connected.", instance->mqtt_name);
        return;
    }

    /* No client teardown here: instance->mqtt_starter() (_mqtt_ha_start)
     * stops/destroys/NULLs any existing client itself. Destroying the client
     * here without clearing instance->mqtt_client would make the starter
     * operate on an already-freed handle (use-after-free). */
    instance->mqtt_starter(instance);
}

static void _app_event_handler(void *handler_args, esp_event_base_t base, int32_t id, void *event_data) {
    instance_mqtt_t instance = *(instance_mqtt_t *)event_data;
    if (!instance) {
        ESP_LOGE(TAG, "Invalid MQTT instance");
        return;
    }
    switch (id) {
        case MQTT_APP_START:
        case MQTT_APP_RESTART:
            if (instance->is_using)
                mqtt_start_interface(instance, id);
            break;
        default:
            ESP_LOGW(TAG, "Unknown MQTT app event: %ld", id);
            break;
    }
}

int indicator_mqtt_init(void) {
    esp_event_loop_args_t mqtt_event_task_args = {
        .queue_size = 5,
        .task_name = "mqtt_event_task",
        .task_priority = uxTaskPriorityGet(NULL),
        .task_stack_size = 4096,
        .task_core_id = tskNO_AFFINITY
    };
    ESP_ERROR_CHECK(esp_event_loop_create(&mqtt_event_task_args, &mqtt_app_event_handle));

    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(
        mqtt_app_event_handle, MQTT_APP_EVENT_BASE, ESP_EVENT_ANY_ID,
        _app_event_handler, NULL, NULL));

    return ESP_OK;
}

void log_error_if_nonzero(const char *message, int error_code) {
    if (error_code != 0)
        ESP_LOGE(TAG, "Last error %s: 0x%x", message, error_code);
}
