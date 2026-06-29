/**
 * @file led.h
 * @brief Onboard LED control (GPIO2)
 *
 * Blinks on REST API command results:
 * - 1 blink = success
 * - 2 blinks = error
 */

#ifndef LED_H
#define LED_H

#include <Arduino.h>

// Onboard LED pin (ESP32 DevKit V1)
#define PIN_LED 2

// LED is active-HIGH on most DevKit boards
#define LED_ON  HIGH
#define LED_OFF LOW

// Timing for blink state machine
#define LED_BLINK_ON_MS      150
#define LED_BLINK_OFF_MS     100

// Non-blocking blink state machine — call led_process() from loop()
void led_init(void);
void led_blink_success(void);  // 1 blink
void led_blink_error(void);    // 2 blinks
void led_process(void);        // call every loop iteration

enum LedTransportMode {
    LED_TRANSPORT_OFF,          // USB active (Tauri heartbeat) → LED off
    LED_TRANSPORT_ADVERTISING,  // BLE advertising, no client  → solid on
    LED_TRANSPORT_CONNECTED     // BLE connected (script/Tauri) → blink 1Hz
};

void led_set_transport_mode(LedTransportMode mode);

#endif // LED_H
