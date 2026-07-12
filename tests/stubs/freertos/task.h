#pragma once

#include "FreeRTOS.h"

inline TickType_t xTaskGetTickCount() { return 0; }
inline void vTaskDelay(const TickType_t) {}
