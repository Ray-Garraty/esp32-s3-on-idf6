/**
 * @file wifi_manager.cpp
 * @brief WiFi connection manager with captive portal
 *
 * Handles WiFi credentials storage, connection management,
 * and captive portal for initial configuration
 * Uses the main AsyncWebServer instance (no separate server)
 */

#include "wifi_manager.h"
#include "logger.h"
#include "config.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include <ESPAsyncWebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <time.h>
#include <atomic>
#include "esp_task_wdt.h"

// Captive portal DNS redirect IP
static IPAddress ap_ip(192, 168, 4, 1);
static IPAddress ap_gateway(192, 168, 4, 1);
static IPAddress ap_subnet(255, 255, 255, 0);

// DNS server for captive portal
static DNSServer dns_server;

// Reference to main web server
static AsyncWebServer* main_server = nullptr;

// State tracking
static std::atomic<bool> is_ap_mode{false};

static const uint16_t WIFI_POLL_DELAY_MS = 500;
static const uint16_t WIFI_POST_CONNECT_DELAY_MS = 500;
static const uint16_t WIFI_MDNS_DELAY_MS = 100;
static const uint32_t WIFI_RECONNECT_INTERVAL_MS = 30000;
static const uint8_t WIFI_CRED_MAX_LEN = 64;
static const uint8_t NTP_TIME_STR_BUF_SIZE = 32;

// Forward declarations
static bool try_connect_wifi(const char* ssid, const char* password);
static bool load_wifi_credentials(char* ssid_buf, size_t ssid_buf_size, char* pass_buf, size_t pass_buf_size);
static bool save_wifi_credentials(const char* ssid, const char* password);
static bool clear_wifi_credentials(void);
static void start_captive_portal(void);
static void register_captive_routes(AsyncWebServer* server);

// HTML for captive portal
static const char* CAPTIVE_PORTAL_HTML = R"rawliteral(
<!DOCTYPE html>
<html lang="ru">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>EcoTiter - Настройка WiFi</title>
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body {
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            min-height: 100vh;
            display: flex;
            align-items: center;
            justify-content: center;
            padding: 20px;
        }
        .container {
            background: white;
            border-radius: 16px;
            box-shadow: 0 20px 60px rgba(0,0,0,0.3);
            max-width: 400px;
            width: 100%;
            padding: 40px;
        }
        h1 { color: #333; margin-bottom: 10px; font-size: 24px; }
        .subtitle { color: #666; margin-bottom: 30px; font-size: 14px; }
        .form-group { margin-bottom: 20px; }
        label { display: block; margin-bottom: 8px; color: #555; font-weight: 500; font-size: 14px; }
        input[type="text"], input[type="password"] {
            width: 100%;
            padding: 12px 16px;
            border: 2px solid #e0e0e0;
            border-radius: 8px;
            font-size: 16px;
            transition: border-color 0.3s;
        }
        input:focus { outline: none; border-color: #667eea; }
        button {
            width: 100%;
            padding: 14px;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            color: white;
            border: none;
            border-radius: 8px;
            font-size: 16px;
            font-weight: 600;
            cursor: pointer;
            transition: transform 0.2s, box-shadow 0.2s;
        }
        button:hover { transform: translateY(-2px); box-shadow: 0 10px 20px rgba(102, 126, 234, 0.3); }
        button:active { transform: translateY(0); }
        button:disabled { opacity: 0.5; cursor: not-allowed; transform: none; }
        #toggle-password.active { background: #667eea !important; border-color: #667eea !important; color: white !important; }
        .status { margin-top: 20px; padding: 12px; border-radius: 8px; font-size: 14px; display: none; }
        .status.info { background: #e3f2fd; color: #1976d2; display: block; }
        .status.success { background: #e8f5e9; color: #388e3c; display: block; }
        .status.error { background: #ffebee; color: #d32f2f; display: block; }
        .spinner {
            display: inline-block;
            width: 16px; height: 16px;
            border: 2px solid #f3f3f3;
            border-top: 2px solid #667eea;
            border-radius: 50%;
            animation: spin 1s linear infinite;
            margin-right: 8px;
            vertical-align: middle;
        }
        @keyframes spin { 0% { transform: rotate(0deg); } 100% { transform: rotate(360deg); } }
    </style>
</head>
<body>
    <div class="container">
        <h1>🌐 EcoTiter</h1>
        <p class="subtitle">Настройка подключения к WiFi сети</p>
        <form id="wifi-form">
            <div class="form-group">
                <label for="ssid">Название WiFi сети (SSID)</label>
                <input type="text" id="ssid" name="ssid" required placeholder="Введите SSID сети">
            </div>
            <div class="form-group">
                <label for="password">Пароль</label>
                <div style="display: flex; gap: 8px;">
                    <input type="password" id="password" name="password" required placeholder="Введите пароль" style="flex: 1;">
                    <button type="button" id="toggle-password" onclick="togglePasswordVisibility()" 
                            style="width: 48px; min-width: 48px; font-size: 20px; padding: 8px; background: #e0e0e0; border: 2px solid #e0e0e0; border-radius: 8px; cursor: pointer;">👁</button>
                </div>
            </div>
            <button type="submit" id="submit-btn">Подключиться</button>
        </form>
        <div id="status" class="status"></div>
    </div>
    <script>
        function togglePasswordVisibility() {
            const pw = document.getElementById('password');
            const btn = document.getElementById('toggle-password');
            pw.type = pw.type === 'password' ? 'text' : 'password';
            btn.classList.toggle('active', pw.type === 'text');
        }

        const form = document.getElementById('wifi-form');
        const submitBtn = document.getElementById('submit-btn');
        const statusDiv = document.getElementById('status');
        form.addEventListener('submit', async (e) => {
            e.preventDefault();
            const ssid = document.getElementById('ssid').value;
            const password = document.getElementById('password').value;
            submitBtn.disabled = true;
            submitBtn.innerHTML = '<span class="spinner"></span> Подключение...';
            statusDiv.className = 'status info';
            statusDiv.innerHTML = 'Пробуем подключиться к сети...';
            try {
                const response = await fetch('/wifi/connect', {
                    method: 'POST',
                    headers: {'Content-Type': 'application/json'},
                    body: JSON.stringify({ssid: ssid, password: password})
                });
                const result = await response.json();
                if (result.success) {
                    statusDiv.className = 'status success';
                    statusDiv.innerHTML = '✓ Подключение успешно! Сохранение настроек и перезагрузка...';
                    submitBtn.innerHTML = '✓ Готово!';
                    setTimeout(() => {
                        statusDiv.innerHTML += '<br><br>Устройство перезагружается. Подождите 10-15 секунд.';
                    }, 2000);
                    return; // Устройство перезагружается — дальше код не выполняется
                } else {
                    statusDiv.className = 'status error';
                    statusDiv.innerHTML = '✗ Ошибка: ' + (result.message || 'Не удалось подключиться');
                    submitBtn.disabled = false;
                    submitBtn.innerHTML = 'Подключиться';
                }
            } catch (error) {
                // Если соединение оборвалось, но устройство перезагружается — это успех
                // Проверяем, не была ли это ложная ошибка при успешном подключении
                statusDiv.className = 'status error';
                statusDiv.innerHTML = '✗ Соединение прервано. Если подключение было успешным, устройство перезагружается. Подождите 10-15 секунд.';
                submitBtn.disabled = false;
                submitBtn.innerHTML = 'Подключиться';
            }
        });
    </script>
</body>
</html>
)rawliteral";

static Preferences prefs;

/**
 * @brief Load saved WiFi credentials from Preferences (EEPROM-like storage)
 */
static bool load_wifi_credentials(char* ssid_buf, size_t ssid_buf_size, char* pass_buf, size_t pass_buf_size) {
    prefs.begin("wifi", true);  // read-only mode
    String saved_ssid = prefs.getString("ssid", "");
    String saved_pass = prefs.getString("password", "");
    prefs.end();

    if (saved_ssid.length() == 0 && saved_pass.length() == 0) {
        logger.info("No WiFi credentials found in Preferences (both ssid and password empty)");
        return false;
    }

    if (saved_ssid.length() == 0) {
        logger.warn("WiFi credentials found but ssid is empty (password present)");
        return false;
    }

    if (saved_pass.length() == 0) {
        logger.warn("WiFi credentials found but password is empty (ssid=%s)", saved_ssid.c_str());
        return false;
    }

    strncpy(ssid_buf, saved_ssid.c_str(), ssid_buf_size - 1);
    ssid_buf[ssid_buf_size - 1] = '\0';
    strncpy(pass_buf, saved_pass.c_str(), pass_buf_size - 1);
    pass_buf[pass_buf_size - 1] = '\0';

    logger.info("WiFi credentials loaded from Preferences: %s", ssid_buf);
    return true;
}

/**
 * @brief Save WiFi credentials to Preferences (EEPROM-like storage)
 */
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
static bool save_wifi_credentials(const char* ssid, const char* password) {
    prefs.begin("wifi", false);  // read-write mode
    prefs.putString("ssid", ssid);
    prefs.putString("password", password);
    prefs.end();  // end() автоматически вызывает commit()

    logger.info("WiFi credentials saved to Preferences: %s", ssid);
    return true;
}

/**
 * @brief Clear saved WiFi credentials
 */
static bool clear_wifi_credentials(void) {
    prefs.begin("wifi", false);
    prefs.clear();
    prefs.end();

    logger.info("WiFi credentials cleared from Preferences");
    return true;
}

/**
 * @brief Try to connect to WiFi network
 */
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
static bool try_connect_wifi(const char* ssid, const char* password) {
    logger.info("Trying to connect to WiFi: %s", ssid);

    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_CONNECT_TIMEOUT_MS) {
        vTaskDelay(pdMS_TO_TICKS(WIFI_POLL_DELAY_MS));
    }

    if (WiFi.status() == WL_CONNECTED) {
        logger.info("WiFi connected! IP: %s, RSSI: %d dBm",
                    WiFi.localIP().toString().c_str(), WiFi.RSSI());
        
        vTaskDelay(pdMS_TO_TICKS(WIFI_POST_CONNECT_DELAY_MS));
        time_t now = time(nullptr);
        if (now > NTP_MIN_VALID_TIMESTAMP) {
            logger.syncTime();
            char buf[NTP_TIME_STR_BUF_SIZE];
            strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", localtime(&now));
            logger.info("NTP synchronized: %s", buf);
        } else {
            logger.warn("NTP not yet synchronized, using uptime timestamps");
        }
        
        vTaskDelay(pdMS_TO_TICKS(WIFI_MDNS_DELAY_MS));
        if (MDNS.begin("ecotiter")) {
            logger.info("mDNS hostname: http://ecotiter.local");
            MDNS.addService("http", "tcp", 80);
            logger.info("mDNS service registered");
        } else {
            logger.error("mDNS failed to start");
        }
        
        return true;
    } else {
        logger.error("WiFi connection failed for: %s", ssid);
        WiFi.disconnect();
        return false;
    }
}

/**
 * @brief Register captive portal routes on the main server
 */
static void register_captive_routes(AsyncWebServer* server) {
    // Captive portal page
    server->on("/wifi", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(200, "text/html", CAPTIVE_PORTAL_HTML);
    });

    // Handle WiFi connection request
    server->on("/wifi/connect", HTTP_POST, [](AsyncWebServerRequest* request) {}, NULL,
        [](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            String body = String((char*)data);
            JsonDocument doc;
            DeserializationError err = deserializeJson(doc, body);

            if (err) {
                request->send(400, "application/json", "{\"success\":false,\"message\":\"Invalid JSON\"}");
                return;
            }

            const char* ssid = doc["ssid"];
            const char* password = doc["password"];

            if (ssid == nullptr || password == nullptr) {
                request->send(400, "application/json", "{\"success\":false,\"message\":\"Missing ssid or password\"}");
                return;
            }

            logger.info("WiFi connection attempt for: %s", ssid);

            // Try to connect
            bool connected = try_connect_wifi(ssid, password);

            if (connected) {
                if (save_wifi_credentials(ssid, password)) {
                    request->send(200, "application/json", "{\"success\":true,\"message\":\"Connected and saved\"}");
                    logger.info("WiFi connected successfully, restarting in STA mode");
                    ESP.restart();
                } else {
                    request->send(500, "application/json", "{\"success\":false,\"message\":\"Failed to save credentials\"}");
                }
            } else {
                request->send(200, "application/json", "{\"success\":false,\"message\":\"Connection failed. Check SSID and password.\"}");
            }
        }
    );

    // WiFi status
    server->on("/wifi/status", HTTP_GET, [](AsyncWebServerRequest* request) {
        JsonDocument doc;
        doc["ap_mode"] = is_ap_mode.load(std::memory_order_relaxed);
        doc["connected"] = WiFi.status() == WL_CONNECTED;
        if (WiFi.status() == WL_CONNECTED) {
            doc["ip"] = WiFi.localIP().toString();
            doc["ssid"] = WiFi.SSID();
            doc["rssi"] = WiFi.RSSI();
        }
        String json;
        serializeJson(doc, json);
        request->send(200, "application/json", json);
    });
}

/**
 * @brief Start captive portal mode
 */
static void start_captive_portal(void) {
    logger.info("Starting captive portal mode...");

    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(ap_ip, ap_gateway, ap_subnet);
    WiFi.softAP(AP_SSID, AP_PASSWORD, AP_CHANNEL, AP_HIDDEN, AP_MAX_CONNECTIONS);

    logger.info("AP started: SSID=%s, IP=%s", AP_SSID, AP_IP_ADDRESS);

    // Start DNS server (redirect all to AP IP)
    dns_server.start(53, "*", ap_ip);

    // Register captive portal routes on main server
    if (main_server != nullptr) {
        register_captive_routes(main_server);
        
        // Captive portal redirect for unknown routes
        main_server->onNotFound([](AsyncWebServerRequest* request) {
            request->redirect("http://" + ap_ip.toString() + "/wifi");
        });
    }

    is_ap_mode.store(true, std::memory_order_relaxed);
    logger.info("Captive portal ready! Connect to: %s, then http://%s/wifi", AP_SSID, AP_IP_ADDRESS);
}

/**
 * @brief Initialize WiFi manager
 */
void wifi_manager_init(AsyncWebServer* server) {
    main_server = server;

    logger.info("Initializing WiFi manager...");

    char ssid[WIFI_CRED_MAX_LEN] = {0};
    char password[WIFI_CRED_MAX_LEN] = {0};

    // Try to load saved credentials
    bool loaded = load_wifi_credentials(ssid, sizeof(ssid), password, sizeof(password));
    logger.info("WiFi credentials loaded: %s", loaded ? "YES" : "NO");

    if (loaded) {
        logger.info("Attempting to connect with saved SSID: %s", ssid);
        if (try_connect_wifi(ssid, password)) {
            logger.info("Connected to WiFi with saved credentials!");
            is_ap_mode.store(false, std::memory_order_relaxed);
            return;
        }
        logger.warn("Failed to connect with saved credentials, clearing");
        clear_wifi_credentials();
        is_ap_mode.store(true, std::memory_order_relaxed);
    } else {
        logger.info("No saved WiFi credentials found");
    }

    // No valid credentials - start AP mode
    logger.info("Starting captive portal");
    start_captive_portal();
}

/**
 * @brief Attempt to reconnect to WiFi (called from loop)
 */
void wifi_manager_reconnect(void) {
    if (is_ap_mode.load(std::memory_order_relaxed)) {
        dns_server.processNextRequest();
        return;
    }

    static unsigned long last_reconnect_attempt = 0;
    unsigned long current_time = millis();

    if (current_time - last_reconnect_attempt > WIFI_RECONNECT_INTERVAL_MS) {
        logger.info("Attempting WiFi reconnection...");
        WiFi.reconnect();
        last_reconnect_attempt = current_time;
    }
}

/**
 * @brief Process WiFi manager tasks (called from loop)
 */
void wifi_manager_process(void) {
    if (is_ap_mode.load(std::memory_order_relaxed)) {
        dns_server.processNextRequest();
    }
}


