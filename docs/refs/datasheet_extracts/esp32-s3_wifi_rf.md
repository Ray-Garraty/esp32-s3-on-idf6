---
type: Hardware Reference
title: ESP32-S3 Wi-Fi RF Characteristics
description: Wi-Fi radio and baseband features, MAC features, networking features, TX power, EVM, RX sensitivity, maximum RX level, and adjacent channel rejection for the ESP32-S3.
tags: [esp32-s3, hardware, wifi, rf, wireless]
timestamp: 2026-07-13
source: esp32-s3_datasheet_en.md (v2.2), lines 3585–4216
---

# ESP32-S3 Wi-Fi RF Characteristics

Extracted from ESP32-S3 Series Datasheet v2.2, Sections 4.3.2 and 6.1.

## 4.3.2 Wi-Fi

This subsection describes the chip's Wi-Fi capabilities, which facilitate wireless communication at a high data rate.

### 4.3.2.1 Wi-Fi Radio and Baseband

The ESP32-S3 Wi-Fi radio and baseband support the following features:
- 802.11b/g/n
- 802.11n MCS0-7 that supports 20 MHz and 40 MHz bandwidth
- 802.11n MCS32
- 802.11n 0.4 μs guard-interval
- Data rate up to 150 Mbps
- RX STBC (single spatial stream)
- Adjustable transmitting power
- Antenna diversity with external RF switch, controlled by one or more GPIOs

### 4.3.2.2 Wi-Fi MAC

ESP32-S3 implements the full 802.11b/g/n Wi-Fi MAC protocol. It supports the Basic Service Set (BSS) STA and SoftAP operations under the Distributed Control Function (DCF). Power management is handled automatically with minimal host interaction.

- Four virtual Wi-Fi interfaces
- Simultaneous Infrastructure BSS Station mode, SoftAP mode, and Station + SoftAP mode
- RTS protection, CTS protection, Immediate Block ACK
- Fragmentation and defragmentation
- TX/RX A-MPDU, TX/RX A-MSDU
- TXOP
- WMM
- GCMP, CCMP, TKIP, WAPI, WEP, BIP, WPA2-PSK/WPA2-Enterprise, and WPA3-PSK/WPA3-Enterprise
- Automatic beacon monitoring (hardware TSF)
- 802.11mc FTM

### 4.3.2.3 Networking Features

Users are provided with libraries for TCP/IP networking, ESP-WIFI-MESH networking, and other networking protocols over Wi-Fi. TLS 1.2 support is also provided.

## 6.1 Wi-Fi Radio

The RF data is measured at the antenna port, where RF cable is connected, including the front-end loss. The front-end circuit is a 0 Ω resistor. Unless otherwise stated, the RF tests are conducted with a 3.3 V (±5%) supply at 25 °C ambient temperature.

### Table 6-1. Wi-Fi RF Characteristics

| Name | Description |
|---|---|
| Center frequency range of operating channel | 2412~2484 MHz |
| Wi-Fi wireless standard | IEEE 802.11b/g/n |

### 6.1.1 Wi-Fi RF Transmitter (TX) Characteristics

### Table 6-2. TX Power with Spectral Mask and EVM Meeting 802.11 Standards

| Rate | Min (dBm) | Typ (dBm) | Max (dBm) |
|---|---|---|---|
| 802.11b, 1 Mbps | — | 21.0 | — |
| 802.11b, 11 Mbps | — | 21.0 | — |
| 802.11g, 6 Mbps | — | 20.5 | — |
| 802.11g, 54 Mbps | — | 19.0 | — |
| 802.11n, HT20, MCS0 | — | 19.5 | — |
| 802.11n, HT20, MCS7 | — | 18.5 | — |
| 802.11n, HT40, MCS0 | — | 19.5 | — |
| 802.11n, HT40, MCS7 | — | 18.0 | — |

### Table 6-3. TX EVM Test

| Rate | Min (dB) | Typ (dB) | Limit (dB) |
|---|---|---|---|
| 802.11b, 1 Mbps, @21 dBm | — | −24.5 | 10 |
| 802.11b, 11 Mbps, @21 dBm | — | −24.5 | 10 |
| 802.11g, 6 Mbps, @20.5 dBm | — | −21.5 | 5 |
| 802.11g, 54 Mbps, @19 dBm | — | −28.0 | 25 |
| 802.11n, HT20, MCS0, @19.5 dBm | — | −23.0 | 5 |
| 802.11n, HT20, MCS7, @18.5 dBm | — | −29.5 | 27 |
| 802.11n, HT40, MCS0, @19.5 dBm | — | −23.0 | 5 |
| 802.11n, HT40, MCS7, @18 dBm | — | −29.5 | 27 |

EVM is measured at the corresponding typical TX power provided in Table 6-2.

### 6.1.2 Wi-Fi RF Receiver (RX) Characteristics

For RX tests, the PER (packet error rate) limit is 8% for 802.11b, and 10% for 802.11g/n.

### Table 6-4. RX Sensitivity

| Rate | Min (dBm) | Typ (dBm) | Max (dBm) |
|---|---|---|---|
| 802.11b, 1 Mbps | — | −98.4 | — |
| 802.11b, 2 Mbps | — | −95.4 | — |
| 802.11b, 5.5 Mbps | — | −93.0 | — |
| 802.11b, 11 Mbps | — | −88.6 | — |
| 802.11g, 6 Mbps | — | −93.2 | — |
| 802.11g, 9 Mbps | — | −91.8 | — |
| 802.11g, 12 Mbps | — | −91.2 | — |
| 802.11g, 18 Mbps | — | −88.6 | — |
| 802.11g, 24 Mbps | — | −86.0 | — |
| 802.11g, 36 Mbps | — | −82.4 | — |
| 802.11g, 48 Mbps | — | −78.2 | — |
| 802.11g, 54 Mbps | — | −76.5 | — |
| 802.11n, HT20, MCS0 | — | −92.6 | — |
| 802.11n, HT20, MCS1 | — | −91.0 | — |
| 802.11n, HT20, MCS2 | — | −88.2 | — |
| 802.11n, HT20, MCS3 | — | −85.0 | — |
| 802.11n, HT20, MCS4 | — | −81.8 | — |
| 802.11n, HT20, MCS5 | — | −77.4 | — |
| 802.11n, HT20, MCS6 | — | −75.8 | — |
| 802.11n, HT20, MCS7 | — | −74.2 | — |
| 802.11n, HT40, MCS0 | — | −90.0 | — |
| 802.11n, HT40, MCS1 | — | −88.0 | — |
| 802.11n, HT40, MCS2 | — | −85.2 | — |
| 802.11n, HT40, MCS3 | — | −82.0 | — |
| 802.11n, HT40, MCS4 | — | −79.0 | — |
| 802.11n, HT40, MCS5 | — | −74.4 | — |
| 802.11n, HT40, MCS6 | — | −72.8 | — |
| 802.11n, HT40, MCS7 | — | −71.4 | — |

### Table 6-5. Maximum RX Level

| Rate | Min (dBm) | Typ (dBm) | Max (dBm) |
|---|---|---|---|
| 802.11b, 1 Mbps | — | −5 | — |
| 802.11b, 11 Mbps | — | −5 | — |
| 802.11g, 6 Mbps | — | −5 | — |
| 802.11g, 54 Mbps | — | 0 | — |
| 802.11n, HT20, MCS0 | — | −5 | — |
| 802.11n, HT20, MCS7 | — | 0 | — |
| 802.11n, HT40, MCS0 | — | −5 | — |
| 802.11n, HT40, MCS7 | — | 0 | — |

### Table 6-6. RX Adjacent Channel Rejection

| Rate | Min (dB) | Typ (dB) | Max (dB) |
|---|---|---|---|
| 802.11b, 1 Mbps | — | 35 | — |
| 802.11b, 11 Mbps | — | 35 | — |
| 802.11g, 6 Mbps | — | 31 | — |
| 802.11g, 54 Mbps | — | 20 | — |
| 802.11n, HT20, MCS0 | — | 31 | — |
| 802.11n, HT20, MCS7 | — | 16 | — |
| 802.11n, HT40, MCS0 | — | 25 | — |
| 802.11n, HT40, MCS7 | — | 11 | — |
