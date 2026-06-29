/**
 * @file handlers/sensors.h
 * @brief Sensor command handlers: temperature, stallguard
 */

#ifndef HANDLERS_SENSORS_H
#define HANDLERS_SENSORS_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <cstddef>
#include <cstdint>

/** @brief Read DS18B20 temperature */
String handle_temp_read(const JsonDocument& doc, uint64_t id, bool* success, char* response_buf, size_t buf_size);
/** @brief Get current StallGuard threshold */
String handle_sg_get_threshold(const JsonDocument& doc, uint64_t id, bool* success, char* response_buf, size_t buf_size);
/** @brief Set StallGuard threshold (saved to NVS) */
String handle_sg_set_threshold(const JsonDocument& doc, uint64_t id, bool* success, char* response_buf, size_t buf_size);
#endif // HANDLERS_SENSORS_H