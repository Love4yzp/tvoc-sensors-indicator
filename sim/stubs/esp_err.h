#pragma once
#include <stdint.h>

typedef int32_t esp_err_t;
#define ESP_OK   ((esp_err_t)0)
#define ESP_FAIL ((esp_err_t)-1)

#define ESP_ERROR_CHECK(x) do { (void)(x); } while (0)

static inline const char *esp_err_to_name(esp_err_t code) {
    (void)code; return "ESP_ERR";
}
