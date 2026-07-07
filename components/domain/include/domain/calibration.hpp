#pragma once

#include <cstdint>
#include <expected>
#include <vector>
#include "domain/types.hpp"

namespace ecotiter::domain {

struct RampConfig {
    uint32_t accelSteps;
    uint32_t decelSteps;
    uint32_t minIntervalUs; // full speed
    uint32_t maxIntervalUs; // start/stop
};

[[nodiscard]] std::vector<uint32_t> computeRamp(
    Steps totalSteps,
    const RampConfig& config);

struct AdcCalibration {
    float a; // slope (mV per ADC count)
    float b; // offset (mV)
};

[[nodiscard]] float adcToMv(uint16_t raw, const AdcCalibration& cal) noexcept;

struct CalibrationData {
    float stepsPerMl;
    float nominalVolumeMl;
};

[[nodiscard]] Steps mlToSteps(Ml volume, const CalibrationData& cal) noexcept;
[[nodiscard]] Ml stepsToMl(Steps steps, const CalibrationData& cal) noexcept;

} // namespace ecotiter::domain
