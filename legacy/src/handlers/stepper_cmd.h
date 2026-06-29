#ifndef HANDLER_STEPPER_CMD_H
#define HANDLER_STEPPER_CMD_H

#include <Arduino.h>
#include <ArduinoJson.h>

/** @brief Move exact steps at frequency (raw stepper control) */
String handle_burette_move_steps(const JsonDocument& doc, uint64_t id, bool* success, char* response_buf, size_t buf_size);
/** @brief Move until limit switch at frequency (raw stepper control) */
String handle_burette_move_to_stop(const JsonDocument& doc, uint64_t id, bool* success, char* response_buf, size_t buf_size);
/** @brief Set stepper direction (LIQ_IN / LIQ_OUT) */
String handle_burette_set_direction(const JsonDocument& doc, uint64_t id, bool* success, char* response_buf, size_t buf_size);

#endif
