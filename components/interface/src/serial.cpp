#include "interface/serial.hpp"
#include "esp_log.h"

static constexpr auto TAG = "serial";

namespace ecotiter::interface {

Result<void> SerialPort::init() noexcept {
    // Phase 1: stub — real UART init in later phase
    ESP_LOGI(TAG, "Serial port init placeholder");
    initialized_ = true;
    return {};
}

void SerialPort::poll() noexcept {
    // Phase 1: no-op
}

} // namespace ecotiter::interface
