#ifndef BURETTE_PLANNER_H
#define BURETTE_PLANNER_H

#include <cstdint>

// ── Validation constants (from legacy constants.js) ──
#define PLANNER_MIN_VOLUME_ML      0.01f
#define PLANNER_MAX_VOLUME_ML      50.0f
#define PLANNER_MIN_SPEED_ML_MIN   0.1f
#define PLANNER_MAX_SPEED_ML_MIN   20.0f
#define PLANNER_EPSILON_FLOAT      0.001f
#define PLANNER_RESIDUAL_THRESHOLD 0.01f

// ── Dose Volume ──
enum DoseAction {
    DOSE_REJECT,
    DOSE_FILL_FIRST,
    DOSE_DIRECT
};

struct DosePlan {
    DoseAction  action;
    const char* reject_reason;
    float       first_cycle_vol;
    uint8_t     total_cycles;
    float       remaining_vol;
};

DosePlan plan_dose_volume(
    float   vol_ml,
    float   speed_ml_min,
    float   current_vol_ml,
    float   nominal_vol,
    bool    is_busy
);

// ── Fill / Empty ──
enum SimpleAction {
    SIMPLE_REJECT,
    SIMPLE_EXECUTE
};

struct SimplePlan {
    SimpleAction action;
    const char*  reject_reason;
};

SimplePlan plan_fill(float speed_ml_min, bool is_busy);
SimplePlan plan_empty(float speed_ml_min, bool is_busy);

// ── Rinse ──
struct RinsePlan {
    SimpleAction action;
    const char*  reject_reason;
    uint8_t      cycles;
};

RinsePlan plan_rinse(uint8_t cycles, float speed_ml_min, bool is_busy);

// ── Cal run ──
enum CalAction {
    CAL_REJECT,
    CAL_DOSE,
    CAL_SPEED
};

struct CalRunPlan {
    CalAction   action;
    const char* reject_reason;
    uint16_t    freq_hz;
    float       speed_ml_min;
};

CalRunPlan plan_cal_run(
    const char* mode,
    float       speed_ml_min,
    uint16_t    freq_hz,
    float       max_freq,
    float       speed_coeff,
    bool        is_busy
);

// ── Cal speed seq ──
struct CalSpeedSeqPlan {
    SimpleAction action;
    const char*  reject_reason;
    float        fill_speed_ml_min;
};

CalSpeedSeqPlan plan_cal_speed_seq(
    uint8_t     freq_count,
    float       fill_speed_ml_min,
    float       max_freq,
    float       speed_coeff,
    bool        is_busy
);

// ── Helpers (extracted from burette_ops.cpp static) ──
uint8_t calc_total_cycles(float volume_ml, float nominal_vol);
float   calc_remaining_vol(float volume_ml, float nominal_vol);

#endif
