#include "domain/calibration.hpp"
#include "domain/errors.hpp"

namespace ecotiter::infrastructure::storage {

domain::Result<domain::CalibrationData, domain::ResourceError> calibrationRead() {
    domain::CalibrationData cal{};
    cal.stepsPerMl = domain::CalibrationData::kDefaultStepsPerMl;
    cal.nominalVolumeMl = domain::CalibrationData::kDefaultNominalVolumeMl;
    cal.speedCoeff = domain::CalibrationData::kDefaultSpeedCoeff;
    cal.minFreqHz = domain::CalibrationData::kDefaultMinFreqHz;
    cal.maxFreqHz = domain::CalibrationData::kDefaultMaxFreqHz;
    return cal;
}

domain::Result<void, domain::ResourceError> calibrationWrite(const domain::CalibrationData&) {
    return {};
}

} // namespace ecotiter::infrastructure::storage
