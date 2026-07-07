#pragma once
#include <cstdint>
#include <cstdlib>

typedef int BaseType_t;
typedef uint32_t TickType_t;

#define pdMS_TO_TICKS(ms) ((TickType_t)((ms) / portTICK_PERIOD_MS))
#define portTICK_PERIOD_MS 1
#define portMAX_DELAY ((TickType_t)0xFFFFFFFFU)
#define pdTRUE  ((BaseType_t)1)
#define pdFALSE ((BaseType_t)0)
#define pdPASS  pdTRUE
#define pdFAIL  ((BaseType_t)0)
