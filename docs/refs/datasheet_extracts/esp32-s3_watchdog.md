---
type: Hardware Reference
title: ESP32-S3 Watchdog Timers
description: Main System Watchdog Timers (MWDT), RTC Watchdog Timer (RWDT), and XTAL32K watchdog timer features, stages, and expiry actions for the ESP32-S3.
tags: [esp32-s3, hardware, watchdog, wdt]
timestamp: 2026-07-13
source: esp32-s3_datasheet_en.md (v2.2), lines 3002–3030
---

# ESP32-S3 Watchdog Timers

Extracted from ESP32-S3 Series Datasheet v2.2, Sections 4.1.3.8 and 4.1.3.9.

## 4.1.3.8 Watchdog Timers

ESP32-S3 contains three watchdog timers: one in each of the two timer groups (called Main System Watchdog Timers, or MWDT) and one in the RTC Module (called the RTC Watchdog Timer, or RWDT).

During the flash boot process, RWDT and the first MWDT are enabled automatically in order to detect and recover from booting errors.

### Feature List

- **Four stages**: each with a programmable timeout value; each stage can be configured, enabled and disabled separately
- **Upon expiry of each stage**:
  - Interrupt, CPU reset, or core reset occurs for MWDT
  - Interrupt, CPU reset, core reset, or system reset occurs for RWDT
- **32-bit expiry counter**
- **Write protection**: to prevent RWDT and MWDT configuration from being altered inadvertently
- **Flash boot protection**: If the boot process from an SPI flash does not complete within a predetermined period of time, the watchdog will reboot the entire main system

For details, see ESP32-S3 Technical Reference Manual > Chapter Watchdog Timers.

## 4.1.3.9 XTAL32K Watchdog Timers

### Interrupt and Wake-Up

When the XTAL32K watchdog timer detects the oscillation failure of XTAL32K_CLK, an oscillation failure interrupt RTC_XTAL32K_DEAD_INT is generated. At this point, the CPU will be woken up if in Light-sleep mode or Deep-sleep mode.

### BACKUP32K_CLK

Once the XTAL32K watchdog timer detects the oscillation failure of XTAL32K_CLK, it replaces XTAL32K_CLK with BACKUP32K_CLK (with a frequency of 32 kHz or so) derived from RTC_CLK as RTC's SLOW_CLK, so as to ensure proper functioning of the system.

For details, see ESP32-S3 Technical Reference Manual > Chapter XTAL32K Watchdog Timers.
