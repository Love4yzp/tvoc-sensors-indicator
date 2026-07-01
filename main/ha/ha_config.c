#include <string.h>

#include "ha_config.h"
#include "ha_mqtt.h"
#include "view_data.h"
#include "home_assistant_config.h"
#include "storage_nvs.h"
#include "lv_port.h"
#include "indicator_util.h"
#include "esp_log.h"
#include "sdkconfig.h"

#define MAX_BROKER_URL_LEN 128

static const char *TAG = "ha-config";

static lv_obj_t *s_broker_modal               = NULL;
static lv_obj_t *s_broker_ip_textarea          = NULL;
static lv_obj_t *s_broker_client_id_textarea   = NULL;
static lv_obj_t *s_broker_username_textarea    = NULL;
static lv_obj_t *s_broker_password_textarea    = NULL;
static lv_obj_t *s_broker_keyboard             = NULL;
static lv_obj_t *s_form_container              = NULL;

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

static void _hide_broker_modal(void)
{
    if (s_broker_keyboard) {
        lv_obj_add_flag(s_broker_keyboard, LV_OBJ_FLAG_HIDDEN);
    }
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

    /* Switch keyboard mode: number for IP, text for everything else */
    if (ta == s_broker_ip_textarea) {
        lv_keyboard_set_mode(s_broker_keyboard, LV_KEYBOARD_MODE_NUMBER);
    } else {
        lv_keyboard_set_mode(s_broker_keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
    }

    lv_obj_remove_flag(s_broker_keyboard, LV_OBJ_FLAG_HIDDEN);
}

static void _on_broker_keyboard_done(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_READY && code != LV_EVENT_CANCEL && code != LV_EVENT_DEFOCUSED) {
        return;
    }

    if (s_broker_keyboard) {
        lv_obj_add_flag(s_broker_keyboard, LV_OBJ_FLAG_HIDDEN);
    }
}

/* ── confirm & save ──────────────────────────────────────────────────────── */

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

static void _on_broker_confirm(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    esp_event_post_to(view_event_handle, VIEW_EVENT_BASE,
                      VIEW_EVENT_MQTT_ADDR_CHANGED, NULL, 0, portMAX_DELAY);
}

static void handle_mqtt_config_save(void)
{
    const char *new_ip = s_broker_ip_textarea ?
        lv_textarea_get_text(s_broker_ip_textarea) : "";
    const char *new_client_id = s_broker_client_id_textarea ?
        lv_textarea_get_text(s_broker_client_id_textarea) : "";
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

    ha_cfg_interface ha_cfg;
    ha_cfg_get(&ha_cfg);

    char broker_url[MAX_BROKER_URL_LEN];
    assemble_broker_url(new_ip, broker_url, sizeof(broker_url));

    if (strlcpy(ha_cfg.broker_url, broker_url, sizeof(ha_cfg.broker_url))
        >= sizeof(ha_cfg.broker_url)) {
        ESP_LOGE(TAG, "Broker URL too long");
        show_message_box("Broker URL too long", lv_palette_main(LV_PALETTE_RED));
        return;
    }

    if (strlcpy(ha_cfg.client_id, new_client_id, sizeof(ha_cfg.client_id))
        >= sizeof(ha_cfg.client_id)) {
        ESP_LOGE(TAG, "Client ID too long");
        show_message_box("Client ID too long", lv_palette_main(LV_PALETTE_RED));
        return;
    }

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

    ESP_LOGI(TAG, "MQTT config saved: broker=%s, client_id=%s, username=%s",
             ha_cfg.broker_url, ha_cfg.client_id, ha_cfg.username);

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
    lv_obj_set_size(s_form_container, 420, 395);
    lv_obj_set_align(s_form_container, LV_ALIGN_TOP_MID);
    lv_obj_set_y(s_form_container, 90);
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

    int y = 0;

    /* ── 1. Broker Address ──────────────────────────────────────────────── */
    lv_obj_t *addr_label = lv_label_create(s_form_container);
    lv_label_set_text(addr_label, "Broker Address");
    lv_obj_set_style_text_color(addr_label, lv_color_hex(0x9E9E9E),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(addr_label, &lv_font_montserrat_14,
                               LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_pos(addr_label, 0, y + ROW_LABEL_Y);

    /* Row: "mqtt://" + textarea + ":1883" */
    lv_obj_t *prefix = lv_label_create(s_form_container);
    lv_label_set_text(prefix, "mqtt://");
    lv_obj_set_style_text_color(prefix, lv_color_hex(0x9E9E9E),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(prefix, &lv_font_montserrat_14,
                               LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_pos(prefix, 0, y + ROW_INPUT_Y + 12);

    s_broker_ip_textarea = lv_textarea_create(s_form_container);
    lv_obj_set_size(s_broker_ip_textarea, 265, 40);
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

    lv_obj_t *suffix = lv_label_create(s_form_container);
    lv_label_set_text(suffix, ":1883");
    lv_obj_set_style_text_color(suffix, lv_color_hex(0x9E9E9E),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(suffix, &lv_font_montserrat_14,
                               LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_pos(suffix, 335, y + ROW_INPUT_Y + 12);

    y += ROW_HEIGHT;

    /* ── 2. Client ID ───────────────────────────────────────────────────── */
    lv_obj_t *cid_label = lv_label_create(s_form_container);
    lv_label_set_text(cid_label, "Client ID");
    lv_obj_set_style_text_color(cid_label, lv_color_hex(0x9E9E9E),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(cid_label, &lv_font_montserrat_14,
                               LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_pos(cid_label, 0, y + ROW_LABEL_Y);

    s_broker_client_id_textarea = lv_textarea_create(s_form_container);
    lv_obj_set_size(s_broker_client_id_textarea, 420, 40);
    lv_obj_set_pos(s_broker_client_id_textarea, 0, y + ROW_INPUT_Y);
    lv_textarea_set_max_length(s_broker_client_id_textarea, 15);
    lv_textarea_set_placeholder_text(s_broker_client_id_textarea, "indicator-edge-01");
    lv_textarea_set_one_line(s_broker_client_id_textarea, true);
    _style_textarea(s_broker_client_id_textarea);
    lv_obj_add_event_cb(s_broker_client_id_textarea, _on_textarea_focused,
                        LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_broker_client_id_textarea, _on_broker_keyboard_done,
                        LV_EVENT_DEFOCUSED, NULL);

    y += ROW_HEIGHT;

    /* ── 3. Username ────────────────────────────────────────────────────── */
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

    /* ── 4. Password ────────────────────────────────────────────────────── */
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

    y += ROW_HEIGHT + 10;

    /* ── 5. Confirm button ──────────────────────────────────────────────── */
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
    lv_obj_set_size(s_broker_keyboard, 480, 240);
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

    if (s_broker_client_id_textarea) {
        lv_textarea_set_text(s_broker_client_id_textarea, ha_cfg->client_id);
    }

    if (s_broker_username_textarea) {
        lv_textarea_set_text(s_broker_username_textarea, ha_cfg->username);
    }

    if (s_broker_password_textarea) {
        lv_textarea_set_text(s_broker_password_textarea, ha_cfg->password);
    }
}

/* ── show modal ──────────────────────────────────────────────────────────── */

static void _show_broker_modal(void)
{
    _ensure_broker_modal();

    ha_cfg_interface ha_cfg;
    ha_cfg_get(&ha_cfg);
    update_config_fields(&ha_cfg);

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
    int len = sizeof(ha_cfg_interface);
    memset(ha_cfg, 0, sizeof(ha_cfg_interface));
    esp_err_t err = indicator_nvs_read(MQTT_HA_CFG_STORAGE, ha_cfg, &len);
    if (err == ESP_OK && len == sizeof(ha_cfg_interface)) {
        ESP_LOGI(TAG, "mqtt broker cfg read successful");
    } else {
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGI(TAG, "mqtt broker cfg not find");
        } else {
            ESP_LOGI(TAG, "mqtt broker cfg read err:%d", err);
        }
        strlcpy(ha_cfg->broker_url, CONFIG_BROKER_URL, sizeof(ha_cfg->broker_url));
        strlcpy(ha_cfg->client_id, CONFIG_MQTT_CLIENT_ID, sizeof(ha_cfg->client_id));
        strlcpy(ha_cfg->username, CONFIG_MQTT_USERNAME, sizeof(ha_cfg->username));
        strlcpy(ha_cfg->password, CONFIG_MQTT_PASSWORD, sizeof(ha_cfg->password));
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