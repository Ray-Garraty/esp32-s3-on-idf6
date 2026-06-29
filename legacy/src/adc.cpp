/**
 * @file adc.cpp
 * @brief ADC for pH electrode measurement
 *
 * Dedicated FreeRTOS task on core 0 reads ADC every ~164 ms:
 *   64 samples x analogReadMilliVolts(GPIO34) with 1ms spacing
 *   -> averaged -> stored in global variable
 *
 * Linear calibration: electrode_mV = a * raw_mV + b
 * Coefficients loaded from NVS on boot, writable via commands.
 */

#include "adc.h"
#include "logger.h"
#include <Preferences.h>
#include <atomic>

static std::atomic<uint16_t> s_millivolts{0};
static std::atomic<float> s_coeff_a{ADC_CAL_DEFAULT_A};
static std::atomic<float> s_coeff_b{ADC_CAL_DEFAULT_B};

static void adcTask(void *pvParameters) {
    (void)pvParameters;
    for (;;) {
        uint32_t sum = 0;
        for (int i = 0; i < ADC_SAMPLES; i++) {
            sum += analogReadMilliVolts(PIN_ADC);
            vTaskDelay(pdMS_TO_TICKS(ADC_SAMPLE_INTERVAL_MS));
        }
        s_millivolts.store((uint16_t)(sum / ADC_SAMPLES), std::memory_order_relaxed);
        vTaskDelay(pdMS_TO_TICKS(ADC_UPDATE_INTERVAL_MS));
    }
}

bool adc_init_module(void) {
    analogSetPinAttenuation(PIN_ADC, ADC_11db);

    BaseType_t res = xTaskCreatePinnedToCore(
        adcTask,
        "ADC_Task",
        ADC_TASK_STACK_SIZE,
        NULL,
        ADC_TASK_PRIORITY,
        NULL,
        ADC_TASK_CORE
    );

    if (res != pdPASS) {
        logger.error("ADC task creation failed");
        return false;
    }

    adc_init_calibration();

    logger.info("ADC Task started (GPIO34, analogReadMilliVolts, %d samples)", ADC_SAMPLES);
    return true;
}

// ============================================================================
// Калибровка
// ============================================================================

bool adc_init_calibration(void) {
    Preferences prefs;
    if (!prefs.begin(ADC_CAL_NVS_NS, true)) {
        logger.warn("ADC cal NVS open failed, using defaults");
        s_coeff_a.store(ADC_CAL_DEFAULT_A, std::memory_order_relaxed);
        s_coeff_b.store(ADC_CAL_DEFAULT_B, std::memory_order_relaxed);
        return false;
    }
    s_coeff_a.store(prefs.getFloat(ADC_CAL_NVS_KEY_A, ADC_CAL_DEFAULT_A), std::memory_order_relaxed);
    s_coeff_b.store(prefs.getFloat(ADC_CAL_NVS_KEY_B, ADC_CAL_DEFAULT_B), std::memory_order_relaxed);
    prefs.end();

    logger.info("ADC calibration loaded: a=%.6f, b=%.6f", s_coeff_a.load(std::memory_order_relaxed), s_coeff_b.load(std::memory_order_relaxed));
    return true;
}

uint16_t adc_get_raw_mv(void) {
    return s_millivolts.load(std::memory_order_relaxed);
}

int16_t adc_get_calibrated_mv(void) {
    float result = s_coeff_a.load(std::memory_order_relaxed) * (float)s_millivolts.load(std::memory_order_relaxed) + s_coeff_b.load(std::memory_order_relaxed);
    if (result > ADC_CAL_CLAMP_MAX) result = ADC_CAL_CLAMP_MAX;
    if (result < ADC_CAL_CLAMP_MIN) result = ADC_CAL_CLAMP_MIN;
    return (int16_t)roundf(result);
}

void adc_set_calibration(float a, float b) {
    Preferences prefs;
    prefs.begin(ADC_CAL_NVS_NS, false);
    prefs.putFloat(ADC_CAL_NVS_KEY_A, a);
    prefs.putFloat(ADC_CAL_NVS_KEY_B, b);
    prefs.end();

    s_coeff_a.store(a, std::memory_order_relaxed);
    s_coeff_b.store(b, std::memory_order_relaxed);

    logger.info("ADC calibration saved: a=%.6f, b=%.6f", a, b);
}

void adc_reset_calibration(void) {
    adc_set_calibration(ADC_CAL_DEFAULT_A, ADC_CAL_DEFAULT_B);
    logger.info("ADC calibration reset to defaults");
}

void adc_get_coefficients(float *a, float *b) {
    *a = s_coeff_a.load(std::memory_order_relaxed);
    *b = s_coeff_b.load(std::memory_order_relaxed);
}
