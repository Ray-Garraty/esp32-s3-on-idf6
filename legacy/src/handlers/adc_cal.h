/**
 * @file handlers/adc_cal.h
 * @brief ADC calibration command handlers
 */

#ifndef HANDLERS_ADC_CAL_H
#define HANDLERS_ADC_CAL_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <cstddef>
#include <cstdint>

/** @brief Return current calibration status, raw/calibrated mv, collected points */
String handle_adc_cal_get(const JsonDocument& doc, uint64_t id, bool* success, char* response_buf, size_t buf_size);
/** @brief Stabilize signal, sample median raw_mv, store calibration point */
String handle_adc_cal_measure(const JsonDocument& doc, uint64_t id, bool* success, char* response_buf, size_t buf_size);
/** @brief OLS over 5 points → a, b, R² → store pending for save */
String handle_adc_cal_compute(const JsonDocument& doc, uint64_t id, bool* success, char* response_buf, size_t buf_size);
/** @brief Commit pending coefficients to NVS */
String handle_adc_cal_save(const JsonDocument& doc, uint64_t id, bool* success, char* response_buf, size_t buf_size);
/** @brief Clear points, restore default coefficients */
String handle_adc_cal_reset(const JsonDocument& doc, uint64_t id, bool* success, char* response_buf, size_t buf_size);

#endif // HANDLERS_ADC_CAL_H
