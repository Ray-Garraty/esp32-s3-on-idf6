#include "burette_planner.h"
#include <cmath>
#include <cstring>

// ============================================================================
// Helpers
// ============================================================================

uint8_t calc_total_cycles(float volume_ml, float nominal_vol) {
    return (uint8_t)ceilf(volume_ml / nominal_vol);
}

float calc_remaining_vol(float volume_ml, float nominal_vol) {
    float rem = fmodf(volume_ml, nominal_vol);
    if (rem < PLANNER_RESIDUAL_THRESHOLD) rem = nominal_vol;
    return rem;
}

// ============================================================================
// Dose Volume
// ============================================================================

DosePlan plan_dose_volume(float vol_ml, float speed_ml_min,
                           float current_vol_ml, float nominal_vol,
                           bool is_busy) {
    DosePlan plan = {DOSE_REJECT, nullptr, 0, 0, 0};

    if (vol_ml <= 0 || speed_ml_min <= 0) {
        plan.reject_reason = "invalid_params";
        return plan;
    }
    if (vol_ml < PLANNER_MIN_VOLUME_ML || vol_ml > PLANNER_MAX_VOLUME_ML) {
        plan.reject_reason = "volume_out_of_range";
        return plan;
    }
    if (speed_ml_min < PLANNER_MIN_SPEED_ML_MIN ||
        speed_ml_min > PLANNER_MAX_SPEED_ML_MIN) {
        plan.reject_reason = "speed_out_of_range";
        return plan;
    }
    if (is_busy) {
        plan.reject_reason = "burette_busy";
        return plan;
    }

    plan.total_cycles = 1;
    plan.remaining_vol = vol_ml;
    if (vol_ml > nominal_vol + PLANNER_EPSILON_FLOAT) {
        plan.total_cycles = calc_total_cycles(vol_ml, nominal_vol);
        plan.remaining_vol = calc_remaining_vol(vol_ml, nominal_vol);
    }
    plan.first_cycle_vol = (plan.total_cycles == 1) ? vol_ml : nominal_vol;

    if (current_vol_ml < plan.first_cycle_vol) {
        plan.action = DOSE_FILL_FIRST;
    } else {
        plan.action = DOSE_DIRECT;
    }

    return plan;
}

// ============================================================================
// Fill / Empty
// ============================================================================

SimplePlan plan_fill(float speed_ml_min, bool is_busy) {
    if (speed_ml_min <= 0 ||
        speed_ml_min < PLANNER_MIN_SPEED_ML_MIN ||
        speed_ml_min > PLANNER_MAX_SPEED_ML_MIN) {
        return {SIMPLE_REJECT, "invalid_params"};
    }
    if (is_busy) {
        return {SIMPLE_REJECT, "burette_busy"};
    }
    return {SIMPLE_EXECUTE, nullptr};
}

SimplePlan plan_empty(float speed_ml_min, bool is_busy) {
    if (speed_ml_min <= 0 ||
        speed_ml_min < PLANNER_MIN_SPEED_ML_MIN ||
        speed_ml_min > PLANNER_MAX_SPEED_ML_MIN) {
        return {SIMPLE_REJECT, "invalid_params"};
    }
    if (is_busy) {
        return {SIMPLE_REJECT, "burette_busy"};
    }
    return {SIMPLE_EXECUTE, nullptr};
}

// ============================================================================
// Rinse
// ============================================================================

RinsePlan plan_rinse(uint8_t cycles, float speed_ml_min, bool is_busy) {
    if (cycles == 0 || speed_ml_min <= 0) {
        return {SIMPLE_REJECT, "invalid_params", 0};
    }
    if (speed_ml_min < PLANNER_MIN_SPEED_ML_MIN ||
        speed_ml_min > PLANNER_MAX_SPEED_ML_MIN) {
        return {SIMPLE_REJECT, "speed_out_of_range", 0};
    }
    if (is_busy) {
        return {SIMPLE_REJECT, "burette_busy", 0};
    }
    return {SIMPLE_EXECUTE, nullptr, cycles};
}

// ============================================================================
// Cal run
// ============================================================================

CalRunPlan plan_cal_run(const char* mode, float speed_ml_min,
                         uint16_t freq_hz, float max_freq,
                         float speed_coeff, bool is_busy) {
    CalRunPlan plan = {CAL_REJECT, nullptr, 0, 0};

    if (std::strcmp(mode, "dose") != 0 && std::strcmp(mode, "speed") != 0) {
        plan.reject_reason = "invalid_params: mode must be 'dose' or 'speed'";
        return plan;
    }
    if (is_busy) {
        plan.reject_reason = "burette_busy";
        return plan;
    }
    if (std::strcmp(mode, "dose") == 0) {
        if (freq_hz == 0) freq_hz = (uint16_t)(max_freq / 2.0f + 0.5f);
        float cal_speed = (float)freq_hz * speed_coeff;
        if (cal_speed < PLANNER_MIN_SPEED_ML_MIN) cal_speed = 15.0f;
        plan.action = CAL_DOSE;
        plan.freq_hz = freq_hz;
        plan.speed_ml_min = cal_speed;
    } else {
        if (freq_hz == 0) {
            plan.reject_reason = "invalid_params: freq_hz required for speed mode";
            return plan;
        }
        if (speed_ml_min < PLANNER_MIN_SPEED_ML_MIN) {
            speed_ml_min = (max_freq / 2.0f) * speed_coeff;
        }
        plan.action = CAL_SPEED;
        plan.freq_hz = freq_hz;
        plan.speed_ml_min = speed_ml_min;
    }

    return plan;
}

// ============================================================================
// Cal speed seq
// ============================================================================

CalSpeedSeqPlan plan_cal_speed_seq(uint8_t freq_count, float fill_speed_ml_min,
                                    float max_freq, float speed_coeff,
                                    bool is_busy) {
    if (freq_count < 3 || freq_count > 8) {
        return {SIMPLE_REJECT, "invalid_params: freq_count must be 3..8", 0};
    }
    if (is_busy) {
        return {SIMPLE_REJECT, "burette_busy", 0};
    }
    if (fill_speed_ml_min < PLANNER_MIN_SPEED_ML_MIN) {
        fill_speed_ml_min = (max_freq / 2.0f) * speed_coeff;
    }
    return {SIMPLE_EXECUTE, nullptr, fill_speed_ml_min};
}
