#ifndef HANDLER_BURETTE_OPS_H
#define HANDLER_BURETTE_OPS_H

#include <Arduino.h>
#include <ArduinoJson.h>

/** @brief Dispatch burette.doseVolume: validate params, reserve pending, start motor, ACK */
String handle_dose_volume(const JsonDocument& doc, uint64_t id, bool* success, char* response_buf, size_t buf_size);

/** @brief Dispatch burette.fill: valve→input, move to FULL limit, ACK */
String handle_fill(const JsonDocument& doc, uint64_t id, bool* success, char* response_buf, size_t buf_size);

/** @brief Dispatch burette.empty: valve→output, move to EMPTY limit, ACK */
String handle_empty(const JsonDocument& doc, uint64_t id, bool* success, char* response_buf, size_t buf_size);

/** @brief Dispatch burette.rinse: init rinse state machine, first fill/empty, ACK */
String handle_rinse(const JsonDocument& doc, uint64_t id, bool* success, char* response_buf, size_t buf_size);

/** @brief Dispatch burette.stop: set stop_requested flag, soft-stop motor, ACK */
String handle_burette_stop(const JsonDocument& doc, uint64_t id, bool* success, char* response_buf, size_t buf_size);

/** @brief Dispatch burette.emergencyStop: force-stop motor, reset pending slot, respond */
String handle_emergency_stop(const JsonDocument& doc, uint64_t id, bool* success, char* response_buf, size_t buf_size);

/** @brief Dispatch burette.getStatus: return current {status, volume_ml, speed_ml_min} */
String handle_burette_get_status(const JsonDocument& doc, uint64_t id, bool* success, char* response_buf, size_t buf_size);

/** @brief Dispatch burette.cal.run: fill→empty calibration cycle, ACK */

String handle_burette_cal_run(const JsonDocument& doc, uint64_t id, bool* success, char* response_buf, size_t buf_size);

/** @brief Dispatch burette.cal.runSpeedSeq: 3-point speed calibration, one command */
String handle_burette_cal_run_speed_seq(const JsonDocument& doc, uint64_t id, bool* success, char* response_buf, size_t buf_size);

#endif
