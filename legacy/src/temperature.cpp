/**
 * @file temperature.cpp
 * @brief Temperature sensor (DS18B20) via OneWire
 *
 * Uses DallasTemperature library for DS18B20
 */

#include "temperature.h"
#include "logger.h"
#include <OneWire.h>
#include <DallasTemperature.h>

// OneWire and DallasTemperature instances
static OneWire* oneWire = nullptr;
static DallasTemperature* sensors = nullptr;

// State
static temp_sensor_state_t state = {
    .isConnected = false,
    .value = NAN
};

// Conversion tracking
static bool conversion_started = false;

bool temperature_init(void) {
    oneWire = new OneWire(PIN_TEMP);
    sensors = new DallasTemperature(oneWire);

    sensors->begin();

    // Non-blocking mode: requestTemperatures() returns immediately,
    // result is ready after ~750ms (checked in loop via temperature_read_result)
    sensors->setWaitForConversion(false);

    // Check if sensor is detected
    int device_count = sensors->getDeviceCount();
    if (device_count > 0) {
        state.isConnected = true;
        logger.info("DS18B20 detected on GPIO%d", PIN_TEMP);
    } else {
        state.isConnected = false;
        logger.warn("DS18B20 not detected");
    }

    return state.isConnected;
}

void temperature_start_conversion(void) {
    if (!state.isConnected) {
        return;
    }

    sensors->requestTemperatures();
    conversion_started = true;
}

bool temperature_read_result(float* temperature) {
    if (!state.isConnected || !conversion_started) {
        return false;
    }

    float temp = sensors->getTempCByIndex(0);

    // Проверяем только DEVICE_DISCONNECTED_C (-127°C)
    // DEVICE_DISCONNECTED_F не проверяем — функция возвращает Цельсии
    if (temp != DEVICE_DISCONNECTED_C) {
        // Датчик подключён — обновляем состояние
        state.isConnected = true;
        state.value = temp;
        *temperature = temp;
        conversion_started = false;
        return true;
    }

    // Датчик отключён — сбрасываем состояние
    logger.info("Sensor disconnected, resetting state");
    state.isConnected = false;
    state.value = NAN;
    conversion_started = false;
    return false;
}

void temperature_update_state(void) {
    // Динамически проверяем наличие датчика
    int device_count = sensors->getDeviceCount();
    bool was_connected = state.isConnected;

    if (device_count > 0) {
        state.isConnected = true;
        if (!was_connected) {
            logger.info("DS18B20 reconnected");
        }
    } else {
        state.isConnected = false;
        state.value = NAN;
        if (was_connected) {
            logger.info("DS18B20 disconnected");
        }
    }
}

temp_sensor_state_t temperature_get_state(void) {
    return state;
}
