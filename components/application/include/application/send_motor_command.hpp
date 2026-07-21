#pragma once

#include "domain/motor_command.hpp"

namespace ecotiter::application
{

bool sendMotorCommand(const domain::MotorCommand& cmd);

} // namespace ecotiter::application
