---
type: Hardware Reference
title: ESP32-S3 SAR ADC
description: Two 12-bit SAR ADCs with 20 channels, ADC characteristics, calibration results, and limitations including ADC2/Wi-Fi coexistence for the ESP32-S3.
tags: [esp32-s3, hardware, adc, analog]
timestamp: 2026-07-13
source: esp32-s3_datasheet_en.md (v2.2), lines 3510–3522 and 3838–3864
---

# ESP32-S3 SAR ADC

Extracted from ESP32-S3 Series Datasheet v2.2, Sections 4.2.2.1 and 5.5.

## 4.2.2.1 SAR ADC

ESP32-S3 integrates two 12-bit SAR ADCs and supports measurements on 20 channels (analog-enabled pins). For power-saving purpose, the ULP coprocessors in ESP32-S3 can also be used to measure voltage in sleep modes. By using threshold settings or other methods, we can awaken the CPU from sleep modes.

Note: The ADC2_CH... analog functions cannot be used with Wi-Fi simultaneously.

For details, see ESP32-S3 Technical Reference Manual > Chapter On-Chip Sensors and Analog Signal Processing.

### Pin Assignment

For details, see Section 2.3.5 Peripheral Pin Assignment.

## 5.5 ADC Characteristics

The measurements in this section are taken with an external 100 nF capacitor connected to the ADC, using DC signals as input, and at an ambient temperature of 25 °C with disabled Wi-Fi.

### Table 5-5. ADC Characteristics

| Symbol | Min | Max | Unit |
|---|---|---|---|
| DNL (Differential nonlinearity) | −4 | 4 | LSB |
| INL (Integral nonlinearity) | −8 | 8 | LSB |
| Sampling rate | — | 100 | kSPS |

Notes:
- To get better DNL results, you can sample multiple times and apply a filter, or calculate the average value.
- kSPS means kilo samples-per-second.

The calibrated ADC results after hardware calibration and software calibration are shown below. For higher accuracy, you may implement your own calibration methods.

### Table 5-6. ADC Calibration Results

| Parameter | Description | Min | Max | Unit |
|---|---|---|---|---|
| Total error | ATTEN0, effective measurement range of 0~850 | — | 55 | mV |
| | ATTEN1, effective measurement range of 0~1100 | — | 66 | mV |
| | ATTEN2, effective measurement range of 0~1600 | — | 10 | mV |
| | ATTEN3, effective measurement range of 0~2900 | — | 50 | mV |
