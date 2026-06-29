/**
 * @file handlers/common.h
 * @brief Common utilities for handlers: JSON response builders
 */

#ifndef HANDLERS_COMMON_H
#define HANDLERS_COMMON_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <cstdint>
#include <cstdio>

/** @brief Build JSON ok response: {"id":...,"status":"ok","data":...} */
inline void make_response_ok(char* buf, size_t buf_size, uint64_t id, const char* data) {
    snprintf(buf, buf_size, "{\"id\":%" PRIu64 ",\"status\":\"ok\",\"data\":%s}", id, data);
}

/** @brief Build JSON error response: {"id":...,"status":"error","message":"..."} */
inline void make_response_error(char* buf, size_t buf_size, uint64_t id, const char* msg) {
    snprintf(buf, buf_size, "{\"id\":%" PRIu64 ",\"status\":\"error\",\"message\":\"%s\"}", id, msg);
}

const char* get_param_string(const JsonDocument& doc, const char* key, const char* default_val = "");

int get_param_int(const JsonDocument& doc, const char* key, int default_val = 0);

#endif // HANDLERS_COMMON_H