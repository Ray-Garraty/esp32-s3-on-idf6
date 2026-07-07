#pragma once

#include <cstdint>

namespace ecotiter::diag {

// RAII watchdog for main loop iterations.
// Records TickBegin in constructor, TickEnd in destructor.
// If iteration exceeds threshold, logs a warning.
class TickWatchdog {
public:
    TickWatchdog() noexcept;
    ~TickWatchdog() noexcept;

    TickWatchdog(const TickWatchdog&) = delete;
    TickWatchdog& operator=(const TickWatchdog&) = delete;

private:
    uint64_t startUs_;
};

} // namespace ecotiter::diag
