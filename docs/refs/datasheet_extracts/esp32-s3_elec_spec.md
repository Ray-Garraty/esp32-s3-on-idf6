---
type: Hardware Reference
title: ESP32-S3 Electrical Characteristics
description: Absolute maximum ratings, recommended operating conditions, DC characteristics, ADC characteristics, current consumption, memory specifications, and reliability qualifications for the ESP32-S3.
tags: [esp32-s3, hardware, electrical, specifications]
timestamp: 2026-07-13
source: esp32-s3_datasheet_en.md (v2.2), lines 3620–4098
---

# ESP32-S3 Electrical Characteristics

Extracted from ESP32-S3 Series Datasheet v2.2, Section 5.

## 5.1 Absolute Maximum Ratings

Stresses above those listed below may cause permanent damage to the device. These are stress ratings only and normal operation of the device at these or any other conditions beyond those indicated in Section 5.2 is not implied. Exposure to absolute-maximum-rated conditions for extended periods may affect device reliability.

| Parameter | Description | Min | Max | Unit |
|---|---|---|---|---|
| | Input power pins | Allowed input voltage | 0.3 | 3.6 | V |
| | | Cumulative IO output current | — | 1500 | mA |
| T_STORE | Storage temperature | −40 | 150 | °C |

For more information on input power pins, see Section 2.5.1 Power Pins. The product proved to be fully functional after all its IO pins were pulled high while being connected to ground for 24 consecutive hours at ambient temperature of 25 °C.

## 5.2 Recommended Operating Conditions

For recommended ambient temperature, see Section 1 ESP32-S3 Series Comparison.

| Parameter | Description | Min | Typ | Max | Unit |
|---|---|---|---|---|---|
| VDDA, VDD3P3 | Recommended input voltage | 3.0 | 3.3 | 3.6 | V |
| VDD3P3_RTC | Recommended input voltage | 3.0 | 3.3 | 3.6 | V |
| VDD_SPI (as input) | — | 1.8 | 3.3 | 3.6 | V |
| VDD3P3_CPU | Recommended input voltage | 3.0 | 3.3 | 3.6 | V |
| I_VDD | Cumulative input current | 0.5 | — | — | A |

Notes:
- See in conjunction with Section 2.5 Power Supply.
- If VDD3P3_RTC is used to power VDD_SPI (see Section 2.5.2 Power Scheme), the voltage drop on R_SPI should be accounted for. See also Section 5.3 VDD_SPI Output Characteristics.
- If writing to eFuses, the voltage on VDD3P3_CPU should not exceed 3.3 V as the circuits responsible for burning eFuses are sensitive to higher voltages.
- If you use a single power supply, the recommended output current is 500 mA or more.

## 5.3 VDD_SPI Output Characteristics

| Parameter | Description | Typ | Unit |
|---|---|---|---|
| R_SPI | VDD_SPI powered by VDD3P3_RTC via R_SPI for 3.3 V flash/PSRAM | 14 | Ω |
| I_SPI | Output current when VDD_SPI is powered by Flash Voltage Regulator for 1.8 V flash/PSRAM | 40 | mA |

Notes:
- See in conjunction with Section 2.5.2 Power Scheme.
- VDD3P3_RTC must be more than VDD_flash_min + I_flash_max * R_SPI; where VDD_flash_min = minimum operating voltage of flash/PSRAM, I_flash_max = maximum operating current of flash/PSRAM.

## 5.4 DC Characteristics (3.3 V, 25 °C)

| Parameter | Description | Min | Typ | Max | Unit |
|---|---|---|---|---|---|
| C_IN | Pin capacitance | — | 2 | — | pF |
| V_IH | High-level input voltage | 0.75 × VDD | — | VDD + 0.3 | V |
| V_IL | Low-level input voltage | 0.3 | — | 0.25 × VDD | V |
| I_IH | High-level input current | — | — | 50 | nA |
| I_IL | Low-level input current | — | — | 50 | nA |
| V_OH | High-level output voltage | 0.8 × VDD | — | — | V |
| V_OL | Low-level output voltage | — | — | 0.1 × VDD | V |
| I_OH | High-level source current (VDD = 3.3 V, V_OH >= 2.64 V, PAD_DRIVER = 3) | — | 40 | — | mA |
| I_OL | Low-level sink current (VDD = 3.3 V, V_OL = 0.495 V, PAD_DRIVER = 3) | — | 28 | — | mA |
| R_PU | Internal weak pull-up resistor | — | 45 | — | kΩ |
| R_PD | Internal weak pull-down resistor | — | 45 | — | kΩ |
| V_IH_nRST | Chip reset release voltage (CHIP_PU voltage is within the specified range) | 0.75 × VDD | — | VDD + 0.3 | V |
| V_IL_nRST | Chip reset voltage (CHIP_PU voltage is within the specified range) | 0.3 | — | 0.25 × VDD | V |

Notes:
- VDD – voltage from a power pin of a respective power domain.
- V_OH and V_OL are measured using high-impedance load.

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

## 5.6 Current Consumption

### 5.6.1 Current Consumption in Active Mode

The current consumption measurements are taken with a 3.3 V supply at 25 °C ambient temperature. TX current consumption is rated at a 100% duty cycle. RX current consumption is rated when the peripherals are disabled and the CPU idle.

### Table 5-7. Current Consumption for Wi-Fi (2.4 GHz) in Active Mode

| Work Mode | RF Condition | Description | Peak (mA) |
|---|---|---|---|
| Active (RF working) | TX | 802.11b, 1 Mbps, @21 dBm | 340 |
| | | 802.11g, 54 Mbps, @19 dBm | 291 |
| | | 802.11n, HT20, MCS7, @18.5 dBm | 283 |
| | | 802.11n, HT40, MCS7, @18 dBm | 286 |
| | RX | 802.11b/g/n, HT20 | 88 |
| | | 802.11n, HT40 | 91 |

### Table 5-8. Current Consumption for Bluetooth LE in Active Mode

| Work Mode | RF Condition | Description | Peak (mA) |
|---|---|---|---|
| Active (RF working) | TX | Bluetooth LE @ 21.0 dBm | 335 |
| | | Bluetooth LE @ 9.0 dBm | 193 |
| | | Bluetooth LE @ 0 dBm | 176 |
| | | Bluetooth LE @ -15.0 dBm | 116 |
| | RX | Bluetooth LE | 93 |

### 5.6.2 Current Consumption in Other Modes

The measurements below are applicable to ESP32-S3 and ESP32-S3FH8. Since ESP32-S3R2, ESP32-S3RH2, ESP32-S3R8, ESP32-S3R8V, ESP32-S3R16V, and ESP32-S3FN4R2 are embedded with PSRAM, their current consumption might be higher.

### Table 5-9. Current Consumption in Modem-sleep Mode

| Work mode | Frequency (MHz) | Description | Typ (mA) [1] | Typ (mA) [2] |
|---|---|---|---|---|
| Modem-sleep [3] | 40 | WAITI (Dual core in idle state) | 13.2 | 18.8 |
| | | Single core running 32-bit data access instructions, the other core in idle state | 16.2 | 21.8 |
| | | Dual core running 32-bit data access instructions | 18.7 | 24.4 |
| | | Single core running 128-bit data access instructions, the other core in idle state | 19.9 | 25.4 |
| | | Dual core running 128-bit data access instructions | 23.0 | 28.8 |
| | 80 | WAITI | 22.0 | 36.1 |
| | | Single core running 32-bit data access instructions, the other core in idle state | 28.4 | 42.6 |
| | | Dual core running 32-bit data access instructions | 33.1 | 47.3 |
| | | Single core running 128-bit data access instructions, the other core in idle state | 35.1 | 49.6 |
| | | Dual core running 128-bit data access instructions | 41.8 | 56.3 |
| | 160 | WAITI | 27.6 | 42.3 |
| | | Single core running 32-bit data access instructions, the other core in idle state | 39.9 | 54.6 |
| | | Dual core running 32-bit data access instructions | 49.6 | 64.1 |
| | | Single core running 128-bit data access instructions, the other core in idle state | 54.4 | 69.2 |
| | | Dual core running 128-bit data access instructions | 66.7 | 81.1 |
| | 240 | WAITI | 32.9 | 47.6 |
| | | Single core running 32-bit data access instructions, the other core in idle state | 51.2 | 65.9 |
| | | Dual core running 32-bit data access instructions | 66.2 | 81.3 |
| | | Single core running 128-bit data access instructions, the other core in idle state | 72.4 | 87.9 |
| | | Dual core running 128-bit data access instructions | 91.7 | 107.9 |

Notes:
1. Current consumption when all peripheral clocks are disabled.
2. Current consumption when all peripheral clocks are enabled. In practice, the current consumption might be different depending on which peripherals are enabled.
3. In Modem-sleep mode, Wi-Fi is clock gated, and the current consumption might be higher when accessing flash. For a flash rated at 80 Mbit/s, in SPI 2-line mode the consumption is 10 mA.

### Table 5-10. Current Consumption in Low-Power Modes

| Work mode | Description | Typ (μA) |
|---|---|---|
| Light-sleep [1] | VDD_SPI and Wi-Fi are powered down, and all GPIOs are high-impedance. | 240 |
| Deep-sleep | The ULP co-processor is powered on [2] | ULP-FSM: 170, ULP-RISC-V: 190 |
| | ULP sensor-monitored pattern [3] | 18 |
| | RTC memory and RTC peripherals are powered up. | 8 |
| | RTC memory is powered up. RTC peripherals are powered down. | 7 |
| Power off | CHIP_PU is set to low level. The chip is shut down. | 1 |

Notes:
1. In Light-sleep mode, all related SPI pins are pulled up. For chips embedded with PSRAM, please add corresponding PSRAM consumption values, e.g., 140 μA for 8 MB 8-line PSRAM (3.3 V), 200 μA for 8 MB 8-line PSRAM (1.8 V) and 40 μA for 2 MB 4-line PSRAM (3.3 V).
2. During Deep-sleep, when the ULP co-processor is powered on, peripherals such as GPIO and I2C are able to operate.
3. The "ULP sensor-monitored pattern" refers to the mode where the ULP coprocessor or the sensor works periodically. When touch sensors work with a duty cycle of 1%, the typical current consumption is 18 μA.

## 5.7 Memory Specifications

The data below is sourced from the memory vendor datasheet. These values are guaranteed through design and/or characterization but are not fully tested in production. Devices are shipped with the memory erased.

### Table 5-11. Flash Specifications

| Parameter | Description | Min | Typ | Max | Unit |
|---|---|---|---|---|---|
| VCC | Power supply voltage (1.8 V) | 1.65 | 1.80 | 2.00 | V |
| | Power supply voltage (3.3 V) | 2.7 | 3.3 | 3.6 | V |
| F_C | Maximum clock frequency | 80 | — | — | MHz |
| — | Program/erase cycles | 100,000 | — | — | cycles |
| T_RET | Data retention time | 20 | — | — | years |
| T_PP | Page program time | — | 0.8 | — | ms |
| T_SE | Sector erase time (4 KB) | — | 70 | 500 | ms |
| T_BE1 | Block erase time (32 KB) | — | 0.2 | — | s |
| T_BE2 | Block erase time (64 KB) | — | 0.3 | — | s |
| T_CE | Chip erase time (16 Mb) | — | 7 | 20 | s |
| | Chip erase time (32 Mb) | — | 20 | 60 | s |
| | Chip erase time (64 Mb) | — | 25 | 100 | s |
| | Chip erase time (128 Mb) | — | 60 | 200 | s |
| | Chip erase time (256 Mb) | — | 70 | 300 | s |

### Table 5-12. PSRAM Specifications

| Parameter | Description | Min | Typ | Max | Unit |
|---|---|---|---|---|---|
| VCC | Power supply voltage (1.8 V) | 1.62 | 1.80 | 1.98 | V |
| | Power supply voltage (3.3 V) | 2.7 | 3.3 | 3.6 | V |
| F_C | Maximum clock frequency | 80 | — | — | MHz |

## 5.8 Reliability

| Test Item | Test Conditions | Test Standard |
|---|---|---|
| HTOL (High Temperature Operating Life) | 125 °C, 1000 hours | JESD22-A108 |
| ESD (Electro-Static Discharge Sensitivity) | HBM (Human Body Mode): ±2000 V | JS-001 |
| | CDM (Charge Device Mode): ±1000 V | JS-002 |
| Latch up | Current trigger ±200 mA | JESD78 |
| | Voltage trigger 1.5 × VDD_max | JESD78 |
| Preconditioning | Bake 24 hours @125 °C, Moisture soak (level 3: 192 hours @30 °C, 60% RH), IR reflow solder: 260 +0 °C, 20 seconds, three times | J-STD-020, JESD47, JESD22-A113 |
| TCT (Temperature Cycling Test) | −65 °C / 150 °C, 500 cycles | JESD22-A104 |
| uHAST (Highly Accelerated Stress Test, unbiased) | 130 °C, 85% RH, 96 hours | JESD22-A118 |
| HTSL (High Temperature Storage Life) | 150 °C, 1000 hours | JESD22-A103 |
| LTSL (Low Temperature Storage Life) | −40 °C, 1000 hours | JESD22-A119 |

Notes:
- JEDEC document JEP155 states that 500 V HBM allows safe manufacturing with a standard ESD control process.
- JEDEC document JEP157 states that 250 V CDM allows safe manufacturing with a standard ESD control process.
