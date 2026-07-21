#include "infrastructure/drivers/valve.hpp"

#include "domain/types.hpp"

namespace ecotiter::infrastructure::drivers
{

// Stub gValve for host tests — no GPIO dependency
Valve gValve(static_cast<gpio_num_t>(0));

Valve::Valve(gpio_num_t /*pin*/)
    : pin_(static_cast<gpio_num_t>(0)),
      position_(domain::ValvePosition::Input)
{}

void Valve::setPosition(domain::ValvePosition position)
{
    position_ = position;
    domain::gValvePosition.store(position, std::memory_order_release);
}

domain::ValvePosition Valve::getPosition() const noexcept
{
    return position_;
}

} // namespace ecotiter::infrastructure::drivers

// Stub implementations for valve settle timer (host tests)
void ecotiter::infrastructure::drivers::armValveSettleTimer(domain::ValvePosition)
{
    // No-op: timer requires esp_timer which is unavailable in host tests
    domain::gValveIsSettling.store(true, std::memory_order_release);
}

void ecotiter::infrastructure::drivers::cancelValveSettleTimer()
{
    // No-op: timer requires esp_timer which is unavailable in host tests
    domain::gValveIsSettling.store(false, std::memory_order_release);
}
