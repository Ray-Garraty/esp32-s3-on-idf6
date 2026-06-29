/**
 * @file handlers/adc_cal.cpp
 * @brief ADC calibration command handlers
 *
 * Commands:
 *   adc.cal.get      — return current status (coefficients, raw/calibrated mv, points)
 *   adc.cal.measure  — stabilize, sample median raw_mv, store point
 *   adc.cal.compute  — OLS over 5 points → a, b, R² → save to NVS
 *   adc.cal.reset    — clear points, restore defaults
 */

#include "adc_cal.h"
#include "common.h"
#include "../adc.h"
#include "../logger.h"
#include <time.h>
#include <Preferences.h>
#include <stdlib.h>
#include <string.h>

static const float ADC_CAL_EQUALITY_EPS = 0.0001f;
static const double ADC_CAL_DENOM_EPS = 1e-12;
static const uint8_t ADC_CAL_MSG_BUF_SIZE = 64;
static const uint8_t ADC_CAL_DATE_BUF_SIZE = 48;

// ============================================================================
// Статические данные калибровочных точек
// ============================================================================

typedef struct {
    float ref_mv;
    float raw_mv;
    bool  collected;
} cal_point_t;

static portMUX_TYPE s_adc_cal_mux = portMUX_INITIALIZER_UNLOCKED;

static cal_point_t s_cal_points[ADC_CAL_REF_POINTS] = {};
static int s_cal_points_count = 0;

// Pending computed coefficients (before user confirms save)
static float s_pending_a = 0.0f;
static float s_pending_b = 0.0f;
static float s_pending_r2 = 0.0f;

// ============================================================================
// Утилиты
// ============================================================================

static int compare_float(const void *a, const void *b) {
    float fa = *(const float *)a;
    float fb = *(const float *)b;
    if (fa < fb) return -1;
    if (fa > fb) return 1;
    return 0;
}

// ============================================================================
// adc.cal.get
// ============================================================================

String handle_adc_cal_get(const JsonDocument& doc, uint64_t id, bool* success, char* response_buf, size_t buf_size) {
    (void)doc;

    float a, b;
    adc_get_coefficients(&a, &b);
    uint16_t raw_mv = adc_get_raw_mv();
    int16_t calibrated_mv = adc_get_calibrated_mv();
    bool is_default = (fabs(a - ADC_CAL_DEFAULT_A) < ADC_CAL_EQUALITY_EPS && fabs(b - ADC_CAL_DEFAULT_B) < ADC_CAL_EQUALITY_EPS);

    // Собираем JSON
    JsonDocument resp;
    resp["a"] = (double)a;
    resp["b"] = (double)b;
    resp["raw_mv"] = raw_mv;
    resp["calibrated_mv"] = calibrated_mv;
    resp["is_default"] = is_default;
    int cal_points_count;
    cal_point_t cal_points[ADC_CAL_REF_POINTS];
    taskENTER_CRITICAL(&s_adc_cal_mux);
    cal_points_count = s_cal_points_count;
    memcpy(cal_points, s_cal_points, sizeof(s_cal_points));
    taskEXIT_CRITICAL(&s_adc_cal_mux);

    resp["points_collected"] = cal_points_count;

    // Метаданные калибровки (из NVS)
    Preferences prefs;
    prefs.begin(ADC_CAL_NVS_NS, true);
    resp["r_squared"] = prefs.getFloat("r_squared", 0.0);
    resp["calibrated_at"] = prefs.getString("cal_date", "");
    prefs.end();

    JsonDocument points_arr;
    for (int i = 0; i < ADC_CAL_REF_POINTS; i++) {
        JsonDocument pt;
        pt["ref_mv"] = (double)cal_points[i].ref_mv;
        pt["raw_mv"] = (double)cal_points[i].raw_mv;
        pt["collected"] = cal_points[i].collected;
        points_arr.add(pt);
    }
    resp["points"] = points_arr;

    String data;
    serializeJson(resp, data);
    *success = true;
    make_response_ok(response_buf, buf_size, id, data.c_str());
    return String(response_buf);
}

// ============================================================================
// adc.cal.measure
// ============================================================================

String handle_adc_cal_measure(const JsonDocument& doc, uint64_t id, bool* success, char* response_buf, size_t buf_size) {
    taskENTER_CRITICAL(&s_adc_cal_mux);
    bool full = (s_cal_points_count >= ADC_CAL_REF_POINTS);
    taskEXIT_CRITICAL(&s_adc_cal_mux);

    if (full) {
        *success = false;
        make_response_error(response_buf, buf_size, id,
            "All points already collected, call adc.cal.compute or adc.cal.reset");
        return String(response_buf);
    }

    float ref_mv = doc["ref_mv"] | NAN;
    if (isnan(ref_mv)) {
        *success = false;
        make_response_error(response_buf, buf_size, id, "Missing or invalid ref_mv");
        return String(response_buf);
    }

    // 1. Стабилизация
    bool stable = false;
    for (int attempt = 0; attempt < ADC_CAL_STAB_MAX_ATTEMPTS; attempt++) {
        float readings[ADC_CAL_STAB_SAMPLES];
        for (int i = 0; i < ADC_CAL_STAB_SAMPLES; i++) {
            readings[i] = (float)adc_get_raw_mv();
            yield();
        }
        float min_r = readings[0], max_r = readings[0];
        for (int i = 1; i < ADC_CAL_STAB_SAMPLES; i++) {
            if (readings[i] < min_r) min_r = readings[i];
            if (readings[i] > max_r) max_r = readings[i];
        }
        if ((max_r - min_r) <= ADC_CAL_STAB_TOLERANCE) {
            stable = true;
            break;
        }
    }

    if (!stable) {
        *success = false;
        make_response_error(response_buf, buf_size, id, "Signal not stable");
        return String(response_buf);
    }

    // 2. Сбор медианы
    float samples[ADC_CAL_MEDIAN_SAMPLES];
    for (int i = 0; i < ADC_CAL_MEDIAN_SAMPLES; i++) {
        samples[i] = (float)adc_get_raw_mv();
        yield();
    }
    qsort(samples, ADC_CAL_MEDIAN_SAMPLES, sizeof(float), compare_float);
    float median = samples[ADC_CAL_MEDIAN_SAMPLES / 2];

    // 3. Сохранить точку
    taskENTER_CRITICAL(&s_adc_cal_mux);
    int index = s_cal_points_count;
    s_cal_points[index].ref_mv = ref_mv;
    s_cal_points[index].raw_mv = median;
    s_cal_points[index].collected = true;
    s_cal_points_count++;
    taskEXIT_CRITICAL(&s_adc_cal_mux);

    logger.info("ADC cal point %d: ref=%.1f mV, raw=%.1f mV", index, ref_mv, median);

    // 4. Ответ
    JsonDocument resp;
    resp["index"] = index;
    resp["ref_mv"] = (double)ref_mv;
    resp["raw_median"] = (double)median;
    resp["points_collected"] = s_cal_points_count;

    String data;
    serializeJson(resp, data);
    *success = true;
    make_response_ok(response_buf, buf_size, id, data.c_str());
    return String(response_buf);
}

// ============================================================================
// adc.cal.compute
// ============================================================================

String handle_adc_cal_compute(const JsonDocument& doc, uint64_t id, bool* success, char* response_buf, size_t buf_size) {
    (void)doc;

    taskENTER_CRITICAL(&s_adc_cal_mux);
    bool underfull = (s_cal_points_count < ADC_CAL_REF_POINTS);
    taskEXIT_CRITICAL(&s_adc_cal_mux);

    if (underfull) {
        *success = false;
        char msg[ADC_CAL_MSG_BUF_SIZE];
        snprintf(msg, sizeof(msg), "Not enough points: need %d, have %d",
                 ADC_CAL_REF_POINTS, s_cal_points_count);
        make_response_error(response_buf, buf_size, id, msg);
        return String(response_buf);
    }

    // Копируем точки под блокировкой
    cal_point_t pts[ADC_CAL_REF_POINTS];
    taskENTER_CRITICAL(&s_adc_cal_mux);
    memcpy(pts, s_cal_points, sizeof(s_cal_points));
    taskEXIT_CRITICAL(&s_adc_cal_mux);

    // OLS: линейная регрессия ref_mv (x) → raw_mv (y)
    // Ищем: raw_mv = a * ref_mv + b
    // (т.е. по опорному напряжению предсказываем raw АЦП)
    // Затем для обратного преобразования: electrode_mV = (1/a) * raw_mv + (-b/a)
    // Но проще: храним прямые коэффициенты a_raw, b_raw для raw = a * ref + b
    // и при применении: calibrated = (raw - b_raw) / a_raw

    // На самом деле OLS даёт raw = a * ref + b
    // Тогда обратное: ref = (raw - b) / a
    // В adc_get_calibrated_mv мы применяем: calibrated = coeff_a * raw + coeff_b
    // Значит: coeff_a = 1/a, coeff_b = -b/a

    double sum_x = 0.0, sum_y = 0.0, sum_xy = 0.0, sum_x2 = 0.0, sum_y2 = 0.0;
    int n = ADC_CAL_REF_POINTS;

    for (int i = 0; i < n; i++) {
        double x = (double)pts[i].ref_mv;
        double y = (double)pts[i].raw_mv;
        sum_x  += x;
        sum_y  += y;
        sum_xy += x * y;
        sum_x2 += x * x;
        sum_y2 += y * y;
    }

    double denom = n * sum_x2 - sum_x * sum_x;
    if (fabs(denom) < ADC_CAL_DENOM_EPS) {
        *success = false;
        make_response_error(response_buf, buf_size, id, "Degenerate calibration points");
        return String(response_buf);
    }

    double a_raw = (n * sum_xy - sum_x * sum_y) / denom;
    double b_raw = (sum_y - a_raw * sum_x) / n;

    // Обратные коэффициенты для применения: calibrated = (raw - b_raw) / a_raw
    double coeff_a = 1.0 / a_raw;
    double coeff_b = -b_raw / a_raw;

    // R²
    double num = n * sum_xy - sum_x * sum_y;
    double denom_r = (n * sum_x2 - sum_x * sum_x) * (n * sum_y2 - sum_y * sum_y);
    double r_squared = (denom_r > 0.0) ? (num / sqrt(denom_r)) * (num / sqrt(denom_r)) : 0.0;

    // Сохранить в pending (не коммитим, ждём подтверждения)
    taskENTER_CRITICAL(&s_adc_cal_mux);
    s_pending_a = (float)coeff_a;
    s_pending_b = (float)coeff_b;
    s_pending_r2 = (float)r_squared;
    // Очистить точки
    memset(s_cal_points, 0, sizeof(s_cal_points));
    s_cal_points_count = 0;
    taskEXIT_CRITICAL(&s_adc_cal_mux);

    logger.info("ADC calibration computed (pending): a=%.6f, b=%.6f, R²=%.4f", coeff_a, coeff_b, r_squared);

    JsonDocument resp;
    resp["a"] = coeff_a;
    resp["b"] = coeff_b;
    resp["r_squared"] = r_squared;

    String data;
    serializeJson(resp, data);
    *success = true;
    make_response_ok(response_buf, buf_size, id, data.c_str());
    return String(response_buf);
}

// ============================================================================
// adc.cal.save
// ============================================================================

String handle_adc_cal_save(const JsonDocument& doc, uint64_t id, bool* success, char* response_buf, size_t buf_size) {
    (void)doc;

    float pa, pb, pr2;
    taskENTER_CRITICAL(&s_adc_cal_mux);
    pa = s_pending_a;
    pb = s_pending_b;
    pr2 = s_pending_r2;
    s_pending_a = 0.0f;
    s_pending_b = 0.0f;
    s_pending_r2 = 0.0f;
    taskEXIT_CRITICAL(&s_adc_cal_mux);

    adc_set_calibration(pa, pb);

    // Сохранить метаданные в NVS
    {
        Preferences prefs;
        prefs.begin(ADC_CAL_NVS_NS, false);
        prefs.putFloat("r_squared", pr2);
        time_t now = time(nullptr);
        struct tm *tm_info = localtime(&now);
        char date_buf[ADC_CAL_DATE_BUF_SIZE];
        strftime(date_buf, sizeof(date_buf), "%d.%m.%Y %H:%M", tm_info);
        prefs.putString("cal_date", date_buf);
        prefs.end();
    }

    logger.info("ADC calibration saved: a=%.6f, b=%.6f", pa, pb);

    *success = true;
    make_response_ok(response_buf, buf_size, id, "{}");
    return String(response_buf);
}

// ============================================================================
// adc.cal.reset
// ============================================================================

String handle_adc_cal_reset(const JsonDocument& doc, uint64_t id, bool* success, char* response_buf, size_t buf_size) {
    (void)doc;

    adc_reset_calibration();
    taskENTER_CRITICAL(&s_adc_cal_mux);
    memset(s_cal_points, 0, sizeof(s_cal_points));
    s_cal_points_count = 0;
    s_pending_a = 0.0f;
    s_pending_b = 0.0f;
    s_pending_r2 = 0.0f;
    taskEXIT_CRITICAL(&s_adc_cal_mux);

    // Очистить метаданные
    {
        Preferences prefs;
        prefs.begin(ADC_CAL_NVS_NS, false);
        prefs.remove("r_squared");
        prefs.remove("cal_date");
        prefs.end();
    }

    logger.info("ADC calibration reset by command");

    *success = true;
    make_response_ok(response_buf, buf_size, id, "{}");
    return String(response_buf);
}
