#include "burette_cal_handler.h"
#include "../burette_cal.h"
#include "../stepper.h"
#include "../logger.h"
#include "../config.h"
#include "common.h"
#include <cstdio>
#include <ctime>

static const float CAL_DEFAULT_TEMP_C = 25.0f;
static const float CAL_DEFAULT_PRESSURE_KPA = 101.3f;
static const uint16_t CAL_DATA_BUF_SIZE = 512;
static const uint16_t CAL_RESULT_BUF_SIZE = 256;
static const uint16_t CAL_ARR_BUF_SIZE = 128;
static const uint16_t CAL_NUM_BUF_SIZE = 32;
static const uint8_t MAX_CAL_MEASUREMENTS = 16;

extern CalibrationConfig g_burette_cal;

String handle_burette_cal_get(const JsonDocument& doc, uint64_t id, bool* success, char* response_buf, size_t buf_size) {
    (void)doc;
    CalibrationConfig cal;
    taskENTER_CRITICAL(&g_cal_mux);
    cal = g_burette_cal;
    taskEXIT_CRITICAL(&g_cal_mux);
    char data[CAL_DATA_BUF_SIZE];
    snprintf(data, sizeof(data),
             "{\"steps_per_ml\":%.1f,\"nominal_vol\":%.2f,"
             "\"speed_coeff\":%.5f,\"min_freq\":%u,\"max_freq\":%u,"
             "\"calibration_date\":%ld,\"is_default\":%s}",
             cal.steps_per_ml, cal.nominal_vol,
             cal.speed_coeff, cal.min_freq, cal.max_freq,
             (long)cal.calibration_date,
             burette_cal_is_default(cal) ? "true" : "false");
    *success = true;
    make_response_ok(response_buf, buf_size, id, data);
    return String(response_buf);
}

String handle_burette_cal_calc_volume(const JsonDocument& doc, uint64_t id, bool* success, char* response_buf, size_t buf_size) {
    float mass_g = doc["mass_g"] | 0.0f;
    float temp_c = doc["temp_c"] | CAL_DEFAULT_TEMP_C;
    float pressure_kpa = doc["pressure_kpa"] | CAL_DEFAULT_PRESSURE_KPA;

    if (mass_g <= 0) {
        *success = false;
        make_response_error(response_buf, buf_size, id, "invalid_params");
        return String(response_buf);
    }

    float z = get_z_factor(temp_c, pressure_kpa);
    float actual_vol = mass_g * z;
    CalibrationConfig cal;
    taskENTER_CRITICAL(&g_cal_mux);
    cal = g_burette_cal;
    taskEXIT_CRITICAL(&g_cal_mux);
    float new_sps = calculate_new_steps_per_ml(cal.steps_per_ml, cal.nominal_vol, actual_vol);
    float rel_error = (actual_vol - cal.nominal_vol) / cal.nominal_vol * 100.0f;

    // Store in pending
    CalibrationConfig pending = burette_cal_get_pending_copy();
    pending.steps_per_ml = new_sps;
    pending.nominal_vol = actual_vol;
    burette_cal_set_pending(pending);

    char data[CAL_DATA_BUF_SIZE];
    snprintf(data, sizeof(data),
             "{\"z_factor\":%.6f,\"actual_volume_ml\":%.4f,"
             "\"new_steps_per_ml\":%.1f,\"relative_error_pct\":%.2f}",
             z, actual_vol, new_sps, rel_error);
    *success = true;
    make_response_ok(response_buf, buf_size, id, data);
    return String(response_buf);
}

String handle_burette_cal_calc_speed(const JsonDocument& doc, uint64_t id, bool* success, char* response_buf, size_t buf_size) {
    JsonArrayConst measurements = doc["measurements"].as<JsonArrayConst>();
    if (measurements.size() < 2) {
        *success = false;
        make_response_error(response_buf, buf_size, id, "invalid_params: need >= 2 measurements");
        return String(response_buf);
    }

    uint8_t count = measurements.size();
    if (count > MAX_CAL_MEASUREMENTS) count = MAX_CAL_MEASUREMENTS;
    float freqs[MAX_CAL_MEASUREMENTS], speeds[MAX_CAL_MEASUREMENTS];

    for (uint8_t i = 0; i < count; i++) {
        JsonObjectConst m = measurements[i];
        freqs[i] = m["freq_hz"] | 0.0f;
        speeds[i] = m["speed_ml_min"] | 0.0f;
    }

    SpeedCalResult result = calculate_speed_calibration(freqs, speeds, count);

    // Store in pending
    CalibrationConfig pending = burette_cal_get_pending_copy();
    pending.speed_coeff = result.k;
    burette_cal_set_pending(pending);

    uint16_t min_f, max_f;
    taskENTER_CRITICAL(&g_cal_mux);
    min_f = g_burette_cal.min_freq;
    max_f = g_burette_cal.max_freq;
    taskEXIT_CRITICAL(&g_cal_mux);
    char data[CAL_RESULT_BUF_SIZE];
    snprintf(data, sizeof(data),
             "{\"k\":%.6f,\"r_squared\":%.4f,\"min_freq\":%u,\"max_freq\":%u}",
             result.k, result.r_squared,
             min_f, max_f);
    *success = true;
    make_response_ok(response_buf, buf_size, id, data);
    return String(response_buf);
}

String handle_burette_cal_save(const JsonDocument& doc, uint64_t id, bool* success, char* response_buf, size_t buf_size) {
    (void)doc;
    CalibrationConfig cal = burette_cal_get_pending_copy();
    time_t now = time(nullptr);
    if (now > NTP_MIN_VALID_TIMESTAMP) {
        cal.calibration_date = now;
    }
    burette_cal_save(cal);
    taskENTER_CRITICAL(&g_cal_mux);
    g_burette_cal = cal;
    taskEXIT_CRITICAL(&g_cal_mux);
    logger.info("Burette cal saved from pending: spm=%.1f, nom=%.2f, coeff=%.5f",
                cal.steps_per_ml, cal.nominal_vol,
                cal.speed_coeff);
    *success = true;
    make_response_ok(response_buf, buf_size, id, "{\"saved\":true}");
    return String(response_buf);
}

String handle_burette_cal_reset(const JsonDocument& doc, uint64_t id, bool* success, char* response_buf, size_t buf_size) {
    (void)doc;
    CalibrationConfig cal;
    burette_cal_reset(cal);
    taskENTER_CRITICAL(&g_cal_mux);
    g_burette_cal = cal;
    taskEXIT_CRITICAL(&g_cal_mux);
    burette_cal_set_pending(cal);
    *success = true;
    make_response_ok(response_buf, buf_size, id, "{\"reset\":true}");
    return String(response_buf);
}

String handle_burette_cal_get_result(const JsonDocument& doc, uint64_t id, bool* success, char* response_buf, size_t buf_size) {
    (void)doc;
    float seq_results[CAL_SEQ_POINTS];
    int n = stepper_get_cal_speed_seq_results(seq_results, CAL_SEQ_POINTS);
    if (n > 0) {
        char data[CAL_RESULT_BUF_SIZE];
        char arr[CAL_ARR_BUF_SIZE] = "";
        for (int i = 0; i < n; i++) {
            char buf[CAL_NUM_BUF_SIZE];
            snprintf(buf, sizeof(buf), "%s%.2f", (i > 0) ? "," : "", seq_results[i]);
            strncat(arr, buf, sizeof(arr) - strlen(arr) - 1);
        }
        snprintf(data, sizeof(data), "{\"speeds\":[%s]}", arr);
        *success = true;
        make_response_ok(response_buf, buf_size, id, data);
        return String(response_buf);
    }
    int32_t steps = stepper_get_cal_steps_taken();
    float speed = stepper_get_cal_measured_speed();
    char data[CAL_RESULT_BUF_SIZE];
    if (steps > 0) {
        snprintf(data, sizeof(data), "{\"steps_taken\":%ld}", (long)steps);
    } else if (speed > 0) {
        snprintf(data, sizeof(data), "{\"speed_ml_min\":%.2f}", speed);
    } else {
        snprintf(data, sizeof(data), "{\"steps_taken\":%ld,\"speed_ml_min\":%.2f}", (long)steps, speed);
    }
    *success = true;
    make_response_ok(response_buf, buf_size, id, data);
    return String(response_buf);
}
