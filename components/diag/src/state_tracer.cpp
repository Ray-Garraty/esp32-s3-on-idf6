#include "diag/state_tracer.hpp"
#include "esp_log.h"

static constexpr auto TAG = "state";

namespace ecotiter::diag {

void StateTracer::logBuretteTransition(
    std::string_view from,
    std::string_view to) noexcept {
    ESP_LOGI(TAG, "Burette: %.*s -> %.*s",
             static_cast<int>(from.size()), from.data(),
             static_cast<int>(to.size()), to.data());
}

void StateTracer::logTransportTransition(
    std::string_view from,
    std::string_view to) noexcept {
    ESP_LOGI(TAG, "Transport: %.*s -> %.*s",
             static_cast<int>(from.size()), from.data(),
             static_cast<int>(to.size()), to.data());
}

void StateTracer::logError(std::string_view source,
                           std::string_view message) noexcept {
    ESP_LOGE(TAG, "Error [%.*s]: %.*s",
             static_cast<int>(source.size()), source.data(),
             static_cast<int>(message.size()), message.data());
}

} // namespace ecotiter::diag
