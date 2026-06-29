#ifndef BLE_H
#define BLE_H

#include <Arduino.h>
#include <cstddef>

enum ActiveTransport {
    ACTIVE_USB,  // Serial is active (highest priority, default)
    ACTIVE_BLE   // BLE is active (medium priority)
};

bool ble_init(void);
void ble_start_advertising(void);
void ble_stop_advertising(void);
void ble_disconnect_all(void);
void ble_flush_cmd_queue(void);
bool ble_is_client_connected(void);
bool ble_is_advertising(void);
void ble_send(const char* data, size_t len);
void ble_process(void);

#endif
