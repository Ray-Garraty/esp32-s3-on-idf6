---
type: Known Issue
title: Widespread Single Responsibility Principle violations across firmware layers
description: Seven high-severity SRP violations identified by systematic code audit, including valve controlled by motor task, stale gBuretteState on queue-full, low-level drivers writing domain globals, REST API bypassing application dispatch, and dead Valve driver class
tags: [srp, architecture, refactoring, audit]
timestamp: 2026-07-14
status: active
---

# Widespread Single Responsibility Principle violations

## Problem

A systematic audit of 57 source files in `components/` and `main/` found **7 high-severity** and **5 medium-severity** violations of the Single Responsibility Principle. Components frequently handle responsibilities that belong elsewhere — the motor task controls the valve GPIO, the OneWire driver writes domain-level temperature state, the REST API bypasses the application dispatch layer, and a fully functional `Valve` driver class exists but is never instantiated (dead code).

Concrete failures caused by these violations:
- `valve.setPosition` returned `{"status":"ok"}` for 2+ years but never toggled GPIO14 because the handler only formatted JSON — **discovered July 14, 2026 by integration test**
- `handleValvePostCore` (REST API) writes `domain::gValvePosition.store()` **without** calling `gpio_set_level()`, creating a discrepancy between shared state and physical hardware
- Burette operation handlers call `gBuretteState.store()` **before** the motor command is enqueued; if the queue is full, the handler returns an error but `gBuretteState` is already permanently changed
- `onewire.cpp` (a low-level bitbang protocol driver) writes `domain::gTempCX100` directly, while `temp_thread.cpp` is the designated owner of temperature state — concurrent writers with no synchronisation

## Root cause

The firmware evolved through three rewrites (Arduino → Rust → C++) under time pressure. Architectural boundaries eroded:

1. **No task ownership contract.** Which task owns which GPIO, which atomic, and which hardware peripheral is not documented or enforced. Any component can write any global.

2. **Domain layer became a shared-memory bus.** `domain/types.hpp` defines 17 mutable inline atomic globals. Every layer (drivers, infrastructure, application, interface, main) reads and writes them freely — no encapsulation, no access control.

3. **REST API duplicate code path.** The REST API handlers in `rest_api.cpp` repeat logic from the command handlers (`handlers/*.cpp`) with subtle differences, creating two code paths that must be kept in sync.

4. **Dead code from incomplete migration.** The `Valve` driver class (`drivers/valve.hpp`, `drivers/valve.cpp`) from the Rust-era architecture was fully ported but never instantiated. Valve GPIO is instead toggled by raw `gpio_set_level()` calls in the motor task.

5. **No interface abstraction.** Application-layer handlers directly include `infrastructure/motor_task.hpp` and access `gMotorCmdQueue`, `gTmcUart`, and `gSmResultQueue` as extern globals. No domain-level interfaces or dependency injection.

## Violation catalogue

### HIGH severity (7)

| # | Violation | File(s) | Impact |
|---|---|---|---|
| **H1** | **Valve GPIO controlled by motor task.** `MotorCommandType::SetValve` in the motor enum, `set_valve()` in `motion.cpp` | `motor/task.cpp:224`, `motor/motion.cpp:28`, `motor_task.hpp:26`, `handlers/valve.cpp` | Valve is a simple GPIO14 toggle with 50ms settle — does not belong in the stepper motor command queue. Creates unnecessary coupling; valve commands queue behind motor moves. |
| **H2** | **REST API valve bypass writes `gValvePosition` without toggling GPIO.** `handleValvePostCore` stores to the atomic but never calls `gpio_set_level(PIN_VALVE)` | `rest_api.cpp:115` | REST clients believe the valve switched; physical hardware does not change. Same class of bug as H1, second code path. |
| **H3** | **`gBuretteState` written before motor command enqueued.** Six handlers in `burette_ops.cpp` call `.store()` then `xQueueSend()`. If the queue is full, the handler returns error but `gBuretteState` is already permanently changed. | `burette_ops.cpp:24,46,90,116,166,189`, `task.cpp:127,132,141,176,188,201,214`, `motion.cpp:75,82,96,138`, `homing.cpp:39,89,122` | Stuck-state bug: `gBuretteState` shows "working" but no motor command was ever accepted. The motor task should be the sole writer. |
| **H4** | **OneWire driver writes `gTempCX100`.** `onewire.cpp` stores temperature directly to a domain global; `temp_thread.cpp` also writes the same global. | `onewire.cpp:96,107,120,124` | Two concurrent writers to `gTempCX100` with no synchronisation. Low-level driver protocol code should not own application state. |
| **H5** | **Application layer depends on `infrastructure/motor_task.hpp`** in 7 files. `SmResult`, `MotorCommand`, and queue handles leak from infrastructure into application. | `handlers/burette_ops.cpp`, `handlers/burette_cal.cpp`, `handlers/valve.cpp`, `handlers/sensors.cpp`, `response.hpp`, `response.cpp` | Dependency inversion: application should define an interface; infrastructure should implement it. |
| **H6** | **Dead `Valve` driver class.** `drivers/valve.hpp` and `valve.cpp` define a complete `Valve` class with `setPosition()`/`getPosition()`. Never instantiated anywhere in the codebase. | `drivers/valve.hpp`, `drivers/valve.cpp` | 2 files of dead code. The working valve control is raw `gpio_set_level()` in the motor task. |
| **H7** | **StallGuard threshold written directly to TMC UART from application handler.** `handleStallGuardSetThreshold` in `sensors.cpp` writes `gStallGuardThreshold` AND calls `gTmcUart.writeRegister(TMC_REG_SGTHRS)` — bypassing the motor task which owns the TMC UART. | `handlers/sensors.cpp:323-326` | Application layer directly accesses a motor-task-owned hardware peripheral. Race condition if motor task also writes SGTHRS concurrently. |

### MEDIUM severity (5)

| # | Violation | File(s) | Impact |
|---|---|---|---|
| **M1** | **Domain layer contains mutable global `gCalCache`.** `domain/calibration.hpp` stores a mutable pointer written by infrastructure (NVS) and read by handlers. | `domain/calibration.hpp:114` | Domain should define pure data types, not own mutable state. |
| **M2** | **REST API directly polls `gSmResultQueue`.** Line 222-237 in `rest_api.cpp` reads from the motor task's result queue. | `rest_api.cpp:222-237` | Interface layer bypasses application layer. Queue ownership ambiguity. |
| **M3** | **Main loop writes `gMotorIsMoving` and `gSpeedMlMin`** via heuristic (`gBuretteState != Idle`). Motor task also writes speed. | `main.cpp:288-291` | Second writer for motor-state atoms. Desync risk. |
| **M4** | **`handleRunCalibration` bypasses `sendMotorCommand()` helper.** Directly accesses `gMotorCmdQueue` instead of using the shared helper from `burette_ops.cpp`. | `burette_cal.cpp:188-193` | Two different queue-send patterns for motor commands. One has error checking, the other does not. |
| **M5** | **`MotorCommandType::SetValve` in motor enum.** Same file-level issue as H1 but tracked separately. | `motor_task.hpp:26` | Pollutes the motor task's API with a non-motor concern. |

### LOW severity (4)

| # | Violation | File(s) | Notes |
|---|---|---|---|
| L1 | ADC calibration globals `gCoeffAX1000`/`gCoeffB` in driver header | `drivers/adc.hpp:16-17` | Move to storage layer |
| L2 | BLE driver writes `gBleError` directly | `ble.cpp:215,225,356,382,398` | Acceptable — flag is BLE-specific |
| L3 | 17 atomic globals inline-defined in `domain/types.hpp` | `domain/types.hpp` | Architectural choice; accept |
| L4 | `gpio_config.cpp` init overlaps with motor task assumptions | `gpio_config.cpp`, `homing.cpp:43` | Document contract |

## Solution

### Phase 1 — Immediate (valve decoupling)
1. Remove `MotorCommandType::SetValve` from `motor_task.hpp`
2. Instantiate the `Valve` class from `drivers/valve.hpp` in `gpio_config.cpp` or `main.cpp`
3. `handleSetPosition` calls `Valve::setPosition()` directly — no motor queue
4. `move_fill()` / `move_empty()` in `motion.cpp` use the same `Valve::setPosition()`
5. Remove `handleValvePostCore`'s direct `gValvePosition.store()` — route through dispatch

### Phase 2 — State ownership
1. Make motor task sole writer of `gBuretteState` — remove `.store()` from handlers
2. Move `gCalCache` out of `domain/` to `infrastructure/storage/`
3. Remove `gTempCX100.store()` from `onewire.cpp` — let `temp_thread.cpp` own it

### Phase 3 — Layering
1. Move `SmResult` to `domain/`; create `IMotorController` interface in `application/`
2. Route REST API valve commands through `application::dispatch()`
3. Standardise queue-send pattern across all handlers

## Edge cases

- **Valve settle timing during fill/empty.** `move_fill()` currently calls `set_valve()`, then starts the stepper. If the valve is now a separate object, the coordination must still ensure valve settles (50ms) before the stepper moves. This is already handled by `vTaskDelay(VALVE_SETTLE_MS)` — just move it into the `Valve::setPosition()` call.

- **REST API synchronous wait.** `handleCommandCore` in `rest_api.cpp` waits for `gSmResultQueue` with a timeout. If the motor task is removed from direct queue access, the REST API needs an alternative way to wait for command completion. The application state machine's `pending_` timer could be exposed as a completion signal.

- **Thread safety of `gTempCX100`.** Currently two writers (`onewire.cpp` and `temp_thread.cpp`). After Phase 2, only `temp_thread.cpp` writes it. The OneWire driver must be changed to return `std::optional<float>` instead.

## Related files

- [SRP audit LL-049](../lessons_learned/LL-049.yaml) — lesson learned from the valve.setPosition bug
- [GR-0.1](../../AGENTS.md) — "Green unit tests prove nothing" rule
- [Command handler code](../../components/application/src/handlers/) — all handler files
- [Motor task](../../components/infrastructure/src/motor/task.cpp) — motor command dispatch
- [Motor task types](../../components/infrastructure/include/infrastructure/motor_task.hpp) — MotorCommandType, MotorCommand
- [Domain types](../../components/domain/include/domain/types.hpp) — all `g*` globals
- [Valve driver (dead)](../../components/infrastructure/src/drivers/valve.cpp) — unused Valve class
- [REST API valve handler](../../components/interface/src/rest_api.cpp) — duplicate code path
- [Detailed audit results](ISSUE-004-srp-violations-detailed.md) (generated by AI explore agent)
