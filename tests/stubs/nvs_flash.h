#pragma once

#include <cstdint>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t nvs_handle_t;

typedef enum {
    NVS_READONLY,
    NVS_READWRITE
} nvs_open_mode_t;

#define ESP_ERR_NVS_NOT_FOUND 0x1101
#define ESP_ERR_NVS_INVALID_NAME 0x1102

#ifdef __cplusplus
}
#endif
