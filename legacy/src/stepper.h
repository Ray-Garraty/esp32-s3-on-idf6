#ifndef STEPPER_H
#define STEPPER_H

#include <Arduino.h>
#include "burette_cal.h"

enum StepperResult {
    STEPPER_OK = 0,
    STEPPER_ERR_BUSY,
    STEPPER_ERR_MOVE_FAILED,
    STEPPER_ERR_AT_LIMIT_FULL,
    STEPPER_ERR_AT_LIMIT_EMPTY,
};

enum StepperDirection {
    LIQ_IN,
    LIQ_OUT
};

enum RinsePhase {
    RINSE_PRE_FILL,
    RINSE_EMPTYING,
    RINSE_FILLING,
    RINSE_DONE
};

enum TransportSource {
    TRANSPORT_USB = 0,
    TRANSPORT_BLE
};

typedef void (*stepper_result_cb_t)(const char* json_line);

void stepper_set_result_callback(stepper_result_cb_t cb);

enum PendingType {
    PENDING_NONE,
    PENDING_DOSE,
    PENDING_FILL,
    PENDING_EMPTY,
    PENDING_RINSE,
    PENDING_CAL_DOSE,
    PENDING_CAL_SPEED,
    PENDING_CAL_SPEED_SEQ,
    PENDING_AUTO_DOSE
};

enum AutoDosePhase {
    AUTO_DOSE_FILLING,
    AUTO_DOSE_DOSING,
    AUTO_DOSE_DONE
};

/** @brief One pending command slot — two-phase protocol state */
struct PendingCmd {
    uint64_t id;
    uint64_t timestamp_ms;
    bool stop_requested;
    PendingType type;
    float volume_ml;
    float speed_ml_min;
    float actual_dispensed;
    uint8_t rinse_cycle;
    uint8_t rinse_total;
    RinsePhase rinse_phase;
    uint8_t ad_cycle;
    uint8_t ad_total_cycles;
    float ad_remaining_vol;
    float ad_this_cycle_vol;
    AutoDosePhase ad_phase;
    TransportSource transport;
};

/** @brief Snapshot of stepper runtime state for status broadcast */
typedef struct {
    bool busy;
    bool direction_liq_in;
    uint32_t steps_remaining;
    uint32_t steps_total;
    uint16_t frequency;
    bool stall_detected;
    bool en_enabled;
    int32_t base_steps;
    float current_volume_ml;
    float volume_at_start;
    float target_volume_ml;
    float current_speed_ml_min;
    PendingType operation;
} stepper_state_t;

extern PendingCmd g_pending;
extern CalibrationConfig g_burette_cal;
extern portMUX_TYPE g_pending_mux;
extern portMUX_TYPE g_cal_mux;

/** @brief Init FastAccelStepper, set pins/speed/accel */
bool stepper_init(void);

/** @brief Move exact steps in direction at frequency, non-blocking */
StepperResult stepper_move_steps(uint32_t steps, StepperDirection dir, uint16_t frequency);

/** @brief Move until limit switch hit in direction at frequency, non-blocking */
StepperResult stepper_move_to_stop(StepperDirection dir, uint16_t frequency);

/** @brief Soft stop: forceStop + disable driver if idle */
void stepper_stop(void);

/** @brief Hard stop: forceStop + disable driver + set stall flag */
void stepper_emergency_stop(void);

/** @brief Call every loop: handles pending result, stop, rinse SM, limits, watchdog */
void stepper_process(void);

/** @brief Rinse state machine — fill → empty × N, called from stepper_process() */
void stepper_process_rinse(void);

/** @brief Auto-dose state machine — multi-cycle fill+dose, called from stepper_process() */
void stepper_process_auto_dose(void);

/** @brief Fill: valve→input, move to FULL limit */
StepperResult stepper_fill(float speed_ml_min);

/** @brief Empty: valve→output, move to EMPTY limit */
StepperResult stepper_empty(float speed_ml_min);

/** @brief Dose exact volume: valve→output, move steps */
StepperResult stepper_dose_volume(float volume_ml, float speed_ml_min);

/** @brief Init rinse SM, start first move (fill or empty depending on current vol) */
StepperResult stepper_rinse(uint8_t cycles, float speed_ml_min);

/** @brief Copy current state to caller-provided struct */
void stepper_get_state(stepper_state_t* state_ptr);

bool stepper_is_busy(void);
uint32_t stepper_get_actual_steps(void);

void stepper_set_direction(StepperDirection dir);

/** @brief Return current_volume_ml from runtime state */
float stepper_get_volume_ml(void);

/** @brief Return steps_taken from last CAL_DOSE operation */
int32_t stepper_get_cal_steps_taken(void);

/** @brief Return measured speed from last CAL_SPEED operation */
float stepper_get_cal_measured_speed(void);

/** @brief Poll limit switches, stop motor if triggered and moving toward limit */
void stepper_check_limits(void);

/** @brief Clear limit_possible flag after handling */
void stepper_clear_limits_flag(void);

/** @brief Start boot homing: valve→INPUT, move to FULL at 50% max_freq */
void stepper_start_homing(void);

/** @brief true while boot homing sequence is in progress */
bool stepper_is_homing(void);

/** @brief Poll homing state — call from loop() after stepper_check_limits() */
void stepper_process_homing(void);

/** @brief Start 3-point speed calibration sequence */
void stepper_start_cal_speed_seq(const uint16_t* freqs, float fill_speed);

/** @brief Poll speed seq SM — call from stepper_process() */
void stepper_process_cal_speed_seq(void);

/** @brief Get results from completed speed seq; returns count (0 if not done) */
int stepper_get_cal_speed_seq_results(float* out, int max_count);

#endif
