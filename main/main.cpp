#include <cstdio>
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "infrastructure/config.hpp"
#include "diag/black_box.hpp"
#include "diag/stack_monitor.hpp"
#include "diag/tick_watchdog.hpp"

static constexpr auto TAG = "main";

namespace {

// GR-6: stack budget enforcement
bool initNvs() {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    return ret == ESP_OK;
}

// GR-3: init order triangle — WiFi -> HTTP -> BLE
// Phase 1: stub — real implementation in infrastructure/network
bool initNetworkStack() {
    ESP_LOGI(TAG, "Network stack init placeholder");
    return true;
}

} // namespace

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "=== ecotiter C++ firmware boot ===");
    ESP_LOGI(TAG, "IDF version: %s", esp_get_idf_version());

    if (!initNvs()) {
        ESP_LOGE(TAG, "NVS init failed — aborting");
        return;
    }

    // GR-7: Diagnostic subsystem FIRST — before anything else
    ecotiter::diag::BlackBox::instance().init();
    ecotiter::diag::StackMonitor::instance().registerMainTask();

    // Network stack (GR-3 init order enforced)
    if (!initNetworkStack()) {
        ESP_LOGE(TAG, "Network stack init failed");
    }

    // GR-1: main loop MUST NOT block
    // GR-6: main task stack = 32 KB (CONFIG_ESP_MAIN_TASK_STACK_SIZE)
    TickType_t last_wake = xTaskGetTickCount();
    constexpr TickType_t PACING_TICK = pdMS_TO_TICKS(10);

    while (true) {
        ecotiter::diag::TickWatchdog watchdog; // GR-7 instrumentation

        // Non-blocking polling only (atomics, try_lock)
        // Phase 1: empty — real dispatch in application/

        vTaskDelayUntil(&last_wake, PACING_TICK);
    }
}
