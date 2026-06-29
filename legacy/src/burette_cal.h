#ifndef BURETTE_CAL_H
#define BURETTE_CAL_H

#include <Arduino.h>

#define BURETTE_CAL_NVS_NS "burette_cal"
#define BURETTE_CAL_NVS_KEY_SPS "steps_per_ml"
#define BURETTE_CAL_NVS_KEY_NOM "nominal_vol"
#define BURETTE_CAL_NVS_KEY_SPD "speed_coeff"
#define BURETTE_CAL_NVS_KEY_MNF "min_freq"
#define BURETTE_CAL_NVS_KEY_MXF "max_freq"

#define BURETTE_CAL_NVS_KEY_DAT "cal_date"

#define BURETTE_CAL_DEFAULT_SPS 7730.0f
#define BURETTE_CAL_DEFAULT_NOM 8.14f
#define BURETTE_CAL_DEFAULT_SPD 0.03052f
#define BURETTE_CAL_DEFAULT_MNF 30
#define BURETTE_CAL_DEFAULT_MXF 3000
#define BURETTE_CAL_DEFAULT_DAT 0

#define CAL_Z_TABLE_ROWS 31
#define CAL_Z_TABLE_COLS 6

/** @brief Runtime cache of all calibration coefficients loaded from NVS */
struct CalibrationConfig {
    float steps_per_ml;
    float nominal_vol;
    float speed_coeff;
    uint16_t min_freq;
    uint16_t max_freq;
    time_t calibration_date;  // unix timestamp of last save, 0 = never
};

/** @brief Load calibration from NVS namespace "burette_cal" into cfg */
void burette_cal_load(CalibrationConfig& cfg);

/** @brief Persist cfg to NVS */
void burette_cal_save(const CalibrationConfig& cfg);

/** @brief Remove NVS keys and reset cfg to factory defaults */
void burette_cal_reset(CalibrationConfig& cfg);

/** @brief True if cfg equals compile-time defaults */
bool burette_cal_is_default(const CalibrationConfig& cfg);

/** @brief Convert ml → motor steps (rounded), returns 0 for negative input */
int32_t volume_to_steps(float volume_ml, float steps_per_ml);

/** @brief Convert motor steps → ml, clamped to [0, nominal_vol] */
float steps_to_volume(int32_t steps, int32_t base_steps, float steps_per_ml, float nominal_vol);

/** @brief Convert ml/min → Hz, clamped to [min_freq, max_freq] */
uint16_t speed_to_frequency(float speed_ml_min, float coeff, uint16_t min_freq, uint16_t max_freq);

/** @brief Convert Hz → ml/min: freq * coeff */
float frequency_to_speed(uint16_t freq_hz, float coeff);

/** @brief Bilinear-interpolated Z-factor from 31×6 table (15–30°C, 80–106.7 kPa, ISO 8655) */
float get_z_factor(float temperature, float pressure);

/** @brief New steps_per_ml from gravimetric result */
float calculate_new_steps_per_ml(float current_s_p_ml, float target_vol_ml, float actual_vol_ml);

/** @brief Result of OLS speed calibration */
struct SpeedCalResult {
    float k;
    float r_squared;
};

/** @brief OLS regression k = Σ(f·v) / Σ(f²), needs ≥2 points */
SpeedCalResult calculate_speed_calibration(const float* frequencies, const float* speeds, uint8_t count);

/** @brief Thread-safe setter for pending config (uses internal portMUX) */
void burette_cal_set_pending(const CalibrationConfig& cfg);

/** @brief Thread-safe getter: returns a copy of pending config */
CalibrationConfig burette_cal_get_pending_copy(void);

#endif
