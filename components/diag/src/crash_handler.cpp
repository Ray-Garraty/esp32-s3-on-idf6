#include <cstdint>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "hal/uart_hal.h"
#include "diag/black_box.hpp"

// Direct UART HAL output — no stdio/VFS dependency
// Safe even when the UART driver or VFS layer is corrupted (LL-026).
namespace {

uart_hal_context_t s_panic_uart = { .dev = &UART0 };

void panic_putc(char c) {
    uint32_t sz = 0;
    while (uart_hal_get_txfifo_len(&s_panic_uart) == 0) { }
    uart_hal_write_txfifo(&s_panic_uart, reinterpret_cast<const uint8_t*>(&c), 1, &sz);
}

void panic_puts(const char* s) {
    for (; *s; ++s) {
        if (*s == '\n') {
            panic_putc('\r');
        }
        panic_putc(*s);
    }
}

// Minimal unsigned integer printer to a static buffer
void panic_print_uint(unsigned long val) {
    char buf[16];
    char* p = buf + sizeof(buf);
    *--p = '\0';
    if (val == 0) {
        *--p = '0';
    } else {
        while (val > 0) {
            *--p = '0' + (val % 10);
            val /= 10;
        }
    }
    panic_puts(p);
}

} // anonymous namespace

extern "C" void __wrap_esp_panic_handler(void* info) noexcept {

    __asm__ volatile("rsil a0, 3");

    panic_puts("\n=== CRASH ===\n");
    panic_puts("Black box dump:\n");
    ecotiter::diag::BlackBox::instance().dump(panic_puts);

    panic_puts("=== STACK ===\n");
    UBaseType_t wm = uxTaskGetStackHighWaterMark(nullptr);
    panic_puts("current watermark=");
    panic_print_uint(static_cast<unsigned long>(wm));
    panic_puts("\n");

    panic_puts("!!! EXCEPTION END !!!\n");

    extern void __real_esp_panic_handler(void*);
    __real_esp_panic_handler(info);
}
