#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <time.h>
#include "esp_task_wdt.h"
#include "soc/rtc_cntl_reg.h"

#include "stepper_drv.h"
#include "stepper.h"
#include "stallguard.h"
#include "valve.h"
#include "temperature.h"
#include "adc.h"
#include "limitswitch.h"
#include "logger.h"
#include "webserver.h"
#include "command.h"
#include "led.h"
#include "wifi_manager.h"
#include "config.h"
#include "burette_cal.h"
#include "ble.h"

void IRAM_ATTR limit_full_isr();
void IRAM_ATTR limit_empty_isr();

static bool temp_conversion_started = false;
static uint32_t last_temp_time = 0;
static uint32_t last_status_time = 0;
static String serial_cmd_buffer = "";
static bool serial_cmd_ready = false;
static uint32_t last_overflow_log_ms = 0;
static uint32_t g_last_serial_activity = 0;  // 0 = no USB command ever received

static ActiveTransport g_active_transport = ACTIVE_USB;
static bool g_ble_ok = false;

static void transport_send(const char* json_line) {
    if (g_active_transport == ACTIVE_BLE) {
        ble_send(json_line, strlen(json_line));
    } else {
        Serial.printf("%s\n", json_line);
    }
}

static void process_serial_command() {
    g_last_serial_activity = millis();
    Serial.flush();
    g_serial_silent.store(true, std::memory_order_release);
    bool success = false;
    String response = execute_json_command(serial_cmd_buffer, &success);
    transport_send(response.c_str());
    Serial.flush();
    g_serial_silent.store(false, std::memory_order_release);
    logger.debug("serial_cmd_rx: %s", serial_cmd_buffer.c_str());
    logger.info("Serial cmd: %s -> %s", success ? "OK" : "ERR", serial_cmd_buffer.c_str());
    serial_cmd_buffer = "";
    serial_cmd_ready = false;
}

void setup() {
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

    Serial.setRxBufferSize(SERIAL_RX_BUFFER_SIZE);
    Serial.setTxBufferSize(SERIAL_TX_BUFFER_SIZE);
    Serial.begin(SERIAL_BAUD_RATE);
    {
        uint32_t start = millis();
        while (millis() - start < SETUP_INIT_WAIT_MS) { yield(); }
    }

    if (!LittleFS.begin(true)) {
        logger.error("LittleFS mount failed!");
    }

    configTzTime("MSK-3", "pool.ntp.org", "time.nis.gov");

    logger.init();
    esp_task_wdt_reset();

    logger.info("=== EcoTiter ESP32 Firmware v3 ===");
    logger.info("Chip: %s, Rev: %d", ESP.getChipModel(), ESP.getChipRevision());
    logger.info("CPU: %d MHz, Flash: %d KB", ESP.getCpuFreqMHz(), ESP.getFlashChipSize() / 1024);
    logger.info("Free heap: %d bytes", ESP.getFreeHeap());
    esp_task_wdt_reset();

    burette_cal_load(g_burette_cal);

    logger.info("Initializing hardware...");

    if (!stepperDrv_init()) {
        logger.warn("Stepper driver not detected");
    } else {
        logger.info("Stepper driver detected");
        if (!stepperDrv_configure_stealthchop()) {
            logger.warn("Stepper driver configuration failed");
        } else {
            stepperDrv_enable();
            logger.info("Stepper driver configured and enabled");
        }
    }
    esp_task_wdt_reset();

    if (!stallguard_init()) {
        logger.warn("StallGuard initialization failed");
    } else {
        logger.info("StallGuard initialized");
    }

    if (!stepper_init()) {
        logger.error("Stepper initialization failed!");
    } else {
        logger.info("Stepper initialized");
        stepper_set_result_callback(transport_send);
    }

    if (!valve_init()) {
        logger.error("Valve initialization failed!");
    } else {
        logger.info("Valve initialized");
    }
    esp_task_wdt_reset();

    if (!temperature_init()) {
        logger.warn("DS18B20 not detected");
    } else {
        logger.info("Temperature sensor initialized");
    }

    if (!adc_init_module()) {
        logger.error("ADC initialization failed!");
    } else {
        logger.info("ADC initialized");
    }

    limitswitch_init();
    led_init();
    led_set_transport_mode(LED_TRANSPORT_ADVERTISING);
    esp_task_wdt_reset();

    webserver_init();
    wifi_manager_init(webserver_get_server());

    pinMode(PIN_LIMIT_FULL, INPUT_PULLDOWN);
    pinMode(PIN_LIMIT_EMPTY, INPUT_PULLDOWN);
    attachInterrupt(digitalPinToInterrupt(PIN_LIMIT_FULL), limit_full_isr, RISING);
    attachInterrupt(digitalPinToInterrupt(PIN_LIMIT_EMPTY), limit_empty_isr, RISING);

    {
        uint32_t start = millis();
        while (millis() - start < PRE_HOMING_DELAY_MS) { yield(); }
    }
    stepper_clear_limits_flag();
    esp_task_wdt_reset();

    stepper_start_homing();

    webserver_start();
    esp_task_wdt_reset();
    g_ble_ok = ble_init();

    logger.info("=== HEAP AFTER BLE INIT ===");
    logger.info("Free heap: %u", ESP.getFreeHeap());
    logger.info("Max alloc block: %u", ESP.getMaxAllocHeap());
    logger.info("Free PSRAM: %u", ESP.getFreePsram());
    logger.info("============================");

    led_set_transport_mode(g_ble_ok ? LED_TRANSPORT_ADVERTISING : LED_TRANSPORT_OFF);
    logger.info("=== Setup Complete ===");
    if (WiFi.status() == WL_CONNECTED) {
        logger.info("Access dashboard at: http://%s/", WiFi.localIP().toString().c_str());
    } else {
        logger.info("AP mode: connect to EcoTiter-AP, then http://192.168.4.1/");
    }
}

void loop() {
    uint32_t current_time = millis();

    // === 1. BLE Process (FIRST — before any transport switching) ===
    ble_process();

    // === 2. Serial Processing (BEFORE Block A — updates g_last_serial_activity immediately) ===
    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\n' || c == '\r') {
            if (serial_cmd_buffer.length() > 0) {
                serial_cmd_ready = true;
                break;
            }
        } else {
            serial_cmd_buffer += c;
            if (serial_cmd_buffer.length() > 1024) {
                if (millis() - last_overflow_log_ms > 1000) {
                    logger.error("Serial buffer overflow, resetting");
                    last_overflow_log_ms = millis();
                }
                serial_cmd_buffer = "";
            }
        }
    }

    if (serial_cmd_ready) {
        process_serial_command(); // Updates g_last_serial_activity INSIDE
    }

    // === 3. Block A — Linear Transport State Machine ===
    bool usb_alive = (g_last_serial_activity > 0 &&
                     (millis() - g_last_serial_activity < USB_HEARTBEAT_TIMEOUT_MS));
    bool ble_alive = g_ble_ok && ble_is_client_connected();
    bool ble_adv = ble_is_advertising();

    if (usb_alive) {
        // === USB HAS ABSOLUTE PRIORITY ===
        if (g_active_transport != ACTIVE_USB) {
            g_active_transport = ACTIVE_USB;
            logger.info("USB active, transport switched to USB");
        }

        // Kill BLE connection if exists (clean handoff)
        // NOTE: onDisconnect callback (from NimBLE task) may call ble_start_advertising()
        // between ble_disconnect_all() and ble_stop_advertising(). This creates a ~10ms window
        // where advertising is briefly active. Harmless — the next loop iteration stops it.
        if (ble_alive) {
            ble_disconnect_all();
            ble_alive = false; // Update local flag to avoid LED/transport desync
        }

        // Stop advertising (device invisible to BLE)
        if (ble_adv || ble_is_advertising()) { // Check again in case onDisconnect restarted it
            ble_stop_advertising();
        }

        led_set_transport_mode(LED_TRANSPORT_OFF);
    }
    else {
        // === USB INACTIVE: RESTORE BLE ===
        if (ble_alive) {
            if (g_active_transport != ACTIVE_BLE) {
                g_active_transport = ACTIVE_BLE;
                logger.info("BLE active, transport switched to BLE");
            }
            if (ble_adv) {
                ble_stop_advertising(); // NimBLE should auto-stop on connect, but ensure it
            }
            led_set_transport_mode(LED_TRANSPORT_CONNECTED);
        }
        else {
            if (g_active_transport != ACTIVE_USB) {
                g_active_transport = ACTIVE_USB;
            }
            if (!ble_adv) {
                ble_start_advertising();
            }
            led_set_transport_mode(LED_TRANSPORT_ADVERTISING);
        }
    }

    // === 4. Safety Net ===
    if (!usb_alive && !ble_alive && !ble_is_advertising() && g_ble_ok) {
        ble_start_advertising();
        led_set_transport_mode(LED_TRANSPORT_ADVERTISING);
        logger.info("Standby mode, BLE advertising resumed (safety net)");
    }

    // === 5. Other Processes ===
    stepper_process();
    led_process();
    stepper_check_limits();
    stepper_process_homing();

    if (current_time - last_temp_time >= TEMP_READ_INTERVAL_MS) {
        if (!temp_conversion_started) {
            temperature_update_state();
            temperature_start_conversion();
            temp_conversion_started = true;
        } else {
            float temp;
            if (temperature_read_result(&temp)) {
                (void)temp;
            }
            temp_conversion_started = false;
            last_temp_time = current_time;
        }
    }

    // === 6. Broadcast (route to active transport) ===
    if (current_time - last_status_time >= STATUS_BROADCAST_INTERVAL_MS) {
        String serial_json = format_status_response();
        transport_send(serial_json.c_str()); // Routes based on g_active_transport
        sse_broadcast_all();                 // SSE always runs in parallel
        last_status_time = current_time;
    }

    wifi_manager_process();

    if (WiFi.status() != WL_CONNECTED) {
        wifi_manager_reconnect();
    }

    esp_task_wdt_reset();

    static uint32_t last_loop_time = 0;
    while (millis() - last_loop_time < LOOP_MIN_CYCLE_MS) {
        yield();
    }
    last_loop_time = millis();
}
