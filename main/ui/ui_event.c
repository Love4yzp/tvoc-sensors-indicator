#include "ui_event.h"
#include "view_data.h"
#include "esp_log.h"

/* Deliberately LVGL-free so it host-compiles for the unit test
 * (test/host). The LVGL-dependent ui_defer() lives in ui_defer.c. */

static const char *TAG = "ui-event";

esp_err_t ui_event_post(int32_t event_id, const void *data, size_t size)
{
    /* timeout 0 → never blocks. See ui_event.h for why blocking here freezes
     * the UI. A full queue means we are momentarily overloaded; dropping a UI
     * event is recoverable, a deadlock is not. */
    esp_err_t err = esp_event_post_to(view_event_handle, VIEW_EVENT_BASE,
                                      event_id, (void *)data, size, 0);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "view queue full, dropped event %ld (%s)",
                 (long)event_id, esp_err_to_name(err));
    }
    return err;
}
