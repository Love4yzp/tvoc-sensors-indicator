#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>

#include "ha_config.h"
#include "ha_mqtt.h"
#include "view_data.h"
#include "home_assistant_config.h"
#include "storage_nvs.h"
#include "lv_port.h"
#include "indicator_util.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "sdkconfig.h"

#define MAX_BROKER_URL_LEN 128

/* NVS blobs written by firmware before device_name/topic_prefix existed hold
 * only the first four fields (128+32+32+64 = 256 bytes). */
#define HA_CFG_LEGACY_SIZE  offsetof(ha_cfg_interface, device_name)

/* Keyboard is pinned to the bottom of the 480px-high modal; the form
 * container starts at y=90. When the keyboard is up, shrink the form so
 * its bottom edge stays above the keyboard and the focused field can be
 * scrolled into the visible strip instead of being covered. */
#define FORM_TOP_Y       90
#define FORM_FULL_HEIGHT 395
#define KEYBOARD_HEIGHT  240
#define FORM_KBD_HEIGHT  (CONFIG_LCD_EVB_SCREEN_HEIGHT - KEYBOARD_HEIGHT - FORM_TOP_Y)

static const char *TAG = "ha-config";

static lv_obj_t *s_broker_modal                 = NULL;
static lv_obj_t *s_broker_ip_textarea           = NULL;
static lv_obj_t *s_broker_port_textarea         = NULL;
static lv_obj_t *s_broker_device_name_textarea  = NULL;
static lv_obj_t *s_broker_topic_prefix_textarea = NULL;
static lv_obj_t *s_broker_username_textarea     = NULL;
static lv_obj_t *s_broker_password_textarea     = NULL;
static lv_obj_t *s_broker_keyboard              = NULL;
static lv_obj_t *s_form_container               = NULL;
static lv_obj_t *s_identity_label               = NULL;
static lv_obj_t *s_preview_data_label           = NULL;
static lv_obj_t *s_preview_status_label         = NULL;

/* ── shared config validation (UI + setmqtt console command) ─────────────── */

bool ha_cfg_validate_device_name(const char *name)
{
    if (!name || name[0] == '\0' || strlen(name) > 31) {
        return false;
    }
    for (const char *p = name; *p; p++) {
        char c = *p;
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') || c == '-' || c == '_')) {
            return false;
        }
    }
    return true;
}

bool ha_cfg_validate_topic_prefix(const char *prefix)
{
    if (!prefix || prefix[0] == '\0' || strlen(prefix) > 63) {
        return false;
    }
    if (prefix[0] == '/' || prefix[strlen(prefix) - 1] == '/') {
        return false;
    }
    for (const char *p = prefix; *p; p++) {
        if (*p == '+' || *p == '#' || *p == ' ') {
            return false;
        }
    }
    return true;
}

/* Default device identity: derived from the eFuse MAC at runtime
 * ("indicator-<last two MAC bytes, hex>"), never forced back into NVS. */
static void _derive_default_device_name(char *out, size_t out_size)
{
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(out, out_size, "indicator-%02x%02x", mac[4], mac[5]);
}

/* ── helpers ─────────────────────────────────────────────────────────────── */

static void btn_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        lv_obj_t *mbox = (lv_obj_t *)lv_event_get_user_data(e);
        lv_msgbox_close(mbox);
    }
}

static void show_message_box(const char *message, lv_color_t color)
{
    lv_obj_t *mbox = lv_msgbox_create(NULL);
    lv_msgbox_add_title(mbox, "Notification");
    lv_msgbox_add_text(mbox, message);
    lv_obj_t *ok_btn = lv_msgbox_add_footer_button(mbox, "OK");

    lv_obj_set_style_bg_color(mbox, color, LV_PART_MAIN);
    lv_obj_set_style_text_color(mbox, lv_color_white(), LV_PART_MAIN);
    lv_obj_add_event_cb(ok_btn, btn_event_cb, LV_EVENT_CLICKED, mbox);
    lv_obj_center(mbox);
}

/* ── modal visibility ────────────────────────────────────────────────────── */

static void _set_keyboard_visible(bool visible)
{
    if (s_broker_keyboard) {
        if (visible) {
            lv_obj_remove_flag(s_broker_keyboard, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_broker_keyboard, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (s_form_container) {
        lv_obj_set_height(s_form_container,
                          visible ? FORM_KBD_HEIGHT : FORM_FULL_HEIGHT);
    }
}

static void _hide_broker_modal(void)
{
    _set_keyboard_visible(false);
    if (s_broker_modal) {
        lv_obj_add_flag(s_broker_modal, LV_OBJ_FLAG_HIDDEN);
    }
}

static void _on_broker_back(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    _hide_broker_modal();
}

/* ── keyboard interaction ────────────────────────────────────────────────── */

static void _on_textarea_focused(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED || !s_broker_keyboard) {
        return;
    }

    lv_obj_t *ta = lv_event_get_target(e);
    lv_keyboard_set_textarea(s_broker_keyboard, ta);

    /* Switch keyboard mode: number for IP/port, text for everything else */
    if (ta == s_broker_ip_textarea || ta == s_broker_port_textarea) {
        lv_keyboard_set_mode(s_broker_keyboard, LV_KEYBOARD_MODE_NUMBER);
    } else {
        lv_keyboard_set_mode(s_broker_keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
    }

    _set_keyboard_visible(true);

    /* Keep the focused field above the keyboard: the form container was just
     * shrunk to the visible strip, so scrolling the textarea into view also
     * lifts it clear of the keyboard. */
    lv_obj_scroll_to_view_recursive(ta, LV_ANIM_ON);
}

static void _on_broker_keyboard_done(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_READY && code != LV_EVENT_CANCEL && code != LV_EVENT_DEFOCUSED) {
        return;
    }

    _set_keyboard_visible(false);
}

/* ── confirm & save ──────────────────────────────────────────────────────── */

static void handle_mqtt_config_save(void);

/* Helper: style a textarea consistently with the dark theme */
static void _style_textarea(lv_obj_t *ta)
{
    lv_obj_set_style_bg_color(ta, lv_color_hex(0x1E2124),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ta, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(ta, lv_color_hex(0x3A3F45),
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ta, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ta, 6, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ta, lv_color_white(),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(ta, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
}

/* Recompose the read-only topic preview from the current field text, falling
 * back to the defaults for empty fields — the same rule the save path uses.
 * Runs on the LVGL task (textarea event callbacks / view handler). */
static void _refresh_topic_preview(void)
{
    if (!s_preview_data_label || !s_preview_status_label) {
        return;
    }

    const char *prefix = s_broker_topic_prefix_textarea ?
        lv_textarea_get_text(s_broker_topic_prefix_textarea) : "";
    const char *name = s_broker_device_name_textarea ?
        lv_textarea_get_text(s_broker_device_name_textarea) : "";

    char default_name[32];
    if (!prefix || prefix[0] == '\0') {
        prefix = CONFIG_MQTT_TOPIC_PREFIX;
    }
    if (!name || name[0] == '\0') {
        _derive_default_device_name(default_name, sizeof(default_name));
        name = default_name;
    }

    char buf[128];
    snprintf(buf, sizeof(buf), "Data:   %s/%s/data", prefix, name);
    lv_label_set_text(s_preview_data_label, buf);
    snprintf(buf, sizeof(buf), "Status: %s/%s/status", prefix, name);
    lv_label_set_text(s_preview_status_label, buf);
}

static void _on_identity_field_changed(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) {
        return;
    }
    _refresh_topic_preview();
}

static void _on_broker_confirm(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    /* Close the keyboard and restore the full-height form before saving so
     * the result message box is not covered. */
    _set_keyboard_visible(false);

    /* This callback runs on the LVGL task. Save directly instead of posting
     * VIEW_EVENT_MQTT_ADDR_CHANGED to our own view handler with
     * portMAX_DELAY — if the view queue were full, the LVGL task would block
     * forever and the whole UI would freeze. */
    handle_mqtt_config_save();
}

static void handle_mqtt_config_save(void)
{
    const char *new_ip = s_broker_ip_textarea ?
        lv_textarea_get_text(s_broker_ip_textarea) : "";
    const char *new_port = s_broker_port_textarea ?
        lv_textarea_get_text(s_broker_port_textarea) : "";
    const char *new_device_name = s_broker_device_name_textarea ?
        lv_textarea_get_text(s_broker_device_name_textarea) : "";
    const char *new_topic_prefix = s_broker_topic_prefix_textarea ?
        lv_textarea_get_text(s_broker_topic_prefix_textarea) : "";
    const char *new_username = s_broker_username_textarea ?
        lv_textarea_get_text(s_broker_username_textarea) : "";
    const char *new_password = s_broker_password_textarea ?
        lv_textarea_get_text(s_broker_password_textarea) : "";

    /* Validate broker IP */
    if (!is_valid_ipv4(new_ip)) {
        ESP_LOGE(TAG, "Invalid IPv4 address: %s", new_ip);
        show_message_box("Invalid IPv4 address", lv_palette_main(LV_PALETTE_RED));
        return;
    }

    /* Empty port falls back to the MQTT default 1883 */
    if (new_port[0] == '\0') {
        new_port = "1883";
    }

    /* Validate port: digits only (textarea already enforces this), 1–65535 */
    char *end = NULL;
    long port_num = strtol(new_port, &end, 10);
    if (end == new_port || *end != '\0' || port_num < 1 || port_num > 65535) {
        ESP_LOGE(TAG, "Invalid MQTT port: %s", new_port);
        show_message_box("Invalid port (1-65535)", lv_palette_main(LV_PALETTE_RED));
        return;
    }

    /* Empty device name / topic prefix mean "restore the default"
     * (MAC-derived name / "seeed") — ha_cfg_get() fills defaults for empty
     * fields. Non-empty values must pass the shared validation. */
    if (new_device_name[0] != '\0' && !ha_cfg_validate_device_name(new_device_name)) {
        ESP_LOGE(TAG, "Invalid device name: %s", new_device_name);
        show_message_box("Invalid device name ([A-Za-z0-9-_], max 31 chars)",
                         lv_palette_main(LV_PALETTE_RED));
        return;
    }
    if (new_topic_prefix[0] != '\0' && !ha_cfg_validate_topic_prefix(new_topic_prefix)) {
        ESP_LOGE(TAG, "Invalid topic prefix: %s", new_topic_prefix);
        show_message_box("Invalid topic prefix (no +/#/space, no leading/trailing /)",
                         lv_palette_main(LV_PALETTE_RED));
        return;
    }

    ha_cfg_interface ha_cfg;
    ha_cfg_get(&ha_cfg);

    char broker_url[MAX_BROKER_URL_LEN];
    assemble_broker_url(new_ip, new_port, broker_url, sizeof(broker_url));

    if (strlcpy(ha_cfg.broker_url, broker_url, sizeof(ha_cfg.broker_url))
        >= sizeof(ha_cfg.broker_url)) {
        ESP_LOGE(TAG, "Broker URL too long");
        show_message_box("Broker URL too long", lv_palette_main(LV_PALETTE_RED));
        return;
    }

    /* Validated above (≤31 / ≤63 chars), so these always fit. */
    strlcpy(ha_cfg.device_name, new_device_name, sizeof(ha_cfg.device_name));
    strlcpy(ha_cfg.topic_prefix, new_topic_prefix, sizeof(ha_cfg.topic_prefix));
    /* One name everywhere: the client id follows the device name. */
    strlcpy(ha_cfg.client_id, new_device_name, sizeof(ha_cfg.client_id));

    if (strlcpy(ha_cfg.username, new_username, sizeof(ha_cfg.username))
        >= sizeof(ha_cfg.username)) {
        ESP_LOGE(TAG, "Username too long");
        show_message_box("Username too long", lv_palette_main(LV_PALETTE_RED));
        return;
    }

    if (strlcpy(ha_cfg.password, new_password, sizeof(ha_cfg.password))
        >= sizeof(ha_cfg.password)) {
        ESP_LOGE(TAG, "Password too long");
        show_message_box("Password too long", lv_palette_main(LV_PALETTE_RED));
        return;
    }

    if (ha_cfg_set(&ha_cfg) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save MQTT config");
        show_message_box("Failed to save", lv_palette_main(LV_PALETTE_RED));
        return;
    }

    ESP_LOGI(TAG, "MQTT config saved: broker=%s, device=%s, prefix=%s, username=%s",
             ha_cfg.broker_url, ha_cfg.device_name, ha_cfg.topic_prefix, ha_cfg.username);

    _refresh_topic_preview();

    /* Notify MQTT module to restart with new config */
    esp_event_post_to(ha_cfg_event_handle, HA_CFG_EVENT_BASE, HA_CFG_BROKER_CHANGED,
                      ha_cfg.broker_url, sizeof(ha_cfg.broker_url), portMAX_DELAY);

    show_message_box("MQTT config saved", lv_palette_main(LV_PALETTE_GREEN));
}

/* ── modal UI construction ───────────────────────────────────────────────── */

static void _ensure_broker_modal(void)
{
    if (s_broker_modal) {
        return;
    }

    /* ── Full-screen modal ── */
    s_broker_modal = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_broker_modal, CONFIG_LCD_EVB_SCREEN_WIDTH,
                    CONFIG_LCD_EVB_SCREEN_HEIGHT);
    lv_obj_set_align(s_broker_modal, LV_ALIGN_CENTER);
    lv_obj_add_flag(s_broker_modal, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(s_broker_modal,
                       LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_style_bg_color(s_broker_modal, lv_color_hex(0x101418),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(s_broker_modal, LV_OPA_COVER,
                            LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(s_broker_modal, 0,
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(s_broker_modal, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(s_broker_modal, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_flag(s_broker_modal, LV_OBJ_FLAG_HIDDEN);

    /* ── Header (85 px) ── */
    lv_obj_t *header = lv_obj_create(s_broker_modal);
    lv_obj_set_size(header, 480, 85);
    lv_obj_set_align(header, LV_ALIGN_TOP_MID);
    lv_obj_remove_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP,
                            LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(header, LV_OPA_TRANSP,
                                LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *back = lv_button_create(header);
    lv_obj_set_size(back, 100, 50);
    lv_obj_set_pos(back, 10, 17);
    lv_obj_set_style_bg_opa(back, LV_OPA_TRANSP,
                            LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(back, lv_color_hex(0x2a3036),
                              LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(back, LV_OPA_40,
                            LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_border_width(back, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(back, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(back, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(back, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(back, _on_broker_back, LV_EVENT_CLICKED, NULL);
    lv_obj_t *back_label = lv_label_create(back);
    lv_label_set_text(back_label, LV_SYMBOL_LEFT " Back");
    lv_obj_set_style_text_color(back_label, lv_color_hex(0xe7ecef),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_center(back_label);

    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "MQTT");
    lv_obj_set_style_text_color(title, lv_color_white(),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_align(title, LV_ALIGN_BOTTOM_MID);

    /* ── Scrollable form container (480 - 85 = 395 px tall) ── */
    s_form_container = lv_obj_create(s_broker_modal);
    lv_obj_set_size(s_form_container, 420, FORM_FULL_HEIGHT);
    lv_obj_set_align(s_form_container, LV_ALIGN_TOP_MID);
    lv_obj_set_y(s_form_container, FORM_TOP_Y);
    lv_obj_set_style_bg_opa(s_form_container, LV_OPA_TRANSP,
                            LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(s_form_container, LV_OPA_TRANSP,
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(s_form_container, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    /* Scrollable: when keyboard (240 px) covers the bottom, user can scroll to
     * reach the lower fields and confirm button. */
    lv_obj_remove_flag(s_form_container, LV_OBJ_FLAG_SCROLL_ELASTIC |
                                         LV_OBJ_FLAG_SCROLL_MOMENTUM);

    /* ── Row offsets inside the form container ── */
    #define ROW_LABEL_Y  0
    #define ROW_INPUT_Y  22
    #define ROW_HEIGHT   70   /* label(20) + input(40) + gap(10) */

    /* Device identity line (Name / MAC / IP) for on-site matching against the
     * broker — text is refreshed in update_config_fields(). */
    s_identity_label = lv_label_create(s_form_container);
    lv_obj_set_style_text_color(s_identity_label, lv_color_hex(0x9E9E9E),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(s_identity_label, &lv_font_montserrat_14,
                               LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_pos(s_identity_label, 0, 0);

    int y = 48;   /* below the two-line identity label */

    /* ── 1. Broker Address ──────────────────────────────────────────────── */
    lv_obj_t *addr_label = lv_label_create(s_form_container);
    lv_label_set_text(addr_label, "Broker Address");
    lv_obj_set_style_text_color(addr_label, lv_color_hex(0x9E9E9E),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(addr_label, &lv_font_montserrat_14,
                               LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_pos(addr_label, 0, y + ROW_LABEL_Y);

    /* Row: "mqtt://" + IP textarea + ":" + port textarea */
    lv_obj_t *prefix = lv_label_create(s_form_container);
    lv_label_set_text(prefix, "mqtt://");
    lv_obj_set_style_text_color(prefix, lv_color_hex(0x9E9E9E),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(prefix, &lv_font_montserrat_14,
                               LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_pos(prefix, 0, y + ROW_INPUT_Y + 12);

    s_broker_ip_textarea = lv_textarea_create(s_form_container);
    lv_obj_set_size(s_broker_ip_textarea, 258, 40);
    lv_obj_set_pos(s_broker_ip_textarea, 62, y + ROW_INPUT_Y);
    lv_textarea_set_accepted_chars(s_broker_ip_textarea, "0123456789.");
    lv_textarea_set_max_length(s_broker_ip_textarea, 20);
    lv_textarea_set_placeholder_text(s_broker_ip_textarea, "192.168.1.10");
    lv_textarea_set_one_line(s_broker_ip_textarea, true);
    _style_textarea(s_broker_ip_textarea);
    lv_obj_add_event_cb(s_broker_ip_textarea, _on_textarea_focused,
                        LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_broker_ip_textarea, _on_broker_keyboard_done,
                        LV_EVENT_DEFOCUSED, NULL);

    lv_obj_t *colon = lv_label_create(s_form_container);
    lv_label_set_text(colon, ":");
    lv_obj_set_style_text_color(colon, lv_color_hex(0x9E9E9E),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(colon, &lv_font_montserrat_14,
                               LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_pos(colon, 326, y + ROW_INPUT_Y + 12);

    /* Port — default 1883, editable (1–65535) */
    s_broker_port_textarea = lv_textarea_create(s_form_container);
    lv_obj_set_size(s_broker_port_textarea, 82, 40);
    lv_obj_set_pos(s_broker_port_textarea, 338, y + ROW_INPUT_Y);
    lv_textarea_set_accepted_chars(s_broker_port_textarea, "0123456789");
    lv_textarea_set_max_length(s_broker_port_textarea, 5);
    lv_textarea_set_placeholder_text(s_broker_port_textarea, "1883");
    lv_textarea_set_one_line(s_broker_port_textarea, true);
    _style_textarea(s_broker_port_textarea);
    lv_obj_add_event_cb(s_broker_port_textarea, _on_textarea_focused,
                        LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_broker_port_textarea, _on_broker_keyboard_done,
                        LV_EVENT_DEFOCUSED, NULL);

    y += ROW_HEIGHT;

    /* ── 2. Topic Prefix (row order mirrors <prefix>/<device_name>/<leaf>) ── */
    lv_obj_t *prefix_label = lv_label_create(s_form_container);
    lv_label_set_text(prefix_label, "Topic Prefix");
    lv_obj_set_style_text_color(prefix_label, lv_color_hex(0x9E9E9E),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(prefix_label, &lv_font_montserrat_14,
                               LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_pos(prefix_label, 0, y + ROW_LABEL_Y);

    s_broker_topic_prefix_textarea = lv_textarea_create(s_form_container);
    lv_obj_set_size(s_broker_topic_prefix_textarea, 420, 40);
    lv_obj_set_pos(s_broker_topic_prefix_textarea, 0, y + ROW_INPUT_Y);
    lv_textarea_set_max_length(s_broker_topic_prefix_textarea, 63);
    lv_textarea_set_placeholder_text(s_broker_topic_prefix_textarea,
                                     CONFIG_MQTT_TOPIC_PREFIX);
    lv_textarea_set_one_line(s_broker_topic_prefix_textarea, true);
    _style_textarea(s_broker_topic_prefix_textarea);
    lv_obj_add_event_cb(s_broker_topic_prefix_textarea, _on_textarea_focused,
                        LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_broker_topic_prefix_textarea, _on_broker_keyboard_done,
                        LV_EVENT_DEFOCUSED, NULL);
    lv_obj_add_event_cb(s_broker_topic_prefix_textarea, _on_identity_field_changed,
                        LV_EVENT_VALUE_CHANGED, NULL);

    y += ROW_HEIGHT;

    /* ── 3. Device Name (one name everywhere: topic + MQTT client id) ────── */
    lv_obj_t *name_label = lv_label_create(s_form_container);
    lv_label_set_text(name_label, "Device Name");
    lv_obj_set_style_text_color(name_label, lv_color_hex(0x9E9E9E),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(name_label, &lv_font_montserrat_14,
                               LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_pos(name_label, 0, y + ROW_LABEL_Y);

    s_broker_device_name_textarea = lv_textarea_create(s_form_container);
    lv_obj_set_size(s_broker_device_name_textarea, 420, 40);
    lv_obj_set_pos(s_broker_device_name_textarea, 0, y + ROW_INPUT_Y);
    lv_textarea_set_max_length(s_broker_device_name_textarea, 31);
    /* Placeholder shows the MAC-derived default; saving an empty field
     * restores that default. */
    char default_name[32];
    _derive_default_device_name(default_name, sizeof(default_name));
    lv_textarea_set_placeholder_text(s_broker_device_name_textarea, default_name);
    lv_textarea_set_one_line(s_broker_device_name_textarea, true);
    _style_textarea(s_broker_device_name_textarea);
    lv_obj_add_event_cb(s_broker_device_name_textarea, _on_textarea_focused,
                        LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_broker_device_name_textarea, _on_broker_keyboard_done,
                        LV_EVENT_DEFOCUSED, NULL);
    lv_obj_add_event_cb(s_broker_device_name_textarea, _on_identity_field_changed,
                        LV_EVENT_VALUE_CHANGED, NULL);

    y += ROW_HEIGHT;

    /* ── 4. Username ────────────────────────────────────────────────────── */
    lv_obj_t *user_label = lv_label_create(s_form_container);
    lv_label_set_text(user_label, "Username");
    lv_obj_set_style_text_color(user_label, lv_color_hex(0x9E9E9E),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(user_label, &lv_font_montserrat_14,
                               LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_pos(user_label, 0, y + ROW_LABEL_Y);

    s_broker_username_textarea = lv_textarea_create(s_form_container);
    lv_obj_set_size(s_broker_username_textarea, 420, 40);
    lv_obj_set_pos(s_broker_username_textarea, 0, y + ROW_INPUT_Y);
    lv_textarea_set_max_length(s_broker_username_textarea, 31);
    lv_textarea_set_placeholder_text(s_broker_username_textarea, "sensor-node");
    lv_textarea_set_one_line(s_broker_username_textarea, true);
    _style_textarea(s_broker_username_textarea);
    lv_obj_add_event_cb(s_broker_username_textarea, _on_textarea_focused,
                        LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_broker_username_textarea, _on_broker_keyboard_done,
                        LV_EVENT_DEFOCUSED, NULL);

    y += ROW_HEIGHT;

    /* ── 5. Password ────────────────────────────────────────────────────── */
    lv_obj_t *pass_label = lv_label_create(s_form_container);
    lv_label_set_text(pass_label, "Password");
    lv_obj_set_style_text_color(pass_label, lv_color_hex(0x9E9E9E),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(pass_label, &lv_font_montserrat_14,
                               LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_pos(pass_label, 0, y + ROW_LABEL_Y);

    s_broker_password_textarea = lv_textarea_create(s_form_container);
    lv_obj_set_size(s_broker_password_textarea, 420, 40);
    lv_obj_set_pos(s_broker_password_textarea, 0, y + ROW_INPUT_Y);
    lv_textarea_set_max_length(s_broker_password_textarea, 63);
    lv_textarea_set_placeholder_text(s_broker_password_textarea, "password");
    lv_textarea_set_one_line(s_broker_password_textarea, true);
    lv_textarea_set_password_mode(s_broker_password_textarea, true);
    _style_textarea(s_broker_password_textarea);
    lv_obj_add_event_cb(s_broker_password_textarea, _on_textarea_focused,
                        LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_broker_password_textarea, _on_broker_keyboard_done,
                        LV_EVENT_DEFOCUSED, NULL);

    y += ROW_HEIGHT;

    /* ── 6. Live topic preview (read-only; updated on every keystroke) ────── */
    s_preview_data_label = lv_label_create(s_form_container);
    lv_obj_set_style_text_color(s_preview_data_label, lv_color_hex(0x9E9E9E),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(s_preview_data_label, &lv_font_montserrat_14,
                               LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_pos(s_preview_data_label, 0, y);

    s_preview_status_label = lv_label_create(s_form_container);
    lv_obj_set_style_text_color(s_preview_status_label, lv_color_hex(0x9E9E9E),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(s_preview_status_label, &lv_font_montserrat_14,
                               LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_pos(s_preview_status_label, 0, y + 20);

    y += 50;

    /* ── 7. Confirm button ──────────────────────────────────────────────── */
    lv_obj_t *confirm = lv_button_create(s_form_container);
    lv_obj_set_size(confirm, 420, 50);
    lv_obj_set_pos(confirm, 0, y);
    lv_obj_set_style_bg_color(confirm, lv_color_hex(0x4AAEE6),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(confirm, lv_color_hex(0x3A8EC6),
                              LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_radius(confirm, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(confirm, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(confirm, _on_broker_confirm, LV_EVENT_CLICKED, NULL);
    lv_obj_t *confirm_label = lv_label_create(confirm);
    lv_label_set_text(confirm_label, "Confirm");
    lv_obj_set_style_text_color(confirm_label, lv_color_white(),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_center(confirm_label);

    /* ── Keyboard (480×240, hidden by default, pinned to bottom of modal) ── */
    s_broker_keyboard = lv_keyboard_create(s_broker_modal);
    lv_keyboard_set_mode(s_broker_keyboard, LV_KEYBOARD_MODE_NUMBER);
    lv_keyboard_set_textarea(s_broker_keyboard, s_broker_ip_textarea);
    lv_obj_set_size(s_broker_keyboard, 480, KEYBOARD_HEIGHT);
    lv_obj_set_align(s_broker_keyboard, LV_ALIGN_BOTTOM_MID);
    lv_obj_add_flag(s_broker_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(s_broker_keyboard, _on_broker_keyboard_done,
                        LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(s_broker_keyboard, _on_broker_keyboard_done,
                        LV_EVENT_CANCEL, NULL);

    #undef ROW_LABEL_Y
    #undef ROW_INPUT_Y
    #undef ROW_HEIGHT
}

/* ── populate fields from stored config ──────────────────────────────────── */

/* Identity line for on-site use: effective device name, eFuse MAC, STA IP. */
static void _refresh_identity_label(const ha_cfg_interface *ha_cfg)
{
    if (!s_identity_label) {
        return;
    }

    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);

    char ip_str[24] = "not connected";
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ip_info = {0};
    if (netif && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK &&
        ip_info.ip.addr != 0) {
        snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_info.ip));
    }

    char buf[96];
    snprintf(buf, sizeof(buf),
             "Name: %s  MAC: %02x:%02x:%02x:%02x:%02x:%02x\nIP: %s",
             ha_cfg->device_name,
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], ip_str);
    lv_label_set_text(s_identity_label, buf);
}

static void update_config_fields(const ha_cfg_interface *ha_cfg)
{
    _ensure_broker_modal();

    if (s_broker_ip_textarea) {
        char ip[16];
        if (extract_ip_from_url(ha_cfg->broker_url, ip, sizeof(ip))) {
            lv_textarea_set_text(s_broker_ip_textarea, ip);
        } else {
            ESP_LOGE(TAG, "Failed to extract IP from URL: %s", ha_cfg->broker_url);
            lv_textarea_set_text(s_broker_ip_textarea, "");
        }
    }

    if (s_broker_port_textarea) {
        char port[8];
        /* No port in the stored URL means the MQTT default 1883 */
        if (extract_port_from_url(ha_cfg->broker_url, port, sizeof(port))) {
            lv_textarea_set_text(s_broker_port_textarea, port);
        } else {
            lv_textarea_set_text(s_broker_port_textarea, "1883");
        }
    }

    if (s_broker_device_name_textarea) {
        lv_textarea_set_text(s_broker_device_name_textarea, ha_cfg->device_name);
    }

    if (s_broker_topic_prefix_textarea) {
        lv_textarea_set_text(s_broker_topic_prefix_textarea, ha_cfg->topic_prefix);
    }

    if (s_broker_username_textarea) {
        lv_textarea_set_text(s_broker_username_textarea, ha_cfg->username);
    }

    if (s_broker_password_textarea) {
        lv_textarea_set_text(s_broker_password_textarea, ha_cfg->password);
    }

    _refresh_identity_label(ha_cfg);
    _refresh_topic_preview();
}

/* ── show modal ──────────────────────────────────────────────────────────── */

static void _show_broker_modal(void)
{
    _ensure_broker_modal();

    ha_cfg_interface ha_cfg;
    ha_cfg_get(&ha_cfg);
    update_config_fields(&ha_cfg);

    /* Always reopen in a clean state: keyboard down, full-height form,
     * scrolled back to the top. */
    _set_keyboard_visible(false);
    if (s_form_container) {
        lv_obj_scroll_to_y(s_form_container, 0, LV_ANIM_OFF);
    }

    if (s_broker_modal) {
        lv_obj_remove_flag(s_broker_modal, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_broker_modal);
    }
}

/* ── event handler ───────────────────────────────────────────────────────── */

static void view_event_handler(void *handler_args, esp_event_base_t base,
                                int32_t id, void *event_data)
{
    lv_port_sem_take();

    switch (id) {
        case VIEW_EVENT_SCREEN_START: {
            if (!event_data) {
                break;
            }
            uint8_t screen = *(uint8_t *)event_data;
            if (screen == SCREEN_BROKER_MODAL) {
                _show_broker_modal();
            }
            break;
        }
        case VIEW_EVENT_MQTT_ADDR_CHANGED: {
            handle_mqtt_config_save();
            break;
        }
        case VIEW_EVENT_HA_ADDR_DISPLAY: {
            ha_cfg_interface ha_cfg;
            ha_cfg_get(&ha_cfg);
            update_config_fields(&ha_cfg);
            break;
        }
        default:
            ESP_LOGW(TAG, "Unhandled event: %ld", id);
            break;
    }

    lv_port_sem_give();
}

/* ── public API ──────────────────────────────────────────────────────────── */

esp_err_t ha_cfg_get(ha_cfg_interface *ha_cfg)
{
    memset(ha_cfg, 0, sizeof(ha_cfg_interface));
    size_t len = sizeof(ha_cfg_interface);
    esp_err_t err = indicator_nvs_read(MQTT_HA_CFG_STORAGE, ha_cfg, &len);
    if (err == ESP_OK && len == sizeof(ha_cfg_interface)) {
        ESP_LOGI(TAG, "mqtt broker cfg read successful");
    } else {
        /* Legacy blobs (written before device_name/topic_prefix existed) are
         * HA_CFG_LEGACY_SIZE bytes. Re-read at the legacy size: the first four
         * fields share the same layout and the tail stays zeroed, so the new
         * fields fall through to the defaults below and the stored broker
         * config survives the firmware upgrade. */
        size_t legacy_len = HA_CFG_LEGACY_SIZE;
        esp_err_t legacy_err = indicator_nvs_read(MQTT_HA_CFG_STORAGE, ha_cfg, &legacy_len);
        if (legacy_err == ESP_OK && legacy_len == HA_CFG_LEGACY_SIZE) {
            ESP_LOGI(TAG, "mqtt broker cfg uses legacy layout — new fields defaulted");
            err = ESP_OK;
        } else {
            if (err == ESP_ERR_NVS_NOT_FOUND) {
                ESP_LOGI(TAG, "mqtt broker cfg not find");
            } else {
                ESP_LOGI(TAG, "mqtt broker cfg read err:%d", err);
            }
            strlcpy(ha_cfg->broker_url, CONFIG_BROKER_URL, sizeof(ha_cfg->broker_url));
            strlcpy(ha_cfg->username, CONFIG_MQTT_USERNAME, sizeof(ha_cfg->username));
            strlcpy(ha_cfg->password, CONFIG_MQTT_PASSWORD, sizeof(ha_cfg->password));
        }
    }

    /* Fill-in defaults, shared by fresh and legacy configs. */
    if (ha_cfg->topic_prefix[0] == '\0') {
        strlcpy(ha_cfg->topic_prefix, CONFIG_MQTT_TOPIC_PREFIX, sizeof(ha_cfg->topic_prefix));
    }
    if (ha_cfg->device_name[0] == '\0') {
        _derive_default_device_name(ha_cfg->device_name, sizeof(ha_cfg->device_name));
    }
    /* One name everywhere: the client id follows the device name unless it
     * was explicitly overridden (setmqtt -c). */
    if (ha_cfg->client_id[0] == '\0') {
        strlcpy(ha_cfg->client_id, ha_cfg->device_name, sizeof(ha_cfg->client_id));
    }
    return err;
}

esp_err_t ha_cfg_set(ha_cfg_interface *cfg)
{
    esp_err_t err = indicator_nvs_write(MQTT_HA_CFG_STORAGE, cfg, sizeof(ha_cfg_interface));
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "ha cfg write err:%d", err);
    } else {
        ESP_LOGI(TAG, "ha cfg write successful");
    }
    return err;
}

void ha_config_view_init(void)
{
    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(
        view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_SCREEN_START,
        view_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(
        view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_MQTT_ADDR_CHANGED,
        view_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(
        view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_HA_ADDR_DISPLAY,
        view_event_handler, NULL, NULL));
}
