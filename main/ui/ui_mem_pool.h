#ifndef UI_MEM_POOL_H
#define UI_MEM_POOL_H

/* Add a PSRAM overflow pool to LVGL's builtin allocator. Call once after
 * lv_port_init() (LVGL must be initialized first). */
void ui_mem_pool_init(void);

#endif /* UI_MEM_POOL_H */
