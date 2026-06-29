/**
 * @file webserver.h
 * @brief ESPAsyncWebServer with REST API endpoints
 *
 * REST API + SSE + Static files from LittleFS
 */

#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>

// Server port
#define HTTP_PORT 80

/** @brief Initialize web server (static files, API routes, CORS) */
void webserver_init(void);
/** @brief Start listening on HTTP_PORT */
void webserver_start(void);
/** @brief Return pointer to the AsyncWebServer instance */
AsyncWebServer* webserver_get_server(void);

/** @brief Format full status JSON string (sensors, valve, burette) */
String format_status_response(void);
/** @brief Fill JsonDocument with status data (for both SSE and Serial) */
void format_status_response_doc(JsonDocument& doc);

/** @brief Broadcast consolidated status + debug + limitswitch to all SSE clients */
void sse_broadcast_all(void);
/** @brief Send log message to SSE log stream */
void sse_send_log(const char* level, const char* msg);
#endif // WEBSERVER_H
