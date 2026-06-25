#ifndef DISPLAY_VIEW_H
#define DISPLAY_VIEW_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

int indicator_display_view_init(void);

void brighness_cfg_event_cb(lv_event_t* e);
void display_cfg_apply_event_cb(lv_event_t* e);
void brighness_update_callback(lv_event_t* e);

#ifdef __cplusplus
}
#endif

#endif /* DISPLAY_VIEW_H */
