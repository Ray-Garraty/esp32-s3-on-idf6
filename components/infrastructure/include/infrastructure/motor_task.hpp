#pragma once

#include <cstdint>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "domain/types.hpp"

extern "C" void motorTaskEntry(void* pvParameters);

namespace ecotiter::infrastructure {

enum class MotorCommandType : uint8_t {
    MoveSteps,
    Stop,
    EmergencyStop,
    Home,
    SetDirection,
    SetSpeed,
    SetAccel
};

struct MotorCommand {
    MotorCommandType type;
    int32_t steps;
    domain::Direction direction;
    uint32_t speedHz;
    uint32_t accelHzPerS;
};

extern QueueHandle_t gMotorCmdQueue;

} // namespace ecotiter::infrastructure
