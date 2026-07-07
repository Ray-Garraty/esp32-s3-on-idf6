#pragma once

#include <cstdint>
#include "domain/types.hpp"
#include "domain/errors.hpp"

namespace ecotiter::domain {

enum class BuretteState : uint8_t {
    Idle,
    Homing,
    Filling,
    Emptying,
    Dosing,
    Rinsing,
    Stopping,
    Error
};

enum class BuretteCommand : uint8_t {
    Fill,
    Dose,
    Empty,
    Rinse,
    Stop,
    EmergencyStop,
    Reset
};

struct BuretteController {
    BuretteState state = BuretteState::Idle;

    [[nodiscard]] Result<void, StateError> validateTransition(
        BuretteCommand cmd) const noexcept;
    [[nodiscard]] Result<void, StateError> transition(BuretteCommand cmd) noexcept;
};

} // namespace ecotiter::domain
