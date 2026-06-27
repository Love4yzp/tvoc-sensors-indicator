/* Host unit tests for the UI interaction layer (main/ui/ui_event.c).
 *
 * These reproduce, on the host, the exact runtime condition that froze the
 * device: the single-consumer view-event loop is busy/full while UI code needs
 * to post. They assert ui_event_post() returns promptly (never blocks) and the
 * loop always drains — i.e. the freeze cannot recur. */
#include "unity.h"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "ui_event.h"

/* ui_event.c posts to these globals; in the firmware they live in main.c. */
ESP_EVENT_DEFINE_BASE(VIEW_EVENT_BASE);
esp_event_loop_handle_t view_event_handle;

#define EV_BLOCK 1 /* makes the loop task block inside the handler */
#define EV_FILL  2 /* fills the queue behind the blocked handler   */

static SemaphoreHandle_t s_gate;
static volatile int s_handled;

static void handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (id == EV_BLOCK) {
        xSemaphoreTake(s_gate, portMAX_DELAY); /* hold the loop hostage */
    }
    s_handled++;
}

static void create_loop(uint32_t queue_size)
{
    s_handled = 0;
    s_gate = xSemaphoreCreateBinary();
    esp_event_loop_args_t args = {
        .queue_size = queue_size,
        .task_name = "vloop",
        .task_priority = 5,
        .task_stack_size = 4096,
        .task_core_id = tskNO_AFFINITY,
    };
    TEST_ESP_OK(esp_event_loop_create(&args, &view_event_handle));
    TEST_ESP_OK(esp_event_handler_register_with(view_event_handle, VIEW_EVENT_BASE,
                                                ESP_EVENT_ANY_ID, handler, NULL));
}

static void destroy_loop(void)
{
    TEST_ESP_OK(esp_event_loop_delete(view_event_handle));
    view_event_handle = NULL;
    vSemaphoreDelete(s_gate);
}

/* The core invariant: with the loop stuck and its queue full, ui_event_post()
 * must return ESP_ERR_TIMEOUT instead of blocking. A blocking (portMAX_DELAY)
 * post here is the freeze — from the LVGL task it holds the lvgl lock forever,
 * from the loop itself it self-deadlocks. */
void test_ui_event_post_never_blocks_when_queue_full(void)
{
    create_loop(2);

    /* Loop dequeues EV_BLOCK then blocks; give it a moment to get there. */
    TEST_ASSERT_EQUAL(ESP_OK, ui_event_post(EV_BLOCK, NULL, 0));
    vTaskDelay(pdMS_TO_TICKS(50));

    /* Fill both queue slots. */
    TEST_ASSERT_EQUAL(ESP_OK, ui_event_post(EV_FILL, NULL, 0));
    TEST_ASSERT_EQUAL(ESP_OK, ui_event_post(EV_FILL, NULL, 0));

    /* Queue full → must not block. */
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, ui_event_post(EV_FILL, NULL, 0));

    /* Release: the loop drains the two queued EV_FILL (no deadlock). */
    xSemaphoreGive(s_gate);
    vTaskDelay(pdMS_TO_TICKS(100));
    TEST_ASSERT_EQUAL(3, s_handled); /* EV_BLOCK + 2x EV_FILL */

    destroy_loop();
}

/* Sanity: under normal conditions posts are queued and handled. */
void test_ui_event_post_delivers_when_idle(void)
{
    create_loop(8);

    for (int i = 0; i < 5; i++) {
        TEST_ASSERT_EQUAL(ESP_OK, ui_event_post(EV_FILL, NULL, 0));
    }
    vTaskDelay(pdMS_TO_TICKS(100));
    TEST_ASSERT_EQUAL(5, s_handled);

    destroy_loop();
}
