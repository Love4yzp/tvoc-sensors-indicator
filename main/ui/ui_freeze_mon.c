#include "ui_freeze_mon.h"

#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_debug_helpers.h"
#include "esp_private/crosscore_int.h"

#include "lvgl.h"
#include "lv_port.h"

/* taskLVGL must service this heartbeat well within one timeout window even
 * under full-refresh load; 8 s of silence means it is truly stuck. */
#define UI_HEARTBEAT_PERIOD_MS 500
#define UI_FREEZE_TIMEOUT_MS   8000
#define UI_BOOT_GRACE_MS       5000

static const char *TAG = "ui-freeze-mon";

/* Incremented by an LVGL timer, i.e. from inside taskLVGL's lv_timer_handler.
 * Stops advancing if taskLVGL spins or blocks anywhere. */
static volatile uint32_t s_ui_heartbeat;

static void _heartbeat_cb(lv_timer_t *timer)
{
    (void)timer;
    s_ui_heartbeat++;
}

static const char *_task_state_name(eTaskState state)
{
    switch (state) {
        case eReady:     return "Ready";
        case eRunning:   return "Running";
        case eBlocked:   return "Blocked";
        case eSuspended: return "Suspended";
        case eDeleted:   return "Deleted";
        default:         return "Invalid";
    }
}

static void _dump_and_abort(uint32_t stuck_beat)
{
    TaskHandle_t lvgl_task = xTaskGetHandle("taskLVGL");

    ESP_LOGE(TAG, "taskLVGL unresponsive: heartbeat stuck at %lu for >%d ms",
             (unsigned long)stuck_beat, UI_FREEZE_TIMEOUT_MS);
    if (lvgl_task) {
        ESP_LOGE(TAG, "taskLVGL state: %s",
                 _task_state_name(eTaskGetState(lvgl_task)));
    }

    /* Backtraces of what each core is executing right now. This pinpoints
     * the hang when taskLVGL is spinning; when it is blocked the cores show
     * unrelated tasks and the blocked stack is recovered from the coredump
     * that the abort below produces (decode with espcoredump.py + the elf). */
    int this_core = xPortGetCoreID();
    ESP_LOGE(TAG, "Print CPU %d (current core) backtrace", this_core);
    esp_backtrace_print(100);
#if !CONFIG_ESP_SYSTEM_SINGLE_CORE_MODE
    ESP_LOGE(TAG, "Print CPU %d backtrace", !this_core);
    esp_crosscore_int_send_print_backtrace(!this_core);
#endif

    /* Give the cross-core dump time to reach the UART, then panic: with
     * CONFIG_ESP_COREDUMP_ENABLE_TO_UART this dumps all tasks (including the
     * frozen taskLVGL) and reboots, so the device recovers on its own. */
    vTaskDelay(pdMS_TO_TICKS(1000));
    abort();
}

static void _monitor_task(void *arg)
{
    (void)arg;
    uint32_t last_beat = 0;
    TickType_t stale_start = 0;

    vTaskDelay(pdMS_TO_TICKS(UI_BOOT_GRACE_MS));

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));

        uint32_t beat = s_ui_heartbeat;
        if (beat != last_beat) {
            last_beat = beat;
            stale_start = 0;
            continue;
        }

        if (stale_start == 0) {
            stale_start = xTaskGetTickCount();
            continue;
        }

        if (xTaskGetTickCount() - stale_start >= pdMS_TO_TICKS(UI_FREEZE_TIMEOUT_MS)) {
            _dump_and_abort(last_beat);
        }
    }
}

void ui_freeze_mon_start(void)
{
    lv_port_sem_take();
    lv_timer_create(_heartbeat_cb, UI_HEARTBEAT_PERIOD_MS, NULL);
    lv_port_sem_give();

    /* Priority 1 is enough: when a spinning taskLVGL hoggs one core, the
     * monitor is scheduled on the other core regardless of priority. */
    xTaskCreate(_monitor_task, "ui_freeze_mon", 4096, NULL, 1, NULL);
    ESP_LOGI(TAG, "UI freeze monitor started (timeout %d ms)", UI_FREEZE_TIMEOUT_MS);
}
