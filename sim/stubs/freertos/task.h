#pragma once
#include "FreeRTOS.h"

typedef void *TaskHandle_t;

static inline void      vTaskDelay(TickType_t ticks)     { (void)ticks; }
static inline TickType_t xTaskGetTickCount(void)         { return 0; }
static inline void      vTaskDelete(TaskHandle_t h)      { (void)h; }
