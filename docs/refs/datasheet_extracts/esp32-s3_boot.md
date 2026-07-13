---
type: Hardware Reference
title: ESP32-S3 Boot Configurations
description: Strapping pins, boot mode control, VDD_SPI voltage control, ROM messages printing control, and JTAG signal source control for the ESP32-S3.
tags: [esp32-s3, hardware, boot, strapping]
timestamp: 2026-07-13
source: esp32-s3_datasheet_en.md (v2.2), lines 2340–2459
---

# ESP32-S3 Boot Configurations

Extracted from ESP32-S3 Series Datasheet v2.2, Section 3.

The chip allows for configuring the following boot parameters through strapping pins and eFuse parameters at power-up or a hardware reset, without microcontroller interaction.

- Chip boot mode — Strapping pin: GPIO0 and GPIO46
- VDD_SPI voltage — Strapping pin: GPIO45; eFuse parameter: EFUSE_VDD_SPI_FORCE and EFUSE_VDD_SPI_TIEH
- ROM message printing — Strapping pin: GPIO46; eFuse parameter: EFUSE_UART_PRINT_CONTROL and EFUSE_DIS_USB_SERIAL_JTAG_ROM_PRINT
- JTAG signal source — Strapping pin: GPIO3; eFuse parameter: EFUSE_DIS_PAD_JTAG, EFUSE_DIS_USB_JTAG, and EFUSE_STRAP_JTAG_SEL

The default values of all the above eFuse parameters are 0, which means they are not burnt. Given that eFuse is one-time programmable, once programmed to 1, it can never be reverted to 0. For how to program eFuse parameters, please refer to ESP32-S3 Technical Reference Manual > Chapter eFuse Controller.

The default values of the strapping pins, namely the logic levels, are determined by pins' internal weak pull-up/pull-down resistors at reset if the pins are not connected to any circuit, or connected to an external high-impedance circuit.

### Table 3-1. Default Configuration of Strapping Pins

| Strapping Pin | Default Configuration | Bit Value |
|---|---|---|
| GPIO0 | Weak pull-up | 1 |
| GPIO3 | Floating | — |
| GPIO45 | Weak pull-down | 0 |
| GPIO46 | Weak pull-down | 0 |

To change the bit values, the strapping pins should be connected to external pull-down/pull-up resistances. If the ESP32-S3 is used as a device by a host MCU, the strapping pin voltage levels can also be controlled by the host MCU.

All strapping pins have latches. At Chip Reset, the latches sample the bit values of their respective strapping pins and store them until the chip is powered down or shut down. The states of latches cannot be changed in any other way. It makes the strapping pin values available during the entire chip operation, and the pins are freed up to be used as regular IO pins after reset. For details on Chip Reset, see ESP32-S3 Technical Reference Manual > Chapter Reset and Clock.

The timing of signals connected to the strapping pins should adhere to the setup time and hold time specifications below.

### Table 3-2. Description of Timing Parameters for the Strapping Pins

| Parameter | Description | Min (ms) |
|---|---|---|
| t_SU | Setup time is the time reserved for the power rails to stabilize before the CHIP_PU pin is pulled high to activate the chip. | 0 |
| t_H | Hold time is the time reserved for the chip to read the strapping pin values after CHIP_PU is already high and before these pins start operating as regular IO pins. | 3 |

## 3.1 Chip Boot Mode Control

GPIO0 and GPIO46 control the boot mode after the reset is released.

### Table 3-3. Chip Boot Mode Control

| Boot Mode | GPIO0 | GPIO46 |
|---|---|---|
| SPI boot mode | 1 | Any value |
| Joint download boot mode | 0 | 0 |

Notes:
- Bold marks the default value and configuration (SPI boot mode).
- Joint Download Boot mode supports the following download methods: USB Download Boot (USB-Serial-JTAG Download Boot, USB-OTG Download Boot) and UART Download Boot.
- In addition to SPI Boot and Joint Download Boot modes, ESP32-S3 also supports SPI Download Boot mode.
- For details, please see ESP32-S3 Technical Reference Manual > Chapter Chip Boot Control.

## 3.2 VDD_SPI Voltage Control

The required VDD_SPI voltage for the chips of the ESP32-S3 Series can be found in Table 1-1 ESP32-S3 Series Comparison.

The VDD_SPI voltage can be:
- (Default) 3.3 V supplied by VDD3P3_RTC via R_SPI
- 1.8 V supplied by the Flash Voltage Regulator

The voltage is determined by EFUSE_VDD_SPI_FORCE, GPIO45, and EFUSE_VDD_SPI_TIEH.

### Table 3-4. VDD_SPI Voltage Control

| VDD_SPI power source | Voltage | EFUSE_VDD_SPI_FORCE | GPIO45 | EFUSE_VDD_SPI_TIEH |
|---|---|---|---|---|
| VDD3P3_RTC via R_SPI | 3.3 V | 0 | 0 | Ignored |
| | | 1 | Ignored | 1 |
| Flash Voltage Regulator | 1.8 V | 0 | 1 | Ignored |
| | | 1 | Ignored | 0 |

Notes:
- Bold marks the default value and configuration.
- See Section 2.5.2 Power Scheme.

## 3.3 ROM Messages Printing Control

During the boot process, the messages by the ROM code can be printed to:
- (Default) UART0 and USB Serial/JTAG controller
- USB Serial/JTAG controller
- UART0

The ROM messages printing to UART or USB Serial/JTAG controller can be respectively disabled by configuring registers and eFuse. For detailed information, please refer to ESP32-S3 Technical Reference Manual > Chapter Chip Boot Control.

## 3.4 JTAG Signal Source Control

The strapping pin GPIO3 can be used to control the source of JTAG signals during the early boot process. This pin does not have any internal pull resistors and the strapping value must be controlled by the external circuit that cannot be in a high impedance state.

GPIO3 is used in combination with EFUSE_DIS_PAD_JTAG, EFUSE_DIS_USB_JTAG, and EFUSE_STRAP_JTAG_SEL.

### Table 3-5. JTAG Signal Source Control

| JTAG Signal Source | EFUSE_DIS_PAD_JTAG | EFUSE_DIS_USB_JTAG | EFUSE_STRAP_JTAG_SEL | GPIO3 |
|---|---|---|---|---|
| USB Serial/JTAG Controller | 0 | 0 | 0 | Ignored |
| | 0 | 0 | 1 | 1 |
| | 1 | 0 | Ignored | Ignored |
| JTAG pins | 0 | 0 | 1 | 0 |
| | 0 | 1 | Ignored | Ignored |
| JTAG is disabled | 1 | 1 | Ignored | Ignored |

Notes:
- Bold marks the default value and configuration.
- JTAG pins refer to MTDI, MTCK, MTMS, and MTDO.
