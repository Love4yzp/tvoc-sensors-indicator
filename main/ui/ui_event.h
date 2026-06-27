#ifndef UI_EVENT_H
#define UI_EVENT_H

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Low-level UI interaction layer.
 * ------------------------------------------------------------------------
 * All UI code runs in one of two contexts that must NEVER block:
 *
 *   1. the LVGL task, while it holds the esp_lvgl_port recursive lock — this is
 *      every widget event callback (button / slider / keyboard / list item …);
 *   2. the view_event_handle loop task, which is the SOLE consumer of the UI
 *      event queue.
 *
 * `lv_port_sem_take()` == `lvgl_port_lock(0)`, which waits forever. So a
 * *blocking* post to `view_event_handle`:
 *   - from context 1 → the lvgl lock is never released → the whole screen
 *     freezes (rendering and touch both die);
 *   - from context 2 → the loop blocks on its own full queue with itself as the
 *     only consumer → permanent self-deadlock.
 *
 * Both shipped as real freezes (Wi-Fi scan freeze, Wi-Fi connect freeze). Rather
 * than remember the rule at every call site (it was violated in 8 places across
 * 4 domains), this layer makes the safe path the only path: posting is always
 * non-blocking, and background tasks defer widget work onto the LVGL task
 * instead of locking by hand.
 *
 * Enforced by scripts/check_event_post_safety.py: view/screen code MUST use
 * ui_event_post() and never call esp_event_post_to(view_event_handle, …)
 * directly.
 */

/* Post a UI event to the shared view event loop, NON-BLOCKING.
 * `data` (size bytes) is copied into the loop on success.
 * Returns ESP_OK if queued, or ESP_ERR_TIMEOUT if the queue was full (the event
 * is dropped — always recoverable: a later render/refresh follows, or the user
 * repeats the action). Never blocks, so it is safe from any UI context. */
esp_err_t ui_event_post(int32_t event_id, const void *data, size_t size);

/* Run cb(arg) on the LVGL task. It executes with the lvgl lock already held, so
 * it may touch widgets directly and must NOT take the lock again. Use from
 * background/model tasks to update the UI without manual locking. Returns ESP_OK
 * or ESP_FAIL if it could not be scheduled. */
esp_err_t ui_defer(void (*cb)(void *arg), void *arg);

#ifdef __cplusplus
}
#endif

#endif /* UI_EVENT_H */
