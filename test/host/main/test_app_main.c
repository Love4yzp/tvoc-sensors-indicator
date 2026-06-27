#include <stdlib.h>
#include "unity.h"

void test_ui_event_post_never_blocks_when_queue_full(void);
void test_ui_event_post_delivers_when_idle(void);

void app_main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_ui_event_post_never_blocks_when_queue_full);
    RUN_TEST(test_ui_event_post_delivers_when_idle);
    int failures = UNITY_END();

    /* The linux-target FreeRTOS scheduler keeps running after app_main returns,
     * so exit explicitly with the test result for CI / ./dev test. */
    exit(failures == 0 ? 0 : 1);
}
