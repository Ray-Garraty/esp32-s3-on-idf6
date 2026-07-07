#pragma once

#include <cstdint>

namespace ecotiter::diag {

// RAII guard for ESP-IDF C API boundaries.
// Records FfiEnter in constructor, FfiExit in destructor.
// Must wrap every ESP-IDF C API call (GR-7).
class FfiGuard {
public:
    explicit FfiGuard(uint16_t boundaryId) noexcept;
    ~FfiGuard() noexcept;

    FfiGuard(const FfiGuard&) = delete;
    FfiGuard& operator=(const FfiGuard&) = delete;

private:
    uint16_t boundaryId_;
    bool exited_ = false;
};

} // namespace ecotiter::diag
