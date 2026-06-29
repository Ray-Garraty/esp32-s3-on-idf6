#include "system.h"
#include "common.h"
#include "../webserver.h"
#include "../logger.h"
#include "../config.h"
#include <inttypes.h>
#include <cstring>
#include <cstdio>
#include <ArduinoJson.h>

extern void sse_send_log(const char* level, const char* msg);

static const uint8_t JSON_TAIL_OVERHEAD = 4;

// -------------------------------------------------------------------
// Безопасная сборка JSON-ответа с логированием
// -------------------------------------------------------------------

/**
 * @brief Возвращает команду system.getStatus в виде полного JSON-ответа.
 */
String handle_system_get_status(const JsonDocument& doc, uint64_t id, bool* success, char* response_buf, size_t buf_size) {
    (void)doc;
    String json = format_status_response();
    snprintf(response_buf, buf_size,
            "{\"id\":%" PRIu64 ",\"status\":\"ok\",\"data\":%s}", id, json.c_str());
    *success = true;
    return String(response_buf);
}

/**
 * @brief Получение форматированных логов из файла (для скачивания).
 */
String handle_get_formatted_logs(const JsonDocument& doc, uint64_t id, bool* success, char* response_buf, size_t buf_size) {
    int limit = get_param_int(doc, "lines", -1);
    String logs = logger.getFormattedLogsFromFile(limit);

    // Ручная безопасная сборка JSON-ответа с экранированием строки
    const size_t header_size = snprintf(nullptr, 0, "{\"id\":%" PRIu64 ",\"status\":\"ok\",\"data\":{\"logs\":\"", id);
    const size_t footer_size = 3; // "}}"
    size_t pos = 0;

    if (buf_size <= header_size + footer_size + 1) {
        // Буфер слишком мал даже для заголовка
        *success = false;
        return String("{\"id\":0,\"status\":\"error\",\"message\":\"Response buffer too small\"}");
    }

    pos = snprintf(response_buf, buf_size, "{\"id\":%" PRIu64 ",\"status\":\"ok\",\"data\":{\"logs\":\"", id);
    // pos сейчас < buf_size, проверено выше

    // Записываем содержимое логов с экранированием кавычек и обратных слешей
    const char* src = logs.c_str();
    while (*src && pos + 2 < buf_size - footer_size) { // +2 — место на возможный экранирующий символ и сам символ
        char c = *src++;
        if (c == '"' || c == '\\') {
            if (pos + 3 >= buf_size - footer_size) break; // не хватает места для "\c"
            response_buf[pos++] = '\\';
        }
        response_buf[pos++] = c;
    }

    // Закрываем строку и JSON
    if (pos + footer_size < buf_size) {
        response_buf[pos++] = '"';
        response_buf[pos++] = '}';
        response_buf[pos++] = '}';
        response_buf[pos] = '\0';
        *success = true;
    } else {
        // Если вдруг не влезло (например, буфер мал), возвращаем ошибку
        snprintf(response_buf, buf_size, "{\"id\":%" PRIu64 ",\"status\":\"error\",\"message\":\"Logs too large for response buffer\"}", id);
        *success = false;
    }
    return String(response_buf);
}

/**
 * @brief Чтение логов из RAM-буфера (для API / командной строки).
 */
String handle_system_read_log(const JsonDocument& doc, uint64_t id, bool* success, char* response_buf, size_t buf_size) {
    int limit = get_param_int(doc, "lines", LOG_LINE_LIMIT);
    if (limit < 1) limit = 1;
    if (limit > LOG_LINE_LIMIT) limit = LOG_LINE_LIMIT;

    String logs_str = logger.getLogs(0, limit);

    JsonDocument temp;
    DeserializationError err = deserializeJson(temp, logs_str);
    if (err) {
        snprintf(response_buf, buf_size,
                "{\"id\":%" PRIu64 ",\"status\":\"error\",\"message\":\"Failed to parse logs\"}",
                id);
        *success = false;
        return String(response_buf);
    }
    JsonArray logs_arr = temp.as<JsonArray>();

    // Заголовок
    size_t pos = snprintf(response_buf, buf_size, "{\"id\":%" PRIu64 ",\"status\":\"ok\",\"data\":{\"logs\":[", id);
    if (pos >= buf_size - JSON_TAIL_OVERHEAD) {
        *success = false;
        return String("{\"id\":0,\"status\":\"error\",\"message\":\"Response buffer too small\"}");
    }

    bool first = true;
    for (JsonArray::iterator it = logs_arr.begin(); it != logs_arr.end(); ++it) {
        String entry;
        serializeJson(*it, entry);
        size_t entry_len = entry.length();

        // Резервируем место для "]}}" + '\0', плюс возможная запятая перед элементом
        size_t required_space = (first ? 0 : 1) + entry_len + JSON_TAIL_OVERHEAD;
        if (pos + required_space >= buf_size) {
            // Не влезает — прерываем цикл, вывод будет обрезан
            break;
        }

        if (!first) {
            response_buf[pos++] = ',';
        }
        first = false;
        memcpy(response_buf + pos, entry.c_str(), entry_len);
        pos += entry_len;
    }

    // Закрывающие скобки гарантированно помещаются (проверяли required_space)
    if (pos + JSON_TAIL_OVERHEAD <= buf_size) {
        response_buf[pos++] = ']';
        response_buf[pos++] = '}';
        response_buf[pos++] = '}';
        response_buf[pos] = '\0';
        *success = true;
    } else {
        // fallback — ошибка, если что-то пошло не так
        snprintf(response_buf, buf_size,
                "{\"id\":%" PRIu64 ",\"status\":\"error\",\"message\":\"Buffer overflow prevented\"}",
                id);
        *success = false;
    }
    return String(response_buf);
}