/**
 * @file adc.h
 * @brief ADC for pH electrode measurement
 *
 * ESP32 GPIO34 (ADC1_CH6) - analog input only
 * ADC is read by a dedicated FreeRTOS task on core 0:
 *   - 64 samples per cycle with 1ms spacing
 *   - averaged value published every ~160ms
 *   - linear calibration: electrode_mV = a * raw_mV + b
 *   - coefficients stored in NVS namespace "adc_cal"
 */

#ifndef ADC_H
#define ADC_H

#include <Arduino.h>

#define PIN_ADC 34                 // GPIO34, ADC1_CH6
#define ADC_SAMPLES 64              // сэмплов за цикл усреднения
#define ADC_SAMPLE_INTERVAL_MS 1    // пауза между сэмплами (мс)
#define ADC_UPDATE_INTERVAL_MS 100  // пауза между публикациями (мс)
#define ADC_TASK_STACK_SIZE 2048    // размер стека задачи (байт)
#define ADC_TASK_PRIORITY 1         // приоритет задачи
#define ADC_TASK_CORE 0             // ядро FreeRTOS

// Калибровка по умолчанию (identity — pass-through)
#define ADC_CAL_DEFAULT_A      1.0f
#define ADC_CAL_DEFAULT_B      0.0f

// NVS
#define ADC_CAL_NVS_NS         "adc_cal"
#define ADC_CAL_NVS_KEY_A      "coeff_a"
#define ADC_CAL_NVS_KEY_B      "coeff_b"

// Клиппинг для int16_t
#define ADC_CAL_CLAMP_MAX   32767.0f
#define ADC_CAL_CLAMP_MIN  -32768.0f

// Параметры процесса калибровки
#define ADC_CAL_REF_POINTS     5           // количество опорных точек
#define ADC_CAL_STAB_SAMPLES   32          // чтений для проверки стабильности
#define ADC_CAL_STAB_TOLERANCE 5.0f        // допуск ±5 мВ
#define ADC_CAL_MEDIAN_SAMPLES 32          // сэмплов для расчёта медианы
#define ADC_CAL_STAB_MAX_ATTEMPTS 10       // макс попыток стабилизации

bool adc_init_module(void);

// Калибровка
bool adc_init_calibration(void);
uint16_t adc_get_raw_mv(void);
int16_t adc_get_calibrated_mv(void);
void adc_set_calibration(float a, float b);
void adc_reset_calibration(void);
void adc_get_coefficients(float *a, float *b);

#endif // ADC_H
