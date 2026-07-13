---
type: Hardware Reference
title: ESP32-S3 Memory Organization
description: Internal memory (ROM, SRAM, RTC memory, eFuse), external flash and RAM, cache architecture, and memory specifications for the ESP32-S3.
tags: [esp32-s3, hardware, memory, sram, flash, psram, cache]
timestamp: 2026-07-13
source: esp32-s3_datasheet_en.md (v2.2), lines 2544–2699 and 4005–4055
---

# ESP32-S3 Memory Organization

Extracted from ESP32-S3 Series Datasheet v2.2, Sections 4.1.2 and 5.7.

## Address Mapping Structure

The address mapping structure of ESP32-S3 is organized as follows:

| Address Range | Size | Bus | Description |
|---|---|---|---|
| 0x0000_0000 – 0x3BFF_FFFF | ~960 MB | Data bus | Reserved |
| 0x3C00_0000 – 0x3DFF_FFFF | 32 MB | Data bus | External memory (via Cache MMU) |
| 0x3E00_0000 – 0x3FC8_7FFF | ~76.5 MB | Data bus | Reserved |
| 0x3FC8_8000 – 0x3FCF_FFFF | 480 KB | Data bus | Internal memory (SRAM) |
| 0x3FD0_0000 – 0x3FEF_FFFF | 2 MB | Data bus | Reserved |
| 0x3FF0_0000 – 0x3FF1_FFFF | 128 KB | Data bus | Internal memory |
| 0x3FF2_0000 – 0x3FFF_FFFF | ~896 KB | Data bus | Peripherals |
| 0x4000_0000 – 0x4005_FFFF | 384 KB | Instruction bus | Internal memory (ROM) |
| 0x4006_0000 – 0x4036_FFFF | ~3 MB | Instruction bus | Reserved |
| 0x4037_0000 – 0x403D_FFFF | 448 KB | Instruction bus | Internal memory (SRAM) |
| 0x403E_0000 – 0x41FF_FFFF | ~31.75 MB | Instruction bus | Reserved |
| 0x4200_0000 – 0x43FF_FFFF | 32 MB | Instruction bus | External memory (via Cache MMU) |
| 0x4400_0000 – 0x4FFF_FFFF | ~191 MB | Instruction bus | Reserved |
| 0x5000_0000 – 0x5000_1FFF | 8 KB | Data/Instruction bus | Internal memory (RTC Fast Memory, accessible by ULP co-processor) |
| 0x5000_2000 – 0x5FFF_FFFF | ~256 MB | Data/Instruction bus | Reserved |
| 0x6000_0000 – 0x600D_0FFF | ~836 KB | Data/Instruction bus | Peripherals (RTC Peripherals, Other Peripherals) |
| 0x600D_1000 – 0x600F_DFFF | ~180 KB | Data/Instruction bus | Not available for use |
| 0x600F_E000 – 0x600F_FFFF | 8 KB | Data/Instruction bus | Internal memory (RTC Slow Memory) |
| 0x6010_0000 – 0xFFFF_FFFF | ~2.6 GB | Data/Instruction bus | Reserved |

- RTC Fast Memory (8 KB) and RTC Slow Memory (8 KB) are accessible by the ULP co-processor.

## 4.1.2.1 Internal Memory

The internal memory of ESP32-S3 refers to the memory integrated on the chip die or in the chip package, including ROM, SRAM, eFuse, and flash.

- **384 KB ROM**: for booting and core functions
- **512 KB on-chip SRAM**: for data and instructions, running at a configurable frequency of up to 240 MHz
- **RTC FAST memory**: 8 KB SRAM that supports read/write/instruction fetch by the main CPU (LX7 dual-core processor). It can retain data in Deep-sleep mode
- **RTC SLOW Memory**: 8 KB SRAM that supports read/write/instruction fetch by the main CPU (LX7 dual-core processor) or coprocessors. It can retain data in Deep-sleep mode
- **4096-bit eFuse memory**: 1792 bits are available for users, such as encryption key and device ID
- **In-package flash and PSRAM**: see flash and PSRAM size in Chapter 1 ESP32-S3 Series Comparison; for specifications, refer to Section 5.7 Memory Specifications

For details, see ESP32-S3 Technical Reference Manual > Chapter System and Memory.

## 4.1.2.2 External Flash and RAM

ESP32-S3 supports SPI, Dual SPI, Quad SPI, Octal SPI, QPI, and OPI interfaces that allow connection to multiple external flash and RAM.

The external flash and RAM can be mapped into the CPU instruction memory space and read-only data memory space. The external RAM can also be mapped into the CPU data memory space. ESP32-S3 supports up to 1 GB of external flash and RAM, and hardware encryption/decryption based on XTS-AES to protect users' programs and data in flash and external RAM.

Through high-speed caches, ESP32-S3 can support at a time up to:
- External flash or RAM mapped into 32 MB instruction space as individual blocks of 64 KB
- External RAM mapped into 32 MB data space as individual blocks of 64 KB. 8-bit, 16-bit, 32-bit, and 128-bit reads and writes are supported. External flash can also be mapped into 32 MB data space as individual blocks of 64 KB, but only supporting 8-bit, 16-bit, 32-bit and 128-bit reads.

Note: After ESP32-S3 is initialized, firmware can customize the mapping of external RAM or flash into the CPU address space.

For details, see ESP32-S3 Technical Reference Manual > Chapter System and Memory.

## 4.1.2.3 Cache

ESP32-S3 has an instruction cache and a data cache shared by the two CPU cores. Each cache can be partitioned into multiple banks.

- Instruction cache: 16 KB (one bank) or 32 KB (two banks)
- Data cache: 32 KB (one bank) or 64 KB (two banks)
- Instruction cache: four-way or eight-way set associative
- Data cache: four-way set associative
- Block size of 16 bytes or 32 bytes for both instruction cache and data cache
- Pre-load function
- Lock function
- Critical word first and early restart

For details, see ESP32-S3 Technical Reference Manual > Chapter System and Memory.

## 4.1.2.4 eFuse Controller

ESP32-S3 contains a 4-Kbit eFuse to store parameters, which are burned and read by an eFuse controller.

- 4 Kbits in total, with 1792 bits reserved for users, e.g., encryption key and device ID
- One-time programmable storage
- Configurable write protection

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
