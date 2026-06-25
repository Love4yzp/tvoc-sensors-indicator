#ifndef WIFI_LIST_SCREEN_H
#define WIFI_LIST_SCREEN_H

#include "lvgl.h"
#include "view_data.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct wifi_list_screen wifi_list_screen_t;

/* parent      — screen object to attach the list to (e.g. ui_screen_wifi)
 * scan_wait   — spinner widget to show/hide during scans                  */
wifi_list_screen_t *wifi_list_screen_create(lv_obj_t *parent, lv_obj_t *scan_wait);

/* Register tap callbacks; call before first update.
 * on_connected_tap   — user tapped the currently-connected AP
 * on_unconnected_tap — user tapped an unconnected AP                      */
void wifi_list_screen_set_item_callbacks(wifi_list_screen_t *s,
                                          lv_event_cb_t on_connected_tap,
                                          lv_event_cb_t on_unconnected_tap);

/* Get the SSID string for a tapped list item (use inside tap callbacks). */
const char *wifi_list_screen_get_item_ssid(wifi_list_screen_t *s, lv_obj_t *item);

void wifi_list_screen_update(wifi_list_screen_t *s, const struct view_data_wifi_list *list);
void wifi_list_screen_show_spinner(wifi_list_screen_t *s);
void wifi_list_screen_destroy(wifi_list_screen_t *s);

#ifdef __cplusplus
}
#endif

#endif /* WIFI_LIST_SCREEN_H */
