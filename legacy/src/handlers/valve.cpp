/**
 * @file handlers/valve.cpp
 * @brief Valve command handlers
 */

#include "valve.h"
#include "../webserver.h"
#include "../valve.h"
#include "../stepper.h"
#include <string.h>

static const uint16_t VALVE_STATE_BUF_SIZE = 128;

String handle_valve_set_position(const JsonDocument& doc, uint64_t id, bool* success, char* response_buf, size_t buf_size) {
    if (!doc["position"].is<const char*>()) {
        *success = false;
        snprintf(response_buf, buf_size,
                "{\"id\":%" PRIu64 ",\"status\":\"error\",\"message\":\"position must be string\"}",
                id);
        return String(response_buf);
    }

    const char* position = doc["position"];
    if (strcmp(position, "input") != 0 && strcmp(position, "output") != 0) {
        *success = false;
        snprintf(response_buf, buf_size,
                "{\"id\":%" PRIu64 ",\"status\":\"error\",\"message\":\"position must be 'input' or 'output', got '%s'\"}",
                id, position);
        return String(response_buf);
    }

    uint64_t pending_id;
    taskENTER_CRITICAL(&g_pending_mux);
    pending_id = g_pending.id;
    taskEXIT_CRITICAL(&g_pending_mux);
    if (pending_id != 0 || stepper_is_busy()) {
        *success = false;
        snprintf(response_buf, buf_size,
                "{\"id\":%" PRIu64 ",\"status\":\"error\",\"message\":\"burette_busy\"}",
                id);
        return String(response_buf);
    }

    if (valve_set_position(position)) {
        *success = true;
        snprintf(response_buf, buf_size,
                "{\"id\":%" PRIu64 ",\"status\":\"ok\",\"data\":{\"position\":\"%s\"}}", id, position);
    } else {
        *success = false;
        snprintf(response_buf, buf_size,
                "{\"id\":%" PRIu64 ",\"status\":\"error\",\"message\":\"Invalid position\"}", id);
    }
    return String(response_buf);
}

String handle_valve_get_state(const JsonDocument& doc, uint64_t id, bool* success, char* response_buf, size_t buf_size) {
    (void)doc;
    char state_json[VALVE_STATE_BUF_SIZE];
    if (valve_get_state_json(state_json, sizeof(state_json))) {
        *success = true;
        snprintf(response_buf, buf_size,
                "{\"id\":%" PRIu64 ",\"status\":\"ok\",\"data\":%s}", id, state_json);
    } else {
        *success = false;
        snprintf(response_buf, buf_size,
                "{\"id\":%" PRIu64 ",\"status\":\"error\",\"message\":\"Failed to get state\"}", id);
    }
    return String(response_buf);
}