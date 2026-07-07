#include <cstdio>
#include <cstdint>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "diag/black_box.hpp"

extern "C" [[gnu::section(".iram1")]] void __wrap_esp_panic_handler(
    void* info) noexcept {

    __asm__ volatile("rsil a0, 3");

    printf("\n=== CRASH ===\n");
    printf("Black box dump:\n");
    ecotiter::diag::BlackBox::instance().dump();

    printf("=== STACK ===\n");
    for (int i = 0; i < configNUM_CORES; ++i) {
        TaskHandle_t task = xTaskGetHandle(nullptr);
        if (task) {
            UBaseType_t watermark = uxTaskGetStackHighWaterMark(task);
            printf("t%d current watermark=%u\n", i,
                   static_cast<unsigned>(watermark));
        }
    }

    printf("!!! EXCEPTION END !!!\n");

    extern void __real_esp_panic_handler(void*);
    __real_esp_panic_handler(info);
}
