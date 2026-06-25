#pragma once
#include <stdint.h>

typedef uint32_t TickType_t;
typedef int32_t  BaseType_t;
typedef uint32_t UBaseType_t;

#define portMAX_DELAY    ((TickType_t)0xFFFFFFFFU)
#define pdTRUE           ((BaseType_t)1)
#define pdFALSE          ((BaseType_t)0)
#define pdPASS           pdTRUE
#define pdFAIL           pdFALSE
#define configMAX_TASK_NAME_LEN 16
