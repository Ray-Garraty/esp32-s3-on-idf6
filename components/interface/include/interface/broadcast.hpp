#pragma once

#include <cstdint>
#include <string_view>

#include "domain/memory.hpp"
#include "domain/types.hpp"

namespace ecotiter::interface {

struct BroadcastEvent {
    uint32_t tick;
    int32_t tempCX100;
    uint16_t mv;
    uint16_t electrodeMv;
    domain::ValvePosition vlv;
    domain::BuretteState brt;
    float volumeMl;
    float speedMlMin;
    bool limitFull;
    bool limitEmpty;
    bool usbSerialConnected;
    bool bleConnected;
    bool stepperDrvConnected;
    bool stepperDrvOtpw;
    bool stepperDrvOt;
    uint8_t stallGuardValue;
    bool isStalled;
    uint8_t stallGuardThreshold;
    bool motorIsMoving;
    uint32_t stepsTaken;
};

// Serialize BroadcastEvent to a pre-allocated JSON buffer.
// Returns a string_view into buf, or empty view on truncation.
[[nodiscard]] std::string_view serializeBroadcast(
    const BroadcastEvent& evt,
    domain::memory::ResponseBuffer& buf);

} // namespace ecotiter::interface
