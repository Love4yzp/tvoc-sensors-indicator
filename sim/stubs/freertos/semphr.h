#pragma once
#include "FreeRTOS.h"

typedef void *SemaphoreHandle_t;

/* In the simulator LVGL runs single-threaded, so semaphore ops are no-ops. */
static inline SemaphoreHandle_t xSemaphoreCreateMutex(void) { return (void *)1; }
static inline BaseType_t xSemaphoreTake(SemaphoreHandle_t s, TickType_t t)
    { (void)s; (void)t; return pdTRUE; }
static inline void xSemaphoreGive(SemaphoreHandle_t s) { (void)s; }
static inline void vSemaphoreDelete(SemaphoreHandle_t s) { (void)s; }
