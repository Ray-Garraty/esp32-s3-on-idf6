---
type: Hardware Reference
title: ESP32-S3 Power Management Unit
description: Predefined power modes (Active, Modem-sleep, Light-sleep, Deep-sleep), power domains, components, and wake-up sources for the ESP32-S3.
tags: [esp32-s3, hardware, power, pmu, sleep]
timestamp: 2026-07-13
source: esp32-s3_datasheet_en.md (v2.2), lines 2790–2974
---

# ESP32-S3 Power Management Unit (PMU)

Extracted from ESP32-S3 Series Datasheet v2.2, Section 4.1.3.5.

ESP32-S3 has an advanced Power Management Unit (PMU). It can be flexibly configured to power up different power domains of the chip to achieve the best balance between chip performance, power consumption, and wakeup latency.

The integrated Ultra-Low-Power (ULP) coprocessors allow ESP32-S3 to operate in Deep-sleep mode with most of the power domains turned off, thus achieving extremely low-power consumption.

Configuring the PMU is a complex procedure. To simplify power management for typical scenarios, there are the following predefined power modes that power up different combinations of power domains:

- **Active mode** — The CPU, RF circuits, and all peripherals are on. The chip can process data, receive, transmit, and listen.
- **Modem-sleep mode** — The CPU is on, but the clock frequency can be reduced. The wireless connections can be configured to remain active as RF circuits are periodically switched on when required.
- **Light-sleep mode** — The CPU stops running, and can be optionally powered on. The RTC peripherals, as well as the ULP coprocessor can be woken up periodically by the timer. The chip can be woken up via all wake up mechanisms: MAC, RTC timer, or external interrupts. Wireless connections can remain active. Some groups of digital peripherals can be optionally powered off.
- **Deep-sleep mode** — Only RTC is powered on. Wireless connection data is stored in RTC memory.

For power consumption in different power modes, see Section 5.6 Current Consumption.

## Components and Power Domains

The chip components are distributed across the following power domains:

- **Analog Power Domain**: 2.4 GHz Balun + Switch, 2.4 GHz Receiver, 2.4 GHz Transmitter, RF Synthesizer, Phase Lock Loop (PLL), XTAL_CLK (External Main Clock), RC_FAST_CLK (Fast RC Oscillator)
- **Digital Power Domain**: Wi-Fi MAC, Wi-Fi Baseband, Bluetooth LE Link Controller, Bluetooth LE Baseband, ROM, SRAM, Flash Encryption, RNG, USB Serial/JTAG, GPIO, UART, TWAI, General-purpose Timers, I2S, I2C, Pulse Counter, LED PWM, Camera Interface, SPI0/1, RMT, DIG ADC, System Timer, LCD Interface, Main System Watchdog Timers, MCPWM, RSA, AES, HMAC, Secure Boot, SPI2/3, GDMA, SD/MMC Host, USB OTG
- **RTC Power Domain**: RTC Memory, RTC Watchdog Timer, PMU, RTC GPIO, Temperature Sensor, Touch Sensor, ULP Coprocessor, RTC ADC, Optional RTC Peripherals (RTC I2C), eFuse Controller
- **CPU Power Subdomain**: Xtensa Dual-core 32-bit LX7 Microprocessor, JTAG, Cache, Interrupt Matrix, World Controller
- **Optional Digital Peripherals Subdomain**: RSA, AES, HMAC, Secure Boot, SPI2/3, GDMA, SD/MMC Host, USB OTG
- **Super Watchdog**: Independent power domain

### Table 4-1. Components and Power Domains

| Power Mode | RTC Power Domain | Optional RTC Periph | CPU | Optional Digital Periph | Wireless Digital Circuits | RC_FAST_CLK | XTAL_CLK | PLL | RF Circuits |
|---|---|---|---|---|---|---|---|---|---|
| Active | ON | ON | ON | ON | ON | ON | ON | ON | ON |
| Modem-sleep | ON | ON | ON | ON | ON | ON | ON | ON | OFF |
| Light-sleep | ON | ON | ON[1] | OFF[1] | OFF[1] | ON | OFF | OFF | OFF |
| Deep-sleep | ON | ON[1] | OFF | OFF | OFF | OFF | ON | OFF | OFF |

[1] Configurable. See ESP32-S3 Technical Reference Manual > Chapter Low-power Management for more details.
[2] If Wireless Digital Circuits are on, RF circuits are periodically switched on when required by internal operation to keep active wireless connections running.

For details, see ESP32-S3 Technical Reference Manual > Chapter Low Power Management.
