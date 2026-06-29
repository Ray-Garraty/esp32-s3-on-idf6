#include "serial.h"
#include "common.h"

String handle_serial_ping(const JsonDocument& doc, uint64_t id, bool* success, char* response_buf, size_t buf_size) {
    (void)doc;
    snprintf(response_buf, buf_size,
             "{\"id\":%" PRIu64 ",\"status\":\"ok\",\"data\":{\"status\":\"ok\"}}", id);
    *success = true;
    return String(response_buf);
}
