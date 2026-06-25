#ifndef VIEW_DATA_H
#define VIEW_DATA_H

#include "view_data_types.h"

#include "esp_event.h"
#include "esp_event_base.h"

#ifdef __cplusplus
extern "C" {
#endif

ESP_EVENT_DECLARE_BASE(VIEW_EVENT_BASE);
extern esp_event_loop_handle_t view_event_handle;

#ifdef __cplusplus
}
#endif

#endif /* VIEW_DATA_H */
