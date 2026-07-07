#pragma once

#include <cstdint>
#include <expected>
#include "domain/errors.hpp"

namespace ecotiter::interface {

enum class SerialError : uint8_t {
    InitFailed,
    ReadFailed,
    WriteFailed
};

template <typename T>
using Result = std::expected<T, SerialError>;

class SerialPort {
public:
    Result<void> init() noexcept;
    void poll() noexcept;

    [[nodiscard]] bool isInitialized() const noexcept { return initialized_; }

private:
    bool initialized_ = false;
};

} // namespace ecotiter::interface
