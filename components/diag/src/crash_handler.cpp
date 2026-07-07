#include <cstdio>
#include <cstdint>
#include "esp_private/panic_reason.h"
#include "soc/soc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "diag/black_box.hpp"

// CONTRACT: This function runs in panic handler context.
// No heap, no mutex, no blocking calls. Must be in IRAM.
// Wraps esp_panic_handler via linker --wrap to dump black box before default handler.
extern "C" [[gnu::section(".iram1")]] void __wrap_esp_panic_handler(
    void* info) noexcept {

    // Disable interrupts to prevent nested crashes
    __asm__ volatile("rsil a0, 3");

    printf("\n=== CRASH ===\n");
    printf("Black box dump:\n");
    ecotiter::diag::BlackBox::instance().dump();

    printf("=== STACK ===\n");
    // CONTRACT: uxTaskGetStackHighWaterMark can be called from panic context
    // on FreeRTOS if the scheduler is still minimally functional.
    for (int i = 0; i < configNUM_CORES; ++i) {
        TaskHandle_t task = xTaskGetHandle(nullptr); // current task
        if (task) {
            UBaseType_t watermark = uxTaskGetStackHighWaterMark(task);
            printf("t%d current watermark=%u\n", i,
                   static_cast<unsigned>(watermark));
        }
    }

    printf("!!! EXCEPTION END !!!\n");

    // Call the real esp_panic_handler
    extern void __real_esp_panic_handler(void*);
    __real_esp_panic_handler(info);
}
