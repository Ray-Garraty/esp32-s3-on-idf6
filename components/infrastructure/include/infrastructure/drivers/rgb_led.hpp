#pragma once

#include <cstdint>

#include "driver/gpio.h"

#include "domain/types.hpp"

namespace ecotiter::infrastructure::drivers {

class RgbLed {
public:
    explicit RgbLed(gpio_num_t pin);
    ~RgbLed();

    RgbLed(const RgbLed&) = delete;
    RgbLed& operator=(const RgbLed&) = delete;
    RgbLed(RgbLed&&) = delete;
    RgbLed& operator=(RgbLed&&) = delete;

    void setColor(uint8_t r, uint8_t g, uint8_t b);
    void refresh();

    void setTransportMode(domain::TransportMode mode, bool error = false);

private:
    gpio_num_t pin_;
    uint8_t r_{0};
    uint8_t g_{0};
    uint8_t b_{0};
    void* channel_{nullptr};
    void* encoder_{nullptr};
    bool initialized_{false};
};

} // namespace ecotiter::infrastructure::drivers
