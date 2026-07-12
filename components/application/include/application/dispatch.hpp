#pragma once

#include <expected>

#include "application/command.hpp"
#include "domain/errors.hpp"

namespace ecotiter::application {

// Central dispatch: routes a parsed Command to the correct handler module.
// Returns a CommandResponse that the caller sends back over the transport.
[[nodiscard]] std::expected<CommandResponse, domain::AppError> dispatch(
    const Command& cmd);

// Set callback for ADC sample reads (platform-specific — called from main.cpp)
void setAdcSampleReadCb(uint16_t (*cb)());

} // namespace ecotiter::application
