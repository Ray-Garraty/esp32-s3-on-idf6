#include "application/command_dispatch.hpp"
#include "esp_log.h"

static constexpr auto TAG = "command";

namespace ecotiter::application {

domain::Result<void, domain::ProtocolError> CommandDispatch::parse(
    std::string_view json) noexcept {

    // Phase 1: stub — real JSON parsing in later phase
    ESP_LOGI(TAG, "Command received: %.*s",
             static_cast<int>(json.size()), json.data());
    return {};
}

void CommandDispatch::poll() noexcept {
    // Phase 1: no-op
}

} // namespace ecotiter::application
