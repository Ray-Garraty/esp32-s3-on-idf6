#ifndef HANDLERS_SERIAL_H
#define HANDLERS_SERIAL_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <cstddef>
#include <cstdint>

String handle_serial_ping(const JsonDocument& doc, uint64_t id, bool* success, char* response_buf, size_t buf_size);

#endif
