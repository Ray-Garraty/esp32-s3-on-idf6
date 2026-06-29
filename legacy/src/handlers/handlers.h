#ifndef HANDLERS_HANDLERS_H
#define HANDLERS_HANDLERS_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <cstddef>
#include <cstdint>

#include "../command.h"
#include "common.h"
#include "burette_ops.h"
#include "valve.h"
#include "sensors.h"
#include "system.h"
#include "adc_cal.h"
#include "burette_cal_handler.h"
#include "stepper_cmd.h"
#include "serial.h"

typedef String (*cmd_handler_t)(const JsonDocument& doc, uint64_t id, bool* success, char* response_buf, size_t buf_size);

#endif
