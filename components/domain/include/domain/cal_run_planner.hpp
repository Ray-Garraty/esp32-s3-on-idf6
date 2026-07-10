#pragma once

#include <cstdint>
#include <cstring>
#include "domain/types.hpp"
#include "domain/calibration.hpp"

namespace ecotiter::domain::sm {

enum class CalRunAction { Reject, CalDose, CalSpeed };
enum class CalRunRejectReason { None, InvalidMode, BuretteBusy };

struct CalRunPlan {
    CalRunAction action;
    CalRunRejectReason rejectReason;
    uint16_t freqHz;
    float speedMlMin;
};

inline CalRunPlan planCalRun(
    const char* mode,
    float speedMlMin,
    uint16_t freqHz,
    float maxFreqHz,
    float speedCoeff,
    bool isBusy
) noexcept {
    if (mode == nullptr) {
        return {CalRunAction::Reject, CalRunRejectReason::InvalidMode, 0, 0.0f};
    }
    if (std::strcmp(mode, "dose") != 0 && std::strcmp(mode, "speed") != 0) {
        return {CalRunAction::Reject, CalRunRejectReason::InvalidMode, 0, 0.0f};
    }
    if (isBusy) {
        return {CalRunAction::Reject, CalRunRejectReason::BuretteBusy, 0, 0.0f};
    }

    if (std::strcmp(mode, "dose") == 0) {
        uint16_t calFreq = (freqHz == 0)
            ? static_cast<uint16_t>(maxFreqHz / 2.0f + 0.5f)
            : freqHz;
        float calSpeed = static_cast<float>(calFreq) * speedCoeff;
        if (calSpeed < 15.0f) {
            calSpeed = 15.0f;
        }
        return {CalRunAction::CalDose, CalRunRejectReason::None, calFreq, calSpeed};
    }

    if (freqHz == 0) {
        return {CalRunAction::Reject, CalRunRejectReason::InvalidMode, 0, 0.0f};
    }
    float fillSpeed = (speedMlMin < 15.0f)
        ? (maxFreqHz / 2.0f) * speedCoeff
        : speedMlMin;
    return {CalRunAction::CalSpeed, CalRunRejectReason::None, freqHz, fillSpeed};
}

} // namespace ecotiter::domain::sm
