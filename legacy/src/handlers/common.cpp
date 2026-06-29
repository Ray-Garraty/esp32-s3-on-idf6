/**
 * @file handlers/common.cpp
 * @brief Common utilities for handlers
 */

#include "common.h"
#include <string.h>
#include <stdlib.h>

const char* get_param_string(const JsonDocument& doc, const char* key, const char* default_val) {
    auto val = doc[key];
    if (val.is<const char*>()) {
        return val.as<const char*>();
    }
    return default_val;
}

int get_param_int(const JsonDocument& doc, const char* key, int default_val) {
    auto val = doc[key];
    if (val.is<int>()) {
        return val.as<int>();
    }
    if (val.is<const char*>()) {
        return atoi(val.as<const char*>());
    }
    if (val.is<double>()) {
        return static_cast<int>(val.as<double>());
    }
    return default_val;
}
