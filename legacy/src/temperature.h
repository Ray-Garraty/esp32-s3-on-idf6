/**
 * @file temperature.h
 * @brief Temperature sensor (DS18B20) via OneWire
 *
 * Uses DallasTemperature library for DS18B20
 */

#ifndef TEMPERATURE_H
#define TEMPERATURE_H

#include <Arduino.h>

// Temperature sensor pin
#define PIN_TEMP 33

// Temperature sensor state
typedef struct {
    bool isConnected;
    float value;
} temp_sensor_state_t;

/** @brief Init OneWire and DallasTemperature library, detect sensor */
bool temperature_init(void);
/** @brief Check if sensor is still connected (called before each conversion) */
void temperature_update_state(void);
/** @brief Start non-blocking temperature conversion */
void temperature_start_conversion(void);
/** @brief Read result after conversion, returns false if disconnected */
bool temperature_read_result(float* temperature);
/** @brief Return current temperature state struct */
temp_sensor_state_t temperature_get_state(void);

#endif // TEMPERATURE_H
