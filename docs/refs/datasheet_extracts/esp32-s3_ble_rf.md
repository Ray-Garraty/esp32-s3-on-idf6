---
type: Hardware Reference
title: ESP32-S3 Bluetooth LE RF Characteristics
description: Bluetooth LE 5.0 PHY modes, link controller features, TX and RX characteristics including sensitivity, transmit power, and adjacent channel selectivity for the ESP32-S3.
tags: [esp32-s3, hardware, bluetooth, ble, rf, wireless]
timestamp: 2026-07-13
source: esp32-s3_datasheet_en.md (v2.2), lines 3623–3655 and 4217–4599
---

# ESP32-S3 Bluetooth LE RF Characteristics

Extracted from ESP32-S3 Series Datasheet v2.2, Sections 4.3.3 and 6.2.

## 4.3.3 Bluetooth LE

ESP32-S3 includes a Bluetooth Low Energy subsystem that integrates a hardware link layer controller, an RF/modem block and a feature-rich software protocol stack. It supports the core features of Bluetooth 5 and Bluetooth Mesh.

### 4.3.3.1 Bluetooth LE PHY

- 1 Mbps PHY
- 2 Mbps PHY for high transmission speed and high data throughput
- Coded PHY for high RX sensitivity and long range (125 Kbps and 500 Kbps)
- Class 1 transmit power without external PA
- HW Listen Before Talk (LBT)

### 4.3.3.2 Bluetooth LE Link Controller

- LE Advertising Extensions, to enhance broadcasting capacity and broadcast more intelligent data
- Multiple Advertising Sets
- Simultaneous Advertising and Scanning
- Multiple connections in simultaneous central and peripheral roles
- Adaptive Frequency Hopping (AFH) and Channel Assessment
- LE Channel Selection Algorithm #2
- Connection Parameter Update
- High Duty Cycle Non-Connectable Advertising
- LE Privacy v1.2
- LE Data Packet Length Extension
- Link Layer Extended Scanner Filter Policies
- Low Duty Cycle Directed Advertising
- Link Layer Encryption
- LE Ping

## 6.2 Bluetooth LE Radio

### Table 6-7. Bluetooth LE Frequency

| Parameter | Min (MHz) | Typ (MHz) | Max (MHz) |
|---|---|---|---|
| Center frequency of operating channel | 2402 | — | 2480 |

### 6.2.1 Bluetooth LE RF Transmitter (TX) Characteristics

### Table 6-8. Transmitter Characteristics - Bluetooth LE 1 Mbps

| Parameter | Description | Min | Typ | Max | Unit |
|---|---|---|---|---|---|
| RF transmit power | RF power control range | −24.00 | 0 | 20.00 | dBm |
| | Gain control step | — | 3.00 | — | dB |
| Carrier frequency offset and drift | Max\|f_n\| (n=0,1,2,..k) | — | 2.50 | — | kHz |
| | Max\|f_0−f_n\| | — | 2.00 | — | kHz |
| | Max\|f_n−f_n−5\| | — | 1.39 | — | kHz |
| | \|f_1−f_0\| | — | 0.80 | — | kHz |
| Modulation characteristics | ∆f1_avg | — | 249.00 | — | kHz |
| | Min∆f2_max (for at least 99.9% of all ∆f2_max) | — | 198.00 | — | kHz |
| | ∆f2_avg/∆f1_avg | — | 0.86 | — | |
| In-band spurious emissions | ±2 MHz offset | — | −37.00 | — | dBm |
| | ±3 MHz offset | — | −42.00 | — | dBm |
| | >±3 MHz offset | — | −44.00 | — | dBm |

### Table 6-9. Transmitter Characteristics - Bluetooth LE 2 Mbps

| Parameter | Description | Min | Typ | Max | Unit |
|---|---|---|---|---|---|
| RF transmit power | RF power control range | −24.00 | 0 | 20.00 | dBm |
| | Gain control step | — | 3.00 | — | dB |
| Carrier frequency offset and drift | Max\|f_n\| (n=0,1,2,..k) | — | 2.50 | — | kHz |
| | Max\|f_0−f_n\| | — | 1.90 | — | kHz |
| | Max\|f_n−f_n−5\| | — | 1.40 | — | kHz |
| | \|f_1−f_0\| | — | 1.10 | — | kHz |
| Modulation characteristics | ∆f1_avg | — | 499.00 | — | kHz |
| | Min∆f2_max (for at least 99.9% of all ∆f2_max) | — | 416.00 | — | kHz |
| | ∆f2_avg/∆f1_avg | — | 0.89 | — | |
| In-band spurious emissions | ±4 MHz offset | — | −43.80 | — | dBm |
| | ±5 MHz offset | — | −45.80 | — | dBm |
| | >±5 MHz offset | — | −47.00 | — | dBm |

### Table 6-10. Transmitter Characteristics - Bluetooth LE 125 Kbps

| Parameter | Description | Min | Typ | Max | Unit |
|---|---|---|---|---|---|
| RF transmit power | RF power control range | −24.00 | 0 | 20.00 | dBm |
| | Gain control step | — | 3.00 | — | dB |
| Carrier frequency offset and drift | Max\|f_n\| (n=0,1,2,..k) | — | 0.80 | — | kHz |
| | Max\|f_0−f_n\| | — | 0.98 | — | kHz |
| | \|f_n−f_n−3\| | — | 0.30 | — | kHz |
| | \|f_0−f_3\| | — | 1.00 | — | kHz |
| Modulation characteristics | ∆f1_avg | — | 248.00 | — | kHz |
| | Min∆f1_max (for at least 99.9% of all ∆f1_max) | — | 222.00 | — | kHz |
| In-band spurious emissions | ±2 MHz offset | — | −37.00 | — | dBm |
| | ±3 MHz offset | — | −42.00 | — | dBm |
| | >±3 MHz offset | — | −44.00 | — | dBm |

### Table 6-11. Transmitter Characteristics - Bluetooth LE 500 Kbps

| Parameter | Description | Min | Typ | Max | Unit |
|---|---|---|---|---|---|
| RF transmit power | RF power control range | −24.00 | 0 | 20.00 | dBm |
| | Gain control step | — | 3.00 | — | dB |
| Carrier frequency offset and drift | Max\|f_n\| (n=0,1,2,..k) | — | 0.70 | — | kHz |
| | Max\|f_0−f_n\| | — | 0.90 | — | kHz |
| | \|f_n−f_n−3\| | — | 0.85 | — | kHz |
| | \|f_0−f_3\| | — | 0.34 | — | kHz |
| Modulation characteristics | ∆f2_avg | — | 213.00 | — | kHz |
| | Min∆f2_max (for at least 99.9% of all ∆f2_max) | — | 196.00 | — | kHz |
| In-band spurious emissions | ±2 MHz offset | — | −37.00 | — | dBm |
| | ±3 MHz offset | — | −42.00 | — | dBm |
| | >±3 MHz offset | — | −44.00 | — | dBm |

### 6.2.2 Bluetooth LE RF Receiver (RX) Characteristics

### Table 6-12. Receiver Characteristics - Bluetooth LE 1 Mbps

| Parameter | Description | Min | Typ | Max | Unit |
|---|---|---|---|---|---|
| Sensitivity @30.8% PER | | — | −97.5 | — | dBm |
| Maximum received signal @30.8% PER | | — | −8 | — | dBm |
| Co-channel C/I | F = F0 MHz | — | 9 | — | dB |
| Adjacent channel selectivity C/I | F = F0 + 1 MHz | — | 3 | — | dB |
| | F = F0 – 1 MHz | — | 3 | — | dB |
| | F = F0 + 2 MHz | — | 28 | — | dB |
| | F = F0 – 2 MHz | — | 30 | — | dB |
| | F = F0 + 3 MHz | — | 31 | — | dB |
| | F = F0 – 3 MHz | — | 33 | — | dB |
| | F>F0 + 3 MHz | — | 32 | — | dB |
| | F>F0 – 3 MHz | — | 36 | — | dB |
| Image frequency | | — | 32 | — | dB |
| Adjacent channel to image frequency | F = F_image + 1 MHz | — | 39 | — | dB |
| | F = F_image – 1 MHz | — | 31 | — | dB |
| Out-of-band blocking performance | 30 MHz~2000 MHz | — | −9 | — | dBm |
| | 2003 MHz~2399 MHz | — | −19 | — | dBm |
| | 2484 MHz~2997 MHz | — | −16 | — | dBm |
| | 3000 MHz~12.75 GHz | — | −5 | — | dBm |
| Intermodulation | | — | −31 | — | dBm |

### Table 6-13. Receiver Characteristics - Bluetooth LE 2 Mbps

| Parameter | Description | Min | Typ | Max | Unit |
|---|---|---|---|---|---|
| Sensitivity @30.8% PER | | — | −93.5 | — | dBm |
| Maximum received signal @30.8% PER | | — | −3 | — | dBm |
| Co-channel C/I | F = F0 MHz | — | 10 | — | dB |
| Adjacent channel selectivity C/I | F = F0 + 2 MHz | — | 8 | — | dB |
| | F = F0 – 2 MHz | — | 5 | — | dB |
| | F = F0 + 4 MHz | — | 31 | — | dB |
| | F = F0 – 4 MHz | — | 33 | — | dB |
| | F = F0 + 6 MHz | — | 37 | — | dB |
| | F = F0 – 6 MHz | — | 37 | — | dB |
| | F>F0 + 6 MHz | — | 40 | — | dB |
| | F>F0 – 6 MHz | — | 40 | — | dB |
| Image frequency | | — | 31 | — | dB |
| Adjacent channel to image frequency | F = F_image + 2 MHz | — | 37 | — | dB |
| | F = F_image – 2 MHz | — | 8 | — | dB |
| Out-of-band blocking performance | 30 MHz~2000 MHz | — | −16 | — | dBm |
| | 2003 MHz~2399 MHz | — | −20 | — | dBm |
| | 2484 MHz~2997 MHz | — | −16 | — | dBm |
| | 3000 MHz~12.75 GHz | — | −16 | — | dBm |
| Intermodulation | | — | −30 | — | dBm |

### Table 6-14. Receiver Characteristics - Bluetooth LE 125 Kbps

| Parameter | Description | Min | Typ | Max | Unit |
|---|---|---|---|---|---|
| Sensitivity @30.8% PER | | — | −104.5 | — | dBm |
| Maximum received signal @30.8% PER | | — | −8 | — | dBm |
| Co-channel C/I | F = F0 MHz | — | 6 | — | dB |
| Adjacent channel selectivity C/I | F = F0 + 1 MHz | — | 6 | — | dB |
| | F = F0 – 1 MHz | — | 5 | — | dB |
| | F = F0 + 2 MHz | — | 32 | — | dB |
| | F = F0 – 2 MHz | — | 39 | — | dB |
| | F = F0 + 3 MHz | — | 35 | — | dB |
| | F = F0 – 3 MHz | — | 45 | — | dB |
| | F>F0 + 3 MHz | — | 35 | — | dB |
| | F>F0 – 3 MHz | — | 48 | — | dB |
| Image frequency | | — | 35 | — | dB |
| Adjacent channel to image frequency | F = F_image + 1 MHz | — | 49 | — | dB |
| | F = F_image – 1 MHz | — | 32 | — | dB |

### Table 6-15. Receiver Characteristics - Bluetooth LE 500 Kbps

| Parameter | Description | Min | Typ | Max | Unit |
|---|---|---|---|---|---|
| Sensitivity @30.8% PER | | — | −101 | — | dBm |
| Maximum received signal @30.8% PER | | — | −8 | — | dBm |
| Co-channel C/I | F = F0 MHz | — | 4 | — | dB |
| Adjacent channel selectivity C/I | F = F0 + 1 MHz | — | 5 | — | dB |
| | F = F0 – 1 MHz | — | 5 | — | dB |
| | F = F0 + 2 MHz | — | 28 | — | dB |
| | F = F0 – 2 MHz | — | 36 | — | dB |
| | F = F0 + 3 MHz | — | 36 | — | dB |
| | F = F0 – 3 MHz | — | 38 | — | dB |
| | F>F0 + 3 MHz | — | 37 | — | dB |
| | F>F0 – 3 MHz | — | 41 | — | dB |
| Image frequency | | — | 37 | — | dB |
| Adjacent channel to image frequency | F = F_image + 1 MHz | — | 44 | — | dB |
| | F = F_image – 1 MHz | — | 28 | — | dB |
