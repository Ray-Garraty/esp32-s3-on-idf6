/**
 * @file led.cpp
 * @brief Onboard LED control (GPIO2)
 *
 * Non-blocking blink via state machine:
 * - 1 blink = success
 * - 2 blinks = error
 */

#include "led.h"

// State machine states
enum {
    LED_IDLE = 0,
    LED_ON_PHASE,
    LED_OFF_BETWEEN,
    LED_OFF_FINAL
};

// Blink state (static to avoid heap allocation)
static volatile uint32_t led_state = LED_IDLE;
static volatile uint32_t led_next_time = 0;
static volatile int led_blinks_remaining = 0;

// Transport mode state
static LedTransportMode g_led_transport_mode = LED_TRANSPORT_OFF;
static bool g_led_transport_on = false;

void led_init(void) {
    pinMode(PIN_LED, OUTPUT);
    digitalWrite(PIN_LED, LED_OFF);
    led_state = LED_IDLE;
    led_blinks_remaining = 0;
}

void led_blink_success(void) {
    // Only start if not already blinking
    if (led_state != LED_IDLE) return;
    led_blinks_remaining = 1;
    led_state = LED_ON_PHASE;
    led_next_time = millis();
    digitalWrite(PIN_LED, LED_ON);
}

void led_blink_error(void) {
    if (led_state != LED_IDLE) return;
    led_blinks_remaining = 2;
    led_state = LED_ON_PHASE;
    led_next_time = millis();
    digitalWrite(PIN_LED, LED_ON);
}

void led_set_transport_mode(LedTransportMode mode) {
    if (g_led_transport_mode == mode) return;
    g_led_transport_mode = mode;
    if (led_state != LED_IDLE) return;
    g_led_transport_on = false;
    led_next_time = 0;
    if (mode == LED_TRANSPORT_OFF) {
        digitalWrite(PIN_LED, LED_OFF);
    }
}

void led_process(void) {
    uint32_t now = millis();

    if (led_state != LED_IDLE) {
        if (now < led_next_time) return;

        switch (led_state) {
        case LED_ON_PHASE:
            digitalWrite(PIN_LED, LED_OFF);
            led_blinks_remaining--;
            if (led_blinks_remaining > 0) {
                led_state = LED_OFF_BETWEEN;
                led_next_time = now + LED_BLINK_OFF_MS;
            } else {
                led_state = LED_OFF_FINAL;
                led_next_time = now + LED_BLINK_OFF_MS;
            }
            break;

        case LED_OFF_BETWEEN:
            led_state = LED_ON_PHASE;
            led_next_time = now + LED_BLINK_ON_MS;
            digitalWrite(PIN_LED, LED_ON);
            break;

        case LED_OFF_FINAL:
            led_state = LED_IDLE;
            led_blinks_remaining = 0;
            break;

        default:
            led_state = LED_IDLE;
            break;
        }
        return;
    }

    // Idle — apply transport mode pattern
    if (now < led_next_time) return;

    switch (g_led_transport_mode) {
    case LED_TRANSPORT_ADVERTISING:  // solid on
        digitalWrite(PIN_LED, LED_ON);
        led_next_time = now + 1000;
        break;

    case LED_TRANSPORT_CONNECTED:  // blink 1Hz (500ms on/off)
        g_led_transport_on = !g_led_transport_on;
        digitalWrite(PIN_LED, g_led_transport_on ? LED_ON : LED_OFF);
        led_next_time = now + 500;
        break;

    default:  // LED_TRANSPORT_OFF
        digitalWrite(PIN_LED, LED_OFF);
        led_next_time = now + 1000;
        break;
    }
}
