#include "burette_cal.h"
#include "logger.h"
#include <Preferences.h>
#include <cmath>

static Preferences prefs;
static CalibrationConfig s_pending_cal;
static portMUX_TYPE cal_mux = portMUX_INITIALIZER_UNLOCKED;

// ── Z-factor table from ISO 8655 (31 temp × 6 pressure)
// Оригинал: legacy/backend_nodejs/src/calibration/stepsToVolume/calibrationService.js
static const float Z_TABLE[CAL_Z_TABLE_ROWS][CAL_Z_TABLE_COLS] = {
    {1.0018f, 1.0018f, 1.0019f, 1.0019f, 1.0020f, 1.0020f},
    {1.0018f, 1.0018f, 1.0019f, 1.0020f, 1.0020f, 1.0021f},
    {1.0019f, 1.0020f, 1.0020f, 1.0021f, 1.0022f, 1.0022f},
    {1.0020f, 1.0020f, 1.0021f, 1.0022f, 1.0022f, 1.0023f},
    {1.0021f, 1.0021f, 1.0022f, 1.0022f, 1.0023f, 1.0023f},
    {1.0022f, 1.0022f, 1.0023f, 1.0024f, 1.0024f, 1.0024f},
    {1.0022f, 1.0023f, 1.0024f, 1.0024f, 1.0025f, 1.0025f},
    {1.0023f, 1.0024f, 1.0025f, 1.0025f, 1.0026f, 1.0026f},
    {1.0024f, 1.0025f, 1.0025f, 1.0026f, 1.0027f, 1.0027f},
    {1.0025f, 1.0026f, 1.0026f, 1.0027f, 1.0028f, 1.0028f},
    {1.0026f, 1.0027f, 1.0027f, 1.0028f, 1.0029f, 1.0029f},
    {1.0027f, 1.0028f, 1.0028f, 1.0029f, 1.0030f, 1.0030f},
    {1.0028f, 1.0029f, 1.0031f, 1.0031f, 1.0032f, 1.0032f},
    {1.0030f, 1.0030f, 1.0032f, 1.0032f, 1.0033f, 1.0033f},
    {1.0031f, 1.0031f, 1.0033f, 1.0033f, 1.0034f, 1.0035f},
    {1.0032f, 1.0032f, 1.0034f, 1.0035f, 1.0035f, 1.0036f},
    {1.0033f, 1.0033f, 1.0035f, 1.0036f, 1.0036f, 1.0037f},
    {1.0034f, 1.0035f, 1.0036f, 1.0037f, 1.0038f, 1.0038f},
    {1.0035f, 1.0036f, 1.0037f, 1.0038f, 1.0039f, 1.0039f},
    {1.0037f, 1.0037f, 1.0038f, 1.0039f, 1.0040f, 1.0041f},
    {1.0038f, 1.0038f, 1.0039f, 1.0040f, 1.0041f, 1.0042f},
    {1.0039f, 1.0040f, 1.0041f, 1.0041f, 1.0042f, 1.0043f},
    {1.0040f, 1.0041f, 1.0042f, 1.0042f, 1.0043f, 1.0045f},
    {1.0042f, 1.0042f, 1.0043f, 1.0044f, 1.0045f, 1.0046f},
    {1.0043f, 1.0044f, 1.0044f, 1.0045f, 1.0048f, 1.0049f},
    {1.0046f, 1.0046f, 1.0047f, 1.0048f, 1.0048f, 1.0049f},
    {1.0046f, 1.0046f, 1.0047f, 1.0048f, 1.0049f, 1.0049f},
    {1.0047f, 1.0048f, 1.0048f, 1.0049f, 1.0050f, 1.0050f},
    {1.0049f, 1.0049f, 1.0050f, 1.0051f, 1.0051f, 1.0052f},
    {1.0050f, 1.0051f, 1.0051f, 1.0052f, 1.0053f, 1.0053f},
    {1.0052f, 1.0052f, 1.0053f, 1.0054f, 1.0054f, 1.0055f},
};

static const float TEMP_VALS[CAL_Z_TABLE_ROWS] = {
    15.0f, 15.5f, 16.0f, 16.5f, 17.0f, 17.5f, 18.0f, 18.5f,
    19.0f, 19.5f, 20.0f, 20.5f, 21.0f, 21.5f, 22.0f, 22.5f,
    23.0f, 23.5f, 24.0f, 24.5f, 25.0f, 25.5f, 26.0f, 26.5f,
    27.0f, 27.5f, 28.0f, 28.5f, 29.0f, 29.5f, 30.0f
};
static const float PRESS_VALS[CAL_Z_TABLE_COLS] = {80.0f, 85.3f, 90.7f, 96.0f, 101.3f, 106.7f};

void burette_cal_load(CalibrationConfig& cfg) {
    prefs.begin(BURETTE_CAL_NVS_NS, true);
    cfg.steps_per_ml = prefs.getFloat(BURETTE_CAL_NVS_KEY_SPS, BURETTE_CAL_DEFAULT_SPS);
    cfg.nominal_vol  = prefs.getFloat(BURETTE_CAL_NVS_KEY_NOM, BURETTE_CAL_DEFAULT_NOM);
    cfg.speed_coeff  = prefs.getFloat(BURETTE_CAL_NVS_KEY_SPD, BURETTE_CAL_DEFAULT_SPD);
    cfg.min_freq     = prefs.getUShort(BURETTE_CAL_NVS_KEY_MNF, BURETTE_CAL_DEFAULT_MNF);
    cfg.max_freq     = prefs.getUShort(BURETTE_CAL_NVS_KEY_MXF, BURETTE_CAL_DEFAULT_MXF);
    cfg.calibration_date = prefs.getLong(BURETTE_CAL_NVS_KEY_DAT, BURETTE_CAL_DEFAULT_DAT);
    prefs.end();
    burette_cal_set_pending(cfg);
    logger.info("Burette cal loaded: spm=%.1f, nom=%.2f, coeff=%.5f, freq=%u-%u",
                cfg.steps_per_ml, cfg.nominal_vol, cfg.speed_coeff,
                cfg.min_freq, cfg.max_freq);
}

void burette_cal_save(const CalibrationConfig& cfg) {
    prefs.begin(BURETTE_CAL_NVS_NS, false);
    prefs.putFloat(BURETTE_CAL_NVS_KEY_SPS, cfg.steps_per_ml);
    prefs.putFloat(BURETTE_CAL_NVS_KEY_NOM, cfg.nominal_vol);
    prefs.putFloat(BURETTE_CAL_NVS_KEY_SPD, cfg.speed_coeff);
    prefs.putUShort(BURETTE_CAL_NVS_KEY_MNF, cfg.min_freq);
    prefs.putUShort(BURETTE_CAL_NVS_KEY_MXF, cfg.max_freq);
    prefs.putLong(BURETTE_CAL_NVS_KEY_DAT, cfg.calibration_date);
    prefs.end();
    logger.info("Burette cal saved to NVS, date=%ld", (long)cfg.calibration_date);
}

void burette_cal_reset(CalibrationConfig& cfg) {
    prefs.begin(BURETTE_CAL_NVS_NS, false);
    prefs.remove(BURETTE_CAL_NVS_KEY_SPS);
    prefs.remove(BURETTE_CAL_NVS_KEY_NOM);
    prefs.remove(BURETTE_CAL_NVS_KEY_SPD);
    prefs.remove(BURETTE_CAL_NVS_KEY_MNF);
    prefs.remove(BURETTE_CAL_NVS_KEY_MXF);
    prefs.remove(BURETTE_CAL_NVS_KEY_DAT);
    prefs.end();
    cfg = {BURETTE_CAL_DEFAULT_SPS, BURETTE_CAL_DEFAULT_NOM,
           BURETTE_CAL_DEFAULT_SPD, BURETTE_CAL_DEFAULT_MNF,
           BURETTE_CAL_DEFAULT_MXF, BURETTE_CAL_DEFAULT_DAT};
    logger.info("Burette cal reset to defaults");
}

bool burette_cal_is_default(const CalibrationConfig& cfg) {
    return (fabs(cfg.steps_per_ml - BURETTE_CAL_DEFAULT_SPS) < 0.01f &&
            fabs(cfg.nominal_vol - BURETTE_CAL_DEFAULT_NOM) < 0.01f &&
            fabs(cfg.speed_coeff - BURETTE_CAL_DEFAULT_SPD) < 0.00001f &&
            cfg.min_freq == BURETTE_CAL_DEFAULT_MNF &&
            cfg.max_freq == BURETTE_CAL_DEFAULT_MXF &&
            cfg.calibration_date == BURETTE_CAL_DEFAULT_DAT);
}

int32_t volume_to_steps(float volume_ml, float steps_per_ml) {
    if (volume_ml < 0) return 0;
    return lroundf(volume_ml * steps_per_ml);
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
float steps_to_volume(int32_t steps, int32_t base_steps, float steps_per_ml, float nominal_vol) {
    if (steps_per_ml < 0.001f) return 0;
    int32_t delta = steps - base_steps;
    float vol = (float)delta / steps_per_ml;
    if (vol < 0) vol = 0;
    if (vol > nominal_vol) vol = nominal_vol;
    return vol;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
uint16_t speed_to_frequency(float speed_ml_min, float coeff, uint16_t min_freq, uint16_t max_freq) {
    if (coeff < 0.000001f) return min_freq;
    uint16_t freq = (uint16_t)lroundf(speed_ml_min / coeff);
    if (freq < min_freq) freq = min_freq;
    if (freq > max_freq) freq = max_freq;
    return freq;
}

float frequency_to_speed(uint16_t freq_hz, float coeff) {
    return (float)freq_hz * coeff;
}

// ── Z-factor bilinear interpolation ──
static float lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
float get_z_factor(float temperature, float pressure) {
    if (temperature < TEMP_VALS[0]) temperature = TEMP_VALS[0];
    if (temperature > TEMP_VALS[CAL_Z_TABLE_ROWS - 1]) temperature = TEMP_VALS[CAL_Z_TABLE_ROWS - 1];
    if (pressure < PRESS_VALS[0]) pressure = PRESS_VALS[0];
    if (pressure > PRESS_VALS[CAL_Z_TABLE_COLS - 1]) pressure = PRESS_VALS[CAL_Z_TABLE_COLS - 1];

    int ti = 0, pi = 0;
    for (int i = 0; i < CAL_Z_TABLE_ROWS - 1; i++) {
        if (temperature >= TEMP_VALS[i]) ti = i;
    }
    for (int i = 0; i < CAL_Z_TABLE_COLS - 1; i++) {
        if (pressure >= PRESS_VALS[i]) pi = i;
    }

    float t_frac = (temperature - TEMP_VALS[ti]) / (TEMP_VALS[ti + 1] - TEMP_VALS[ti]);
    float p_frac = (pressure - PRESS_VALS[pi]) / (PRESS_VALS[pi + 1] - PRESS_VALS[pi]);

    float z_t1 = lerp(Z_TABLE[ti][pi], Z_TABLE[ti][pi + 1], p_frac);
    float z_t2 = lerp(Z_TABLE[ti + 1][pi], Z_TABLE[ti + 1][pi + 1], p_frac);
    return lerp(z_t1, z_t2, t_frac);
}

float calculate_new_steps_per_ml(float current_s_p_ml, float target_vol_ml, float actual_vol_ml) {
    if (actual_vol_ml < 0.0001f) return current_s_p_ml;
    return current_s_p_ml * target_vol_ml / actual_vol_ml;
}

// ── Speed calibration OLS ──
SpeedCalResult calculate_speed_calibration(const float* frequencies, const float* speeds, uint8_t count) {
    SpeedCalResult result = {0, 0};
    if (count < 2) return result;
    float sum_f = 0, sum_v = 0, sum_ff = 0, sum_fv = 0;
    for (uint8_t i = 0; i < count; i++) {
        sum_f += frequencies[i];
        sum_v += speeds[i];
        sum_ff += frequencies[i] * frequencies[i];
        sum_fv += frequencies[i] * speeds[i];
    }
    float denom = sum_ff - sum_f * sum_f / (float)count;
    if (fabs(denom) < 0.000001f) return result;
    result.k = (sum_fv - sum_f * sum_v / (float)count) / denom;
    float mean_v = sum_v / (float)count;
    float ss_res = 0, ss_tot = 0;
    for (uint8_t i = 0; i < count; i++) {
        float pred = result.k * frequencies[i];
        ss_res += (speeds[i] - pred) * (speeds[i] - pred);
        ss_tot += (speeds[i] - mean_v) * (speeds[i] - mean_v);
    }
    if (ss_tot > 0.000001f) {
        result.r_squared = 1.0f - ss_res / ss_tot;
    }
    return result;
}

void burette_cal_set_pending(const CalibrationConfig& cfg) {
    taskENTER_CRITICAL(&cal_mux);
    s_pending_cal = cfg;
    taskEXIT_CRITICAL(&cal_mux);
}

CalibrationConfig burette_cal_get_pending_copy(void) {
    CalibrationConfig copy;
    taskENTER_CRITICAL(&cal_mux);
    copy = s_pending_cal;
    taskEXIT_CRITICAL(&cal_mux);
    return copy;
}
