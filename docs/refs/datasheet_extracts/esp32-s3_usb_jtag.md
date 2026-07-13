---
type: Hardware Reference
title: ESP32-S3 USB and JTAG
description: USB 2.0 OTG Full-Speed interface and USB Serial/JTAG controller features including device/host modes, CDC-ACM, and integrated transceiver for the ESP32-S3.
tags: [esp32-s3, hardware, usb, jtag, serial]
timestamp: 2026-07-13
source: esp32-s3_datasheet_en.md (v2.2), lines 3365–3419
---

# ESP32-S3 USB and JTAG

Extracted from ESP32-S3 Series Datasheet v2.2, Sections 4.2.1.7 and 4.2.1.8.

## 4.2.1.7 USB 2.0 OTG Full-Speed Interface

ESP32-S3 features a full-speed USB OTG interface along with an integrated transceiver. The USB OTG interface complies with the USB 2.0 specification.

### General Features

- FS and LS data rates
- HNP and SRP as A-device or B-device
- Dynamic FIFO (DFIFO) sizing
- Multiple modes of memory access:
  - Scatter/Gather DMA mode
  - Buffer DMA mode
  - Slave mode
- Can choose integrated transceiver or external transceiver
- Utilizing integrated transceiver with USB Serial/JTAG by time-division multiplexing when only integrated transceiver is used
- Support USB OTG using one of the transceivers while USB Serial/JTAG using the other one when both integrated transceiver or external transceiver are used

### Device Mode Features

- Endpoint number 0 always present (bi-directional, consisting of EP0 IN and EP0 OUT)
- Six additional endpoints (endpoint numbers 1 to 6), configurable as IN or OUT
- Maximum of five IN endpoints concurrently active at any time (including EP0 IN)
- All OUT endpoints share a single RX FIFO
- Each IN endpoint has a dedicated TX FIFO

### Host Mode Features

- Eight channels (pipes)
  - A control pipe consists of two channels (IN and OUT), as IN and OUT transactions must be handled separately. Only Control transfer type is supported.
  - Each of the other seven channels is dynamically configurable to be IN or OUT, and supports Bulk, Isochronous, and Interrupt transfer types.
- All channels share an RX FIFO, non-periodic TX FIFO, and periodic TX FIFO. The size of each FIFO is configurable.

For details, see ESP32-S3 Technical Reference Manual > Chapter USB On-The-Go.

### Pin Assignment

For details, see Section 2.3.5 Peripheral Pin Assignment.

## 4.2.1.8 USB Serial/JTAG Controller

ESP32-S3 integrates a USB Serial/JTAG controller.

### Feature List

- USB Full-speed device.
- Can be configured to either use internal USB PHY of ESP32-S3 or external PHY via GPIO matrix.
- Fixed function device, hardwired for CDC-ACM (Communication Device Class - Abstract Control Model) and JTAG adapter functionality.
- Two OUT Endpoints, three IN Endpoints in addition to Control Endpoint 0; Up to 64-byte data payload size.
- Internal PHY, so no or very few external components needed to connect to a host computer.
- CDC-ACM adherent serial port emulation is plug-and-play on most modern OSes.
- JTAG interface allows fast communication with CPU debug core using a compact representation of JTAG instructions.
- CDC-ACM supports host controllable chip reset and entry into download mode.

For details, see ESP32-S3 Technical Reference Manual > Chapter USB Serial/JTAG Controller.

### Pin Assignment

For details, see Section 2.3.5 Peripheral Pin Assignment.

### USB Pins

The USB interface uses the following pins:
- GPIO19 — USB_D-
- GPIO20 — USB_D+
