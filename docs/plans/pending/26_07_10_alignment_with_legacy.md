---
type: Plan
title: Alignment with Legacy Arduino Firmware
description: Audit findings and restoration plan for business logic lost during Arduino → Rust → ESP-IDF migration
tags: [migration, business-logic, calibration, state-machines, api]
timestamp: 2026-07-10
status: in_progress
---

# Alignment with Legacy Arduino Firmware

## Summary

Two migrations (Arduino → Rust → ESP-IDF) caused significant business logic
loss. This document catalogues every discrepancy between the legacy Arduino
firmware (`/home/vlabe/Downloads/legacy/arduino`) and the current ESP-IDF
project, ranked by impact, and prescribes the restoration work.

**Impact after Phases 0–2a:** Calibration defaults match Arduino (Phase 1).
Dose planning with multi-cycle auto-fill works (Phase 2). Fill/empty/dose
send real motor commands. Direction naming aligned with Arduino (`LIQ_IN`/`LIQ_OUT`,
`FULL`/`EMPTY`). Remaining: rinse/cal state machines (Phase 3), Z-factor + OLS
(Phase 4), ADC calibration (Phase 5).

---

## Audit: Business Logic Lost During Migration

### 1. Calibration Defaults — CRITICAL

<!-- grep: calibration-defaults -->

The ESP-IDF defaults do not match the Arduino calibrated values. Using these
will cause the device to dispense wildly incorrect volumes out of the box.

| Parameter | Arduino | ESP-IDF (Phase 1) | Error |
|-----------|---------|-------------------|-------|
| `stepsPerMl` | 7730.0 | ✅ `7730.0` | Fixed |
| `nominalVolumeMl` | 8.14 ml | ✅ `8.14` | Fixed |
| `speedCoeff` (ml/min)/Hz | 0.03052 | ✅ `0.03052` | Fixed |
| `minFreq` Hz | 30 | ✅ `30` (persisted) | Fixed |
| `maxFreq` Hz | 3000 | ✅ `3000` (persisted) | Fixed |

**Status:** Phase 1 complete. All defaults restored, NVS persistence wired. `broadcast.cpp` speed conversion uses `kDefaultSpeedCoeff`. Dispatch uses real `calibrationRead`/`calibrationWrite` callbacks.

### 2. Dose Planner Algorithm — CRITICAL

<!-- grep: dose-planner -->

The Arduino dose planner splits large volumes into auto-fill cycles. The
ESP-IDF has no equivalent logic.

```
Arduino:
  total_cycles = ceil(vol / nominal_vol)       // if vol > nominal_vol + 0.001
  remaining_vol = fmod(vol, nominal_vol)        // raised to nominal_vol if < 0.01
  first_cycle_vol = single_cycle ? vol : nominal_vol
  state: DOSE_FILL_FIRST if current_vol < first_cycle_vol
         DOSE_DIRECT       otherwise

ESP-IDF:
  doseVolume handler → makeAckThenResponse() → NO motor command sent
```

**Source:** Arduino `src/burette_planner.cpp` (validation + cycle logic)

### 3. State Machines — Not Ported

<!-- grep: state-machines-missing -->

| State Machine | States | Arduino Status | ESP-IDF Status |
|---------------|--------|----------------|----------------|
| **Rinse** | PRE_FILL → EMPTYING → FILLING → DONE | Full | Not implemented |
| **Calibration Dose** | CAL_IDLE → FILLING → EMPTYING → DONE | Full | Not implemented |
| **Calibration Speed Single** | CAL_IDLE → FILLING → EMPTYING → done | Full | Not implemented |
| **Calibration Speed Seq** | 3-point sequential with settling | Full | Not implemented |
| **Auto-Dose** | FILLING → DOSING (multi-cycle) | Full | Not implemented |
| **BLE Zombie** | 2 levels | Full | 3 levels (improved) |
| **Transport** | USB/BLE selection | Full | Partial |

### 4. ISO 8655 Gravimetric Correction — Not Ported

<!-- grep: z-factor-table -->

```cpp
// Arduino: Full 31 × 6 bilinear interpolation table
float get_z_factor(float temperature, float pressure);

// Temperature: 15.0 °C → 30.0 °C, step 0.5 °C
// Pressure:    80.0, 85.3, 90.7, 96.0, 101.3, 106.7 kPa

// Gravimetric formula:
float calculate_new_steps_per_ml(float current_s_p_ml,
                                 float target_vol_ml,
                                 float actual_vol_ml) {
    return current_s_p_ml * target_vol_ml / actual_vol_ml;
}

// Actual volume from mass:
actual_volume = mass_g * z_factor
```

**Status:** Struct definitions exist in `calibration.hpp`; Z-table data and
interpolation function are absent.

### 5. OLS Speed Regression — Not Ported

<!-- grep: ols-regression -->

```cpp
// Arduino: OLS with intercept
k = (Σ(f·v) - Σ(f)·Σ(v)/n) / (Σ(f²) - (Σ(f))²/n)

SS_res = Σ(v_i - k·f_i)²
SS_tot = Σ(v_i - mean_v)²
R² = 1 - SS_res / SS_tot
```

**Status:** `cal.calcSpeed` handler returns hardcoded response. No OLS math.

### 6. ADC Calibration — Partial

<!-- grep: adc-calibration -->

| Arduino Command | ESP-IDF Status |
|-----------------|----------------|
| `adc.cal.get` | ✅ Returns defaults |
| `adc.cal.measure` | ❌ **Not in CommandType** |
| `adc.cal.compute` | ❌ **Not in CommandType** |
| `adc.cal.save` | ❌ Handler exists, NVS write is stub |
| `adc.cal.reset` | ❌ **Not in CommandType** |

The Arduino ADC calibration collects 5 reference points with stabilisation
(32 samples, ±5 mV tolerance, max 10 attempts), then computes OLS. The
ESP-IDF has only `adc.cal.get` and `adc.cal.save` (both stubs).

### 7. TMC2209 UART — Not Connected

<!-- grep: tmc-uart -->

| Register | Arduino | ESP-IDF |
|----------|---------|---------|
| IHOLD/IRUN (RMS current) | 800 mA via TMCStepper | Not configured |
| TOFF | 4 | Not configured |
| TBL | 1 | Not configured |
| Microstep resolution | 16 (2^4) | Not configured |
| StallGuard threshold | Read/Write via UART | Not connected |
| CoolStep SEMIN/SEMAX | 5/2 | Not configured |
| Driver status (OTPW, OT, S2GA, etc.) | Polled and logged | Not read |

**GPIOs 16/17 (PDN_UART) are not defined in ESP-IDF config.hpp.**

### 8. API Commands — Handler Stubs

<!-- grep: handler-stubs -->

Commands that parse but do not execute the intended operation:

| Command | Status |
|---------|--------|
| `fill` | ✅ Wired — sends MoveSteps CW to motor queue (Phase 2) |
| `empty` | ✅ Wired — sends MoveSteps CCW to motor queue (Phase 2) |
| `doseVolume` | ✅ Wired — plans multi-cycle dose, sends MoveSteps (Phase 2) |
| `rinse` | 🟡 Basic fill command sent (Phase 2); full SM deferred to Phase 3 |
| `cal.run` | ❌ Returns `makeAckThenResponse()` — no SM started |
| `cal.save` | ❌ NVS write is stub |
| `cal.reset` | ✅ Wired — resets NVS to defaults (Phase 1) |
| `setVolume` | ❌ Returns ack only |
| `configMove` | ❌ Returns ack with speed/accel values |
| `configHome` | ❌ Returns ack with homeSpeed value |
| `configSensor` | ❌ Returns ack with sensorValue |

### 9. HTTP API — Route Changes

<!-- grep: http-routes -->

| Arduino | ESP-IDF | Impact |
|---------|---------|--------|
| `POST /api/valve/set` | `POST /api/valve` | Client break |
| `GET /api/valve/state` | `GET /api/valve` | Client break |
| `GET /api/events` (SSE) | `GET /ws/stream` (WebSocket) | Protocol change |
| `GET /api/nvs/status` | Not present | Monitorability loss |
| mDNS `ecotiter.local` | Not configured | Discovery loss |
| AP password `12345678` | Open (no password) | Security regression |

### 10. Volume Tracking — Not Ported

<!-- grep: volume-tracking -->

The Arduino tracks burette volume after every operation:

| Operation | Volume After |
|-----------|-------------|
| Fill (normal) | `nominal_vol` |
| Empty (normal) | 0 |
| DoseVolume (normal) | `nominal_vol - dispensed` |
| Stop during fill | `volume_at_start + steps_taken / stepsPerMl` |
| Stop during empty/dose | `volume_at_start - steps_taken / stepsPerMl` |
| Boot homing at FULL | `nominal_vol` |

ESP-IDF has `VolumeTracker` struct with `onFillComplete()`, `onEmptyComplete()`,
`onDoseComplete()`, `onStopDuringFill()`, `onStopDuringEmpty()`, `onHomingComplete()`
(Phase 2). It is used by `handleFill`/`handleEmpty`/`handleDoseVolume` but not yet
wired into the motor task for incremental updates during movement.

### 11. Diagnostic Gap: Broadcast Interval

<!-- grep: broadcast-interval -->

Arduino: 300 ms  →  ESP-IDF: ~2000 ms

The 2-second latency will cause BLE/HTTP clients to see stale state,
especially during fast dosing operations.

---

## Verification

### Automated Acceptance Criteria

After each phase, the following must pass:

```bash
scripts/build.sh build      # Zero errors
scripts/build.sh tidy       # Zero clang-tidy warnings
scripts/build.sh test       # All Catch2 tests pass
scripts/smoke_test.py       # Build + flash + 30 s monitor — no panics
```

### Manual Acceptance Criteria

Validation from physical device:

| # | Test | Method |
|---|------|--------|
| 1 | Fill 5 ml at 5 ml/min | Observe motor movement, verify volume in broadcast |
| 2 | Empty | Observe reverse movement, verify volume reaches 0 |
| 3 | Rinse 3 cycles | Observe 3× fill/empty cycles |
| 4 | Cal Dose run | Verify steps recorded, result matches expected |
| 5 | Cal Speed run | Verify speed measurement |
| 6 | `cal.calcVolume` with mass+temp+pressure | Verify ISO 8655 correction applied |
| 7 | `adc.cal.measure` 5 points → compute → save | Verify coefficients saved to NVS |
| 8 | Reboot, verify calibration persists | Verify NVS readback matches |
| 9 | HTTP `GET /api/status` | Verify JSON matches legacy format |
| 10 | BLE connect + read/write | Verify NUS characteristic works |
| 11 | Set AP password, connect | Verify captive portal auth |

---

## Steps / Execution log

### Phase 0: Infrastructure — ✅ COMPLETED (2026-07-10)

<!-- grep: phase-0 -->

**Serial monitor fix:** `scripts/monitor.py` — `reset_input_buffer()` after DTR
reset to discard ROM bootloader binary garbage from log files.

- Before: log files contained `x�x�...` binary characters → unopenable in VS Code
- After: `time.sleep(0.3)` + `ser.reset_input_buffer()` before read loop
- Removed outdated comment: "no reset_input_buffer — we want to capture BOOT_OK_MARKER"
  (BOOT_OK_MARKER arrives from `app_main()` >500ms after reset, flush is safe)

**Smoke test:** ✅ BOOT OK — build, flash, 30s monitor, no panics, log is clean.

### Phase 1 — Calibration Constants — ✅ COMPLETED (2026-07-10)

<!-- grep: phase-1 -->

**1a — Named constants:** Added `CalibrationData::kDefaultStepsPerMl` (7730.0f) and
`CalibrationData::kDefaultNominalVolumeMl` (8.14f). Removed all inline magic numbers
(`1000.0`, `50.0`, `3000.0`). Tests corrected to match new defaults.

**1b — Full CalibrationData + NVS:** Added `speedCoeff` (0.03052), `minFreqHz` (30),
`maxFreqHz` (3000) fields and defaults to `CalibrationData`. Implemented
`calibrationRead()`/`calibrationWrite()` in `nvs.cpp` with fallback to defaults.
Wired real NVS callbacks in `dispatch.cpp`. Fixed broadcast speed conversion:
`speedHz * speedCoeff` instead of `speedHz * 60 / stepsPerMl` — `"spd":30.5` now
matches Arduino (was 7.8).

**Diff:** +93/-28 source lines (7 files), +106/-44 including tests/docs.
**Smoke test:** ✅ BOOT OK — build, flash, 30s monitor, no panics.

### Phase 2: Dose Planner — ✅ COMPLETED (2026-07-10)

<!-- grep: phase-2 -->

- Added `speedMlMin` field to `Command` struct + parsing from JSON
- Added `speedMlMinToHz()` conversion in `calibration.hpp` (Arduino-compatible)
- Implemented `DosePlan` struct + `planDose()` — multi-cycle dose planning:
  `totalCycles`, `firstCycleVolMl`, `remainingVolMl`, `needsFillFirst`
- Implemented `VolumeTracker` struct — tracks volume after fill/empty/dose/stop/homing
- Wired `handleFill`: reads calibration → calculates steps from `nominalVol * stepsPerMl` → sends `MoveSteps` CW
- Wired `handleEmpty`: reads current volume → calculates steps → sends `MoveSteps` CCW
- Wired `handleDoseVolume`: validates via `planDose()` → converts `speedMlMin` to Hz → sends `MoveSteps`
- Wired `handleRinse`: basic fill-to-nominal motor command (full rinse SM deferred to Phase 3)

**Diff:** +165/-5 source lines (6 files).
**Smoke test:** ✅ BOOT OK — build, flash, 30s monitor, no panics.

<!-- grep: phase-2 -->

- Added `speedMlMin` field to `Command` struct + parsing from JSON
- Added `speedMlMinToHz()` conversion in `calibration.hpp` (Arduino-compatible)
- Implemented `DosePlan` struct + `planDose()` — multi-cycle dose planning:
  `totalCycles`, `firstCycleVolMl`, `remainingVolMl`, `needsFillFirst`
- Implemented `VolumeTracker` struct — tracks volume after fill/empty/dose/stop/homing
- Wired `handleFill`: reads calibration → calculates steps from `nominalVol * stepsPerMl` → sends `MoveSteps` CW
- Wired `handleEmpty`: reads current volume → calculates steps → sends `MoveSteps` CCW
- Wired `handleDoseVolume`: validates via `planDose()` → converts `speedMlMin` to Hz → sends `MoveSteps`
- Wired `handleRinse`: basic fill-to-nominal motor command (full rinse SM deferred to Phase 3)

**Diff:** +165/-5 source lines (6 files).
**Smoke test:** ✅ BOOT OK — build, flash, 30s monitor, no panics.

### Phase 3: State Machines — ✅ IMPLEMENTED (2026-07-10)

<!-- grep: phase-3 -->

All 4 state machines implemented and wired:

1. **RinseSm** (`domain/include/domain/rinse_sm.hpp`) — header-only
   - States: `PreFill → Emptying → Filling → Done`
   - Cycles configurable, tracks `currentCycle` vs `totalCycles`
   - Entry: `start(cycles, currentVolumeMl, nominalVolumeMl)` — skips PreFill if already near full

2. **CalDoseSm** (`domain/include/domain/cal_dose_sm.hpp`) — header-only
   - States: `Idle → Filling → Emptying → Done`
   - Records `stepsBefore` on fill complete, calculates `stepsTaken = abs(posAfter - posBefore)`

3. **CalSpeedSingleSm** (`domain/include/domain/cal_speed_sm.hpp`) — header-only
   - States: `Idle → Filling → Emptying → Done`
   - Records `elapsedMs` between fill/empty, calculates `speed = nominalVol / (elapsedMs / 60000.0)`

4. **CalSpeedSeqSm** (`domain/include/domain/cal_speed_sm.hpp`) — header-only
   - 3-point sequential with valve settle timing (VALVE_SWITCH_MS = 1000 ms)
   - Per-point: fill, 1s settle, empty at test freq, measure time → speed

5. **Motor task integration** (`motor_task.hpp`, `motor_task.cpp`)
   - New `MotorCommandType`: `StartRinse`, `StartCalDose`, `StartCalSpeed`, `StartCalSpeedSeq`
   - `SmResult` struct with result queue for all SM types
   - Helper functions: `move_fill()`, `move_empty()`, `set_valve()`, `store_result()`
   - SM drivers: `run_rinse_sm()`, `run_cal_dose_sm()`, `run_cal_speed_sm()`

6. **Handlers wired**:
   - `handleRinse` → sends `StartRinse` to motor queue
   - `handleCalRun` → validates via `mode` ("dose"/"speed"), sends `StartCalDose`/`StartCalSpeed`
   - `handleCalGetResult` → stub (not yet reading from `SmResult`)

**Tests:** SM tests in `test_burette.cpp` (includes all 3 SM headers, 109 lines of tests).
**Motor task stub:** `stub_motor_task.cpp` has `gSmResult` for test linking.

**Smoke test:** ✅ BOOT OK — build, flash, 30s monitor, no panics.

### Phase 4: ISO 8655 Z-Factor + OLS (MEDIUM priority)

<!-- grep: phase-4 -->

1. Embed Z-factor table (31×6) as `constexpr float Z_TABLE[31][6]`
2. Implement bilinear interpolation: `float getZFactor(float temp, float pressure)`
3. Implement gravimetric formula in `handleCalCalcVolume`
4. Implement OLS regression with intercept in `handleCalCalcSpeed`
5. Return R² in response

**Tests:** Z-table: verify interpolation at table vertices and midpoints.
OLS: verify against known dataset, check R² = 1 for perfect fit.

### Phase 5: ADC Calibration (MEDIUM priority)

<!-- grep: phase-5 -->

1. Add `adc.cal.measure` to `CommandType` + parser + handler
   - Stabilise: 32 samples, max 10 attempts, ±5 mV tolerance
   - Record median of last 32 samples
2. Add `adc.cal.compute` to `CommandType` + OLS from 5 points
3. Add `adc.cal.reset` to `CommandType` + clear points + reset defaults
4. Wire `adc.cal.save` to NVS write
5. Wire ADC read task to apply `a_x1000`/`b` correction

**Tests:** OLS from 5 known points, verify coefficients. NVS roundtrip.

### Phase 6: TMC2209 UART (LOW priority)

<!-- grep: phase-6 -->

1. Add GPIO16/17 to `config.hpp` as `TMC_UART_RX_GPIO` / `TMC_UART_TX_GPIO`
2. Init `uart_config_t` (115200 8N1) in motor task init
3. Implement TMC2209 register read/write via UART singleton
4. Configure IHOLD=800 mA, IRUN=800 mA, TOFF=4, TBL=1, microsteps=16
5. Wire StallGuard threshold read/write from NVS
6. Poll driver status (OTPW, OT, S2GA, S2GB, OLA, OLB) in motor task

**Tests:** Verify register writes with mock UART. NVS roundtrip for SG threshold.

### Phase 7: HTTP API Alignment (LOW priority)

<!-- grep: phase-7 -->

1. Add `GET /api/nvs/status` endpoint
2. Add mDNS: `mdns_init()`, `mdns_hostname_set("ecotiter")`, `mdns_service_add("http")`
3. Restore AP password: add `kApPassword = "12345678"` to config, set in WiFi init
4. Document WebSocket vs SSE protocol change in `docs/API/SERIAL_API.md`

**Tests:** Host-based HTTP request/response tests. Manual mDNS resolution test.

### Phase 8: Diagnostics (LOW priority)

<!-- grep: phase-8 -->

1. Add pending watchdog (60 s timeout) in `ApplicationStateMachine::tick()`
2. Add USB heartbeat timeout (10000 ms) for transport SM decision
3. Wire `StateTracer::logBuretteTransition` for every SM transition
4. Wire `BuretteState::Error` → `CrashHandler` / BlackBox event

**Tests:** Simulate timeout → verify transition to Idle/Error.

---

## Files affected

### Phase 1 — Calibration Constants

| File | Change |
|------|--------|
| `components/domain/include/domain/calibration.hpp` | `speedCoeff`, `minFreqHz`, `maxFreqHz` fields + defaults |
| `components/infrastructure/include/infrastructure/config.hpp` | NVS key constants for burette cal |
| `components/infrastructure/include/infrastructure/storage/nvs.hpp` | `calibrationRead()`, `calibrationWrite()` declarations |
| `components/infrastructure/src/storage/nvs.cpp` | `calibrationRead()`, `calibrationWrite()` implementations |
| `components/application/src/dispatch.cpp` | Wire real NVS callbacks |
| `components/application/src/handlers/burette_cal.cpp` | Init all `CalibrationData` fields |
| `components/interface/src/broadcast.cpp` | Speed conversion: `kDefaultSpeedCoeff` |
| `tests/src/test_broadcast.cpp` | Expected `spd` 7.8 → 30.52 |

### Phase 2 — Dose Planner

| File | Change |
|------|--------|
| `components/domain/include/domain/calibration.hpp` | `DosePlan`, `planDose()`, `VolumeTracker`, `speedMlMinToHz()` |
| `components/application/include/application/command.hpp` | `speedMlMin` field in `Command` |
| `components/application/include/application/handlers/burette_ops.hpp` | Updated `handleDoseVolume`/`handleRinse` signatures |
| `components/application/src/command.cpp` | Parse `speed_ml_min` from JSON |
| `components/application/src/dispatch.cpp` | Pass `speedMlMin` to dose handler |
| `components/application/src/handlers/burette_ops.cpp` | Wired fill/empty/dose/rinse to motor queue |

### Phase 3 — State Machines

| File | Change |
|------|--------|
| `components/domain/include/domain/rinse.hpp` | New file |
| `components/domain/include/domain/cal_dose.hpp` | New file |
| `components/domain/include/domain/cal_speed.hpp` | New file |
| `components/domain/src/rinse.cpp` | New file |
| `components/domain/src/cal_dose.cpp` | New file |
| `components/domain/src/cal_speed.cpp` | New file |
| `components/CMakeLists.txt` | Add new source files |

### Phase 4 — Z-Factor + OLS

| File | Change |
|------|--------|
| `components/domain/include/domain/z_factor.hpp` | New file: table + interpolation |
| `components/domain/src/z_factor.cpp` | New file |
| `components/domain/include/domain/ols.hpp` | New file |
| `components/domain/src/ols.cpp` | New file |
| `components/application/src/handlers/burette_cal.cpp` | Wire calc handlers |

### Phase 5 — ADC Calibration

| File | Change |
|------|--------|
| `components/application/include/application/command.hpp` | Add `AdcCalMeasure`, `AdcCalCompute`, `AdcCalReset` |
| `components/application/src/command.cpp` | Parse new cmds |
| `components/application/src/dispatch.cpp` | Route new cmds |
| `components/application/src/handlers/sensors.cpp` | Stub → real impl |
| `components/infrastructure/src/drivers/adc.cpp` | Calibration apply |

### Phase 6 — TMC2209 UART

| File | Change |
|------|--------|
| `components/infrastructure/include/infrastructure/config.hpp` | UART pins |
| `components/infrastructure/include/infrastructure/drivers/stepper.hpp` | UART methods |
| `components/infrastructure/src/drivers/stepper.cpp` | UART init + register ops |
| `components/infrastructure/src/motor_task.cpp` | SG polling |

### Phase 7 — HTTP API

| File | Change |
|------|--------|
| `components/infrastructure/src/network/http_server.cpp` | Routes |
| `components/infrastructure/src/network/wifi.cpp` | AP password |
| `components/infrastructure/src/network/main.cpp` | mDNS init |

### Phase 8 — Diagnostics

| File | Change |
|------|--------|
| `components/application/src/state_machine.cpp` | Pending watchdog |
| `components/application/include/application/state_machine.hpp` | Timeout config |
| `components/interface/src/serial.cpp` | USB heartbeat |
| `main/main.cpp` | StateTracer wiring |

---

## Pre-Flight Checklist (GR-11 Compliance)

Before Phase 2-4 codegen, the following headers in
`/home/vlabe/Downloads/esp-idf-master` MUST be studied:

| Phase | API | Header |
|-------|-----|--------|
| 4 | Math | `esp_rom_uart.h`, `rom/ets_sys.h` |
| 5 | ADC | `esp_adc/adc_oneshot.h`, `esp_adc/adc_cali.h` |
| 6 | UART | `driver/uart.h`, `driver/uart_vfs.h` |
| 7 | mDNS | `mdns.h` |
| 7 | HTTP | `esp_http_server.h` |
| 7 | WiFi | `esp_wifi.h`, `esp_netif.h` |

---

## Rework Budget

| Phase | Estimated additions | Actual | Risk |
|-------|-------------------|--------|------|
| 1 | 60 lines | **93** (source) / **106** (total) | ✅ Done |
| 2 | 200 lines | **165** | ✅ Done |
| 3 | 400 lines | — | Medium — SM correctness |
| 4 | 300 lines | — | High — math correctness |
| 5 | 150 lines | — | Medium — ADC timing |
| 6 | 200 lines | — | Medium — UART on PSRAM |
| 7 | 50 lines | — | Low — routes + mDNS |
| 8 | 80 lines | — | Low — timeouts |

**Total:** ~1430 lines added, ~540 lines changed.
