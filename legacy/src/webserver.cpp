/**
 * @file webserver.cpp
 * @brief ESPAsyncWebServer with REST API endpoints
 *
 * REST API + SSE + Static files from LittleFS
 * Command handlers extracted to src/handlers/
 */

#include "webserver.h"
#include "command.h"
#include "status.h"
#include "logger.h"
#include "led.h"
#include "ble.h"
#include "valve.h"
#include "limitswitch.h"
#include "stallguard.h"
#include "stepper.h"
#include "adc.h"
#include "config.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <WiFi.h>
#include <Preferences.h>
#include <atomic>
#include <map>
#include "handlers/handlers.h"

static AsyncWebServer server(HTTP_PORT);
static AsyncEventSource events("/api/events");

static const uint16_t VALVE_RESPONSE_BUF_SIZE = 256;
static const uint16_t STATE_JSON_BUF_SIZE = 128;
static const uint16_t SSE_STATUS_BUF_SIZE = 2048;
static const uint16_t SSE_DEBUG_BUF_SIZE = 1024;
static const uint16_t SSE_LOG_BUF_SIZE = 512;

// Track SSE client backpressure (Fix G: non-blocking SSE, enforce WebUI ≦ BLE priority)
static std::map<AsyncEventSourceClient*, uint32_t> sse_backlog_count;

void webserver_init(void) {
    logger.info("Initializing web server on port %d", HTTP_PORT);

    server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest* request) {
        String json = format_status_response();
        request->send(200, "application/json", json);
    });

    server.on("/api/command", HTTP_POST,
        [](AsyncWebServerRequest* request) {},
        NULL,
        [](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            JsonDocument doc;
            DeserializationError err = deserializeJson(doc, data, len);
            if (err) {
                request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid JSON\"}");
                return;
            }

            String json_str;
            serializeJson(doc, json_str);
            bool success = false;
            String response = execute_json_command(json_str, &success);
            if (success) {
                led_blink_success();
            } else {
                led_blink_error();
            }
            request->send(200, "application/json", response);
        }
    );

server.on("/api/valve/set", HTTP_POST,
        [](AsyncWebServerRequest* request) {},
        NULL,
        [](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            JsonDocument doc;
            DeserializationError err = deserializeJson(doc, data, len);
            if (err) {
                led_blink_error();
                request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid JSON\"}");
                return;
            }

            const char* position = doc["position"] | "input";
            if (valve_set_position(position)) {
                led_blink_success();
                char response[VALVE_RESPONSE_BUF_SIZE];
                snprintf(response, sizeof(response),
                        "{\"status\":\"ok\",\"data\":{\"position\":\"%s\"}}",
                        position);
                request->send(200, "application/json", response);
            } else {
                led_blink_error();
                request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid position\"}");
            }
        }
    );

    server.on("/api/valve/state", HTTP_GET, [](AsyncWebServerRequest* request) {
        char state_json[STATE_JSON_BUF_SIZE];
        if (valve_get_state_json(state_json, sizeof(state_json))) {
            request->send(200, "application/json", state_json);
        } else {
            request->send(500, "application/json", "{\"status\":\"error\"}");
        }
    });

    server.on("/api/logs/download", HTTP_GET, [](AsyncWebServerRequest* request) {
        String text = logger.getFormattedLogsFromFile();
        request->send(200, "text/plain; charset=utf-8", text);
    });

    server.on("/api/logs", HTTP_GET, [](AsyncWebServerRequest* request) {
        int start = 0;
        int limit = LOG_LINE_LIMIT;
        const char* level = nullptr;

        if (request->hasParam("start")) {
            start = request->getParam("start")->value().toInt();
        }
        if (request->hasParam("limit")) {
            limit = request->getParam("limit")->value().toInt();
        }
        if (request->hasParam("level")) {
            level = request->getParam("level")->value().c_str();
        }

        String logs = logger.getLogs(start, limit, level);
        String response = "{\"total\":" + String(logger.count) + ",\"entries\":" + logs + "}";
        request->send(200, "application/json", response);
    });

    server.on("/api/logs", HTTP_DELETE, [](AsyncWebServerRequest* request) {
        logger.clear();
        request->send(200, "application/json", "{\"status\":\"ok\"}");
    });

    server.on("/api/nvs/status", HTTP_GET, [](AsyncWebServerRequest* request) {
        JsonDocument doc;
        doc["freeHeap"] = ESP.getFreeHeap();
        doc["minFreeHeap"] = ESP.getMinFreeHeap();
        doc["maxAllocHeap"] = ESP.getMaxAllocHeap();

        // WiFi credentials
        Preferences wifi_prefs;
        wifi_prefs.begin("wifi", true);
        String saved_ssid = wifi_prefs.getString("ssid", "");
        wifi_prefs.end();

        doc["wifi"]["saved"] = saved_ssid.length() > 0;
        doc["wifi"]["ssid"] = saved_ssid.length() > 0 ? saved_ssid.c_str() : nullptr;

        // StallGuard threshold
        uint8_t sg_thr = 0;
        stallguard_get_threshold(&sg_thr);
        Preferences sg_prefs;
        sg_prefs.begin("stallguard", true);
        doc["stallguard"]["saved"] = sg_prefs.isKey("threshold");
        sg_prefs.end();
        doc["stallguard"]["threshold"] = sg_thr;

        String json;
        serializeJson(doc, json);
        request->send(200, "application/json", json);
    });

    server.addHandler(&events);

    events.onConnect([](AsyncEventSourceClient* client) {
        logger.info("SSE client connected");
        sse_backlog_count[client] = 0;
        JsonDocument doc;
        format_status_response_doc(doc);
        doc["bleConnected"] = ble_is_client_connected();
        String json;
        serializeJson(doc, json);
        client->send(json.c_str(), "status");
        limitswitch_state_t lim = limitswitch_read();
        char ls_buf[STATE_JSON_BUF_SIZE];
        JsonDocument ls_doc;
        ls_doc["full"] = lim.full;
        ls_doc["empty"] = lim.empty;
        serializeJson(ls_doc, ls_buf, sizeof(ls_buf));
        client->send(ls_buf, "limitsw", millis());
    });

    events.onDisconnect([](AsyncEventSourceClient* client) {
        sse_backlog_count.erase(client);
        logger.info("SSE client disconnected");
    });

    server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

    logger.info("Web server routes configured on port %d", HTTP_PORT);

    logger.set_callback(sse_send_log);
}

void webserver_start(void) {
    server.begin();
    logger.info("Web server started on port %d", HTTP_PORT);
}

void sse_broadcast_all(void) {
    // Non-blocking SSE: skip cycle if any client has backpressure,
    // drop slow clients after 5 consecutive cycles (WebUI ≦ BLE priority).
    static const size_t SSE_BACKLOG_THRESHOLD = 20;

    bool any_blocked = false;
    for (auto it = sse_backlog_count.begin(); it != sse_backlog_count.end(); ) {
        AsyncEventSourceClient* client = it->first;
        if (!client->connected()) {
            it = sse_backlog_count.erase(it);
            continue;
        }
        if (client->packetsWaiting() > SSE_BACKLOG_THRESHOLD) {
            any_blocked = true;
            it->second++;
            if (it->second >= 5) {
                logger.warn("SSE: dropping slow client (backlog >%u for 5+ cycles)", (unsigned)SSE_BACKLOG_THRESHOLD);
                client->close();
                it = sse_backlog_count.erase(it);
                continue;
            }
        } else {
            it->second = 0;
        }
        ++it;
    }
    if (any_blocked) return;

    char json_buf[SSE_STATUS_BUF_SIZE];
    JsonDocument doc;
    format_status_response_doc(doc);

    stepper_state_t stepper_state;
    stepper_get_state(&stepper_state);
    doc["burette"]["frequency"] = stepper_state.frequency;
    doc["burette"]["direction"] = stepper_state.direction_liq_in ? "LIQ_IN" : "LIQ_OUT";
    doc["burette"]["isEnabled"] = stepper_state.en_enabled;

    doc["bleConnected"] = ble_is_client_connected();
    doc["usbSerialConnected"] = false;

    // ADC raw mv (was sse_broadcast_debug)
    doc["adc"]["raw_mv"] = adc_get_raw_mv();
    doc["buretteSteps"]["taken"] = stepper_get_actual_steps();

    // Stepper driver (was sse_broadcast_debug)
    JsonDocument drv;
    drv["isConnected"] = stepperDrv_is_connected();
    if (stepperDrv_is_connected()) {
        stepperDrv_drv_status_t drv_status;
        if (stepperDrv_read_drv_status(&drv_status)) {
            drv["otpw"] = drv_status.otpw;
            drv["ot"] = drv_status.ot;
        }
        drv["motor"]["isMoving"] = stepper_state.busy;
        uint16_t sg_result = 0;
        stallguard_read_result(&sg_result);
        drv["motor"]["stallGuard"]["value"] = sg_result;
        drv["motor"]["stallGuard"]["isStalled"] = stallguard_check_stall();
        uint8_t sg_thr = 0;
        stallguard_get_threshold(&sg_thr);
        drv["motor"]["stallGuard"]["threshold"] = sg_thr;
    }
    doc["stepperDrv"] = drv;

    // Limit switch (was sse_broadcast_limitswitch)
    limitswitch_state_t lim = limitswitch_read();
    doc["limitSwitch"]["full"] = lim.full;
    doc["limitSwitch"]["empty"] = lim.empty;

    serializeJson(doc, json_buf, sizeof(json_buf));
    events.send(json_buf, "status", millis());
}

static std::atomic<bool> g_sse_logging{false};

void sse_send_log(const char* level, const char* msg) {
    if (g_sse_logging.load(std::memory_order_relaxed)) return;
    g_sse_logging.store(true, std::memory_order_relaxed);

    char json_buf[SSE_LOG_BUF_SIZE];
    JsonDocument doc;
    doc["type"] = "log";
    doc["level"] = level;
    doc["msg"] = msg;
    doc["ts"] = millis();
    serializeJson(doc, json_buf, sizeof(json_buf));
    events.send(json_buf, "log", millis());

    g_sse_logging.store(false, std::memory_order_relaxed);
}

AsyncWebServer* webserver_get_server(void) {
    return &server;
}