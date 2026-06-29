#ifndef LOGGER_H
#define LOGGER_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <atomic>
#include <cstddef>

#define LOGGER_BUFFER_SIZE          100
#define LOGGER_MAX_MSG_LEN          128

#define LOG_LEVEL_INFO      "INFO"
#define LOG_LEVEL_WARN     "WARN"
#define LOG_LEVEL_ERROR    "ERROR"
#define LOG_LEVEL_DEBUG    "DEBUG"

struct log_entry_t {
    uint32_t timestamp;
    char level[16];
    char message[LOGGER_MAX_MSG_LEN];
};

typedef void (*log_callback_t)(const char* level, const char* msg);

class FileLogger {
public:
    FileLogger();
    void init();
    void set_callback(log_callback_t callback);

    void log(const char* level, const char* fmt, ...) __attribute__((format(printf, 3, 4)));
    void info(const char* fmt, ...)  __attribute__((format(printf, 2, 3)));
    void warn(const char* fmt, ...)  __attribute__((format(printf, 2, 3)));
    void error(const char* fmt, ...) __attribute__((format(printf, 2, 3)));
    void debug(const char* fmt, ...) __attribute__((format(printf, 2, 3)));

    String getLogs(int start = 0, int limit = 100, const char* level = nullptr);
    String getFormattedLogsFromFile(int limit = -1);
    void compactFile();
    void syncTime();
    void clear();

    int count = 0;
    SemaphoreHandle_t buffer_mutex = nullptr;

private:
    log_entry_t buffer[LOGGER_BUFFER_SIZE];
    int head = 0;
    log_callback_t callback = nullptr;
    bool _initialized = false;

    void addToBuffer(const char* level, const char* msg);
    String entryToJSON(const log_entry_t& entry);
};

extern FileLogger logger;
extern std::atomic<bool> g_serial_silent;

#endif // LOGGER_H
