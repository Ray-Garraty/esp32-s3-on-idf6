#include <cstdio>
#include <cstdint>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "diag/black_box.hpp"

extern "C" void __wrap_esp_panic_handler(void* info) noexcept {

    __asm__ volatile("rsil a0, 3");

    printf("\n=== CRASH ===\n");
    printf("Black box dump:\n");
    ecotiter::diag::BlackBox::instance().dump();

    printf("=== STACK ===\n");
    UBaseType_t wm = uxTaskGetStackHighWaterMark(nullptr);
    printf("current watermark=%u\n", static_cast<unsigned>(wm));

    printf("!!! EXCEPTION END !!!\n");

    extern void __real_esp_panic_handler(void*);
    __real_esp_panic_handler(info);
}
