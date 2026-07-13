---
type: Hardware Reference
title: ESP32-S3 GPIO Pins
description: Comprehensive pin overview, IO MUX functions, RTC functions, analog functions, restrictions, peripheral pin assignment, power supply, and pin mapping for the ESP32-S3.
tags: [esp32-s3, hardware, gpio, pins, iomux]
timestamp: 2026-07-13
source: esp32-s3_datasheet_en.md (v2.2), lines 588–2339 and 4600–5399
---

# ESP32-S3 Pins

Extracted from ESP32-S3 Series Datasheet v2.2, Section 2.

## 2.1 Pin Layout

The ESP32-S3 chip is a 57-pin QFN package (7×7 mm). Pins are numbered in anti-clockwise order starting from Pin 1 in the top view. The chip includes GPIO pins, analog pins, and power pins.

## 2.2 Pin Overview

The ESP32-S3 chip integrates multiple peripherals that require communication with the outside world. Pin multiplexing is controlled via software programmable registers (see ESP32-S3 Technical Reference Manual > Chapter IO MUX and GPIO Matrix).

All in all, the ESP32-S3 chip has the following types of pins:
- **IO pins** with predefined sets of functions: IO MUX functions (Table 2-4), RTC functions (Table 2-6), Analog functions (Table 2-8)
- **Analog pins** with exclusively-dedicated analog functions (Table 2-10)
- **Power pins** that supply power to the chip components and non-power pins (Table 2-11)

### Table 2-1. Pin Overview

| Pin No. | Pin Name | Pin Type | Pin Providing Power | At Reset | After Reset | IO MUX | RTC IO MUX | Analog |
|---|---|---|---|---|---|---|---|---|
| 1 | LNA_IN | Analog | | | | | | |
| 2 | VDD3P3 | Power | | | | | | |
| 3 | VDD3P3 | Power | | | | | | |
| 4 | CHIP_PU | Analog | VDD3P3_RTC | | | | | |
| 5 | GPIO0 | IO | VDD3P3_RTC | WPU, IE | WPU, IE | IO MUX | RTC IO MUX | |
| 6 | GPIO1 | IO | VDD3P3_RTC | IE | IE | IO MUX | RTC IO MUX | Analog |
| 7 | GPIO2 | IO | VDD3P3_RTC | IE | IE | IO MUX | RTC IO MUX | Analog |
| 8 | GPIO3 | IO | VDD3P3_RTC | IE | IE | IO MUX | RTC IO MUX | Analog |
| 9 | GPIO4 | IO | VDD3P3_RTC | | | IO MUX | RTC IO MUX | Analog |
| 10 | GPIO5 | IO | VDD3P3_RTC | | | IO MUX | RTC IO MUX | Analog |
| 11 | GPIO6 | IO | VDD3P3_RTC | | | IO MUX | RTC IO MUX | Analog |
| 12 | GPIO7 | IO | VDD3P3_RTC | | | IO MUX | RTC IO MUX | Analog |
| 13 | GPIO8 | IO | VDD3P3_RTC | | | IO MUX | RTC IO MUX | Analog |
| 14 | GPIO9 | IO | VDD3P3_RTC | IE | | IO MUX | RTC IO MUX | Analog |
| 15 | GPIO10 | IO | VDD3P3_RTC | IE | | IO MUX | RTC IO MUX | Analog |
| 16 | GPIO11 | IO | VDD3P3_RTC | IE | | IO MUX | RTC IO MUX | Analog |
| 17 | GPIO12 | IO | VDD3P3_RTC | IE | | IO MUX | RTC IO MUX | Analog |
| 18 | GPIO13 | IO | VDD3P3_RTC | IE | | IO MUX | RTC IO MUX | Analog |
| 19 | GPIO14 | IO | VDD3P3_RTC | IE | | IO MUX | RTC IO MUX | Analog |
| 20 | VDD3P3_RTC | Power | | | | | | |
| 21 | XTAL_32K_P | IO | VDD3P3_RTC | | | IO MUX | RTC IO MUX | Analog |
| 22 | XTAL_32K_N | IO | VDD3P3_RTC | | | IO MUX | RTC IO MUX | Analog |
| 23 | GPIO17 | IO | VDD3P3_RTC | IE | | IO MUX | RTC IO MUX | Analog |
| 24 | GPIO18 | IO | VDD3P3_RTC | IE | | IO MUX | RTC IO MUX | Analog |
| 25 | GPIO19 | IO | VDD3P3_RTC | | | IO MUX | RTC IO MUX | Analog |
| 26 | GPIO20 | IO | VDD3P3_RTC | USB_PU | USB_PU | IO MUX | RTC IO MUX | Analog |
| 27 | GPIO21 | IO | VDD3P3_RTC | | | IO MUX | RTC IO MUX | |
| 28 | SPICS1 | IO | VDD_SPI | WPU, IE | WPU, IE | IO MUX | | |
| 29 | VDD_SPI | Power | | | | | | |
| 30 | SPIHD | IO | VDD_SPI | WPU, IE | WPU, IE | IO MUX | | |
| 31 | SPIWP | IO | VDD_SPI | WPU, IE | WPU, IE | IO MUX | | |
| 32 | SPICS0 | IO | VDD_SPI | WPU, IE | WPU, IE | IO MUX | | |
| 33 | SPICLK | IO | VDD_SPI | WPU, IE | WPU, IE | IO MUX | | |
| 34 | SPIQ | IO | VDD_SPI | WPU, IE | WPU, IE | IO MUX | | |
| 35 | SPID | IO | VDD_SPI | WPU, IE | WPU, IE | IO MUX | | |
| 36 | SPICLK_N | IO | VDD_SPI/VDD3P3_CPU | IE | IE | IO MUX | | |
| 37 | SPICLK_P | IO | VDD_SPI/VDD3P3_CPU | IE | IE | IO MUX | | |
| 38 | GPIO33 | IO | VDD_SPI/VDD3P3_CPU | IE | | IO MUX | | |
| 39 | GPIO34 | IO | VDD_SPI/VDD3P3_CPU | IE | | IO MUX | | |
| 40 | GPIO35 | IO | VDD_SPI/VDD3P3_CPU | IE | | IO MUX | | |
| 41 | GPIO36 | IO | VDD_SPI/VDD3P3_CPU | IE | | IO MUX | | |
| 42 | GPIO37 | IO | VDD_SPI/VDD3P3_CPU | IE | | IO MUX | | |
| 43 | GPIO38 | IO | VDD3P3_CPU | IE | | IO MUX | | |
| 44 | MTCK | IO | VDD3P3_CPU | IE | | IO MUX | | |
| 45 | MTDO | IO | VDD3P3_CPU | IE | | IO MUX | | |
| 46 | VDD3P3_CPU | Power | | | | | | |
| 47 | MTDI | IO | VDD3P3_CPU | IE | | IO MUX | | |
| 48 | MTMS | IO | VDD3P3_CPU | IE | | IO MUX | | |
| 49 | U0TXD | IO | VDD3P3_CPU | WPU, IE | WPU, IE | IO MUX | | |
| 50 | U0RXD | IO | VDD3P3_CPU | WPU, IE | WPU, IE | IO MUX | | |
| 51 | GPIO45 | IO | VDD3P3_CPU | WPD, IE | WPD, IE | IO MUX | | |
| 52 | GPIO46 | IO | VDD3P3_CPU | WPD, IE | WPD, IE | IO MUX | | |
| 53 | XTAL_N | Analog | | | | | | |
| 54 | XTAL_P | Analog | | | | | | |
| 55 | VDDA | Power | | | | | | |
| 56 | VDDA | Power | | | | | | |
| 57 | GND | Power | | | | | | |

Notes on Pin Overview columns:
- Pin Settings abbreviations: IE = input enabled, WPU = internal weak pull-up resistor enabled, WPD = internal weak pull-down resistor enabled, USB_PU = USB pull-up resistor enabled.
- Default drive strengths: GPIO17 and GPIO18: 10 mA; GPIO19 and GPIO20: 40 mA; all other pins: 20 mA.
- For pins powered by VDD_SPI: power actually comes from the internal power rail supplying power to VDD_SPI.
- Pin Providing Power (either VDD3P3_CPU or VDD_SPI) is decided by eFuse bit EFUSE_PIN_POWER_SELECTION and can be configured via the IO_MUX_PAD_POWER_CTRL bit.
- For ESP32-S3R8V and ESP32-S3R16V chip, as the VDD_SPI voltage has been set to 1.8 V, the working voltage for pins SPICLK_N and SPICLK_P (GPIO47 and GPIO48) would also be 1.8 V.
- MTCK pull-up depends on EFUSE_DIS_PAD_JTAG: 0 = WPU enabled, 1 = pin floating.

### Table 2-2. Power-Up Glitches on Pins

| Pin | Glitch | Typical Time Period (μs) |
|---|---|---|
| GPIO1–GPIO14, XTAL_32K_P, XTAL_32K_N, GPIO17 | Low-level glitch | 60 |
| GPIO18 | Low-level glitch | 60 |
| | High-level glitch | 60 |
| GPIO19 | Low-level glitch | 60 |
| | High-level glitch | 60 |
| GPIO20 | Pull-down glitch | 60 |
| | High-level glitch | 60 |

- Low-level glitch: the pin is at a low level output status during the time period.
- High-level glitch: the pin is at a high level output status during the time period.
- Pull-down glitch: the pin is at an internal weak pulled-down status during the time period.
- GPIO19 and GPIO20 pins both have two high-level glitches during chip power-up, each lasting for about 60 μs. The total duration for the glitches and the delay are 3.2 ms and 2 ms respectively.

## 2.3 IO Pins

### 2.3.1 IO MUX Functions

The IO MUX allows multiple input/output signals to be connected to a single input/output pin. Each IO pin of ESP32-S3 can be connected to one of the five signals (IO MUX functions, i.e., F0-F4).

Among the five sets of signals:
- Some are routed via the GPIO Matrix (GPIO0, GPIO1, etc.), which incorporates internal signal routing circuitry for mapping signals programmatically.
- Some are directly routed from certain peripherals (U0TXD, MTCK, etc.), including UART0/1, JTAG, SPI0/1, and SPI2.

### Table 2-3. Peripheral Signals Routed via IO MUX

| Pin Function | Signal | Description |
|---|---|---|
| U...TXD | Transmit data | UART0/1 interface |
| U...RXD | Receive data | |
| U...RTS | Request to send | |
| U...CTS | Clear to send | |
| MTCK | Test clock | JTAG interface for debugging |
| MTDO | Test Data Out | |
| MTDI | Test Data In | |
| MTMS | Test Mode Select | |
| SPIQ | Master in, slave out | SPI0/1 interface (powered by VDD_SPI) for connection to in-package or off-package flash/PSRAM via SPI bus. Supports 1-, 2-, 4-line SPI modes. |
| SPID | Master out, slave in | |
| SPIHD | Hold | |
| SPIWP | Write protect | |
| SPICLK | Clock | |
| SPICS... | Chip select | |
| SPIIO... | Data | SPI0/1 interface (powered by VDD_SPI or VDD3P3_CPU) for higher 4 bits data line and DQS in 8-line SPI mode |
| SPIDQS | Data strobe/data mask | |
| SPICLK_N_DIFF | Negative clock signal | Differential clock negative/positive for SPI bus |
| SPICLK_P_DIFF | Positive clock signal | |
| SUBSPIQ | Master in, slave out | SPI0/1 interface (powered by VDD3P3_RTC or VDD3V3_CPU) for connection to in-package or off-package flash/PSRAM via SUBSPI bus. Supports 1-, 2-, 4-line SPI modes. |
| SUBSPID | Master out, slave in | |
| SUBSPIHD | Hold | |
| SUBSPIWP | Write protect | |
| SUBSPICLK | Clock | |
| SUBSPICS... | Chip select | |
| SUBSPICLK_N_DIFF | Negative clock signal | Differential clock for SUBSPI bus |
| SUBSPICLK_P_DIFF | Positive clock signal | |
| FSPIQ | Master in, slave out | SPI2 interface for fast SPI connection. Supports 1-, 2-, 4-line SPI modes. |
| FSPID | Master out, slave in | |
| FSPIHD | Hold | |
| FSPIWP | Write protect | |
| FSPICLK | Clock | |
| FSPICS0 | Chip select | |
| FSPIIO... | Data | Higher 4 bits data line and DQS for SPI2 in 8-line SPI mode |
| FSPIDQS | Data strobe/data mask | |
| CLK_OUT... | Clock output | Output clock signals generated by the chip's internal components |

### Table 2-4. IO MUX Functions

| Pin No. | GPIO | F0 | Type | F1 | Type | F2 | Type | F3 | Type | F4 | Type |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 5 | GPIO0 | GPIO0 | I/O/T | GPIO0 | I/O/T | | | | | | |
| 6 | GPIO1 | GPIO1 | I/O/T | GPIO1 | I/O/T | | | | | | |
| 7 | GPIO2 | GPIO2 | I/O/T | GPIO2 | I/O/T | | | | | | |
| 8 | GPIO3 | GPIO3 | I/O/T | GPIO3 | I/O/T | | | | | | |
| 9 | GPIO4 | GPIO4 | I/O/T | GPIO4 | I/O/T | | | | | | |
| 10 | GPIO5 | GPIO5 | I/O/T | GPIO5 | I/O/T | | | | | | |
| 11 | GPIO6 | GPIO6 | I/O/T | GPIO6 | I/O/T | | | | | | |
| 12 | GPIO7 | GPIO7 | I/O/T | GPIO7 | I/O/T | | | | | | |
| 13 | GPIO8 | GPIO8 | I/O/T | GPIO8 | I/O/T | SUBSPICS1 | O/T | | | | |
| 14 | GPIO9 | GPIO9 | I/O/T | GPIO9 | I/O/T | SUBSPIHD | I1/O/T | FSPIHD | I1/O/T | | |
| 15 | GPIO10 | GPIO10 | I/O/T | GPIO10 | I/O/T | FSPIIO4 | I1/O/T | SUBSPICS0 | O/T | FSPICS0 | I1/O/T |
| 16 | GPIO11 | GPIO11 | I/O/T | GPIO11 | I/O/T | FSPIIO5 | I1/O/T | SUBSPID | I1/O/T | FSPID | I1/O/T |
| 17 | GPIO12 | GPIO12 | I/O/T | GPIO12 | I/O/T | FSPIIO6 | I1/O/T | SUBSPICLK | O/T | FSPICLK | I1/O/T |
| 18 | GPIO13 | GPIO13 | I/O/T | GPIO13 | I/O/T | FSPIIO7 | I1/O/T | SUBSPIQ | I1/O/T | FSPIQ | I1/O/T |
| 19 | GPIO14 | GPIO14 | I/O/T | GPIO14 | I/O/T | FSPIDQS | O/T | SUBSPIWP | I1/O/T | FSPIWP | I1/O/T |
| 21 | GPIO15 | GPIO15 | I/O/T | GPIO15 | I/O/T | U0RTS | O | | | | |
| 22 | GPIO16 | GPIO16 | I/O/T | GPIO16 | I/O/T | U0CTS | I1 | | | | |
| 23 | GPIO17 | GPIO17 | I/O/T | GPIO17 | I/O/T | U1TXD | O | | | | |
| 24 | GPIO18 | GPIO18 | I/O/T | GPIO18 | I/O/T | U1RXD | I1 | CLK_OUT3 | O | | |
| 25 | GPIO19 | GPIO19 | I/O/T | GPIO19 | I/O/T | U1RTS | O | CLK_OUT2 | O | | |
| 26 | GPIO20 | GPIO20 | I/O/T | GPIO20 | I/O/T | U1CTS | I1 | CLK_OUT1 | O | | |
| 27 | GPIO21 | GPIO21 | I/O/T | GPIO21 | I/O/T | | | | | | |
| 28 | GPIO26 | SPICS1 | O/T | GPIO26 | I/O/T | | | | | | |
| 30 | GPIO27 | SPIHD | I1/O/T | GPIO27 | I/O/T | | | | | | |
| 31 | GPIO28 | SPIWP | I1/O/T | GPIO28 | I/O/T | | | | | | |
| 32 | GPIO29 | SPICS0 | O/T | GPIO29 | I/O/T | | | | | | |
| 33 | GPIO30 | SPICLK | O/T | GPIO30 | I/O/T | | | | | | |
| 34 | GPIO31 | SPIQ | I1/O/T | GPIO31 | I/O/T | | | | | | |
| 35 | GPIO32 | SPID | I1/O/T | GPIO32 | I/O/T | | | | | | |
| 36 | GPIO48 | SPICLK_N_DIFF | O/T | GPIO48 | I/O/T | SUBSPICLK_N_DIFF | O/T | | | | |
| 37 | GPIO47 | SPICLK_P_DIFF | O/T | GPIO47 | I/O/T | SUBSPICLK_P_DIFF | O/T | | | | |
| 38 | GPIO33 | GPIO33 | I/O/T | GPIO33 | I/O/T | FSPIHD | I1/O/T | SUBSPIHD | I1/O/T | SPIIO4 | I1/O/T |
| 39 | GPIO34 | GPIO34 | I/O/T | GPIO34 | I/O/T | FSPICS0 | I1/O/T | SUBSPICS0 | O/T | SPIIO5 | I1/O/T |
| 40 | GPIO35 | GPIO35 | I/O/T | GPIO35 | I/O/T | FSPID | I1/O/T | SUBSPID | I1/O/T | SPIIO6 | I1/O/T |
| 41 | GPIO36 | GPIO36 | I/O/T | GPIO36 | I/O/T | FSPICLK | I1/O/T | SUBSPICLK | O/T | SPIIO7 | I1/O/T |
| 42 | GPIO37 | GPIO37 | I/O/T | GPIO37 | I/O/T | FSPIQ | I1/O/T | SUBSPIQ | I1/O/T | SPIDQS | I0/O/T |
| 43 | GPIO38 | GPIO38 | I/O/T | GPIO38 | I/O/T | FSPIWP | I1/O/T | SUBSPIWP | I1/O/T | | |
| 44 | GPIO39 | MTCK | I1 | GPIO39 | I/O/T | CLK_OUT3 | O | SUBSPICS1 | O/T | | |
| 45 | GPIO40 | MTDO | O/T | GPIO40 | I/O/T | CLK_OUT2 | O | | | | |
| 47 | GPIO41 | MTDI | I1 | GPIO41 | I/O/T | CLK_OUT1 | O | | | | |
| 48 | GPIO42 | MTMS | I1 | GPIO42 | I/O/T | | | | | | |
| 49 | GPIO43 | U0TXD | O | GPIO43 | I/O/T | CLK_OUT1 | O | | | | |
| 50 | GPIO44 | U0RXD | I1 | GPIO44 | I/O/T | CLK_OUT2 | O | | | | |
| 51 | GPIO45 | GPIO45 | I/O/T | GPIO45 | I/O/T | | | | | | |
| 52 | GPIO46 | GPIO46 | I/O/T | GPIO46 | I/O/T | | | | | | |

Type descriptions: I = input, O = output, T = high impedance. I1 = input; if pin assigned a function other than Fn, the input signal of Fn is always 1. I0 = input; if pin assigned a function other than Fn, the input signal of Fn is always 0.

### 2.3.2 RTC Functions

When the chip is in Deep-sleep mode, the IO MUX will not work. The RTC IO MUX allows multiple input/output signals to be a single input/output pin in Deep-sleep mode, as the pin is connected to the RTC system and powered by VDD3P3_RTC.

RTC IO pins can be assigned to RTC functions. They can either work as RTC GPIOs (RTC_GPIO0, RTC_GPIO1, etc.), connected to the ULP coprocessor; or connect to RTC peripheral signals.

### Table 2-5. RTC Peripheral Signals Routed via RTC IO MUX

| Pin Function | Signal | Description |
|---|---|---|
| sar_i2c_scl... | Serial clock | RTC I2C0/1 interface |
| sar_i2c_sda... | Serial data | |

### Table 2-6. RTC Functions

| Pin No. | RTC IO Name | F0 | F1 | F2 | F3 |
|---|---|---|---|---|---|
| 5 | RTC_GPIO0 | RTC_GPIO0 | sar_i2c_scl_0 | | |
| 6 | RTC_GPIO1 | RTC_GPIO1 | sar_i2c_sda_0 | | |
| 7 | RTC_GPIO2 | RTC_GPIO2 | sar_i2c_scl_1 | | |
| 8 | RTC_GPIO3 | RTC_GPIO3 | sar_i2c_sda_1 | | |
| 9 | RTC_GPIO4 | RTC_GPIO4 | | | |
| 10 | RTC_GPIO5 | RTC_GPIO5 | | | |
| 11 | RTC_GPIO6 | RTC_GPIO6 | | | |
| 12 | RTC_GPIO7 | RTC_GPIO7 | | | |
| 13 | RTC_GPIO8 | RTC_GPIO8 | | | |
| 14 | RTC_GPIO9 | RTC_GPIO9 | | | |
| 15 | RTC_GPIO10 | RTC_GPIO10 | | | |
| 16 | RTC_GPIO11 | RTC_GPIO11 | | | |
| 17 | RTC_GPIO12 | RTC_GPIO12 | | | |
| 18 | RTC_GPIO13 | RTC_GPIO13 | | | |
| 19 | RTC_GPIO14 | RTC_GPIO14 | | | |
| 21 | RTC_GPIO15 | RTC_GPIO15 | | | |
| 22 | RTC_GPIO16 | RTC_GPIO16 | | | |
| 23 | RTC_GPIO17 | RTC_GPIO17 | | | |
| 24 | RTC_GPIO18 | RTC_GPIO18 | | | |
| 25 | RTC_GPIO19 | RTC_GPIO19 | | | |
| 26 | RTC_GPIO20 | RTC_GPIO20 | | | |
| 27 | RTC_GPIO21 | RTC_GPIO21 | | | |

Note: This column lists the RTC GPIO names, since RTC functions are configured with RTC GPIO registers that use RTC GPIO numbering.

### 2.3.3 Analog Functions

Some IO pins also have analog functions, for analog peripherals (such as ADC) in any power mode.

### Table 2-7. Analog Signals Routed to Analog Functions

| Pin Function | Signal | Description |
|---|---|---|
| TOUCH... | Touch sensor channel ... signal | Touch sensor interface |
| ADC..._CH... | ADC1/2 channel ... signal | ADC1/2 interface |
| XTAL_32K_N | Negative clock signal | 32 kHz external clock input/output connected to ESP32-S3's oscillator |
| XTAL_32K_P | Positive clock signal | |
| USB_D- | Data - | USB OTG and USB Serial/JTAG function |
| USB_D+ | Data + | |

### Table 2-8. Analog Functions

| Pin No. | GPIO | F0 | F1 |
|---|---|---|---|
| 6 | RTC_GPIO1 | TOUCH1 | ADC1_CH0 |
| 7 | RTC_GPIO2 | TOUCH2 | ADC1_CH1 |
| 8 | RTC_GPIO3 | TOUCH3 | ADC1_CH2 |
| 9 | RTC_GPIO4 | TOUCH4 | ADC1_CH3 |
| 10 | RTC_GPIO5 | TOUCH5 | ADC1_CH4 |
| 11 | RTC_GPIO6 | TOUCH6 | ADC1_CH5 |
| 12 | RTC_GPIO7 | TOUCH7 | ADC1_CH6 |
| 13 | RTC_GPIO8 | TOUCH8 | ADC1_CH7 |
| 14 | RTC_GPIO9 | TOUCH9 | ADC1_CH8 |
| 15 | RTC_GPIO10 | TOUCH10 | ADC1_CH9 |
| 16 | RTC_GPIO11 | TOUCH11 | ADC2_CH0 |
| 17 | RTC_GPIO12 | TOUCH12 | ADC2_CH1 |
| 18 | RTC_GPIO13 | TOUCH13 | ADC2_CH2 |
| 19 | RTC_GPIO14 | TOUCH14 | ADC2_CH3 |
| 21 | RTC_GPIO15 | XTAL_32K_P | ADC2_CH4 |
| 22 | RTC_GPIO16 | XTAL_32K_N | ADC2_CH5 |
| 23 | RTC_GPIO17 | | ADC2_CH6 |
| 24 | RTC_GPIO18 | | ADC2_CH7 |
| 25 | RTC_GPIO19 | USB_D- | ADC2_CH8 |
| 26 | RTC_GPIO20 | USB_D+ | ADC2_CH9 |

### 2.3.4 Restrictions for GPIOs and RTC_GPIOs

All IO pins of ESP32-S3 have GPIO and some have RTC_GPIO pin functions. However, some IOs have restrictions:

- **IO Pins** — allocated for communication with in-package flash/PSRAM and NOT recommended for other uses. See Section 2.6 Pin Mapping Between Chip and Flash/PSRAM.
- **IO Pins** — have one of the following important functions:
  - **Strapping pins** — need to be at certain logic levels at startup: GPIO0, GPIO3, GPIO45, GPIO46. See Section 3 Boot Configurations.
  - **USB_D+/-** — by default, connected to the USB Serial/JTAG Controller: GPIO19, GPIO20.
  - **JTAG interface** — often used for debugging: GPIO39 (MTCK), GPIO40 (MTDO), GPIO41 (MTDI), GPIO42 (MTMS).
  - **UART0 interface** — often used for debugging: GPIO43 (U0TXD), GPIO44 (U0RXD).
  - **8-line SPI interface** — no restrictions unless connected to flash/PSRAM using 8-line SPI mode.

### 2.3.5 Peripheral Pin Assignment

Table 2-9 highlights which pins can be assigned to each peripheral interface according to the following priorities:
- **Priority 1 (P1)**: Fixed pins connected directly to peripheral signals via IO MUX or RTC IO MUX.
- **Priority 2 (P2)**: GPIO pins can be freely used without restrictions.
- **Priority 3 (P3)**: GPIO pins should be used with caution, as they may conflict with important functions (strapping pins, USB Serial/JTAG, JTAG interface, UART0 interface, 8-line SPI).
- **Priority 4 (P4)**: GPIO pins already allocated or not recommended for use (SPI0/1 interface connected to in-package flash and PSRAM).

## 2.4 Analog Pins

### Table 2-10. Analog Pins

| Pin No. | Pin Name | Pin Type | Function |
|---|---|---|---|
| 1 | LNA_IN | I/O | Low Noise Amplifier (RF LNA) input/output signals |
| 4 | CHIP_PU | I | High: on, enables the chip (powered up). Low: off, disables the chip (powered down). Do not leave floating. |
| 53 | XTAL_N | — | External clock input/output connected to chip's crystal or oscillator. P/N means differential clock positive/negative. |
| 54 | XTAL_P | — | |

## 2.5 Power Supply

### 2.5.1 Power Pins

### Table 2-11. Power Pins

| Pin No. | Pin Name | Direction | Power Domain/Other | IO Pins |
|---|---|---|---|---|
| 2 | VDD3P3 | Input | Analog power domain | |
| 3 | VDD3P3 | Input | Analog power domain | |
| 20 | VDD3P3_RTC | Input | RTC and part of Digital power domains | RTC IO |
| 29 | VDD_SPI | Input | In-package memory (backup power line) | SPI IO |
| | | Output | In-package and off-package flash/PSRAM | SPI IO |
| 46 | VDD3P3_CPU | Input | Digital power domain | Digital IO |
| 55 | VDDA | Input | Analog power domain | |
| 56 | VDDA | Input | Analog power domain | |
| 57 | GND | — | External ground connection | |

### 2.5.2 Power Scheme

### Table 2-12. Voltage Regulators

| Voltage Regulator | Output | Power Supply |
|---|---|---|
| Digital | 1.1 V | Digital power domain |
| Low-power | 1.1 V | RTC power domain |
| Flash | 1.8 V | Can be configured to power in-package flash/PSRAM or off-package memory |

### 2.5.3 Chip Power-up and Reset

Once the power is supplied to the chip, its power rails need a short time to stabilize. After that, CHIP_PU is pulled high to activate the chip.

### Table 2-13. Description of Timing Parameters for Power-up and Reset

| Parameter | Description | Min (μs) |
|---|---|---|
| t_STBL | Time reserved for the power rails of VDDA, VDD3P3, VDD3P3_RTC, and VDD3P3_CPU to stabilize before the CHIP_PU pin is pulled high to activate the chip | 50 |
| t_RST | Time reserved for CHIP_PU to stay below V_IL_nRST to reset the chip | 50 |

## 2.6 Pin Mapping Between Chip and Flash/PSRAM

### Table 2-14. Pin Mapping Between Chip and Flash or PSRAM

| Pin No. | Pin Name | Single SPI | Dual SPI | Quad SPI/QPI | Octal SPI/OPI |
|---|---|---|---|---|---|
| | | Flash | PSRAM | Flash | PSRAM | Flash | PSRAM | Flash | PSRAM |
| 28 | SPICS1 | | | | | CE# | CE# | CE# | CE# |
| 30 | SPIHD | | | | | HOLD#/SIO3 | HOLD#/SIO3 | DQ3 | DQ3 |
| 31 | SPIWP | | | | | WP#/SIO2 | WP#/SIO2 | DQ2 | DQ2 |
| 32 | SPICS0 | CS# | CS# | CS# | CS# | CS# | CS# | CS# | CS# |
| 33 | SPICLK | CLK | CLK | CLK | CLK | CLK | CLK | CLK | CLK |
| 34 | SPIQ | DO/SO/SIO1 | DO/SO/SIO1 | DO/SO/SIO1 | DO/SO/SIO1 | DQ1 | DQ1 |
| 35 | SPID | DI/SI/SIO0 | DI/SI/SIO0 | DI/SI/SIO0 | DI/SI/SIO0 | DQ0 | DQ0 |
| 38 | GPIO33 | | | | | | | DQ4 | DQ4 |
| 39 | GPIO34 | | | | | | | DQ5 | DQ5 |
| 40 | GPIO35 | | | | | | | DQ6 | DQ6 |
| 41 | GPIO36 | | | | | | | DQ7 | DQ7 |
| 42 | GPIO37 | | | | | | | DQS/DM | DQS/DM |

CS0 is for in-package flash. CS1 is for in-package PSRAM.

## Consolidated Pin Overview

### Table 7-1. Consolidated Pin Overview

| Pin No. | Pin Name | Pin Type | Pin Providing Power | At Reset | After Reset | RTC IO MUX F0 | RTC IO MUX F3 | Analog F0 | Analog F1 | IO MUX F0 | Type | IO MUX F1 | Type | IO MUX F2 | Type | IO MUX F3 | Type | IO MUX F4 | Type |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 1 | LNA_IN | Analog | | | | | | | | | | | | | | | | |
| 2 | VDD3P3 | Power | | | | | | | | | | | | | | | | |
| 3 | VDD3P3 | Power | | | | | | | | | | | | | | | | |
| 4 | CHIP_PU | Analog | VDD3P3_RTC | | | | | | | | | | | | | | | |
| 5 | GPIO0 | IO | VDD3P3_RTC | WPU, IE | WPU, IE | RTC_GPIO0 | sar_i2c_scl_0 | | | GPIO0 | I/O/T | GPIO0 | I/O/T | | | | | |
| 6 | GPIO1 | IO | VDD3P3_RTC | IE | IE | RTC_GPIO1 | sar_i2c_sda_0 | TOUCH1 | ADC1_CH0 | GPIO1 | I/O/T | GPIO1 | I/O/T | | | | | |
| 7 | GPIO2 | IO | VDD3P3_RTC | IE | IE | RTC_GPIO2 | sar_i2c_scl_1 | TOUCH2 | ADC1_CH1 | GPIO2 | I/O/T | GPIO2 | I/O/T | | | | | |
| 8 | GPIO3 | IO | VDD3P3_RTC | IE | IE | RTC_GPIO3 | sar_i2c_sda_1 | TOUCH3 | ADC1_CH2 | GPIO3 | I/O/T | GPIO3 | I/O/T | | | | | |
| 9 | GPIO4 | IO | VDD3P3_RTC | | | RTC_GPIO4 | | TOUCH4 | ADC1_CH3 | GPIO4 | I/O/T | GPIO4 | I/O/T | | | | | |
| 10 | GPIO5 | IO | VDD3P3_RTC | | | RTC_GPIO5 | | TOUCH5 | ADC1_CH4 | GPIO5 | I/O/T | GPIO5 | I/O/T | | | | | |
| 11 | GPIO6 | IO | VDD3P3_RTC | | | RTC_GPIO6 | | TOUCH6 | ADC1_CH5 | GPIO6 | I/O/T | GPIO6 | I/O/T | | | | | |
| 12 | GPIO7 | IO | VDD3P3_RTC | | | RTC_GPIO7 | | TOUCH7 | ADC1_CH6 | GPIO7 | I/O/T | GPIO7 | I/O/T | | | | | |
| 13 | GPIO8 | IO | VDD3P3_RTC | | | RTC_GPIO8 | | TOUCH8 | ADC1_CH7 | GPIO8 | I/O/T | GPIO8 | I/O/T | SUBSPICS1 | O/T | | | |
| 14 | GPIO9 | IO | VDD3P3_RTC | IE | | RTC_GPIO9 | | TOUCH9 | ADC1_CH8 | GPIO9 | I/O/T | GPIO9 | I/O/T | SUBSPIHD | I1/O/T | FSPIHD | I1/O/T | | |
| 15 | GPIO10 | IO | VDD3P3_RTC | IE | | RTC_GPIO10 | | TOUCH10 | ADC1_CH9 | GPIO10 | I/O/T | GPIO10 | I/O/T | FSPIIO4 | I1/O/T | SUBSPICS0 | O/T | FSPICS0 | I1/O/T |
| 16 | GPIO11 | IO | VDD3P3_RTC | IE | | RTC_GPIO11 | | TOUCH11 | ADC2_CH0 | GPIO11 | I/O/T | GPIO11 | I/O/T | FSPIIO5 | I1/O/T | SUBSPID | I1/O/T | FSPID | I1/O/T |
| 17 | GPIO12 | IO | VDD3P3_RTC | IE | | RTC_GPIO12 | | TOUCH12 | ADC2_CH1 | GPIO12 | I/O/T | GPIO12 | I/O/T | FSPIIO6 | I1/O/T | SUBSPICLK | O/T | FSPICLK | I1/O/T |
| 18 | GPIO13 | IO | VDD3P3_RTC | IE | | RTC_GPIO13 | | TOUCH13 | ADC2_CH2 | GPIO13 | I/O/T | GPIO13 | I/O/T | FSPIIO7 | I1/O/T | SUBSPIQ | I1/O/T | FSPIQ | I1/O/T |
| 19 | GPIO14 | IO | VDD3P3_RTC | IE | | RTC_GPIO14 | | TOUCH14 | ADC2_CH3 | GPIO14 | I/O/T | GPIO14 | I/O/T | FSPIDQS | O/T | SUBSPIWP | I1/O/T | FSPIWP | I1/O/T |
| 20 | VDD3P3_RTC | Power | | | | | | | | | | | | | | | | | |
| 21 | XTAL_32K_P | IO | VDD3P3_RTC | | | RTC_GPIO15 | XTAL_32K_P | ADC2_CH4 | | GPIO15 | I/O/T | GPIO15 | I/O/T | U0RTS | O | | | |
| 22 | XTAL_32K_N | IO | VDD3P3_RTC | | | RTC_GPIO16 | XTAL_32K_N | ADC2_CH5 | | GPIO16 | I/O/T | GPIO16 | I/O/T | U0CTS | I1 | | | |
| 23 | GPIO17 | IO | VDD3P3_RTC | IE | | RTC_GPIO17 | | ADC2_CH6 | | GPIO17 | I/O/T | GPIO17 | I/O/T | U1TXD | O | | | |
| 24 | GPIO18 | IO | VDD3P3_RTC | IE | | RTC_GPIO18 | | ADC2_CH7 | | GPIO18 | I/O/T | GPIO18 | I/O/T | U1RXD | I1 | CLK_OUT3 | O | | |
| 25 | GPIO19 | IO | VDD3P3_RTC | | | RTC_GPIO19 | USB_D- | ADC2_CH8 | | GPIO19 | I/O/T | GPIO19 | I/O/T | U1RTS | O | CLK_OUT2 | O | | |
| 26 | GPIO20 | IO | VDD3P3_RTC | USB_PU | USB_PU | RTC_GPIO20 | USB_D+ | ADC2_CH9 | | GPIO20 | I/O/T | GPIO20 | I/O/T | U1CTS | I1 | CLK_OUT1 | O | | |
| 27 | GPIO21 | IO | VDD3P3_RTC | | | RTC_GPIO21 | | | | GPIO21 | I/O/T | GPIO21 | I/O/T | | | | | |
| 28 | SPICS1 | IO | VDD_SPI | WPU, IE | WPU, IE | | | | | SPICS1 | O/T | GPIO26 | I/O/T | | | | | |
| 29 | VDD_SPI | Power | | | | | | | | | | | | | | | | |
| 30 | SPIHD | IO | VDD_SPI | WPU, IE | WPU, IE | | | | | SPIHD | I1/O/T | GPIO27 | I/O/T | | | | | |
| 31 | SPIWP | IO | VDD_SPI | WPU, IE | WPU, IE | | | | | SPIWP | I1/O/T | GPIO28 | I/O/T | | | | | |
| 32 | SPICS0 | IO | VDD_SPI | WPU, IE | WPU, IE | | | | | SPICS0 | O/T | GPIO29 | I/O/T | | | | | |
| 33 | SPICLK | IO | VDD_SPI | WPU, IE | WPU, IE | | | | | SPICLK | O/T | GPIO30 | I/O/T | | | | | |
| 34 | SPIQ | IO | VDD_SPI | WPU, IE | WPU, IE | | | | | SPIQ | I1/O/T | GPIO31 | I/O/T | | | | | |
| 35 | SPID | IO | VDD_SPI | WPU, IE | WPU, IE | | | | | SPID | I1/O/T | GPIO32 | I/O/T | | | | | |
| 36 | SPICLK_N | IO | VDD_SPI/VDD3P3_CPU | IE | IE | | | | | SPICLK_N_DIFF | O/T | GPIO48 | I/O/T | SUBSPICLK_N_DIFF | O/T | | | |
| 37 | SPICLK_P | IO | VDD_SPI/VDD3P3_CPU | IE | IE | | | | | SPICLK_P_DIFF | O/T | GPIO47 | I/O/T | SUBSPICLK_P_DIFF | O/T | | | |
| 38 | GPIO33 | IO | VDD_SPI/VDD3P3_CPU | IE | | | | | | GPIO33 | I/O/T | GPIO33 | I/O/T | FSPIHD | I1/O/T | SUBSPIHD | I1/O/T | SPIIO4 | I1/O/T |
| 39 | GPIO34 | IO | VDD_SPI/VDD3P3_CPU | IE | | | | | | GPIO34 | I/O/T | GPIO34 | I/O/T | FSPICS0 | I1/O/T | SUBSPICS0 | O/T | SPIIO5 | I1/O/T |
| 40 | GPIO35 | IO | VDD_SPI/VDD3P3_CPU | IE | | | | | | GPIO35 | I/O/T | GPIO35 | I/O/T | FSPID | I1/O/T | SUBSPID | I1/O/T | SPIIO6 | I1/O/T |
| 41 | GPIO36 | IO | VDD_SPI/VDD3P3_CPU | IE | | | | | | GPIO36 | I/O/T | GPIO36 | I/O/T | FSPICLK | I1/O/T | SUBSPICLK | O/T | SPIIO7 | I1/O/T |
| 42 | GPIO37 | IO | VDD_SPI/VDD3P3_CPU | IE | | | | | | GPIO37 | I/O/T | GPIO37 | I/O/T | FSPIQ | I1/O/T | SUBSPIQ | I1/O/T | SPIDQS | I0/O/T |
| 43 | GPIO38 | IO | VDD3P3_CPU | IE | | | | | | GPIO38 | I/O/T | GPIO38 | I/O/T | FSPIWP | I1/O/T | SUBSPIWP | I1/O/T | | |
| 44 | MTCK | IO | VDD3P3_CPU | IE | | | | | | MTCK | I1 | GPIO39 | I/O/T | CLK_OUT3 | O | SUBSPICS1 | O/T | | |
| 45 | MTDO | IO | VDD3P3_CPU | IE | | | | | | MTDO | O/T | GPIO40 | I/O/T | CLK_OUT2 | O | | | | |
| 46 | VDD3P3_CPU | Power | | | | | | | | | | | | | | | | | |
| 47 | MTDI | IO | VDD3P3_CPU | IE | | | | | | MTDI | I1 | GPIO41 | I/O/T | CLK_OUT1 | O | | | | |
| 48 | MTMS | IO | VDD3P3_CPU | IE | | | | | | MTMS | I1 | GPIO42 | I/O/T | | | | | |
| 49 | U0TXD | IO | VDD3P3_CPU | WPU, IE | WPU, IE | | | | | U0TXD | O | GPIO43 | I/O/T | CLK_OUT1 | O | | | |
| 50 | U0RXD | IO | VDD3P3_CPU | WPU, IE | WPU, IE | | | | | U0RXD | I1 | GPIO44 | I/O/T | CLK_OUT2 | O | | | |
| 51 | GPIO45 | IO | VDD3P3_CPU | WPD, IE | WPD, IE | | | | | GPIO45 | I/O/T | GPIO45 | I/O/T | | | | | |
| 52 | GPIO46 | IO | VDD3P3_CPU | WPD, IE | WPD, IE | | | | | GPIO46 | I/O/T | GPIO46 | I/O/T | | | | | |
| 53 | XTAL_N | Analog | | | | | | | | | | | | | | | | |
| 54 | XTAL_P | Analog | | | | | | | | | | | | | | | | |
| 55 | VDDA | Power | | | | | | | | | | | | | | | | |
| 56 | VDDA | Power | | | | | | | | | | | | | | | | |
| 57 | GND | Power | | | | | | | | | | | | | | | | |
