#pragma once

#include <expected>
#include <string_view>

#include "domain/command_types.hpp"

namespace ecotiter::application
{

// Deserialize a JSON string into a Command
[[nodiscard]] std::expected<domain::Command, domain::ProtocolError> parseCommand(std::string_view json);

// Serialize a single JSON value into the response buffer
[[nodiscard]] std::expected<size_t, domain::ProtocolError>
serializeToBuffer(const domain::CommandResponse& rsp, domain::memory::ResponseBuffer& buf);

// Convenience: build a single-value response from a json-like payload
domain::CommandResponse makeAckThenResponse();
domain::CommandResponse makeErrorResponse(std::string_view message);
domain::CommandResponse makeSingleResponse(std::string_view payload, size_t size);

void serializeStatusJson(domain::memory::ResponseBuffer& buf, size_t& offset,
                         domain::BuretteState state, int32_t tempCX100,
                         domain::ValvePosition valvePos, float mv, domain::Direction dir,
                         uint32_t speed, uint32_t accel, float volumeMl, bool volumeIsNull = false);
domain::CommandResponse makeStatusResponse(uint64_t id, domain::BuretteState state, int32_t tempCX100,
                                           domain::ValvePosition valvePos, float mv, domain::Direction dir,
                                           uint32_t speed, uint32_t accel, float volumeMl,
                                           bool volumeIsNull = false);

} // namespace ecotiter::application
