#pragma once

#include "domain/burette.hpp"
#include "domain/errors.hpp"

namespace ecotiter::application {

// Phase 1: stub
class ApplicationStateMachine {
public:
    domain::Result<void, domain::StateError> handleCommand(
        domain::BuretteCommand cmd) noexcept;

    [[nodiscard]] domain::BuretteState currentState() const noexcept {
        return controller_.state;
    }

private:
    domain::BuretteController controller_;
};

} // namespace ecotiter::application
