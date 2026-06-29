/**
 * @file wifi_manager.h
 * @brief WiFi connection manager with captive portal
 *
 * Handles WiFi credentials storage, connection management,
 * and captive portal for initial configuration
 */

#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <Arduino.h>

// Forward declaration
class AsyncWebServer;

// AP mode configuration
#define AP_SSID "EcoTiter-AP"
#define AP_PASSWORD "12345678"
#define AP_CHANNEL 1
#define AP_HIDDEN false
#define AP_MAX_CONNECTIONS 4

// AP IP address
#define AP_IP_ADDRESS "192.168.4.1"

// WiFi connection timeout
#define WIFI_CONNECT_TIMEOUT_MS 15000

/** @brief Initialize WiFi, connect with saved creds or start captive portal */
void wifi_manager_init(AsyncWebServer* server);
/** @brief Attempt reconnection if not connected (call from loop) */
void wifi_manager_reconnect(void);
/** @brief Process DNS and WiFi tasks (call from loop) */
void wifi_manager_process(void);

#endif // WIFI_MANAGER_H
