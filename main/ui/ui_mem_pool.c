#include "ui_mem_pool.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "lvgl.h"

/* LVGL 9.5 here uses its builtin TLSF allocator with a 64 KB pool in
 * internal RAM (CONFIG_LV_MEM_SIZE_KILOBYTES=64). That pool also backs
 * offscreen render layers (clip_corner strips, msgbox corners, ...), each
 * easily ~12 KB. When the pool is fragmented a layer allocation can fail
 * permanently, and LVGL's "Try later" retry loop then freezes the whole UI
 * (seen as taskLVGL spinning in lv_draw_layer_alloc_buf). This PSRAM
 * overflow pool makes such allocations virtually always succeed; small
 * allocations still land in the fast internal pool first. */
#define UI_LVGL_OVERFLOW_POOL_SIZE (256 * 1024)

static const char *TAG = "ui-mem-pool";

void ui_mem_pool_init(void)
{
    /* LVGL's TLSF requires the pool base to be ALIGN_SIZE (8) aligned, while
     * heap_caps_malloc only guarantees 4. */
    void *mem = heap_caps_aligned_alloc(16, UI_LVGL_OVERFLOW_POOL_SIZE,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if(!mem) {
        ESP_LOGE(TAG, "Failed to allocate %d bytes PSRAM for LVGL overflow pool",
                 UI_LVGL_OVERFLOW_POOL_SIZE);
        return;
    }

    lv_mem_pool_t pool = lv_mem_add_pool(mem, UI_LVGL_OVERFLOW_POOL_SIZE);
    if(!pool) {
        ESP_LOGE(TAG, "lv_mem_add_pool failed");
        heap_caps_free(mem);
        return;
    }

    ESP_LOGI(TAG, "Added %d KB PSRAM overflow pool to LVGL",
             UI_LVGL_OVERFLOW_POOL_SIZE / 1024);
}
