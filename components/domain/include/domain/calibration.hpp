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

[[nodiscard]] inline float adcToMv(uint16_t raw, const AdcCalibration& cal) noexcept {
    return static_cast<float>(raw) * cal.a + cal.b;
}

struct CalibrationData {
    float stepsPerMl;
    float nominalVolumeMl;
    float speedCoeff;
    uint16_t minFreqHz;
    uint16_t maxFreqHz;

    static constexpr inline float kDefaultStepsPerMl = 7730.0f;
    static constexpr inline float kDefaultNominalVolumeMl = 8.14f;
    static constexpr inline float kDefaultSpeedCoeff = 0.03052f;
    static constexpr inline uint16_t kDefaultMinFreqHz = 30;
    static constexpr inline uint16_t kDefaultMaxFreqHz = 3000;
};

[[nodiscard]] inline Steps mlToSteps(Ml volume, const CalibrationData& cal) noexcept {
    return Steps{static_cast<int32_t>(volume.value * cal.stepsPerMl)};
}
[[nodiscard]] inline Ml stepsToMl(Steps steps, const CalibrationData& cal) noexcept {
    return Ml{static_cast<float>(steps.value) / cal.stepsPerMl};
}

} // namespace ecotiter::domain
