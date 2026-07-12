#pragma once

#include <cstdint>
#include <cstdlib>

#include "esp_err.h"

typedef int uart_port_t;

#define UART_NUM_0 ((uart_port_t)0)
#define UART_NUM_1 ((uart_port_t)1)
#define UART_NUM_2 ((uart_port_t)2)
#define UART_PIN_NO_CHANGE (-1)

typedef enum {
    UART_DATA_8_BITS,
} uart_word_length_t;

typedef enum {
    UART_PARITY_DISABLE,
} uart_parity_t;

typedef enum {
    UART_STOP_BITS_1,
} uart_stop_bits_t;

typedef enum {
    UART_HW_FLOWCTRL_DISABLE,
} uart_hw_flowcontrol_t;

typedef enum {
    UART_SCLK_DEFAULT,
} uart_sclk_t;

typedef struct {
    int baud_rate;
    uart_word_length_t data_bits;
    uart_parity_t parity;
    uart_stop_bits_t stop_bits;
    uart_hw_flowcontrol_t flow_ctrl;
    uint8_t rx_flow_ctrl_thresh;
    uart_sclk_t source_clk;
    struct { uint32_t allow_pd : 1; uint32_t backup_before_sleep : 1; } flags;
} uart_config_t;

inline esp_err_t uart_driver_install(uart_port_t, int, int, int, void*, int) { return ESP_OK; }
inline esp_err_t uart_param_config(uart_port_t, const uart_config_t*) { return ESP_OK; }
inline esp_err_t uart_set_pin(uart_port_t, int, int, int, int) { return ESP_OK; }
inline int uart_write_bytes(uart_port_t, const void*, size_t) { return 8; }
inline int uart_read_bytes(uart_port_t, void*, uint32_t, uint32_t) { return 8; }
inline esp_err_t uart_wait_tx_done(uart_port_t, uint32_t) { return ESP_OK; }
inline esp_err_t uart_driver_delete(uart_port_t) { return ESP_OK; }
