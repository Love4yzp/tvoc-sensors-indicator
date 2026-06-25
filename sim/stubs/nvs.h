#pragma once
#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>

typedef uint32_t nvs_handle_t;
#define NVS_READWRITE 2
#define NVS_READONLY  1
#define ESP_ERR_NVS_NOT_FOUND ((esp_err_t)0x1102)

static inline esp_err_t nvs_open(const char *n, int m, nvs_handle_t *h)
    { (void)n; (void)m; *h = 0; return ESP_OK; }
static inline esp_err_t nvs_get_str(nvs_handle_t h, const char *k, char *v, size_t *l)
    { (void)h; (void)k; (void)v; (void)l; return ESP_ERR_NVS_NOT_FOUND; }
static inline esp_err_t nvs_set_str(nvs_handle_t h, const char *k, const char *v)
    { (void)h; (void)k; (void)v; return ESP_OK; }
static inline esp_err_t nvs_get_u8(nvs_handle_t h, const char *k, uint8_t *v)
    { (void)h; (void)k; *v = 0; return ESP_ERR_NVS_NOT_FOUND; }
static inline esp_err_t nvs_set_u8(nvs_handle_t h, const char *k, uint8_t v)
    { (void)h; (void)k; (void)v; return ESP_OK; }
static inline esp_err_t nvs_get_i32(nvs_handle_t h, const char *k, int32_t *v)
    { (void)h; (void)k; *v = 0; return ESP_ERR_NVS_NOT_FOUND; }
static inline esp_err_t nvs_set_i32(nvs_handle_t h, const char *k, int32_t v)
    { (void)h; (void)k; (void)v; return ESP_OK; }
static inline esp_err_t nvs_get_blob(nvs_handle_t h, const char *k, void *v, size_t *l)
    { (void)h; (void)k; (void)v; (void)l; return ESP_ERR_NVS_NOT_FOUND; }
static inline esp_err_t nvs_set_blob(nvs_handle_t h, const char *k, const void *v, size_t l)
    { (void)h; (void)k; (void)v; (void)l; return ESP_OK; }
static inline esp_err_t nvs_commit(nvs_handle_t h)       { (void)h; return ESP_OK; }
static inline void      nvs_close(nvs_handle_t h)        { (void)h; }
static inline esp_err_t nvs_erase_key(nvs_handle_t h, const char *k)
    { (void)h; (void)k; return ESP_OK; }
