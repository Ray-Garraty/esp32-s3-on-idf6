/**
 * @file handlers/system.h
 * @brief System command handlers: getStatus, logs
 */

#ifndef HANDLERS_SYSTEM_H
#define HANDLERS_SYSTEM_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <cstddef>
#include <cstdint>

/** @brief Return system status (uptime, heap, wifi, versions) */
String handle_system_get_status(const JsonDocument& doc, uint64_t id, bool* success, char* response_buf, size_t buf_size);
/** @brief Return formatted logs from RAM buffer */
String handle_get_formatted_logs(const JsonDocument& doc, uint64_t id, bool* success, char* response_buf, size_t buf_size);
/** @brief Read raw log file entries */
String handle_system_read_log(const JsonDocument& doc, uint64_t id, bool* success, char* response_buf, size_t buf_size);

#endif // HANDLERS_SYSTEM_H