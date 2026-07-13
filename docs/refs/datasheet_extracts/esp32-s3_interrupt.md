---
type: Hardware Reference
title: ESP32-S3 Interrupt Matrix
description: Peripheral interrupt allocation to CPU0 and CPU1, with 99 peripheral interrupt sources, 26 interrupts per CPU, and 6 internal interrupts per CPU for the ESP32-S3.
tags: [esp32-s3, hardware, interrupt, interrupt-matrix]
timestamp: 2026-07-13
source: esp32-s3_datasheet_en.md (v2.2), lines 2781–2791 and 2481
---

# ESP32-S3 Interrupt Matrix

Extracted from ESP32-S3 Series Datasheet v2.2, Section 4.1.3.4.

The interrupt matrix embedded in ESP32-S3 independently allocates peripheral interrupt sources to the two CPUs' peripheral interrupts, to timely inform CPU0 or CPU1 to process the interrupts once the interrupt signals are generated.

### Feature List

- **99 peripheral interrupt sources** as input
- **Generate 26 peripheral interrupts to CPU0** and **26 peripheral interrupts to CPU1** as output. The remaining six CPU0 interrupts and six CPU1 interrupts are internal interrupts.
- **Disable CPU non-maskable interrupt (NMI)** sources
- **Query current interrupt status** of peripheral interrupt sources

The CPU supports **32 interrupts at six levels** (see Section 4.1.1.1 CPU).

For details, see ESP32-S3 Technical Reference Manual > Chapter Interrupt Matrix.
