#include "infrastructure/motor_task.hpp"
#include "infrastructure/drivers/tmc_uart.hpp"

QueueHandle_t ecotiter::infrastructure::gMotorCmdQueue = nullptr;
ecotiter::infrastructure::SmResult ecotiter::infrastructure::gSmResult{
    ecotiter::infrastructure::SmResult::Type::None, 0, 0.0f, {}, 0};
ecotiter::infrastructure::drivers::TmcUart ecotiter::infrastructure::gTmcUart;
