#include "cmd.h"
#include "esp_log.h"
#include "argtable3/argtable3.h"
#include "esp_console.h"

#include "home_assistant_config.h"
#include "ha.h"
#include "storage_nvs.h"
#include "indicator_util.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static const char *TAG = "CMD_RESP";

#define PROMPT_STR "Indicator"

static ha_cfg_interface ha_cfg;

static void print_mqtt_usage(void) {
    printf("\nMQTT configuration\n");
    printf("  Show current config:\n");
    printf("    haconfig\n\n");
    printf("  Set broker, device name, topic prefix and credentials:\n");
    printf("    setmqtt -a 192.168.1.10 -n lab-301 -t F01 -u mqtt_user -p mqtt_password\n");
    printf("    setmqtt --addr mqtt://192.168.1.10:1883\n");
    printf("    setmqtt --addr mqtt://broker.emqx.io\n\n");
    printf("  Notes:\n");
    printf("    - Device name: [A-Za-z0-9-_], max 31 chars; default is MAC-derived (indicator-<mac4>).\n");
    printf("      The MQTT client ID follows the device name (one name everywhere).\n");
    printf("    - Topic prefix: no '+', '#' or space, no leading/trailing '/', max 63 chars; default \"seeed\".\n");
    printf("    - setmqtt -c/--id overrides the client ID (advanced — normally not needed).\n");
    printf("    - The screen MQTT page asks for the broker IP and port (default 1883). It builds mqtt://<ip>:<port>.\n");
    printf("    - Restart is automatic after setmqtt succeeds.\n\n");
    printf("MQTT topics and payloads\n");
    printf("  Sensor data (JSON, every 5 s, NTP-gated):\n");
    printf("    topic: <prefix>/<device_name>/data    e.g. seeed/indicator-3f2a/data\n");
    printf("    data : {\"seq\":1,\"timestamp\":1719792000,\"device\":\"indicator-3f2a\",\"metrics\":[{\"name\":\"sen5x/pm2_5\",\"value\":6.8},...]}\n\n");
    printf("  Online status (retained; broker LWT publishes \"offline\"):\n");
    printf("    topic: <prefix>/<device_name>/status  payload: \"online\" | \"offline\"\n\n");
    printf("  Wildcard subscription examples:\n");
    printf("    seeed/+/data    — all devices under the default prefix\n");
    printf("    F01/+/status    — presence of every device in group F01\n\n");
}

static bool normalize_broker_url(const char *input, char *output, size_t output_size) {
    if (!input || input[0] == '\0' || !output || output_size == 0) return false;

    int written = 0;
    if (strncmp(input, "mqtt://", 7) == 0 || strncmp(input, "mqtts://", 8) == 0)
        written = snprintf(output, output_size, "%s", input);
    else
        written = snprintf(output, output_size, "mqtt://%s", input);

    return written > 0 && (size_t)written < output_size;
}

static int read_ha_config(int argc, char **argv) {
    ha_cfg_get(&ha_cfg);
    char data_topic[MQTT_TOPIC_MAX_LEN];
    char status_topic[MQTT_TOPIC_MAX_LEN];
    mqtt_topics_build(&ha_cfg, data_topic, sizeof(data_topic),
                      status_topic, sizeof(status_topic));
    ESP_LOGI(TAG, "| Broker Address               | %-40s |", ha_cfg.broker_url);
    ESP_LOGI(TAG, "| Device Name                  | %-40s |", ha_cfg.device_name);
    ESP_LOGI(TAG, "| Topic Prefix                 | %-40s |", ha_cfg.topic_prefix);
    ESP_LOGI(TAG, "| Client ID                    | %-40s |", ha_cfg.client_id);
    ESP_LOGI(TAG, "| MQTT username                | %-40s |", ha_cfg.username);
    ESP_LOGI(TAG, "| MQTT password                | %-40s |", ha_cfg.password);
    ESP_LOGI(TAG, "| Data topic                   | %-40s |", data_topic);
    ESP_LOGI(TAG, "| Status topic                 | %-40s |", status_topic);
    ESP_LOGI(TAG, "Run 'mqtthelp' for setmqtt examples and MQTT topic/payload examples.");
    return 0;
}

static int mqtt_help(int argc, char **argv) {
    print_mqtt_usage();
    return 0;
}

static void register_read_config(void) {
    const esp_console_cmd_t cmd = {
        .command = "haconfig",
        .help    = "Show current MQTT broker/client configuration",
        .hint    = NULL,
        .func    = &read_ha_config,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

static void register_mqtt_help(void) {
    const esp_console_cmd_t cmd = {
        .command = "mqtthelp",
        .help    = "Show MQTT setup examples, topics and payloads",
        .hint    = NULL,
        .func    = &mqtt_help,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

struct {
    struct arg_str *username;
    struct arg_str *password;
    struct arg_str *broker_url;
    struct arg_str *client_id;
    struct arg_str *device_name;
    struct arg_str *topic_prefix;
    struct arg_end *end;
} mqtt_args;

static int mqtt_config_set(int argc, char **argv) {
    int nerrors = arg_parse(argc, argv, (void **)&mqtt_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, mqtt_args.end, argv[0]);
        print_mqtt_usage();
        return 1;
    }

    if (!mqtt_args.username->count && !mqtt_args.password->count &&
        !mqtt_args.broker_url->count && !mqtt_args.client_id->count &&
        !mqtt_args.device_name->count && !mqtt_args.topic_prefix->count) {
        print_mqtt_usage();
        return 0;
    }

    ha_cfg_get(&ha_cfg);

    if (mqtt_args.username->count > 0) {
        memset(ha_cfg.username, 0, sizeof(ha_cfg.username));
        strncpy(ha_cfg.username, mqtt_args.username->sval[0], sizeof(ha_cfg.username) - 1);
        ESP_LOGI(TAG, "Set MQTT username: %s", ha_cfg.username);
    }
    if (mqtt_args.password->count > 0) {
        memset(ha_cfg.password, 0, sizeof(ha_cfg.password));
        strncpy(ha_cfg.password, mqtt_args.password->sval[0], sizeof(ha_cfg.password) - 1);
        ESP_LOGI(TAG, "Set MQTT password: %s", ha_cfg.password);
    }
    if (mqtt_args.broker_url->count > 0) {
        char broker_url[sizeof(ha_cfg.broker_url)];
        if (!normalize_broker_url(mqtt_args.broker_url->sval[0], broker_url, sizeof(broker_url))) {
            ESP_LOGE(TAG, "Invalid or too long broker URL: %s", mqtt_args.broker_url->sval[0]);
            return 1;
        }
        memset(ha_cfg.broker_url, 0, sizeof(ha_cfg.broker_url));
        strncpy(ha_cfg.broker_url, broker_url, sizeof(ha_cfg.broker_url) - 1);
        ESP_LOGI(TAG, "Set MQTT broker URL: %s", ha_cfg.broker_url);
    }
    if (mqtt_args.device_name->count > 0) {
        const char *name = mqtt_args.device_name->sval[0];
        if (!ha_cfg_validate_device_name(name)) {
            ESP_LOGE(TAG, "Invalid device name: %s ([A-Za-z0-9-_], max 31 chars)", name);
            return 1;
        }
        memset(ha_cfg.device_name, 0, sizeof(ha_cfg.device_name));
        strncpy(ha_cfg.device_name, name, sizeof(ha_cfg.device_name) - 1);
        /* One name everywhere: the client ID follows the device name. */
        memset(ha_cfg.client_id, 0, sizeof(ha_cfg.client_id));
        strncpy(ha_cfg.client_id, name, sizeof(ha_cfg.client_id) - 1);
        ESP_LOGI(TAG, "Set MQTT device name (and client ID): %s", ha_cfg.device_name);
    }
    if (mqtt_args.client_id->count > 0) {
        /* Advanced override — applied after -n so an explicit client ID wins. */
        memset(ha_cfg.client_id, 0, sizeof(ha_cfg.client_id));
        strncpy(ha_cfg.client_id, mqtt_args.client_id->sval[0], sizeof(ha_cfg.client_id) - 1);
        ESP_LOGI(TAG, "Set MQTT client ID: %s", ha_cfg.client_id);
    }
    if (mqtt_args.topic_prefix->count > 0) {
        const char *prefix = mqtt_args.topic_prefix->sval[0];
        if (!ha_cfg_validate_topic_prefix(prefix)) {
            ESP_LOGE(TAG, "Invalid topic prefix: %s (no +/#/space, no leading/trailing /, max 63 chars)", prefix);
            return 1;
        }
        memset(ha_cfg.topic_prefix, 0, sizeof(ha_cfg.topic_prefix));
        strncpy(ha_cfg.topic_prefix, prefix, sizeof(ha_cfg.topic_prefix) - 1);
        ESP_LOGI(TAG, "Set MQTT topic prefix: %s", ha_cfg.topic_prefix);
    }

    if (ha_cfg_set(&ha_cfg) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save MQTT configuration");
        return 1;
    }
    ha_mqtt_request_restart();
    ESP_LOGI(TAG, "MQTT configuration saved. Reconnecting MQTT client.");
    return 0;
}

static void register_mqtt_config(void) {
    mqtt_args.username     = arg_str0("u", "usr", "<username>", "MQTT username");
    mqtt_args.password     = arg_str0("p", "psw", "<password>", "MQTT password");
    mqtt_args.broker_url   = arg_str0("a", "addr", "<broker_url>",
                                      "MQTT broker URL, e.g. 192.168.1.10 or mqtt://host:1883");
    mqtt_args.client_id    = arg_str0("c", "id", "<client_id>",
                                      "MQTT client ID override (advanced — normally follows the device name)");
    mqtt_args.device_name  = arg_str0("n", "name", "<device_name>",
                                      "Device name [A-Za-z0-9-_], max 31 chars (also sets the client ID)");
    mqtt_args.topic_prefix = arg_str0("t", "topic", "<prefix>",
                                      "MQTT topic prefix, no +/#/space, no leading/trailing /, max 63 chars");
    mqtt_args.end          = arg_end(6);

    const esp_console_cmd_t cmd = {
        .command  = "setmqtt",
        .help     = "Set MQTT config. Example: setmqtt -a 192.168.1.10 -n lab-301 -t F01 -u user -p pass",
        .hint     = NULL,
        .func     = &mqtt_config_set,
        .argtable = &mqtt_args,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

int indicator_cmd_init(void) {
    esp_console_repl_t       *repl        = NULL;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_config.prompt                    = PROMPT_STR ">";
    repl_config.max_cmdline_length        = 1024;

    ha_cfg_get(&ha_cfg);
    register_read_config();
    register_mqtt_help();
    register_mqtt_config();

    esp_console_dev_uart_config_t hw_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_uart(&hw_config, &repl_config, &repl));
    ESP_ERROR_CHECK(esp_console_start_repl(repl));
    return ESP_OK;
}
