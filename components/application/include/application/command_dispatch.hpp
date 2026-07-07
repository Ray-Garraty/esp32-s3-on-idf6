#pragma once

#include <string_view>
#include "domain/burette.hpp"
#include "domain/errors.hpp"

namespace ecotiter::application {

// Phase 1: stub — full implementation in later phase
class CommandDispatch {
public:
    domain::Result<void, domain::ProtocolError> parse(
        std::string_view json) noexcept;

    void poll() noexcept;
};

} // namespace ecotiter::application
