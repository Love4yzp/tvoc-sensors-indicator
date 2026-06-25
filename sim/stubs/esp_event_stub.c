/**
 * Synchronous esp_event stub for the PC simulator.
 *
 * The real ESP-IDF posts events to a FreeRTOS queue and dispatches from a
 * task. Here we dispatch synchronously from the caller's context — safe
 * because the simulator is single-threaded and lv_port_sem_take/give are
 * no-ops.
 *
 * Registry: a flat array of (loop, base, id, handler, arg) tuples.
 * esp_event_post_to walks it and calls every matching handler in order.
 */
#include "esp_event.h"
#include "esp_event_base.h"
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ── Singleton "default" loop used by firmware domains ───────────────────── */

struct sim_event_loop {
    int dummy; /* opaque; never dereferenced outside this file */
};

static struct sim_event_loop s_default_loop;

/* Global handle accessed via view_data.h — defined here, declared extern there */
ESP_EVENT_DEFINE_BASE(VIEW_EVENT_BASE);
esp_event_loop_handle_t view_event_handle = &s_default_loop;

/* ── Handler registry ─────────────────────────────────────────────────────── */

#define MAX_HANDLERS 128

typedef struct {
    esp_event_loop_handle_t loop;
    esp_event_base_t        base;  /* NULL == match any */
    int32_t                 id;    /* ESP_EVENT_ANY_ID (-1) == match any */
    esp_event_handler_t     handler;
    void                   *arg;
} handler_entry_t;

static handler_entry_t s_handlers[MAX_HANDLERS];
static int             s_handler_count = 0;

static esp_err_t register_handler(esp_event_loop_handle_t loop,
                                   esp_event_base_t base, int32_t id,
                                   esp_event_handler_t handler, void *arg) {
    if (s_handler_count >= MAX_HANDLERS) {
        fprintf(stderr, "[esp_event_stub] handler table full\n");
        return ESP_FAIL;
    }
    s_handlers[s_handler_count++] = (handler_entry_t){loop, base, id, handler, arg};
    return ESP_OK;
}

/* ── Public API ───────────────────────────────────────────────────────────── */

esp_err_t esp_event_loop_create_default(void) { return ESP_OK; }

esp_err_t esp_event_loop_create(const esp_event_loop_args_t *cfg,
                                esp_event_loop_handle_t     *out) {
    (void)cfg;
    /* Return the singleton; multiple "loops" all share the same registry */
    *out = &s_default_loop;
    return ESP_OK;
}

esp_err_t esp_event_handler_instance_register_with(
        esp_event_loop_handle_t      loop,
        esp_event_base_t             base,
        int32_t                      id,
        esp_event_handler_t          handler,
        void                        *arg,
        esp_event_handler_instance_t *instance) {
    (void)instance;
    return register_handler(loop, base, id, handler, arg);
}

esp_err_t esp_event_handler_register(
        esp_event_base_t    base,
        int32_t             id,
        esp_event_handler_t handler,
        void               *arg) {
    return register_handler(&s_default_loop, base, id, handler, arg);
}

static esp_err_t dispatch(esp_event_loop_handle_t loop,
                           esp_event_base_t base, int32_t id,
                           const void *data, size_t data_size) {
    /* Copy payload so handlers that mutate it don't corrupt each other */
    void *copy = NULL;
    if (data && data_size > 0) {
        copy = malloc(data_size);
        if (!copy) return ESP_FAIL;
        memcpy(copy, data, data_size);
    }

    for (int i = 0; i < s_handler_count; i++) {
        handler_entry_t *e = &s_handlers[i];
        bool loop_match = (e->loop == loop) || (e->loop == &s_default_loop);
        bool base_match = (e->base == NULL) || (e->base == base);
        bool id_match   = (e->id   == ESP_EVENT_ANY_ID) || (e->id == id);
        if (loop_match && base_match && id_match)
            e->handler(e->arg, base, id, copy);
    }

    free(copy);
    return ESP_OK;
}

esp_err_t esp_event_post_to(esp_event_loop_handle_t loop,
                             esp_event_base_t base, int32_t id,
                             const void *data, size_t data_size,
                             uint32_t ticks_to_wait) {
    (void)ticks_to_wait;
    return dispatch(loop, base, id, data, data_size);
}

esp_err_t esp_event_post(esp_event_base_t base, int32_t id,
                          const void *data, size_t data_size,
                          uint32_t ticks_to_wait) {
    (void)ticks_to_wait;
    return dispatch(&s_default_loop, base, id, data, data_size);
}
