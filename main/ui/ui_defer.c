#include "ui_event.h"
#include "lvgl.h"

/* Split from ui_event.c so ui_event_post() stays LVGL-free and host-testable.
 * ui_defer() schedules cb on the LVGL task via lv_async_call; the callback runs
 * inside lv_timer_handler (lvgl lock already held), so it may touch widgets
 * directly and must not take the lock again. */
esp_err_t ui_defer(void (*cb)(void *arg), void *arg)
{
    return (lv_async_call(cb, arg) == LV_RESULT_OK) ? ESP_OK : ESP_FAIL;
}
