#include "diag/stack_monitor.hpp"
#include <cstdio>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static constexpr auto TAG = "stack_monitor";

namespace ecotiter::diag {

void StackMonitor::registerMainTask() noexcept {
    registerThread("main", 32768);
}

void StackMonitor::registerThread(const char* name, size_t stackSize) noexcept {
    if (count_ >= MAX_THREADS) {
        ESP_LOGW(TAG, "Too many threads registered");
        return;
    }
    names_[count_] = name;
    stackSizes_[count_] = stackSize;
    handles_[count_] = xTaskGetCurrentTaskHandle();
    ++count_;
}

uint32_t StackMonitor::watermarkMain() const noexcept {
    UBaseType_t wm = uxTaskGetStackHighWaterMark(nullptr);
    return static_cast<uint32_t>(wm);
}

void StackMonitor::logAllWatermarks() const noexcept {
    for (size_t i = 0; i < count_; ++i) {
        UBaseType_t wm = uxTaskGetStackHighWaterMark(handles_[i]);
        uint32_t usedBytes = static_cast<uint32_t>(
            stackSizes_[i] - static_cast<size_t>(wm));
        uint32_t usedPct = (stackSizes_[i] > 0)
            ? (usedBytes * 100 / static_cast<uint32_t>(stackSizes_[i])) : 0;
        ESP_LOGI(TAG, "Thread %s: cfg=%zuB wmark=%u used=%u%%",
                 names_[i], stackSizes_[i],
                 static_cast<unsigned>(wm),
                 static_cast<unsigned>(usedPct));
        if (usedPct > 90) {
            ESP_LOGW(TAG, "Thread %s: LOW STACK! %u%% used",
                     names_[i], static_cast<unsigned>(usedPct));
        }
    }
}

} // namespace ecotiter::diag
