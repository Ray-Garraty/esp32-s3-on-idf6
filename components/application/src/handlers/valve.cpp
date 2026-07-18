#include "application/handlers/valve.hpp"

#include <cstdio>
#include <cstring>

#include "application/command.hpp"
#include "application/send_motor_command.hpp"
#include "domain/memory.hpp"
#include "domain/motor_command.hpp"
#include "domain/types.hpp"
#include "infrastructure/config.hpp"
#include "infrastructure/drivers/valve.hpp"

namespace ecotiter::application::handlers::valve
{

std::expected<CommandResponse, domain::AppError>
handleSetPosition(std::optional<domain::ValvePosition> pos)
{
    if (!pos)
    {
        return makeErrorResponse("invalid_params");
    }

    // Queue settle + WS broadcast to motor task (non-blocking)
    domain::MotorCommand cmd{};
    cmd.type = domain::MotorCommandType::SetValvePosition;
    cmd.valvePosition = *pos;
    if (!application::sendMotorCommand(cmd))
    {
        return makeErrorResponse("busy");
    }

    // Set valve position immediately (fast GPIO write — no delay)
    infrastructure::drivers::gValve.setPosition(*pos);
    domain::gValvePosition.store(*pos, std::memory_order_release);

    const char* posStr = (*pos == domain::ValvePosition::Input) ? "input" : "output";
    CommandResponse rsp;
    rsp.kind = ResponseKind::Single;
    rsp.bodySize = static_cast<size_t>(std::snprintf(
        rsp.body.data(), rsp.body.size(),
        R"({"status":"ok","data":{"position":"%s"}})", posStr));
    return rsp;
}

std::expected<CommandResponse, domain::AppError> handleGetState(domain::ValvePosition currentPos)
{
    const char* posStr = (currentPos == domain::ValvePosition::Input) ? "input" : "output";
    CommandResponse rsp;
    rsp.kind = ResponseKind::Single;
    rsp.bodySize = static_cast<size_t>(std::snprintf(
        rsp.body.data(), rsp.body.size(), R"({"status":"ok","data":{"position":"%s"}})", posStr));
    return rsp;
}

} // namespace ecotiter::application::handlers::valve
