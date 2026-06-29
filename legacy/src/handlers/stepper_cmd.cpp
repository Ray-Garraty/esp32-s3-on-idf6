#include "stepper_cmd.h"
#include "../stepper.h"
#include "../logger.h"
#include "common.h"
#include <ArduinoJson.h>

static StepperDirection parse_direction(const JsonDocument& doc) {
    const char* dir_str = doc["direction"] | "";
    if (strcmp(dir_str, "LIQ_IN") == 0) return LIQ_IN;
    return LIQ_OUT;
}

String handle_burette_move_steps(const JsonDocument& doc, uint64_t id, bool* success, char* response_buf, size_t buf_size) {
    int steps = doc["steps"] | 0;
    int freq = doc["freq"] | 0;
    if (steps <= 0 || freq <= 0) {
        *success = false;
        make_response_error(response_buf, buf_size, id, "invalid_params");
        return String(response_buf);
    }
    StepperDirection dir = parse_direction(doc);
    StepperResult res = stepper_move_steps((uint32_t)steps, dir, (uint16_t)freq);
    if (res == STEPPER_OK) {
        *success = true;
        make_response_ok(response_buf, buf_size, id, "{\"status\":\"accepted\"}");
    } else {
        *success = false;
        const char* msg = (res == STEPPER_ERR_BUSY) ? "burette_busy" : "start_failed";
        make_response_error(response_buf, buf_size, id, msg);
    }
    return String(response_buf);
}

String handle_burette_move_to_stop(const JsonDocument& doc, uint64_t id, bool* success, char* response_buf, size_t buf_size) {
    int freq = doc["freq"] | 0;
    if (freq <= 0) {
        *success = false;
        make_response_error(response_buf, buf_size, id, "invalid_params");
        return String(response_buf);
    }
    StepperDirection dir = parse_direction(doc);
    StepperResult res = stepper_move_to_stop(dir, (uint16_t)freq);
    if (res == STEPPER_OK) {
        *success = true;
        make_response_ok(response_buf, buf_size, id, "{\"status\":\"accepted\"}");
    } else {
        *success = false;
        const char* msg = (res == STEPPER_ERR_AT_LIMIT_FULL) ? "limit_full_reached" :
                          (res == STEPPER_ERR_AT_LIMIT_EMPTY) ? "limit_empty_reached" :
                          (res == STEPPER_ERR_BUSY) ? "burette_busy" : "start_failed";
        make_response_error(response_buf, buf_size, id, msg);
    }
    return String(response_buf);
}

String handle_burette_set_direction(const JsonDocument& doc, uint64_t id, bool* success, char* response_buf, size_t buf_size) {
    StepperDirection dir = parse_direction(doc);
    stepper_set_direction(dir);
    *success = true;
    make_response_ok(response_buf, buf_size, id, "{\"status\":\"ok\"}");
    return String(response_buf);
}
