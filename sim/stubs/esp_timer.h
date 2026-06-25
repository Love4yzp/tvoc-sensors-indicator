#pragma once
#include "esp_err.h"
#include <stdint.h>

typedef struct {
    void      (*callback)(void *arg);
    void       *arg;
    const char *name;
} esp_timer_create_args_t;

typedef void *esp_timer_handle_t;

static inline esp_err_t esp_timer_create(const esp_timer_create_args_t *a,
                                          esp_timer_handle_t *h) {
    (void)a; *h = NULL; return ESP_OK;
}
static inline esp_err_t esp_timer_start_once(esp_timer_handle_t h, uint64_t us) {
    (void)h; (void)us; return ESP_OK;
}
static inline esp_err_t esp_timer_stop(esp_timer_handle_t h)   { (void)h; return ESP_OK; }
static inline esp_err_t esp_timer_delete(esp_timer_handle_t h) { (void)h; return ESP_OK; }
