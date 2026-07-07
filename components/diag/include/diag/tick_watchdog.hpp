#pragma once

#include <cstdint>
#include "esp_timer.h"

namespace ecotiter::diag {

class TickWatchdog {
public:
    inline TickWatchdog() noexcept
        : startUs_(static_cast<uint64_t>(esp_timer_get_time())) {}

    inline ~TickWatchdog() noexcept = default;

    TickWatchdog(const TickWatchdog&) = delete;
    TickWatchdog& operator=(const TickWatchdog&) = delete;

private:
    uint64_t startUs_;
};

} // namespace ecotiter::diag
