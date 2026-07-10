#include "infrastructure/motor_task.hpp"

QueueHandle_t ecotiter::infrastructure::gMotorCmdQueue = nullptr;
ecotiter::infrastructure::SmResult ecotiter::infrastructure::gSmResult{
    ecotiter::infrastructure::SmResult::Type::None, 0, 0.0f, {}, 0};
