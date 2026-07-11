---
type: Testing Guide
title: Testing Strategy
description: Three-tier testing strategy for ecotiter C++23 firmware — host unit tests, on-device integration, and HIL
tags: [testing, catch2, pytest, hil]
timestamp: 2026-07-07
---

# Testing Strategy (3-Tier)

## Tier 1: Host Unit Tests (Catch2 v3)

**Purpose:** Verify pure logic (domain layer) at speed. No hardware required.

**Framework:** Catch2 v3 single-header (`tests/catch2/catch.hpp`)

**Scope:**
- `domain::Types` -- Steps arithmetic, conversions, comparison
- `domain::Burette` -- State machine transitions, parameter validation
- `domain::Calibration` -- Steps/ml conversion, ADC regression math
- `application::CommandDispatch` -- Command parsing, routing
- `infrastructure::Network::Dns` -- DNS packet encoding/decoding

**Structure:**
```
tests/
+-- CMakeLists.txt                          # Host CMake project (x86_64)
+-- catch2/catch.hpp                        # Catch2 v3 single-header
+-- src/
    +-- test_main.cpp                       # CATCH_CONFIG_MAIN
    +-- test_types.cpp                      # Steps, Hz, Ml tests
    +-- test_burette.cpp                    # State machine tests
    +-- test_calibration.cpp                # Math tests
```

**Run:**
```bash
cd build-tests && cmake ../tests && cmake --build . && ctest --output-on-failure
```

## Tier 2: On-Device Integration Tests

**Purpose:** Verify hardware integration on real ESP32-S3.

**Scope:**
- RMT stepper pulse generation (scope measurement)
- GPIO endstop interrupt handling
- ADC oneshot read + calibration
- OneWire DS18B20 bitbang protocol
- NVS read/write cycle
- WiFi AP + STA connectivity
- BLE GATT service + notify

**Method:** Custom test firmware with `TEST` Kconfig option, serial commands to trigger each test case.

## Tier 3: Pytest HIL (Hardware-in-the-Loop)

**Purpose:** End-to-end system verification.

**Scope:**
- BLE NUS command/response loop
- REST API endpoint coverage
- WebSocket real-time streaming
- Captive portal redirect flow
- System recovery after fault injection

**Tools:**
- `pytest-embedded` for test orchestration
- `python3 scripts/serial_monitor.py` for log capture
- BLE client (`bleak`) for BLE tests
- HTTP client (`requests`) for API tests

## Test Quality Gates

- All three tiers pass before release
- Tier 1 runs on every commit (CI)
- Tier 2 runs on every hardware change
- Tier 3 runs before tagged releases
