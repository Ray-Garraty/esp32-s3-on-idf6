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
    ++count_;
}

uint32_t StackMonitor::watermarkMain() const noexcept {
    UBaseType_t wm = uxTaskGetStackHighWaterMark(nullptr);
    return static_cast<uint32_t>(wm);
}

void StackMonitor::logAllWatermarks() const noexcept {
    for (size_t i = 0; i < count_; ++i) {
        // Logged manually by the calling thread
        ESP_LOGI(TAG, "Thread %s: stack %zu KB",
                 names_[i], stackSizes_[i] / 1024);
    }
}

} // namespace ecotiter::diag
