#pragma once

typedef const char *esp_event_base_t;

#define ESP_EVENT_DECLARE_BASE(id) extern const char *id
#define ESP_EVENT_DEFINE_BASE(id)  const char *id = #id
