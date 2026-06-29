#ifndef HANDLER_BURETTE_CAL_H
#define HANDLER_BURETTE_CAL_H

#include <Arduino.h>
#include <ArduinoJson.h>

/** @brief Return current calibration coefficients + is_default flag */
String handle_burette_cal_get(const JsonDocument& doc, uint64_t id, bool* success, char* response_buf, size_t buf_size);

/** @brief Gravimetric volume calibration preview (read-only, no NVS commit) */
String handle_burette_cal_calc_volume(const JsonDocument& doc, uint64_t id, bool* success, char* response_buf, size_t buf_size);

/** @brief OLS speed calibration preview from measurement array (read-only) */
String handle_burette_cal_calc_speed(const JsonDocument& doc, uint64_t id, bool* success, char* response_buf, size_t buf_size);

/** @brief Commit current g_burette_cal to NVS */
String handle_burette_cal_save(const JsonDocument& doc, uint64_t id, bool* success, char* response_buf, size_t buf_size);

/** @brief Reset g_burette_cal to defaults and remove NVS keys */
String handle_burette_cal_reset(const JsonDocument& doc, uint64_t id, bool* success, char* response_buf, size_t buf_size);

/** @brief Return result from last calibration run ({steps_taken} or {speed_ml_min, elapsed_ms}) */
String handle_burette_cal_get_result(const JsonDocument& doc, uint64_t id, bool* success, char* response_buf, size_t buf_size);

#endif
