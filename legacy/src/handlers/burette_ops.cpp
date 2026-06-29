#include "burette_ops.h"
#include "../burette_planner.h"
#include "../stepper.h"
#include "../burette_cal.h"
#include "../logger.h"
#include "../status.h"
#include "../config.h"
#include "common.h"
#include <ArduinoJson.h>
#include <cstdio>

static const float EPSILON_FLOAT = 0.001f;
static const float CAL_DEFAULT_SPEED = 15.0f;
static const uint16_t BURETTE_STATUS_BUF_SIZE = 256;

String handle_dose_volume(const JsonDocument& doc, uint64_t id, bool* success, char* response_buf, size_t buf_size) {
    float vol = doc["volume_ml"] | 0.0f;
    float speed = doc["speed_ml_min"] | 0.0f;

    uint64_t pending_id;
    taskENTER_CRITICAL(&g_pending_mux);
    pending_id = g_pending.id;
    taskEXIT_CRITICAL(&g_pending_mux);
    bool busy = (pending_id != 0 || stepper_is_busy());

    float current_vol = stepper_get_volume_ml();

    CalibrationConfig cal;
    taskENTER_CRITICAL(&g_cal_mux);
    cal = g_burette_cal;
    taskEXIT_CRITICAL(&g_cal_mux);

    DosePlan plan = plan_dose_volume(vol, speed, current_vol, cal.nominal_vol, busy);

    if (plan.action == DOSE_REJECT) {
        *success = false;
        make_response_error(response_buf, buf_size, id, plan.reject_reason);
        return String(response_buf);
    }

    taskENTER_CRITICAL(&g_pending_mux);
    g_pending = {id, millis(), false, PENDING_AUTO_DOSE, vol, speed, 0,
                 0, 0, RINSE_DONE,
                 1, plan.total_cycles, plan.remaining_vol, plan.first_cycle_vol, AUTO_DOSE_FILLING, TRANSPORT_USB};
    taskEXIT_CRITICAL(&g_pending_mux);

    StepperResult res;
    if (plan.action == DOSE_FILL_FIRST) {
        res = stepper_fill(speed);
    } else {
        res = stepper_dose_volume(plan.first_cycle_vol, speed);
        if (res == STEPPER_OK) {
            taskENTER_CRITICAL(&g_pending_mux);
            g_pending.ad_phase = AUTO_DOSE_DOSING;
            taskEXIT_CRITICAL(&g_pending_mux);
        }
    }

    if (res != STEPPER_OK) {
        taskENTER_CRITICAL(&g_pending_mux);
        g_pending.id = 0;
        taskEXIT_CRITICAL(&g_pending_mux);
        *success = false;
        const char* msg = (res == STEPPER_ERR_AT_LIMIT_FULL) ? "limit_full_reached" :
                          (res == STEPPER_ERR_AT_LIMIT_EMPTY) ? "limit_empty_reached" :
                          (res == STEPPER_ERR_BUSY) ? "burette_busy" : "start_failed";
        logger.warn("Reject: stepper_fill/dose failed: %s", msg);
        make_response_error(response_buf, buf_size, id, msg);
        return String(response_buf);
    }

    *success = true;
    logger.info("auto_dose accepted: vol=%.2f speed=%.1f cycles=%u", vol, speed, plan.total_cycles);
    make_response_ok(response_buf, buf_size, id, "{\"status\":\"accepted\"}");
    return String(response_buf);
}

String handle_fill(const JsonDocument& doc, uint64_t id, bool* success, char* response_buf, size_t buf_size) {
    float speed = doc["speed_ml_min"] | 0.0f;

    uint64_t pending_id;
    taskENTER_CRITICAL(&g_pending_mux);
    pending_id = g_pending.id;
    taskEXIT_CRITICAL(&g_pending_mux);
    bool busy = (pending_id != 0 || stepper_is_busy());

    SimplePlan plan = plan_fill(speed, busy);
    if (plan.action == SIMPLE_REJECT) {
        *success = false;
        make_response_error(response_buf, buf_size, id, plan.reject_reason);
        return String(response_buf);
    }

    taskENTER_CRITICAL(&g_pending_mux);
    g_pending = {id, millis(), false, PENDING_FILL, 0, speed, 0, 0, 0, RINSE_DONE, 0, 0, 0, 0, AUTO_DOSE_DONE, TRANSPORT_USB};
    taskEXIT_CRITICAL(&g_pending_mux);
    StepperResult res = stepper_fill(speed);
    if (res != STEPPER_OK) {
        taskENTER_CRITICAL(&g_pending_mux);
        g_pending.id = 0;
        taskEXIT_CRITICAL(&g_pending_mux);
        *success = false;
        const char* msg = (res == STEPPER_ERR_AT_LIMIT_FULL) ? "limit_full_reached" :
                          (res == STEPPER_ERR_BUSY) ? "burette_busy" : "start_failed";
        logger.warn("Reject: stepper_fill failed: %s", msg);
        make_response_error(response_buf, buf_size, id, msg);
        return String(response_buf);
    }
    *success = true;
    logger.info("fill accepted: speed=%.1f", speed);
    make_response_ok(response_buf, buf_size, id, "{\"status\":\"accepted\"}");
    return String(response_buf);
}

String handle_empty(const JsonDocument& doc, uint64_t id, bool* success, char* response_buf, size_t buf_size) {
    float speed = doc["speed_ml_min"] | 0.0f;

    uint64_t pending_id;
    taskENTER_CRITICAL(&g_pending_mux);
    pending_id = g_pending.id;
    taskEXIT_CRITICAL(&g_pending_mux);
    bool busy = (pending_id != 0 || stepper_is_busy());

    SimplePlan plan = plan_empty(speed, busy);
    if (plan.action == SIMPLE_REJECT) {
        *success = false;
        make_response_error(response_buf, buf_size, id, plan.reject_reason);
        return String(response_buf);
    }

    taskENTER_CRITICAL(&g_pending_mux);
    g_pending = {id, millis(), false, PENDING_EMPTY, 0, speed, 0, 0, 0, RINSE_DONE, 0, 0, 0, 0, AUTO_DOSE_DONE, TRANSPORT_USB};
    taskEXIT_CRITICAL(&g_pending_mux);
    StepperResult res = stepper_empty(speed);
    if (res != STEPPER_OK) {
        taskENTER_CRITICAL(&g_pending_mux);
        g_pending.id = 0;
        taskEXIT_CRITICAL(&g_pending_mux);
        *success = false;
        const char* msg = (res == STEPPER_ERR_AT_LIMIT_EMPTY) ? "limit_empty_reached" :
                          (res == STEPPER_ERR_BUSY) ? "burette_busy" : "start_failed";
        logger.warn("Reject: stepper_empty failed: %s", msg);
        make_response_error(response_buf, buf_size, id, msg);
        return String(response_buf);
    }
    *success = true;
    logger.info("empty accepted: speed=%.1f", speed);
    make_response_ok(response_buf, buf_size, id, "{\"status\":\"accepted\"}");
    return String(response_buf);
}

String handle_rinse(const JsonDocument& doc, uint64_t id, bool* success, char* response_buf, size_t buf_size) {
    uint8_t cycles = doc["cycles"] | 0;
    float speed = doc["speed_ml_min"] | 0.0f;

    uint64_t pending_id;
    taskENTER_CRITICAL(&g_pending_mux);
    pending_id = g_pending.id;
    taskEXIT_CRITICAL(&g_pending_mux);
    bool busy = (pending_id != 0 || stepper_is_busy());

    RinsePlan plan = plan_rinse(cycles, speed, busy);
    if (plan.action == SIMPLE_REJECT) {
        *success = false;
        make_response_error(response_buf, buf_size, id, plan.reject_reason);
        return String(response_buf);
    }

    taskENTER_CRITICAL(&g_pending_mux);
    g_pending = {id, millis(), false, PENDING_RINSE, 0, speed, 0, 0, 0, RINSE_DONE, 0, 0, 0, 0, AUTO_DOSE_DONE, TRANSPORT_USB};
    taskEXIT_CRITICAL(&g_pending_mux);
    StepperResult res = stepper_rinse(plan.cycles, speed);
    if (res != STEPPER_OK) {
        taskENTER_CRITICAL(&g_pending_mux);
        g_pending.id = 0;
        taskEXIT_CRITICAL(&g_pending_mux);
        *success = false;
        const char* msg = (res == STEPPER_ERR_AT_LIMIT_EMPTY) ? "limit_empty_reached" :
                          (res == STEPPER_ERR_AT_LIMIT_FULL) ? "limit_full_reached" :
                          (res == STEPPER_ERR_BUSY) ? "burette_busy" : "start_failed";
        logger.warn("Reject: stepper_rinse failed: %s", msg);
        make_response_error(response_buf, buf_size, id, msg);
        return String(response_buf);
    }
    *success = true;
    logger.info("rinse accepted: cycles=%u speed=%.1f", plan.cycles, speed);
    make_response_ok(response_buf, buf_size, id, "{\"status\":\"accepted\"}");
    return String(response_buf);
}

String handle_burette_stop(const JsonDocument& doc, uint64_t id, bool* success, char* response_buf, size_t buf_size) {
    (void)doc;
    if (stepper_is_homing()) {
        *success = false;
        make_response_error(response_buf, buf_size, id, "burette_busy_homing");
        return String(response_buf);
    }
    stepper_stop();
    taskENTER_CRITICAL(&g_pending_mux);
    if (g_pending.id != 0) {
        g_pending.stop_requested = true;
    }
    taskEXIT_CRITICAL(&g_pending_mux);
    *success = true;
    make_response_ok(response_buf, buf_size, id, "{\"status\":\"accepted\"}");
    return String(response_buf);
}

String handle_emergency_stop(const JsonDocument& doc, uint64_t id, bool* success, char* response_buf, size_t buf_size) {
    (void)doc;
    if (stepper_is_homing()) {
        *success = false;
        make_response_error(response_buf, buf_size, id, "burette_busy_homing");
        return String(response_buf);
    }
    stepper_emergency_stop();
    taskENTER_CRITICAL(&g_pending_mux);
    g_pending.id = 0;
    g_pending.stop_requested = false;
    taskEXIT_CRITICAL(&g_pending_mux);
    *success = true;
    make_response_ok(response_buf, buf_size, id, "{\"status\":\"stopped\"}");
    return String(response_buf);
}

String handle_burette_get_status(const JsonDocument& doc, uint64_t id, bool* success, char* response_buf, size_t buf_size) {
    (void)doc;
    stepper_state_t s;
    stepper_get_state(&s);
    char data[BURETTE_STATUS_BUF_SIZE];
    if (stepper_is_homing()) {
        snprintf(data, sizeof(data),
                 "{\"status\":\"%s\",\"volume_ml\":null,\"speed_ml_min\":%.1f}",
                 s.busy ? "moving" : "idle", s.current_speed_ml_min);
    } else {
        snprintf(data, sizeof(data),
                 "{\"status\":\"%s\",\"volume_ml\":%.2f,\"speed_ml_min\":%.1f}",
                 s.busy ? "moving" : "idle", s.current_volume_ml, s.current_speed_ml_min);
    }
    *success = true;
    make_response_ok(response_buf, buf_size, id, data);
    return String(response_buf);
}

String handle_burette_cal_run(const JsonDocument& doc, uint64_t id, bool* success, char* response_buf, size_t buf_size) {
    const char* mode = doc["mode"] | "invalid";
    uint16_t freq_hz = doc["freq_hz"] | 0;

    uint64_t pending_id;
    taskENTER_CRITICAL(&g_pending_mux);
    pending_id = g_pending.id;
    taskEXIT_CRITICAL(&g_pending_mux);
    bool busy = (pending_id != 0 || stepper_is_busy());

    CalibrationConfig cal;
    taskENTER_CRITICAL(&g_cal_mux);
    cal = g_burette_cal;
    taskEXIT_CRITICAL(&g_cal_mux);

    float speed_ml_min = doc["speed_ml_min"] | 0.0f;
    CalRunPlan plan = plan_cal_run(mode, speed_ml_min, freq_hz,
                                    (float)cal.max_freq, cal.speed_coeff, busy);

    if (plan.action == CAL_REJECT) {
        *success = false;
        make_response_error(response_buf, buf_size, id, plan.reject_reason);
        return String(response_buf);
    }

    if (plan.action == CAL_DOSE) {
        taskENTER_CRITICAL(&g_pending_mux);
        g_pending = {id, millis(), false, PENDING_CAL_DOSE, 0, plan.speed_ml_min, 0, 0, 0, RINSE_DONE, 0, 0, 0, 0, AUTO_DOSE_DONE, TRANSPORT_USB};
        taskEXIT_CRITICAL(&g_pending_mux);
        logger.info("cal_run dose accepted: freq=%u speed=%.1f", plan.freq_hz, plan.speed_ml_min);
    } else {
        taskENTER_CRITICAL(&g_pending_mux);
        g_pending = {id, millis(), false, PENDING_CAL_SPEED, (float)plan.freq_hz, plan.speed_ml_min, 0, 0, 0, RINSE_DONE, 0, 0, 0, 0, AUTO_DOSE_DONE, TRANSPORT_USB};
        taskEXIT_CRITICAL(&g_pending_mux);
        logger.info("cal_run speed accepted: freq=%u fill_speed=%.1f", plan.freq_hz, plan.speed_ml_min);
    }

    *success = true;
    make_response_ok(response_buf, buf_size, id, "{\"status\":\"accepted\"}");
    return String(response_buf);
}

String handle_burette_cal_run_speed_seq(const JsonDocument& doc, uint64_t id, bool* success, char* response_buf, size_t buf_size) {
    uint64_t pending_id;
    taskENTER_CRITICAL(&g_pending_mux);
    pending_id = g_pending.id;
    taskEXIT_CRITICAL(&g_pending_mux);
    bool busy = (pending_id != 0 || stepper_is_busy());

    JsonArrayConst freqs_arr = doc["freqs"];
    CalibrationConfig cal;
    taskENTER_CRITICAL(&g_cal_mux);
    cal = g_burette_cal;
    taskEXIT_CRITICAL(&g_cal_mux);

    uint8_t freq_count = (freqs_arr && freqs_arr.size() == CAL_SEQ_POINTS) ? CAL_SEQ_POINTS : 0;
    float fill_speed = doc["speed_ml_min"] | 0.0f;

    CalSpeedSeqPlan plan = plan_cal_speed_seq(freq_count, fill_speed,
                                               (float)cal.max_freq, cal.speed_coeff, busy);

    if (plan.action == SIMPLE_REJECT) {
        *success = false;
        make_response_error(response_buf, buf_size, id, plan.reject_reason);
        return String(response_buf);
    }

    uint16_t freqs[CAL_SEQ_POINTS];
    int i = 0;
    for (JsonVariantConst v : freqs_arr) {
        freqs[i++] = v.as<uint16_t>();
    }

    taskENTER_CRITICAL(&g_pending_mux);
    g_pending = {id, millis(), false, PENDING_CAL_SPEED_SEQ, 0, plan.fill_speed_ml_min, 0, 0, 0, RINSE_DONE, 0, 0, 0, 0, AUTO_DOSE_DONE, TRANSPORT_USB};
    taskEXIT_CRITICAL(&g_pending_mux);
    stepper_start_cal_speed_seq(freqs, plan.fill_speed_ml_min);
    logger.info("cal_run speed_seq accepted: freqs=%u,%u,%u fill=%.1f",
                freqs[0], freqs[1], freqs[2], plan.fill_speed_ml_min);
    *success = true;
    make_response_ok(response_buf, buf_size, id, "{\"status\":\"accepted\"}");
    return String(response_buf);
}
