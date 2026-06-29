#include "logger.h"
#include "config.h"
#include <ArduinoJson.h>
#include <stdio.h>
#include <inttypes.h>
#include <cstring>

FileLogger logger;

std::atomic<bool> g_serial_silent{false};

static const uint16_t LOGGER_MUTEX_TIMEOUT_SHORT_MS = 2;
static const uint16_t LOGGER_MUTEX_TIMEOUT_READ_MS = 20;

FileLogger::FileLogger() {}

void FileLogger::init() {
    head = 0;
    count = 0;

    buffer_mutex = xSemaphoreCreateMutex();
    if (buffer_mutex == nullptr) {
        Serial.println("[Logger] FATAL: Could not create mutex!");
        return;
    }

    _initialized = true;
    Serial.printf("[Logger] Initialized (buffer=%d entries, RAM only)\n", LOGGER_BUFFER_SIZE);
}

void FileLogger::set_callback(log_callback_t callback) {
    this->callback = callback;
}

void FileLogger::log(const char* level, const char* fmt, ...) {
    char msg[LOGGER_MAX_MSG_LEN];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);

    addToBuffer(level, msg);

    if (strcmp(level, "DEBUG") != 0 && !g_serial_silent.load(std::memory_order_acquire)) {
        Serial.printf("[%s] %s\n", level, msg);
    }

    if (callback) {
        callback(level, msg);
    }
}

void FileLogger::info(const char* fmt, ...) {
    char msg[LOGGER_MAX_MSG_LEN];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);
    log(LOG_LEVEL_INFO, "%s", msg);
}

void FileLogger::warn(const char* fmt, ...) {
    char msg[LOGGER_MAX_MSG_LEN];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);
    log(LOG_LEVEL_WARN, "%s", msg);
}

void FileLogger::error(const char* fmt, ...) {
    char msg[LOGGER_MAX_MSG_LEN];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);
    log(LOG_LEVEL_ERROR, "%s", msg);
}

void FileLogger::debug(const char* fmt, ...) {
    char msg[LOGGER_MAX_MSG_LEN];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);
    log(LOG_LEVEL_DEBUG, "%s", msg);
}

void FileLogger::addToBuffer(const char* level, const char* msg) {
    if (buffer_mutex == nullptr) return;
    if (xSemaphoreTake(buffer_mutex, pdMS_TO_TICKS(LOGGER_MUTEX_TIMEOUT_SHORT_MS)) != pdTRUE) return;

    buffer[head].timestamp = millis();
    strncpy(buffer[head].level, level, sizeof(buffer[head].level) - 1);
    buffer[head].level[sizeof(buffer[head].level) - 1] = '\0';
    strncpy(buffer[head].message, msg, sizeof(buffer[head].message) - 1);
    buffer[head].message[sizeof(buffer[head].message) - 1] = '\0';

    head = (head + 1) % LOGGER_BUFFER_SIZE;
    if (count < LOGGER_BUFFER_SIZE) {
        count++;
    }

    xSemaphoreGive(buffer_mutex);
}

String FileLogger::entryToJSON(const log_entry_t& entry) {
    JsonDocument doc;
    doc["ts"] = entry.timestamp;
    doc["level"] = entry.level;
    doc["msg"] = entry.message;
    String json;
    serializeJson(doc, json);
    return json;
}

String FileLogger::getLogs(int start, int limit, const char* level) {
    if (buffer_mutex == nullptr) return "[]";
    if (xSemaphoreTake(buffer_mutex, pdMS_TO_TICKS(LOGGER_MUTEX_TIMEOUT_READ_MS)) != pdTRUE) return "[]";

    String result = "[";
    int added = 0;

    for (int i = 0; i < count && added < limit; i++) {
        int idx = (head - 1 - i + LOGGER_BUFFER_SIZE) % LOGGER_BUFFER_SIZE;
        if (i < start) continue;
        if (level && strcmp(buffer[idx].level, level) != 0) continue;

        if (added > 0) result += ",";
        result += entryToJSON(buffer[idx]);
        added++;
    }
    result += "]";

    xSemaphoreGive(buffer_mutex);
    return result;
}

String FileLogger::getFormattedLogsFromFile(int limit) {
    (void)limit;
    return "";
}

void FileLogger::compactFile() {
    // File logging disabled — no-op
}

void FileLogger::syncTime() {
    // File logging disabled — no-op
}

void FileLogger::clear() {
    head = 0;
    count = 0;
}
