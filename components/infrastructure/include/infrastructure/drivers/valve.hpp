#pragma once

#include <cstdint>

#include "driver/gpio.h"

#include "domain/types.hpp"

namespace ecotiter::infrastructure::drivers
{

class Valve
{
public:
    explicit Valve(gpio_num_t pin);

    Valve(const Valve&) = delete;
    Valve& operator=(const Valve&) = delete;

    void setPosition(domain::ValvePosition position);
    [[nodiscard]] domain::ValvePosition getPosition() const noexcept;

private:
    gpio_num_t pin_;
    domain::ValvePosition position_{domain::ValvePosition::Input};
};

extern Valve gValve;

// Valve settle timer — non-blocking esp_timer one-shot
// Callable from any layer; implemented in infrastructure
void armValveSettleTimer(domain::ValvePosition pos);
void cancelValveSettleTimer();

} // namespace ecotiter::infrastructure::drivers
