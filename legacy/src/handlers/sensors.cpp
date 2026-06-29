/**
 * @file handlers/sensors.cpp
 * @brief Sensor command handlers: temperature, stallguard
 */

#include "sensors.h"
#include "../webserver.h"
#include "../logger.h"
#include "../temperature.h"
#include "../stallguard.h"
#include <string.h>

extern void sse_send_log(const char* level, const char* msg);

String handle_temp_read(const JsonDocument& doc, uint64_t id, bool* success, char* response_buf, size_t buf_size) {
    (void)doc;
    temp_sensor_state_t temp_state = temperature_get_state();
    if (temp_state.isConnected && temp_state.value != 0.0f) {
        *success = true;
        snprintf(response_buf, buf_size,
                "{\"id\":%" PRIu64 ",\"status\":\"ok\",\"data\":{\"temperature\":%.1f}}", id, temp_state.value);
    } else {
        *success = false;
        snprintf(response_buf, buf_size,
                "{\"id\":%" PRIu64 ",\"status\":\"error\",\"message\":\"Failed to read temperature\"}", id);
    }
    return String(response_buf);
}

String handle_sg_get_threshold(const JsonDocument& doc, uint64_t id, bool* success, char* response_buf, size_t buf_size) {
    (void)doc;
    uint8_t threshold;
    if (stallguard_get_threshold(&threshold)) {
        *success = true;
        snprintf(response_buf, buf_size,
                "{\"id\":%" PRIu64 ",\"status\":\"ok\",\"data\":{\"sgThrs\":%d}}", id, threshold);
    } else {
        *success = false;
        snprintf(response_buf, buf_size,
                "{\"id\":%" PRIu64 ",\"status\":\"error\",\"message\":\"Failed to get threshold\"}", id);
    }
    return String(response_buf);
}

String handle_sg_set_threshold(const JsonDocument& doc, uint64_t id, bool* success, char* response_buf, size_t buf_size) {
    int threshold = doc["threshold"] | -1;
    if (threshold < 0 || threshold > STALLGUARD_MAX_VALUE) {
        *success = false;
        snprintf(response_buf, buf_size,
                "{\"id\":%" PRIu64 ",\"status\":\"error\",\"message\":\"Threshold must be between 0 and %d\"}", id, STALLGUARD_MAX_VALUE);
        return String(response_buf);
    }
    if (stallguard_set_threshold(threshold)) {
        *success = true;
        snprintf(response_buf, buf_size,
                "{\"id\":%" PRIu64 ",\"status\":\"ok\"}", id);
    } else {
        *success = false;
        snprintf(response_buf, buf_size,
                "{\"id\":%" PRIu64 ",\"status\":\"error\",\"message\":\"Failed to set threshold\"}", id);
    }
    return String(response_buf);
}
