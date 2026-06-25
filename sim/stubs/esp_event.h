#pragma once
#include "esp_err.h"
#include "esp_event_base.h"
#include "freertos/FreeRTOS.h"  /* portMAX_DELAY used by callers */
#include <stddef.h>
#include <stdint.h>

/* Opaque handle — implemented in esp_event_stub.c as a registry */
typedef struct sim_event_loop *esp_event_loop_handle_t;

typedef void (*esp_event_handler_t)(void *handler_arg,
                                    esp_event_base_t base,
                                    int32_t event_id,
                                    void *event_data);
typedef void *esp_event_handler_instance_t;

#define ESP_EVENT_ANY_ID (-1)

/* Loop lifecycle — stubs create/return the singleton sim loop */
esp_err_t esp_event_loop_create_default(void);

typedef struct {
    const char *task_name;
    uint32_t    task_stack_size;
    int         task_priority;
    int32_t     queue_size;
} esp_event_loop_args_t;

esp_err_t esp_event_loop_create(const esp_event_loop_args_t *cfg,
                                esp_event_loop_handle_t     *out);

/* Registration */
esp_err_t esp_event_handler_instance_register_with(
    esp_event_loop_handle_t      event_loop,
    esp_event_base_t             event_base,
    int32_t                      event_id,
    esp_event_handler_t          event_handler,
    void                        *event_handler_arg,
    esp_event_handler_instance_t *instance);

esp_err_t esp_event_handler_register(
    esp_event_base_t  event_base,
    int32_t           event_id,
    esp_event_handler_t event_handler,
    void             *event_handler_arg);

/* Posting — synchronous dispatch in the sim (no FreeRTOS queue) */
esp_err_t esp_event_post_to(
    esp_event_loop_handle_t event_loop,
    esp_event_base_t        event_base,
    int32_t                 event_id,
    const void             *event_data,
    size_t                  event_data_size,
    uint32_t                ticks_to_wait);

esp_err_t esp_event_post(
    esp_event_base_t event_base,
    int32_t          event_id,
    const void      *event_data,
    size_t           event_data_size,
    uint32_t         ticks_to_wait);
