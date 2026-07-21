#include "application/handlers/valve.hpp"

#include <cstdio>

#include "application/command.hpp"
#include "domain/types.hpp"
#include "infrastructure/drivers/valve.hpp"

namespace ecotiter::application::handlers::valve
{
using domain::CommandResponse;
using domain::ResponseKind;

std::expected<CommandResponse, domain::AppError>
handleSetPosition(std::optional<domain::ValvePosition> pos)
{
    if (!pos)
    {
        return makeErrorResponse("invalid_params");
    }

    // Mutual exclusion: burette must be idle AND no concurrent settle
    // TOCTOU: gBuretteState load and gValveIsSettling CAS are not atomic.
    //         A concurrent burette op may CAS-succeed on gBuretteState
    //         between these two checks. HW is safe (motor task sets correct
    //         valve), but the valve_settled WS event 500ms later may show
    //         a stale position. Next broadcast corrects it.
    if (domain::gBuretteState.load(std::memory_order_acquire) != domain::BuretteState::Idle)
    {
        return makeErrorResponse("burette_busy");
    }
    bool expected = false;
    if (!domain::gValveIsSettling.compare_exchange_strong(expected, true, std::memory_order_acq_rel,
                                                          std::memory_order_acquire))
    {
        return makeErrorResponse("burette_busy");
    }

    // GPIO write — immediate
    infrastructure::drivers::gValve.setPosition(*pos);
    domain::gValvePosition.store(*pos, std::memory_order_release);

    // Arm settle timer (fires after VALVE_SETTLE_MS, clears gValveIsSettling)
    infrastructure::drivers::armValveSettleTimer(*pos);

    const char* posStr = (*pos == domain::ValvePosition::Input) ? "input" : "output";
    CommandResponse rsp;
    rsp.kind = ResponseKind::Single;
    rsp.bodySize = static_cast<size_t>(std::snprintf(
        rsp.body.data(), rsp.body.size(), R"({"status":"ok","data":{"position":"%s"}})", posStr));
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
