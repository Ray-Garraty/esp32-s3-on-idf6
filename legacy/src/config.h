#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

#define PIN_STEP 25
#define PIN_DIR  26
#define PIN_EN   27
#define PIN_LIMIT_FULL 32
#define PIN_LIMIT_EMPTY 35

#define STATUS_BROADCAST_INTERVAL_MS 300
#define TEMP_READ_INTERVAL_MS 1000
#define LOG_LINE_LIMIT 100
#define STALLGUARD_MAX_VALUE 255
#define STALLGUARD_DEFAULT_THRESHOLD 75
#define SERIAL_TIMEOUT_MS 5000

#define TMC_TOFF_DEFAULT 4
#define TMC_TBL_DEFAULT 1
#define TMC_PWM_FREQ_DEFAULT 1
#define TMC_INIT_DELAY_MS 500

#define STEPPER_DEFAULT_ACCEL 750
#define STEPPER_DEFAULT_SPEED 500
#define STEPPER_ACCEL_MULTIPLIER 1
#define STEPPER_EN_STABILIZE_MS 2
#define STEPPER_RMT_INIT_MS 5
#define STEPPER_INFINITE_STEPS 0xFFFFFFFF

#define PENDING_WATCHDOG_MS 60000

#define SERIAL_BAUD_RATE        115200
#define SERIAL_RX_BUFFER_SIZE   2048
#define SERIAL_TX_BUFFER_SIZE   2048

#define SETUP_INIT_WAIT_MS      500
#define PRE_HOMING_DELAY_MS     100
#define LOOP_MIN_CYCLE_MS       10

#define CAL_SEQ_POINTS           3
#define DEFAULT_FREQ_DIVISOR     2

#define NTP_MIN_VALID_TIMESTAMP  1000000000

// BLE
#define BLE_DEVICE_NAME_PREFIX      "EcoTiter-"
#define BLE_ADVERTISING_INTERVAL_MS 100
#define BLE_ADVERTISING_INTERVAL    (BLE_ADVERTISING_INTERVAL_MS * 8 / 5) // units of 0.625 ms → 160
#define BLE_MIN_CONN_INTERVAL_MS    15
#define BLE_MAX_CONN_INTERVAL_MS    30
#define BLE_SLAVE_LATENCY           4
#define BLE_SUPERVISION_TIMEOUT_MS  30000
#define BLE_MTU_SIZE                247
#define BLE_TX_QUEUE_SIZE           20
#define BLE_CMD_QUEUE_LENGTH        8

// Delay before updating connection params after connect (let LL procedures settle)
#define BLE_CONN_PARAM_DELAY_MS 2000

// Zombie detection threshold
#define BLE_NOTIFY_FAIL_THRESHOLD   5   // 5 consecutive notify failures = ~1.5s

// USB heartbeat detection (no HW detection on CH340/CP2102)
#define USB_HEARTBEAT_TIMEOUT_MS    10000

#endif
