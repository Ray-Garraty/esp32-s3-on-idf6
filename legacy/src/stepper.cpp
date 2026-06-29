#include "stepper.h"
#include "stepper_drv.h"
#include "valve.h"
#include "limitswitch.h"
#include "config.h"
#include "logger.h"
#include <atomic>
#include <FastAccelStepper.h>

static FastAccelStepperEngine engine;
static FastAccelStepper* stepper = nullptr;

static stepper_state_t state = {
    .busy = false,
    .direction_liq_in = true,
    .steps_remaining = 0,
    .steps_total = 0,
    .frequency = 0,
    .stall_detected = false,
    .en_enabled = false,
    .base_steps = 0,
    .current_volume_ml = 0,
    .volume_at_start = 0,
    .target_volume_ml = 0,
    .current_speed_ml_min = 0,
    .operation = PENDING_NONE,
};

static bool move_to_stop_mode = false;
static int32_t movement_start_pos = 0;
static bool just_started = false;
static std::atomic<bool> limit_possible{false};

// Boot homing state
static bool g_homing_active = false;
static uint32_t g_homing_start_ms = 0;
static const uint32_t HOMING_TIMEOUT_MS = 120000;

// Calibration state machine
enum CalPhase { CAL_IDLE, CAL_FILLING, CAL_EMPTYING, CAL_DONE };
static CalPhase s_cal_phase = CAL_IDLE;
static int32_t s_cal_steps_taken = 0;
static float s_cal_measured_speed = 0;
static uint32_t s_cal_start_ms = 0;
static int32_t s_cal_pos_before = 0;

// Speed seq SM
static const float BURETTE_NEAR_FULL_TOLERANCE = 0.1f;
static const float RINSE_START_TOLERANCE = 0.01f;
static const uint32_t HOMING_VALVE_DELAY_MS = 50;
static const float SPEED_SEQ_MULTIPLIER = 0.5f;
static uint16_t s_seq_freqs[CAL_SEQ_POINTS];
static float s_seq_results[CAL_SEQ_POINTS];
static int s_seq_idx;
static uint32_t s_seq_vlv_ms;
static bool s_seq_vlv_wait;
static uint32_t s_seq_elapsed_ms;
static const uint32_t VALVE_SWITCH_MS = 1000;

PendingCmd g_pending = {0, 0, false, PENDING_NONE, 0, 0, 0, 0, 0, RINSE_DONE, 0, 0, 0, 0, AUTO_DOSE_DONE, TRANSPORT_USB};
CalibrationConfig g_burette_cal;
portMUX_TYPE g_pending_mux = portMUX_INITIALIZER_UNLOCKED;
portMUX_TYPE g_cal_mux = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE state_mux = portMUX_INITIALIZER_UNLOCKED;
static stepper_result_cb_t g_result_cb = nullptr;

static CalibrationConfig cal_snapshot(void) {
    CalibrationConfig c;
    taskENTER_CRITICAL(&g_cal_mux);
    c = g_burette_cal;
    taskEXIT_CRITICAL(&g_cal_mux);
    return c;
}

void IRAM_ATTR limit_full_isr() { limit_possible.store(true, std::memory_order_relaxed); }
void IRAM_ATTR limit_empty_isr() { limit_possible.store(true, std::memory_order_relaxed); }

static void disable_if_not_busy() {
    if (!state.busy) {
        digitalWrite(PIN_EN, HIGH);
        taskENTER_CRITICAL(&state_mux);
        state.en_enabled = false;
        taskEXIT_CRITICAL(&state_mux);
    }
}

void stepper_set_result_callback(stepper_result_cb_t cb) {
    g_result_cb = cb;
}

// Forward declarations for calibration state machines
static void stepper_process_cal_dose(void);
static void stepper_process_cal_speed(void);

bool stepper_init(void) {
    engine.init();
    stepper = engine.stepperConnectToPin(PIN_STEP);
    if (!stepper) {
        logger.error("Stepper: FastAccelStepper connect failed");
        return false;
    }
    stepper->setDirectionPin(PIN_DIR);
    stepper->setEnablePin(PIN_EN);
    stepper->setAutoEnable(false);
    stepper->setSpeedInHz(STEPPER_DEFAULT_SPEED);
    stepper->setAcceleration(STEPPER_DEFAULT_ACCEL);
    state.en_enabled = false;
    logger.info("Stepper: initialized, step=GPIO%d, dir=GPIO%d, en=GPIO%d",
                PIN_STEP, PIN_DIR, PIN_EN);
    return true;
}

StepperResult stepper_move_steps(uint32_t steps, StepperDirection dir, uint16_t frequency) {
    if (state.busy) {
        logger.warn("Stepper: already busy");
        return STEPPER_ERR_BUSY;
    }
    
    stepper->setSpeedInHz(frequency);
    stepper->setAcceleration((uint32_t)frequency * STEPPER_ACCEL_MULTIPLIER);
    digitalWrite(PIN_DIR, (dir == LIQ_OUT) ? HIGH : LOW);
    digitalWrite(PIN_EN, LOW);
    taskENTER_CRITICAL(&state_mux);
    state.en_enabled = true;
    taskEXIT_CRITICAL(&state_mux);
    {
        uint32_t _start = millis();
        while (millis() - _start < STEPPER_EN_STABILIZE_MS) { yield(); }
    }
    int32_t pos_before = stepper->getCurrentPosition();
    movement_start_pos = pos_before;
    taskENTER_CRITICAL(&state_mux);
    state.volume_at_start = state.current_volume_ml;
    taskEXIT_CRITICAL(&state_mux);
    int32_t move_delta = (dir == LIQ_OUT) ? (int32_t)steps : -(int32_t)steps;
    MoveResultCode result = stepper->move(move_delta);
    if (result != MOVE_OK) {
        logger.error("Stepper: move() failed code=%d, pos=%ld", (int)result, (long)pos_before);
        taskENTER_CRITICAL(&state_mux);
        state.busy = false;
        taskEXIT_CRITICAL(&state_mux);
        return STEPPER_ERR_MOVE_FAILED;
    }
    taskENTER_CRITICAL(&state_mux);
    state.busy = true;
    state.steps_total = steps;
    state.steps_remaining = steps;
    state.frequency = frequency;
    state.direction_liq_in = (dir == LIQ_IN);
    state.stall_detected = false;
    taskEXIT_CRITICAL(&state_mux);
    move_to_stop_mode = false;
    just_started = true;
    logger.info("Stepper: move %s %u steps @ %u Hz",
                (dir == LIQ_OUT) ? "OUT" : "IN", steps, frequency);
    return STEPPER_OK;
}

StepperResult stepper_move_to_stop(StepperDirection dir, uint16_t frequency) {
    if (state.busy) {
        logger.warn("Stepper: already busy");
        return STEPPER_ERR_BUSY;
    }
    limitswitch_state_t lim = limitswitch_read();
    if (dir == LIQ_OUT && lim.empty) {
        taskENTER_CRITICAL(&state_mux);
        state.current_volume_ml = 0;
        taskEXIT_CRITICAL(&state_mux);
        logger.warn("Already at EMPTY limit! Refusing LIQ_OUT");
        return STEPPER_ERR_AT_LIMIT_EMPTY;
    }
    if (dir == LIQ_IN && lim.full) {
        CalibrationConfig cal = cal_snapshot();
        taskENTER_CRITICAL(&state_mux);
        state.current_volume_ml = cal.nominal_vol;
        taskEXIT_CRITICAL(&state_mux);
        logger.warn("Already at FULL limit! Refusing LIQ_IN");
        return STEPPER_ERR_AT_LIMIT_FULL;
    }
    stepper->setSpeedInHz(frequency);
    stepper->setAcceleration((uint32_t)frequency * STEPPER_ACCEL_MULTIPLIER);
    digitalWrite(PIN_DIR, (dir == LIQ_OUT) ? HIGH : LOW);
    digitalWrite(PIN_EN, LOW);
    taskENTER_CRITICAL(&state_mux);
    state.en_enabled = true;
    taskEXIT_CRITICAL(&state_mux);
    {
        uint32_t _start = millis();
        while (millis() - _start < STEPPER_EN_STABILIZE_MS) { yield(); }
    }
    int32_t pos_before = stepper->getCurrentPosition();
    logger.info("Stepper: moveToStop %s @ %u Hz, pos=%ld",
                (dir == LIQ_OUT) ? "OUT" : "IN", frequency, (long)pos_before);
    taskENTER_CRITICAL(&state_mux);
    state.volume_at_start = state.current_volume_ml;
    state.busy = true;
    state.steps_total = STEPPER_INFINITE_STEPS;
    state.steps_remaining = 0;
    state.frequency = frequency;
    state.direction_liq_in = (dir == LIQ_IN);
    state.stall_detected = false;
    taskEXIT_CRITICAL(&state_mux);
    move_to_stop_mode = true;
    movement_start_pos = stepper->getCurrentPosition();
    MoveResultCode kickstart = stepper->move(1);
    if (kickstart != MOVE_OK) {
        logger.error("Stepper: kickstart failed code=%d", (int)kickstart);
        taskENTER_CRITICAL(&state_mux);
        state.busy = false;
        taskEXIT_CRITICAL(&state_mux);
        return STEPPER_ERR_MOVE_FAILED;
    }
    just_started = true;
    {
        uint32_t _start = millis();
        while (millis() - _start < STEPPER_RMT_INIT_MS) { yield(); }
    }
    MoveResultCode moveResult;
    if (dir == LIQ_OUT) {
        moveResult = stepper->runForward();
    } else {
        moveResult = stepper->runBackward();
    }
    if (moveResult != MOVE_OK) {
        logger.error("Stepper: runForward/runBackward failed code=%d", (int)moveResult);
        taskENTER_CRITICAL(&state_mux);
        state.busy = false;
        taskEXIT_CRITICAL(&state_mux);
        return STEPPER_ERR_MOVE_FAILED;
    }
    logger.info("Stepper: continuous movement started");
    return STEPPER_OK;
}

void stepper_stop(void) {
    int32_t pos = stepper->getCurrentPosition();
    logger.info("Stepper: soft stop, pos=%ld", (long)pos);
    stepper->forceStop();
    taskENTER_CRITICAL(&state_mux);
    state.busy = false;
    state.steps_remaining = 0;
    state.current_speed_ml_min = 0;
    taskEXIT_CRITICAL(&state_mux);
    move_to_stop_mode = false;
    disable_if_not_busy();
}

void stepper_emergency_stop(void) {
    int32_t pos = stepper->getCurrentPosition();
    logger.error("Stepper: EMERGENCY STOP, pos=%ld", (long)pos);
    stepper->forceStop();
    taskENTER_CRITICAL(&state_mux);
    state.busy = false;
    state.steps_remaining = 0;
    state.stall_detected = true;
    state.current_speed_ml_min = 0;
    taskEXIT_CRITICAL(&state_mux);
    move_to_stop_mode = false;
    digitalWrite(PIN_EN, HIGH);
    taskENTER_CRITICAL(&state_mux);
    state.en_enabled = false;
    taskEXIT_CRITICAL(&state_mux);
}

static void send_serial_response(uint64_t id, const char* status, const char* message, float dispensed) {
    char buf[256];
    if (message) {
        snprintf(buf, sizeof(buf),
                 "{\"id\":%" PRIu64 ",\"status\":\"%s\",\"message\":\"%s\",\"data\":{\"volume_dispensed_ml\":%.3f}}",
                 id, status, message, dispensed);
    } else {
        snprintf(buf, sizeof(buf),
                 "{\"id\":%" PRIu64 ",\"status\":\"%s\",\"data\":{\"volume_dispensed_ml\":%.3f}}",
                 id, status, dispensed);
    }
    if (g_result_cb) g_result_cb(buf);
}

static void send_serial_rinse_result(uint64_t id, const char* status, const char* message, uint8_t cycles) {
    char buf[256];
    if (message) {
        snprintf(buf, sizeof(buf),
                 "{\"id\":%" PRIu64 ",\"status\":\"%s\",\"message\":\"%s\",\"data\":{\"cycles_completed\":%u}}",
                 id, status, message, cycles);
    } else {
        snprintf(buf, sizeof(buf),
                 "{\"id\":%" PRIu64 ",\"status\":\"%s\",\"data\":{\"cycles_completed\":%u}}",
                 id, status, cycles);
    }
    if (g_result_cb) g_result_cb(buf);
}

void stepper_process(void) {
    if (state.busy) {
        if (just_started) {
            just_started = false;
            return;
        }
        if (move_to_stop_mode) {
            taskENTER_CRITICAL(&state_mux);
            state.steps_remaining = 0;
            taskEXIT_CRITICAL(&state_mux);

            // Update speed and volume while moving to stop
            CalibrationConfig cal = cal_snapshot();
            uint32_t steps_taken = stepper_get_actual_steps();
            float delta_ml = (cal.steps_per_ml > 0.001f) ? (float)steps_taken / cal.steps_per_ml : 0;
            taskENTER_CRITICAL(&state_mux);
            state.current_speed_ml_min = (float)state.frequency * cal.speed_coeff;
            if (state.direction_liq_in) {
                state.current_volume_ml = state.volume_at_start + delta_ml;
                if (state.current_volume_ml > cal.nominal_vol) state.current_volume_ml = cal.nominal_vol;
            } else {
                state.current_volume_ml = state.volume_at_start - delta_ml;
                if (state.current_volume_ml < 0) state.current_volume_ml = 0;
            }
            taskEXIT_CRITICAL(&state_mux);
        } else if (!stepper->isRunning()) {
            int32_t final_pos = stepper->getCurrentPosition();
            taskENTER_CRITICAL(&state_mux);
            state.busy = false;
            state.steps_remaining = 0;
            state.current_speed_ml_min = 0;
            taskEXIT_CRITICAL(&state_mux);
            disable_if_not_busy();
            logger.info("Stepper: movement complete, final pos=%ld", (long)final_pos);
        } else {
            int32_t current = stepper->getCurrentPosition();
            int32_t target = stepper->targetPos();
            int32_t remaining = target - current;
            taskENTER_CRITICAL(&state_mux);
            state.steps_remaining = (remaining < 0) ? -remaining : remaining;
            taskEXIT_CRITICAL(&state_mux);

            // Update speed and volume during active movement
            CalibrationConfig cal = cal_snapshot();
            uint32_t steps_taken = stepper_get_actual_steps();
            float delta_ml = (cal.steps_per_ml > 0.001f) ? (float)steps_taken / cal.steps_per_ml : 0;
            taskENTER_CRITICAL(&state_mux);
            state.current_speed_ml_min = (float)state.frequency * cal.speed_coeff;
            if (state.direction_liq_in) {
                state.current_volume_ml = state.volume_at_start + delta_ml;
                if (state.current_volume_ml > cal.nominal_vol) state.current_volume_ml = cal.nominal_vol;
            } else {
                state.current_volume_ml = state.volume_at_start - delta_ml;
                if (state.current_volume_ml < 0) state.current_volume_ml = 0;
            }
            taskEXIT_CRITICAL(&state_mux);
        }
    }

    PendingCmd p;
    taskENTER_CRITICAL(&g_pending_mux);
    p = g_pending;
    taskEXIT_CRITICAL(&g_pending_mux);

    if (p.id == 0) return;

    if (p.stop_requested) {
        if (state.busy) {
            stepper_emergency_stop();
            return;
        }

        CalibrationConfig cal = cal_snapshot();
        uint32_t steps_taken = stepper_get_actual_steps();
        float delta_ml = (cal.steps_per_ml > 0.001f) ? (float)steps_taken / cal.steps_per_ml : 0;

        if (state.direction_liq_in) {
            state.current_volume_ml = state.volume_at_start + delta_ml;
            if (state.current_volume_ml > cal.nominal_vol) state.current_volume_ml = cal.nominal_vol;
        } else {
            state.current_volume_ml = state.volume_at_start - delta_ml;
            if (state.current_volume_ml < 0) state.current_volume_ml = 0;
        }

        if (p.type == PENDING_RINSE) {
            send_serial_rinse_result(p.id, "error", "stopped",
                                     p.rinse_cycle > 0 ? p.rinse_cycle - 1 : 0);
        } else {
            send_serial_response(p.id, "error", "stopped", p.actual_dispensed);
        }
        taskENTER_CRITICAL(&g_pending_mux);
        g_pending.id = 0;
        g_pending.stop_requested = false;
        taskEXIT_CRITICAL(&g_pending_mux);
        return;
    }

    if (p.type == PENDING_RINSE) {
        stepper_process_rinse();
        return;
    }

    if (p.type == PENDING_CAL_DOSE) {
        stepper_process_cal_dose();
        return;
    }

    if (p.type == PENDING_CAL_SPEED) {
        stepper_process_cal_speed();
        return;
    }

    if (p.type == PENDING_CAL_SPEED_SEQ) {
        stepper_process_cal_speed_seq();
        return;
    }

    if (p.type == PENDING_AUTO_DOSE) {
        stepper_process_auto_dose();
        return;
    }

    if (!state.busy) {
        CalibrationConfig cal = cal_snapshot();
        stepper_check_limits();
        limitswitch_state_t lim = limitswitch_read();

        const char* err_msg = nullptr;
        if (lim.empty) {
            err_msg = "limit_empty_reached";
        } else if (lim.full) {
            err_msg = "limit_full_reached";
        }

        if (err_msg) {
            if (lim.empty) {
                taskENTER_CRITICAL(&state_mux);
                state.current_volume_ml = 0;
                taskEXIT_CRITICAL(&state_mux);
            } else if (lim.full) {
                taskENTER_CRITICAL(&state_mux);
                state.current_volume_ml = cal.nominal_vol;
                taskEXIT_CRITICAL(&state_mux);
            }
            send_serial_response(p.id, "error", err_msg, p.actual_dispensed);
        } else if (p.type == PENDING_DOSE) {
            int32_t pos = stepper->getCurrentPosition();
            float dispensed = steps_to_volume(pos, state.base_steps,
                                               cal.steps_per_ml,
                                               cal.nominal_vol);
            taskENTER_CRITICAL(&state_mux);
            state.current_volume_ml = cal.nominal_vol - dispensed;
            if (state.current_volume_ml < 0) state.current_volume_ml = 0;
            taskEXIT_CRITICAL(&state_mux);
            send_serial_response(p.id, "ok", nullptr, p.volume_ml);
        } else {
            float vol = (p.type == PENDING_FILL) ? cal.nominal_vol : 0;
            taskENTER_CRITICAL(&state_mux);
            state.current_volume_ml = vol;
            taskEXIT_CRITICAL(&state_mux);
            send_serial_response(p.id, "ok", nullptr, vol);
        }
        taskENTER_CRITICAL(&g_pending_mux);
        g_pending.id = 0;
        taskEXIT_CRITICAL(&g_pending_mux);
        return;
    }

    taskENTER_CRITICAL(&g_pending_mux);
    p = g_pending;
    taskEXIT_CRITICAL(&g_pending_mux);

    if (p.id != 0 && millis() - p.timestamp_ms > PENDING_WATCHDOG_MS) {
        logger.error("Stepper: watchdog timeout for id=%" PRIu64, p.id);
        stepper_emergency_stop();
        send_serial_response(p.id, "error", "watchdog_timeout", p.actual_dispensed);
        taskENTER_CRITICAL(&g_pending_mux);
        g_pending.id = 0;
        taskEXIT_CRITICAL(&g_pending_mux);
    }
}

void stepper_process_rinse(void) {
    if (state.busy) return;

    CalibrationConfig cal = cal_snapshot();
    RinsePhase phase;
    taskENTER_CRITICAL(&g_pending_mux);
    phase = g_pending.rinse_phase;
    taskEXIT_CRITICAL(&g_pending_mux);

    switch (phase) {
        case RINSE_PRE_FILL: {
            float speed;
            taskENTER_CRITICAL(&g_pending_mux);
            g_pending.rinse_phase = RINSE_EMPTYING;
            speed = g_pending.speed_ml_min;
            taskEXIT_CRITICAL(&g_pending_mux);
            taskENTER_CRITICAL(&state_mux);
            state.current_volume_ml = cal.nominal_vol;
            state.base_steps = stepper->getCurrentPosition();
            taskEXIT_CRITICAL(&state_mux);
            valve_set_position(VALVE_POSITION_OUTPUT);
            stepper_move_to_stop(LIQ_OUT, speed_to_frequency(
                speed, cal.speed_coeff,
                cal.min_freq, cal.max_freq));
            break;
        }

        case RINSE_EMPTYING: {
            taskENTER_CRITICAL(&state_mux);
            state.current_volume_ml = 0;
            taskEXIT_CRITICAL(&state_mux);
            float speed;
            taskENTER_CRITICAL(&g_pending_mux);
            g_pending.rinse_phase = RINSE_FILLING;
            speed = g_pending.speed_ml_min;
            taskEXIT_CRITICAL(&g_pending_mux);
            valve_set_position(VALVE_POSITION_INPUT);
            stepper_move_to_stop(LIQ_IN, speed_to_frequency(
                speed, cal.speed_coeff,
                cal.min_freq, cal.max_freq));
            break;
        }

        case RINSE_FILLING: {
            float speed;
            uint8_t cycle, total;
            uint64_t pid;
            taskENTER_CRITICAL(&g_pending_mux);
            g_pending.rinse_cycle++;
            cycle = g_pending.rinse_cycle;
            speed = g_pending.speed_ml_min;
            total = g_pending.rinse_total;
            pid = g_pending.id;
            taskEXIT_CRITICAL(&g_pending_mux);
            if (cycle > total) {
                taskENTER_CRITICAL(&g_pending_mux);
                g_pending.rinse_phase = RINSE_DONE;
                g_pending.id = 0;
                taskEXIT_CRITICAL(&g_pending_mux);
                send_serial_rinse_result(pid, "ok", nullptr, total);
                break;
            }
            taskENTER_CRITICAL(&state_mux);
            state.current_volume_ml = cal.nominal_vol;
            state.base_steps = stepper->getCurrentPosition();
            taskEXIT_CRITICAL(&state_mux);
            valve_set_position(VALVE_POSITION_OUTPUT);
            stepper_move_to_stop(LIQ_OUT, speed_to_frequency(
                speed, cal.speed_coeff,
                cal.min_freq, cal.max_freq));
            break;
        }

        case RINSE_DONE:
            break;
    }
}

void stepper_process_auto_dose(void) {
    if (state.busy) return;

    AutoDosePhase phase;
    float speed;
    taskENTER_CRITICAL(&g_pending_mux);
    phase = g_pending.ad_phase;
    speed = g_pending.speed_ml_min;
    taskEXIT_CRITICAL(&g_pending_mux);

    switch (phase) {
        case AUTO_DOSE_FILLING: {
            float this_vol;
            taskENTER_CRITICAL(&g_pending_mux);
            g_pending.ad_phase = AUTO_DOSE_DOSING;
            this_vol = g_pending.ad_this_cycle_vol;
            taskEXIT_CRITICAL(&g_pending_mux);
            StepperResult res = stepper_dose_volume(this_vol, speed);
            if (res != STEPPER_OK) {
                logger.error("auto_dose: dose_volume failed in cycle");
                uint64_t pid;
                taskENTER_CRITICAL(&g_pending_mux);
                pid = g_pending.id;
                g_pending.id = 0;
                taskEXIT_CRITICAL(&g_pending_mux);
                send_serial_response(pid, "error", "dose_failed", 0);
            }
            break;
        }

        case AUTO_DOSE_DOSING: {
            taskENTER_CRITICAL(&g_pending_mux);
            g_pending.ad_cycle++;
            taskEXIT_CRITICAL(&g_pending_mux);

            uint8_t total;
            uint8_t cycle;
            taskENTER_CRITICAL(&g_pending_mux);
            cycle = g_pending.ad_cycle;
            total = g_pending.ad_total_cycles;
            taskEXIT_CRITICAL(&g_pending_mux);

            if (cycle > total) {
                uint64_t pid;
                float total_vol;
                taskENTER_CRITICAL(&g_pending_mux);
                pid = g_pending.id;
                total_vol = g_pending.volume_ml;
                g_pending.ad_phase = AUTO_DOSE_DONE;
                g_pending.id = 0;
                taskEXIT_CRITICAL(&g_pending_mux);
                send_serial_response(pid, "ok", nullptr, total_vol);
                logger.info("auto_dose: complete, total=%.2f ml", total_vol);
            } else {
                CalibrationConfig cal = cal_snapshot();
                bool is_last = (cycle == total);
                float next_vol = is_last
                    ? g_pending.ad_remaining_vol
                    : cal.nominal_vol;
                taskENTER_CRITICAL(&g_pending_mux);
                g_pending.ad_this_cycle_vol = next_vol;
                g_pending.ad_phase = AUTO_DOSE_FILLING;
                taskEXIT_CRITICAL(&g_pending_mux);
                logger.info("auto_dose: cycle %u/%u, next dose=%.2f ml",
                            cycle, total, next_vol);
                stepper_fill(speed);
            }
            break;
        }

        case AUTO_DOSE_DONE:
            break;
    }
}

void stepper_process_cal_dose(void) {
    if (state.busy) return;

    CalibrationConfig cal = cal_snapshot();
    float speed;
    taskENTER_CRITICAL(&g_pending_mux);
    speed = g_pending.speed_ml_min;
    taskEXIT_CRITICAL(&g_pending_mux);

    switch (s_cal_phase) {
        case CAL_IDLE:
            s_cal_pos_before = stepper->getCurrentPosition();
            s_cal_phase = CAL_FILLING;
            if (state.current_volume_ml < cal.nominal_vol - BURETTE_NEAR_FULL_TOLERANCE) {
                stepper_fill(speed);
                return;
            }
            s_cal_phase = CAL_EMPTYING;
            stepper_empty(speed);
            return;

        case CAL_FILLING:
            s_cal_pos_before = stepper->getCurrentPosition();
            s_cal_phase = CAL_EMPTYING;
            stepper_empty(speed);
            return;

        case CAL_EMPTYING: {
            s_cal_phase = CAL_DONE;
            int32_t pos_after = stepper->getCurrentPosition();
            s_cal_steps_taken = (pos_after > s_cal_pos_before)
                ? (pos_after - s_cal_pos_before)
                : (s_cal_pos_before - pos_after);
            taskENTER_CRITICAL(&state_mux);
            state.current_volume_ml = 0;
            state.base_steps = pos_after;
            taskEXIT_CRITICAL(&state_mux);

            uint64_t pid;
            taskENTER_CRITICAL(&g_pending_mux);
            pid = g_pending.id;
            g_pending.id = 0;
            taskEXIT_CRITICAL(&g_pending_mux);

            char buf[128];
            snprintf(buf, sizeof(buf),
                     "{\"steps_taken\":%ld}", (long)s_cal_steps_taken);
            char cal_buf[256];
            snprintf(cal_buf, sizeof(cal_buf),
                     "{\"id\":%" PRIu64 ",\"status\":\"ok\",\"data\":%s}",
                     pid, buf);
            if (g_result_cb) g_result_cb(cal_buf);
            s_cal_phase = CAL_IDLE;
            return;
        }

        case CAL_DONE:
            break;
    }
}

void stepper_process_cal_speed(void) {
    if (state.busy) return;

    CalibrationConfig cal = cal_snapshot();
    float speed_ml_min;
    float volume_ml;
    taskENTER_CRITICAL(&g_pending_mux);
    speed_ml_min = g_pending.speed_ml_min;
    volume_ml = g_pending.volume_ml;
    taskEXIT_CRITICAL(&g_pending_mux);

    switch (s_cal_phase) {
        case CAL_IDLE:
            s_cal_phase = CAL_FILLING;
            {
                limitswitch_state_t lim = limitswitch_read();
                if (!lim.full) { stepper_fill(speed_ml_min); return; }
            }
            // fall through

        case CAL_FILLING: {
            s_cal_phase = CAL_EMPTYING;
            s_cal_start_ms = millis();
            uint16_t test_freq = (uint16_t)volume_ml;
            if (test_freq < cal.min_freq) test_freq = cal.min_freq;
            if (test_freq > cal.max_freq) test_freq = cal.max_freq;
            valve_set_position(VALVE_POSITION_OUTPUT);
            stepper_move_to_stop(LIQ_OUT, test_freq);
            return;
        }

        case CAL_EMPTYING: {
            s_cal_phase = CAL_DONE;
            uint32_t elapsed_ms = millis() - s_cal_start_ms;
            float speed = 0;
            if (elapsed_ms > 0) {
                speed = cal.nominal_vol / ((float)elapsed_ms / 60000.0f);
            }
            s_cal_measured_speed = speed;
            taskENTER_CRITICAL(&state_mux);
            state.current_volume_ml = 0;
            taskEXIT_CRITICAL(&state_mux);

            uint64_t pid;
            taskENTER_CRITICAL(&g_pending_mux);
            pid = g_pending.id;
            g_pending.id = 0;
            taskEXIT_CRITICAL(&g_pending_mux);

            char buf[128];
            snprintf(buf, sizeof(buf),
                     "{\"speed_ml_min\":%.2f,\"elapsed_ms\":%lu}",
                     speed, (unsigned long)elapsed_ms);
            char cal_buf[256];
            snprintf(cal_buf, sizeof(cal_buf),
                     "{\"id\":%" PRIu64 ",\"status\":\"ok\",\"data\":%s}",
                     pid, buf);
            if (g_result_cb) g_result_cb(cal_buf);
            s_cal_phase = CAL_IDLE;
            return;
        }

        case CAL_DONE:
            break;
    }
}

void stepper_start_cal_speed_seq(const uint16_t* freqs, float) {
    CalibrationConfig cal = cal_snapshot();
    s_seq_freqs[0] = freqs[0];
    s_seq_freqs[1] = freqs[1];
    s_seq_freqs[2] = freqs[2];
    taskENTER_CRITICAL(&g_pending_mux);
    g_pending.speed_ml_min = (uint32_t)(cal.max_freq * SPEED_SEQ_MULTIPLIER * cal.speed_coeff);
    taskEXIT_CRITICAL(&g_pending_mux);
    s_seq_idx = 0;
    s_seq_vlv_wait = true;
    s_seq_vlv_ms = millis();
    s_seq_elapsed_ms = 0;
    limitswitch_state_t lim = limitswitch_read();
    if (lim.full) {
        s_seq_elapsed_ms = 1;
        s_seq_vlv_wait = true;
        s_seq_vlv_ms = millis();
        valve_set_position(VALVE_POSITION_OUTPUT);
    } else {
        valve_set_position(VALVE_POSITION_INPUT);
    }
}

void stepper_process_cal_speed_seq(void) {
    if (state.busy) return;

    CalibrationConfig cal = cal_snapshot();
    float speed_ml_min;
    taskENTER_CRITICAL(&g_pending_mux);
    speed_ml_min = g_pending.speed_ml_min;
    taskEXIT_CRITICAL(&g_pending_mux);

    if (s_seq_vlv_wait) {
        if (millis() - s_seq_vlv_ms < VALVE_SWITCH_MS) return;
        s_seq_vlv_wait = false;
        if (s_seq_elapsed_ms == 0) {
            limitswitch_state_t lim = limitswitch_read();
            if (lim.full) {
                s_seq_vlv_wait = true;
                s_seq_vlv_ms = millis();
                s_seq_elapsed_ms = 1;
                valve_set_position(VALVE_POSITION_OUTPUT);
            } else {
                uint16_t fill_freq = speed_to_frequency(speed_ml_min,
                    cal.speed_coeff, cal.min_freq, cal.max_freq);
                stepper_move_to_stop(LIQ_IN, fill_freq);
            }
        } else {
            s_seq_elapsed_ms = millis();
            uint16_t f = s_seq_freqs[s_seq_idx];
            if (f < cal.min_freq) f = cal.min_freq;
            if (f > cal.max_freq) f = cal.max_freq;
            stepper_move_to_stop(LIQ_OUT, f);
        }
        return;
    }

    if (s_seq_elapsed_ms == 0) {
        s_seq_elapsed_ms = millis();
        s_seq_vlv_wait = true;
        s_seq_vlv_ms = millis();
        valve_set_position(VALVE_POSITION_OUTPUT);
        return;
    }

    {
        uint32_t elapsed = millis() - s_seq_elapsed_ms;
        float speed = (elapsed > 0)
            ? cal.nominal_vol / ((float)elapsed / 60000.0f)
            : 0;
        s_seq_results[s_seq_idx++] = speed;
        taskENTER_CRITICAL(&state_mux);
        state.current_volume_ml = 0;
        taskEXIT_CRITICAL(&state_mux);

        if (s_seq_idx < CAL_SEQ_POINTS) {
            s_seq_elapsed_ms = 0;
            s_seq_vlv_wait = true;
            s_seq_vlv_ms = millis();
            valve_set_position(VALVE_POSITION_INPUT);
            return;
        }

        taskENTER_CRITICAL(&g_pending_mux);
        g_pending.id = 0;
        taskEXIT_CRITICAL(&g_pending_mux);
    }
}

int stepper_get_cal_speed_seq_results(float* out, int max_count) {
    int n = (s_seq_idx < max_count) ? s_seq_idx : max_count;
    for (int i = 0; i < n; i++) out[i] = s_seq_results[i];
    return n;
}

StepperResult stepper_fill(float speed_ml_min) {
    CalibrationConfig cal = cal_snapshot();
    valve_set_position(VALVE_POSITION_INPUT);
    uint16_t freq = speed_to_frequency(speed_ml_min, cal.speed_coeff,
                                        cal.min_freq, cal.max_freq);
    return stepper_move_to_stop(LIQ_IN, freq);
}

StepperResult stepper_empty(float speed_ml_min) {
    CalibrationConfig cal = cal_snapshot();
    valve_set_position(VALVE_POSITION_OUTPUT);
    uint16_t freq = speed_to_frequency(speed_ml_min, cal.speed_coeff,
                                        cal.min_freq, cal.max_freq);
    return stepper_move_to_stop(LIQ_OUT, freq);
}

StepperResult stepper_dose_volume(float volume_ml, float speed_ml_min) {
    CalibrationConfig cal = cal_snapshot();
    valve_set_position(VALVE_POSITION_OUTPUT);
    int32_t steps = volume_to_steps(volume_ml, cal.steps_per_ml);
    uint16_t freq = speed_to_frequency(speed_ml_min, cal.speed_coeff,
                                        cal.min_freq, cal.max_freq);
    return stepper_move_steps(steps, LIQ_OUT, freq);
}

StepperResult stepper_rinse(uint8_t cycles, float speed_ml_min) {
    CalibrationConfig cal = cal_snapshot();
    taskENTER_CRITICAL(&g_pending_mux);
    g_pending.rinse_phase = RINSE_PRE_FILL;
    g_pending.rinse_cycle = 1;
    g_pending.rinse_total = cycles;
    taskEXIT_CRITICAL(&g_pending_mux);
    if (state.current_volume_ml < cal.nominal_vol - RINSE_START_TOLERANCE) {
        valve_set_position(VALVE_POSITION_INPUT);
        uint16_t freq = speed_to_frequency(speed_ml_min, cal.speed_coeff,
                                            cal.min_freq, cal.max_freq);
        return stepper_move_to_stop(LIQ_IN, freq);
    }
    g_pending.rinse_phase = RINSE_EMPTYING;
    valve_set_position(VALVE_POSITION_OUTPUT);
    uint16_t freq = speed_to_frequency(speed_ml_min, cal.speed_coeff,
                                        cal.min_freq, cal.max_freq);
    return stepper_move_to_stop(LIQ_OUT, freq);
}

void stepper_get_state(stepper_state_t* state_ptr) {
    taskENTER_CRITICAL(&state_mux);
    *state_ptr = state;
    taskEXIT_CRITICAL(&state_mux);
}

bool stepper_is_busy(void) {
    bool busy;
    taskENTER_CRITICAL(&state_mux);
    busy = state.busy;
    taskEXIT_CRITICAL(&state_mux);
    return busy;
}

uint32_t stepper_get_actual_steps(void) {
    if (!stepper) return 0;
    int32_t diff = stepper->getCurrentPosition() - movement_start_pos;
    return (diff < 0) ? -diff : diff;
}

void stepper_set_direction(StepperDirection dir) {
    taskENTER_CRITICAL(&state_mux);
    state.direction_liq_in = (dir == LIQ_IN);
    taskEXIT_CRITICAL(&state_mux);
    if (stepper && !state.busy) {
        digitalWrite(PIN_DIR, (dir == LIQ_OUT) ? HIGH : LOW);
    }
    logger.info("Stepper: direction set to %s", (dir == LIQ_OUT) ? "OUT" : "IN");
}

float stepper_get_volume_ml(void) {
    float vol;
    taskENTER_CRITICAL(&state_mux);
    vol = state.current_volume_ml;
    taskEXIT_CRITICAL(&state_mux);
    return vol;
}

int32_t stepper_get_cal_steps_taken(void) {
    return s_cal_steps_taken;
}

float stepper_get_cal_measured_speed(void) {
    return s_cal_measured_speed;
}

void stepper_start_homing(void) {
    if (state.busy || g_homing_active) {
        logger.warn("Stepper: homing skipped — busy or already homing");
        return;
    }

    limitswitch_state_t lim = limitswitch_read();
    if (lim.full) {
        CalibrationConfig cal = cal_snapshot();
        taskENTER_CRITICAL(&state_mux);
        state.base_steps = stepper->getCurrentPosition();
        state.current_volume_ml = cal.nominal_vol;
        taskEXIT_CRITICAL(&state_mux);
        logger.info("Stepper: already at FULL limit, homing done");
        return;
    }

    CalibrationConfig cal = cal_snapshot();
    valve_set_position(VALVE_POSITION_INPUT);
    delay(HOMING_VALVE_DELAY_MS);

    uint16_t freq = cal.max_freq / DEFAULT_FREQ_DIVISOR;
    if (freq < cal.min_freq) freq = cal.min_freq;
    if (freq > cal.max_freq) freq = cal.max_freq;

    StepperResult res = stepper_move_to_stop(LIQ_IN, freq);
    if (res != STEPPER_OK) {
        logger.error("Stepper: homing move_to_stop failed: %d", res);
        return;
    }

    g_homing_active = true;
    g_homing_start_ms = millis();
    logger.info("Stepper: homing started, freq=%u Hz", freq);
}

bool stepper_is_homing(void) {
    return g_homing_active;
}

void stepper_process_homing(void) {
    if (!g_homing_active) return;

    if (!state.busy && !move_to_stop_mode) {
        g_homing_active = false;
        CalibrationConfig cal = cal_snapshot();
        taskENTER_CRITICAL(&state_mux);
        state.current_volume_ml = cal.nominal_vol;
        taskEXIT_CRITICAL(&state_mux);
        logger.info("Stepper: homing complete, base_steps=%ld",
                    (long)state.base_steps);
        return;
    }

    if (millis() - g_homing_start_ms > HOMING_TIMEOUT_MS) {
        logger.error("Stepper: homing timeout after %lu ms",
                     (unsigned long)HOMING_TIMEOUT_MS);
        g_homing_active = false;
        stepper_emergency_stop();
    }
}

void stepper_check_limits(void) {
    if (!limit_possible.load(std::memory_order_relaxed) || !state.busy) {
        limit_possible.store(false, std::memory_order_relaxed);
        return;
    }
    limitswitch_state_t lim = limitswitch_read();
    bool should_stop = false;
    if (state.direction_liq_in && lim.full) {
        logger.info("Stepper: FULL limit reached");
        should_stop = true;
    } else if (!state.direction_liq_in && lim.empty) {
        logger.info("Stepper: EMPTY limit reached");
        should_stop = true;
    }
    if (should_stop) {
        int32_t pos = stepper->getCurrentPosition();
        stepper->forceStop();
        taskENTER_CRITICAL(&state_mux);
        state.busy = false;
        state.steps_remaining = 0;
        state.base_steps = pos;
        state.current_speed_ml_min = 0;
        taskEXIT_CRITICAL(&state_mux);
        move_to_stop_mode = false;
        disable_if_not_busy();
        logger.info("Stepper: stopped by limit, pos=%ld", (long)pos);
    }
    limit_possible.store(false, std::memory_order_relaxed);
}

void stepper_clear_limits_flag(void) {
    limit_possible.store(false, std::memory_order_relaxed);
}
