#pragma once

#define ESP_LOGI(tag, ...) do {} while(0)
#define ESP_LOGW(tag, ...) do {} while(0)
#define ESP_LOGE(tag, ...) do {} while(0)
#define ESP_LOGD(tag, ...) do {} while(0)

typedef int (*vprintf_like_t)(const char*, va_list);
