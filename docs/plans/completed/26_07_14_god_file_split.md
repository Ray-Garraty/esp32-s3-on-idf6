---
type: Plan
title: God File Splitting — main.cpp (748 lines) + motor_task.cpp (695 lines)
description: Architectural refactoring to split the two largest source files into cohesive modules, reducing cognitive complexity and enabling targeted testing.
tags: [refactoring, soc, god-file, architecture, c++23]
timestamp: 2026-07-14
depends_on: docs/plans/pending/26_07_14_master_audit.md
---

# God File Splitting: main.cpp + motor_task.cpp

**Date:** 2026-07-14  
**Status:** Implemented  
**Risk:** High — architectural changes affecting task creation, linker symbols, and module boundaries.

## Motivation

The Master Audit (`docs/plans/pending/26_07_14_master_audit.md`) flagged both files as HIGH-severity SOC violations:

| File | Lines | Cognitive Complexity Hotspots |
|------|-------|------------------------------|
| `main/main.cpp` | 748 | `app_main` (225), `netTaskEntry` (241) |
| `components/infrastructure/src/motor_task.cpp` | 695 | `motorTaskEntry` (343), `run_rinse_sm` (146), `run_homing` (123), `run_cal_speed_seq_sm` (122), `execute_move_steps` (115) |

## Design Principles

1. **No behavior change** — each split is purely mechanical; zero functional delta.
2. **Each new file has one clear responsibility** (Single Responsibility Principle).
3. **Each new file is independently compilable** and testable where feasible.
4. **Splits respect existing dependency layers** — no circular dependencies introduced.
5. **Each split = one PR = one smoke test** — rollback-safe.

## Proposed Module Structure

### A. `main/` — Split into 4 files

| # | New File | Source Lines | Responsibility |
|---|----------|-------------|---------------|
| A1 | `main/main.cpp` (reduced) | ~430 | `app_main` orchestration: boot steps 1-9, post-boot setup, `sendResponse` lambda, main loop body |
| A2 | `main/gpio_config.cpp` + `.hpp` | ~50 | `configureGpioPins()` — centralized GPIO configuration |
| A3 | `main/log_capture.cpp` | ~60 | `logVprintf()` hook + `wsLogCallback()` |
| A4 | `main/net_owner.cpp` + `.hpp` | ~200 | `netTaskEntry`, `NetTaskParams`, `gHttpServerForWs`, `WsSendEntry`, `WsBroadcastEntry`, queue globals |

**What stays in reduced `main.cpp`:** `extractCmdId()`, `adcSampleRead()`, `logDramBeforeTask()`, `app_main()`, main loop body, broadcast handling, BLE/serial command drain, SmResult delivery.

**Note:** `extractCmdId`, `adcSampleRead`, `logDramBeforeTask` are small enough to stay inline (~40 lines total). No need to extract them unless desired.

### B. `components/infrastructure/src/motor/` — New subdirectory, 4 files

Move motor_task.cpp into a new `motor/` subdirectory under `infrastructure/src/`.

| # | New File | Source Lines | Responsibility |
|---|----------|-------------|---------------|
| B1 | `motor/task.cpp` | ~200 | `motorTaskEntry()` — thread entry, command dispatch switch, module globals (`gMotorCmdQueue`, `gSmResultQueue`, `gTmcUart`), `assert_rmt_preconditions()`, includes/constants |
| B2 | `motor/motion.cpp` | ~120 | `set_valve()`, `ml_min_to_hz()`, `move_to_endstop()`, `move_fill()`, `move_empty()`, `store_result()`, `execute_move_steps()` |
| B3 | `motor/sm_runners.cpp` | ~220 | `run_rinse_sm()`, `run_cal_dose_sm()`, `run_cal_speed_sm()`, `run_cal_speed_seq_sm()` + their `s_*` state machine instances (moved from anonymous namespace) |
| B4 | `motor/homing.cpp` | ~100 | `run_homing()` — full homing sequence with timeout, FfiGuard, limit switches |

**Namespace change (intentional):** Internal functions move from anonymous namespace (current) or `ecotiter::infrastructure` to the explicit sub-namespace `ecotiter::infrastructure::motor`. This provides better module isolation and follows the project pattern of `ecotiter::application::handlers::*`. No runtime effect since these are internal-only.

**Header changes:** The `motor_task.hpp` stays as the public API. Declarations of `motorTaskEntry`, `gMotorCmdQueue`, etc. remain there. Internal functions (`set_valve`, `ml_min_to_hz`, `move_to_endstop`, `move_fill`, `move_empty`, `store_result`, `execute_move_steps`, `run_rinse_sm`, `run_cal_dose_sm`, `run_cal_speed_sm`, `run_cal_speed_seq_sm`, `run_homing`) get declarations in a single internal header (see § Internal Header Design).

## CMakeLists Changes

### `main/CMakeLists.txt`

```cmake
idf_component_register(
    SRCS "main.cpp" "version.cpp" "gpio_config.cpp" "log_capture.cpp" "net_owner.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        diag domain infrastructure interface application
        nvs_flash freertos esp_system esp_driver_gpio esp_driver_rmt esp_timer
)
```

### `components/infrastructure/CMakeLists.txt`

Replace `"src/motor_task.cpp"` with:
```cmake
    "src/motor/task.cpp"
    "src/motor/motion.cpp"
    "src/motor/sm_runners.cpp"
    "src/motor/homing.cpp"
```

Add private include directory for internal motor header (not visible to other components):
```cmake
    INCLUDE_DIRS
        "include"
        "network/include"   # (preserve existing)
    PRIV_INCLUDE_DIRS
        "src/motor"         # NEW — for motor/internal.hpp (private to this component)
```

## Internal Header Design

A minimal `components/infrastructure/src/motor/internal.hpp` declaring all functions shared between the split motor files (co-located with sources, private to the component; accessible via `PRIV_INCLUDE_DIRS "src/motor"`):

```cpp
#pragma once

#include "infrastructure/motor_task.hpp"
#include "infrastructure/drivers/stepper.hpp"
#include "infrastructure/drivers/limitswitch.hpp"

namespace ecotiter::infrastructure::motor {

// ── motion.cpp ────────────────────────────────────────────
void set_valve(domain::ValvePosition pos);
float ml_min_to_hz(float speedMlMin);
domain::Result<void, domain::StepperError> move_to_endstop(
    drivers::StepperMotor& stepper, drivers::LimitSwitch& sw,
    std::atomic<bool>& stopFlag, bool invertDir = false);
domain::Result<void, domain::StepperError> move_fill(
    drivers::StepperMotor& stepper, drivers::LimitSwitch& fullSwitch);
domain::Result<void, domain::StepperError> move_empty(
    drivers::StepperMotor& stepper, drivers::LimitSwitch& emptySwitch);
void store_result(SmResult::Type type, int32_t stepsTaken = 0,
                  float measuredSpeed = 0.0f,
                  const float* results = nullptr, int resultCount = 0);
domain::Result<void, domain::StepperError> execute_move_steps(
    drivers::StepperMotor& stepper, int32_t steps);

// ── sm_runners.cpp ────────────────────────────────────────
domain::Result<void, domain::StepperError> run_rinse_sm(
    drivers::StepperMotor& stepper, drivers::LimitSwitch& fullSwitch,
    drivers::LimitSwitch& emptySwitch);
domain::Result<void, domain::StepperError> run_cal_dose_sm(
    drivers::StepperMotor& stepper, drivers::LimitSwitch& fullSwitch,
    drivers::LimitSwitch& emptySwitch);
domain::Result<void, domain::StepperError> run_cal_speed_sm(
    drivers::StepperMotor& stepper, drivers::LimitSwitch& fullSwitch,
    drivers::LimitSwitch& emptySwitch, drivers::TmcUart& tmc);
domain::Result<void, domain::StepperError> run_cal_speed_seq_sm(
    drivers::StepperMotor& stepper, drivers::LimitSwitch& fullSwitch,
    drivers::LimitSwitch& emptySwitch);

// ── homing.cpp ────────────────────────────────────────────
domain::Result<void, domain::StepperError> run_homing(
    drivers::StepperMotor& stepper, drivers::LimitSwitch& emptySwitch);

} // namespace ecotiter::infrastructure::motor
```

## Risks and Mitigations

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|------------|
| New internal header creates circular dependency | Low | High | Verify include graph; keep `internal.hpp` leaf-only (no reverse includes from other components) |
| Internal header accidentally exposed as public API | Medium | Medium | Use `PRIV_INCLUDE_DIRS` (not `INCLUDE_DIRS`) for `src/motor/` — verified against ESP-IDF v6 convention |
| State machine instance (s_rinseSm etc.) initialization order | Medium | Medium | Move instances to `sm_runners.cpp` as file-scope statics — same behavior as current anonymous namespace |
| `FfiGuard(30)` in homing references different boundary ID | Low | Low | Keep same boundary ID; document in internal header if renumbering |
| Build breaks from missing includes after split | Medium | Medium | Add all necessary includes to each new .cpp; run full build before smoke |
| Binary size increase from separate compilation units | Low | Low | LTO mitigates; no new code added |
| Task watchdog timeout during refactoring commits | Low | Medium | Each split is a single commit; full smoke test after each |

## Execution Plan (Atomic Steps with Smoke)

| Step | Description | Risk | Result |
|------|-------------|------|--------|
| 0 | Create `src/motor/internal.hpp` with declarations | None | ✅ Done |
| 1 | Extract `motor/motion.cpp` — move set_valve, ml_min_to_hz, move_to_endstop, move_fill, move_empty, store_result, execute_move_steps | Low | ✅ Done |
| 2 | Extract `motor/sm_runners.cpp` — move run_rinse_sm, run_cal_dose_sm, run_cal_speed_sm, run_cal_speed_seq_sm + state machine instances | Low | ✅ Done |
| 3 | Extract `motor/homing.cpp` — move run_homing | Low | ✅ Done |
| 4 | Shrink `motor/task.cpp` — migrate to `src/motor/task.cpp`, restructure namespace for type visibility | Low | ✅ Done (+namespace restructure needed) |
| 5 | Extract `main/gpio_config.cpp` — move configureGpioPins | Low | ✅ Done |
| 6 | Extract `main/log_capture.cpp` — move logVprintf, wsLogCallback | Low | ✅ Done |
| 7 | Extract `main/net_owner.cpp` — move netTaskEntry, NetTaskParams, WS queue globals, gHttpServerForWs | Medium | ✅ Done (+missing includes fixed) |
| 8 | Shrink `main/main.cpp` — verify app_main is the only substantive content left | Low | ✅ Done (507 lines) |

Each step: `scripts/idf.sh build` → `scripts/idf.sh test` → `scripts/idf.sh smoke` → commit or revert.
**Final verification:** `scripts/idf.sh build` (0 errors), `scripts/idf.sh test` (780/780), `scripts/idf.sh smoke` (BOOT OK). All gates green.

## Implementation Results

| File | Planned | Actual | Delta |
|------|---------|--------|-------|
| `main/main.cpp` (reduced) | ~430 | 507 | +77 (includes + headers) |
| `main/gpio_config.cpp` | ~50 | 51 | +1 |
| `main/log_capture.cpp` | ~60 | 64 | +4 |
| `main/net_owner.cpp` | ~200 | 119 | -81 (includes leaner than estimated) |
| `motor/task.cpp` | ~200 | 238 | +38 (using declarations, namespace restructure) |
| `motor/motion.cpp` | ~120 | 153 | +33 (includes, namespace boilerplate) |
| `motor/sm_runners.cpp` | ~220 | 253 | +33 (includes, namespace boilerplate) |
| `motor/homing.cpp` | ~100 | 128 | +28 (includes, namespace boilerplate) |

**Verification gates (all passed):**
- ✅ `scripts/idf.sh build` — 0 errors, 0 warnings
- ✅ `scripts/idf.sh test` — 780 assertions in 249 test cases, all passed
- ✅ `scripts/idf.sh smoke` — BOOT OK on real ESP32-S3 hardware

**Deviation from plan:**
1. **`motor/task.cpp` namespace restructuring** — Module globals (`gMotorCmdQueue`, `gSmResultQueue`, `gTmcUart`) remain in `ecotiter::infrastructure` (matching public header extern declarations). `motorTaskEntry` with `extern "C"` is inside `ecotiter::infrastructure::motor` with `using` declarations for `StepperMotor`, `LimitSwitch`, `Direction`, `BuretteState`, `ValvePosition`. This was necessary because the anonymous namespace `using` declarations in the original file were lost during the split.
2. **Internal header signatures** — Match actual original code signatures (e.g., `move_to_endstop` takes `Direction dir, uint32_t speedHz`, not `LimitSwitch& sw` as aspirational plan suggested).
3. **Include fixes** during build iteration: `motion.cpp` needed `<esp_task_wdt.h>` and `diag/rtc_watchdog.hpp`; `task.cpp` needed `diag/state_tracer.hpp`; `net_owner.cpp` needed `domain/types.hpp` and `using namespace ecotiter`.
4. **Old file** `src/motor_task.cpp` deleted after all extractions.

**Files created (8 .cpp, 3 .hpp, 1 internal header):**
- `components/infrastructure/src/motor/internal.hpp`
- `components/infrastructure/src/motor/task.cpp`
- `components/infrastructure/src/motor/motion.cpp`
- `components/infrastructure/src/motor/sm_runners.cpp`
- `components/infrastructure/src/motor/homing.cpp`
- `main/gpio_config.hpp` + `main/gpio_config.cpp`
- `main/log_capture.hpp` + `main/log_capture.cpp`
- `main/net_owner.hpp` + `main/net_owner.cpp`

**Files modified (2):**
- `main/CMakeLists.txt` — added new source files
- `components/infrastructure/CMakeLists.txt` — replaced `src/motor_task.cpp` with 4 motor files, added `PRIV_INCLUDE_DIRS "src/motor"`

**Files deleted (1):**
- `components/infrastructure/src/motor_task.cpp`

## Acceptance Criteria

- `scripts/idf.sh build` — 0 errors, 0 warnings
- `scripts/idf.sh test` — all 780+ assertions pass
- `scripts/idf.sh smoke` — BOOT OK on real ESP32-S3 hardware
- Each function lives in exactly one `.cpp` file matching its responsibility
- No new circular dependencies
- Public API unchanged: `motorTaskEntry`, `gMotorCmdQueue`, `gSmResultQueue` remain in `motor_task.hpp`
- Internal functions use `ecotiter::infrastructure::motor` namespace (intentional — no runtime effect)
- `components/infrastructure/CMakeLists.txt` uses `PRIV_INCLUDE_DIRS` (not `INCLUDE_DIRS`) for internal headers
