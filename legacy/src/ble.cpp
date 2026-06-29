#include "ble.h"
#include "config.h"
#include "logger.h"
#include "command.h"

// NimBLE defines LOG_LEVEL_* as integer constants; logger.h defines them as strings.
// Undef logger's macros before NimBLE include to suppress redefinition warnings.
#undef LOG_LEVEL_DEBUG
#undef LOG_LEVEL_INFO
#undef LOG_LEVEL_WARN
#undef LOG_LEVEL_ERROR
#include <NimBLEDevice.h>
#include <cstring>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <esp_coexist.h>

#define NUS_SERVICE_UUID "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define NUS_RX_CHAR_UUID "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define NUS_TX_CHAR_UUID "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

static NimBLEServer* g_server = nullptr;
static NimBLECharacteristic* g_tx_char = nullptr;
static xQueueHandle g_cmd_queue = nullptr;
static std::atomic<bool> g_ble_connected{false};
static std::atomic<bool> g_ble_advertising{false};
static char g_device_name[16];
static uint16_t g_conn_handle = 0xFFFF; // BLE_HS_CONN_HANDLE_NONE

// === Deferred LL Procedures State ===
static uint32_t g_connection_established_ms = 0;
static bool g_conn_params_updated = false;

// === Zombie Detection State ===
static int g_ble_notify_fail_count = 0;

// Forward declaration
static void ble_kill_zombie(void);

class ServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) override {
        g_conn_handle = connInfo.getConnHandle();
        g_ble_notify_fail_count = 0;
        g_ble_connected = true;
        g_connection_established_ms = millis();
        g_conn_params_updated = false;
        logger.info("BLE client connected, handle=%u, LL procedures deferred for %ums",
                    g_conn_handle, BLE_CONN_PARAM_DELAY_MS);
    }

    void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) override {
        (void)pServer;
        (void)connInfo;
        g_ble_connected = false;
        g_conn_handle = 0xFFFF;
        g_ble_notify_fail_count = 0;
        g_connection_established_ms = 0;
        g_conn_params_updated = false;
        ble_flush_cmd_queue();
        ble_start_advertising();
        logger.info("BLE client disconnected (native), reason=%d (0x%x)", reason, reason);
    }
};

class CmdRxCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override {
        (void)connInfo;

        std::string rx_data = pCharacteristic->getValue();
        if (rx_data.empty()) return;

        char* cmd_str = strdup(rx_data.c_str());
        if (cmd_str) {
            if (xQueueSend(g_cmd_queue, &cmd_str, pdMS_TO_TICKS(10)) != pdPASS) {
                free(cmd_str);
                logger.warn("BLE cmd queue full, dropping message");
            }
        }
    }
};

static void build_device_name(void) {
    uint64_t mac_val = ESP.getEfuseMac();
    uint8_t* mac = (uint8_t*)&mac_val;
    snprintf(g_device_name, sizeof(g_device_name), "%s%02X%02X",
             BLE_DEVICE_NAME_PREFIX, mac[4], mac[5]);
}

bool ble_init(void) {
    build_device_name();

    if (!NimBLEDevice::init(g_device_name)) {
        logger.error("NimBLE init failed, check memory or NVS");
        return false;
    }
    NimBLEDevice::setPower(9);
    NimBLEDevice::setMTU(BLE_MTU_SIZE);
    NimBLEDevice::setSecurityAuth(false, false, false);
    esp_coex_preference_set(ESP_COEX_PREFER_BT);

    g_server = NimBLEDevice::createServer();
    g_server->setCallbacks(new ServerCallbacks());

    NimBLEService* service = g_server->createService(NUS_SERVICE_UUID);

    NimBLECharacteristic* rx_char = service->createCharacteristic(
        NUS_RX_CHAR_UUID,
        NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
    );
    rx_char->setCallbacks(new CmdRxCallbacks());

    g_tx_char = service->createCharacteristic(
        NUS_TX_CHAR_UUID,
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY
    );

    rx_char->setValue("");

    g_cmd_queue = xQueueCreate(BLE_CMD_QUEUE_LENGTH, sizeof(char*));
    if (g_cmd_queue == nullptr) {
        logger.error("Failed to create BLE command queue");
        return false;
    }

    ble_start_advertising();

    logger.info("BLE initialized, device name: %s", g_device_name);
    return true;
}

void ble_start_advertising(void) {
    if (g_ble_advertising) return;

    NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
    adv->stop();

    NimBLEAdvertisementData advData;
    advData.setName(g_device_name);
    advData.setFlags(BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP);
    adv->setAdvertisementData(advData);

    NimBLEAdvertisementData scanData;
    scanData.setCompleteServices(NimBLEUUID(NUS_SERVICE_UUID));
    adv->setScanResponseData(scanData);

    adv->setMinInterval(BLE_ADVERTISING_INTERVAL);
    adv->setMaxInterval(BLE_ADVERTISING_INTERVAL);

    if (!adv->start()) {
        logger.error("BLE advertising start FAILED");
        return;
    }

    g_ble_advertising = true;
    logger.info("BLE advertising started, name: %s", g_device_name);
}

void ble_stop_advertising(void) {
    if (!g_ble_advertising) return;

    NimBLEDevice::getAdvertising()->stop();
    g_ble_advertising = false;
    logger.info("BLE advertising stopped");
}

void ble_disconnect_all(void) {
    if (g_server && g_conn_handle != 0xFFFF) {
        g_server->disconnect(g_conn_handle);
        logger.info("BLE disconnect_all: handle=%u", g_conn_handle);
    }
}

void ble_flush_cmd_queue(void) {
    char* cmd_str = nullptr;
    while (xQueueReceive(g_cmd_queue, &cmd_str, 0) == pdPASS) {
        if (cmd_str) free(cmd_str);
    }
}

bool ble_is_client_connected(void) {
    return g_ble_connected.load(std::memory_order_acquire);
}

bool ble_is_advertising(void) {
    return g_ble_advertising.load(std::memory_order_acquire);
}

// === Zombie cleanup: unified entry point ===
static void ble_kill_zombie(void) {
    if (g_server && g_conn_handle != 0xFFFF) {
        g_server->disconnect(g_conn_handle);
    }
    g_ble_connected = false;
    g_conn_handle = 0xFFFF;
    g_ble_notify_fail_count = 0;
    ble_flush_cmd_queue();
    ble_start_advertising();
    logger.info("BLE zombie killed, advertising resumed");
}

void ble_send(const char* data, size_t len) {
    // Level 3: Check if NimBLE stack knows client is gone (onDisconnect undelivered)
    if (g_server && g_server->getConnectedCount() == 0 && g_ble_connected.load()) {
        logger.warn("ble_send: Stack says 0 clients, but g_ble_connected=true. Killing zombie.");
        ble_kill_zombie();
        return;
    }

    if (!g_ble_connected.load(std::memory_order_acquire) || !g_tx_char) {
        logger.warn("ble_send: dropped %u bytes (connected=%d)", len, g_ble_connected.load());
        return;
    }

    char buf[BLE_MTU_SIZE];
    size_t copy_len = (len < BLE_MTU_SIZE - 1) ? len : (BLE_MTU_SIZE - 1);
    memcpy(buf, data, copy_len);
    buf[copy_len] = '\n';

    if (!g_tx_char->notify((const uint8_t*)buf, copy_len + 1)) {
        // Level 1: Notify failure detection
        g_ble_notify_fail_count++;
        logger.warn("BLE notify failed (%d consecutive)", g_ble_notify_fail_count);
        if (g_ble_notify_fail_count >= BLE_NOTIFY_FAIL_THRESHOLD) {
            logger.error("BLE notify failing consistently (%d), killing zombie", g_ble_notify_fail_count);
            ble_kill_zombie();
        }
        return;
    }
    g_ble_notify_fail_count = 0;
}

void ble_process(void) {
    // Deferred connection parameter update (avoid LL conflicts in first N ms)
    if (g_connection_established_ms > 0 && !g_conn_params_updated &&
        millis() - g_connection_established_ms > BLE_CONN_PARAM_DELAY_MS) {
        if (g_ble_connected.load() && g_conn_handle != 0xFFFF) {
            g_server->updateConnParams(g_conn_handle, 24, 40, 0, 500);
            g_conn_params_updated = true;
            logger.info("Conn params updated after %ums delay (30-50ms interval)",
                        BLE_CONN_PARAM_DELAY_MS);
        }
    }

    char* cmd_str = nullptr;
    while (xQueueReceive(g_cmd_queue, &cmd_str, 0) == pdPASS) {
        if (cmd_str) {
            bool success = false;
            String response = execute_json_command(String(cmd_str), &success);
            ble_send(response.c_str(), response.length());
            free(cmd_str);
        }
    }
}
