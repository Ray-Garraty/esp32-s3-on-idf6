/**
 * @file handlers/valve.h
 * @brief Valve command handlers
 */

#ifndef HANDLERS_VALVE_H
#define HANDLERS_VALVE_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <cstddef>
#include <cstdint>

/** @brief Set valve position ("input" / "output") */
String handle_valve_set_position(const JsonDocument& doc, uint64_t id, bool* success, char* response_buf, size_t buf_size);
/** @brief Get current valve position */
String handle_valve_get_state(const JsonDocument& doc, uint64_t id, bool* success, char* response_buf, size_t buf_size);

#endif // HANDLERS_VALVE_H