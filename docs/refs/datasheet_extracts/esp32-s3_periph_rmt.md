---
type: Hardware Reference
title: ESP32-S3 Remote Control Peripheral (RMT)
description: RMT features including 4 TX and 4 RX channels, 384×32-bit shared RAM, modulation, filtering, wrap modes, and DMA access for the ESP32-S3.
tags: [esp32-s3, hardware, rmt, remote-control]
timestamp: 2026-07-13
source: esp32-s3_datasheet_en.md (v2.2), lines 3470–3486
---

# ESP32-S3 Remote Control Peripheral (RMT)

Extracted from ESP32-S3 Series Datasheet v2.2, Section 4.2.1.11.

The Remote Control Peripheral (RMT) is designed to send and receive infrared remote control signals.

### Feature List

- **Four TX channels**
- **Four RX channels**
- Support multiple channels (programmable) transmitting data simultaneously
- **Eight channels share a 384 × 32-bit RAM**
- Support **modulation on TX pulses**
- Support **filtering and demodulation on RX pulses**
- **Wrap TX mode**
- **Wrap RX mode**
- **Continuous TX mode**
- **DMA access for TX mode on channel 3**
- **DMA access for RX mode on channel 7**

For details, see ESP32-S3 Technical Reference Manual > Chapter Remote Control Peripheral.

### Pin Assignment

For details, see Section 2.3.5 Peripheral Pin Assignment.
