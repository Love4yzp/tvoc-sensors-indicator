#include <stdlib.h>
#include <string.h>
#include "wifi_list_screen.h"
#include "indicator_util.h"

/* Re-declare only the image assets this component needs — avoids pulling
 * in the old generated-UI namespace. */
LV_IMAGE_DECLARE(ui_img_wifi_1_png);
LV_IMAGE_DECLARE(ui_img_wifi_2_png);
LV_IMAGE_DECLARE(ui_img_wifi_3_png);
LV_IMAGE_DECLARE(ui_img_lock_png);

struct wifi_list_screen {
    lv_obj_t        *parent;
    lv_obj_t        *list;
    lv_obj_t        *scan_wait;
    lv_event_cb_t    on_connected_tap;
    lv_event_cb_t    on_unconnected_tap;
};

static void _rebuild_list(wifi_list_screen_t *s) {
    if(s->list) {
        lv_obj_delete(s->list);
    }
    s->list = lv_list_create(s->parent);
    lv_obj_set_style_pad_row(s->list, 8, 0);
    lv_obj_set_align(s->list, LV_ALIGN_CENTER);
    lv_obj_set_size(s->list, 420, 330);
    lv_obj_set_pos(s->list, 0, 35);
    lv_obj_set_style_bg_color(s->list, lv_color_hex(0x101418), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_color(s->list, lv_color_hex(0x101418), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(s->list, lv_color_hex(0x101418), LV_PART_MAIN | LV_STATE_DEFAULT);
}

static void _add_item(wifi_list_screen_t *s, const char *ssid,
                      bool have_password, int rssi, bool is_connect) {
    lv_obj_t *btn = lv_button_create(s->list);
    lv_obj_set_size(btn, 380, 50);
    lv_obj_set_align(btn, LV_ALIGN_CENTER);

    if(is_connect) {
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x529d53), LV_PART_MAIN | LV_STATE_DEFAULT);
        if(s->on_connected_tap) {
            lv_obj_add_event_cb(btn, s->on_connected_tap, LV_EVENT_CLICKED, NULL);
        }
    } else {
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x2c2c2c), LV_PART_MAIN | LV_STATE_DEFAULT);
        if(s->on_unconnected_tap) {
            lv_obj_add_event_cb(btn, s->on_unconnected_tap, LV_EVENT_CLICKED, NULL);
        }
    }

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, ssid);
    lv_obj_set_size(label, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_align(label, LV_ALIGN_LEFT_MID);
    lv_obj_set_x(label, 10);

    lv_obj_t *rssi_icon = lv_image_create(btn);
    lv_obj_set_align(rssi_icon, LV_ALIGN_RIGHT_MID);
    lv_obj_set_x(rssi_icon, -10);
    switch(wifi_rssi_level_get(rssi)) {
        case 1: lv_image_set_src(rssi_icon, &ui_img_wifi_1_png); break;
        case 2: lv_image_set_src(rssi_icon, &ui_img_wifi_2_png); break;
        case 3: lv_image_set_src(rssi_icon, &ui_img_wifi_3_png); break;
        default: break;
    }

    if(have_password) {
        lv_obj_t *lock = lv_image_create(btn);
        lv_image_set_src(lock, &ui_img_lock_png);
        lv_obj_set_align(lock, LV_ALIGN_RIGHT_MID);
        lv_obj_set_x(lock, -60);
    }
}

wifi_list_screen_t *wifi_list_screen_create(lv_obj_t *parent, lv_obj_t *scan_wait) {
    wifi_list_screen_t *s = calloc(1, sizeof(wifi_list_screen_t));
    if(!s) return NULL;

    s->parent    = parent;
    s->scan_wait = scan_wait;
    s->list      = NULL;
    _rebuild_list(s);
    lv_obj_add_flag(s->list, LV_OBJ_FLAG_HIDDEN);

    return s;
}

void wifi_list_screen_set_item_callbacks(wifi_list_screen_t *s,
                                          lv_event_cb_t on_connected_tap,
                                          lv_event_cb_t on_unconnected_tap) {
    if(!s) return;
    s->on_connected_tap   = on_connected_tap;
    s->on_unconnected_tap = on_unconnected_tap;
}

const char *wifi_list_screen_get_item_ssid(wifi_list_screen_t *s, lv_obj_t *item) {
    if(!s || !s->list) return NULL;
    return lv_list_get_btn_text(s->list, item);
}

void wifi_list_screen_show_spinner(wifi_list_screen_t *s) {
    if(!s) return;
    if(s->list)      lv_obj_add_flag(s->list, LV_OBJ_FLAG_HIDDEN);
    if(s->scan_wait) lv_obj_remove_flag(s->scan_wait, LV_OBJ_FLAG_HIDDEN);
}

void wifi_list_screen_update(wifi_list_screen_t *s, const struct view_data_wifi_list *list) {
    if(!s || !list) return;

    _rebuild_list(s);

    if(list->is_connect) {
        _add_item(s, list->connect.ssid, list->connect.auth_mode,
                  list->connect.rssi, true);
    }
    for(int i = 0; i < list->cnt; i++) {
        if(list->is_connect && strcmp(list->aps[i].ssid, list->connect.ssid) == 0) {
            continue;
        }
        _add_item(s, list->aps[i].ssid, list->aps[i].auth_mode,
                  list->aps[i].rssi, false);
    }

    lv_obj_remove_flag(s->list, LV_OBJ_FLAG_HIDDEN);
    if(s->scan_wait) lv_obj_add_flag(s->scan_wait, LV_OBJ_FLAG_HIDDEN);
}

void wifi_list_screen_destroy(wifi_list_screen_t *s) {
    if(!s) return;
    if(s->list) lv_obj_delete(s->list);
    free(s);
}
