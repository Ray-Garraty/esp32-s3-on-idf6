#include "application/send_motor_command.hpp"
#include "infrastructure/motor_task.hpp"

bool ecotiter::application::sendMotorCommand(const domain::MotorCommand& cmd)
{
    if (infrastructure::gMotorCmdQueue == nullptr)
        return false;
    return xQueueSend(infrastructure::gMotorCmdQueue, &cmd, 0) == pdTRUE;
}
