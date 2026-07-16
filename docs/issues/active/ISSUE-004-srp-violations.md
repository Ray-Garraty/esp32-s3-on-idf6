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

## Implementation Plan (10 steps, 3 phases)

**Smoke test** = `scripts/idf.sh smoke` (build + flash + 30s monitor — reboot detection only).
**Business logic tests** = `scripts/testing/serial_api_test.py` (serial cmd/rsp + broadcast format),
`scripts/testing/http_api_test.py` (HTTP endpoints + WebSocket),
`scripts/testing/ble_test.py` (BLE NUS).

All responses must conform to `docs/API/COMMS_PROTOCOL.md`:
- Commands: `{"id": <uint64>, "cmd": "<command>", ...params}`
- Single-phase response: `{"id": <uint64>, "status": "ok", "data": {...}}`
- Error: `{"id": <uint64>, "status": "error", "message": "<code>"}`
- Broadcast: `{"ts": <uint32>, "temp": <float|null>, "mv": <float>, "vlv": "in"|"out"|"unk", "brt": {"sts": "...", "vl": <float>, "spd": <float>}}`

### Phase 1 — Valve decoupling (Steps 1–5)

#### Step 1 — Instantiate `Valve` class globally (H6, prepares H1)

**Files:**
- `components/infrastructure/include/infrastructure/drivers/valve.hpp` — add `extern Valve gValve;`
- `components/infrastructure/src/drivers/valve.cpp` — add `#include "infrastructure/config.hpp"`, define `Valve gValve(config::PIN_VALVE);`
- `main/gpio_config.cpp` — remove duplicate `gpio_set_direction(PIN_VALVE, ...)` / `gpio_set_level(PIN_VALVE, 0)` (Valve ctor already does this)

**Verification:**
1. `scripts/idf.sh smoke` — no Guru Meditation, no WDT panics in 30s
2. `scripts/testing/serial_api_test.py` — valve.getState (id=4), valve.setPosition (id=6-9), broadcast vlv field

#### Step 2 — `handleSetPosition` calls `gValve.setPosition()` directly (H1, partial H5)

**Files:**
- `components/application/src/handlers/valve.cpp` — replace MotorCommand queue send with direct `gValve.setPosition(pos)`, `gValvePosition.store(pos)`, `vTaskDelay(VALVE_SETTLE_MS)`
  - Remove `#include "infrastructure/motor_task.hpp"`
  - Add `#include "infrastructure/drivers/valve.hpp"`, `#include "infrastructure/config.hpp"`, `#include "freertos/task.h"`
  - **Response format unchanged** — already COMMS_PROTOCOL-compliant single-phase:
    ```json
    {"status":"ok","data":{"position":"output"}}
    ```

**Verification:**
1. `scripts/idf.sh smoke`
2. `scripts/testing/serial_api_test.py` — valve.setPosition should respond faster (no motor queue wait)
3. Valve toggles immediately even while motor task is busy

#### Step 3 — `motion.cpp` uses `Valve` class (H1)

**Files:**
- `components/infrastructure/src/motor/motion.cpp` — `set_valve()` calls `gValve.setPosition()` instead of raw `gpio_set_level()`
  - Add `#include "infrastructure/drivers/valve.hpp"`
  - Add `vTaskDelay(VALVE_SETTLE_MS)` after `set_valve()` in both `move_fill()` and `move_empty()` — **pre-existing bug fix**: settle was missing in fill/empty paths

**Verification:**
1. `scripts/idf.sh smoke`
2. `GET /api/status` — `burette.status` + `valve.position` consistent
3. Manual: fill/empty toggle valve correctly with 50ms settle before stepper moves

#### Step 4 — Remove `SetValve` from motor task (H1, M5)

**Files:**
- `components/infrastructure/include/infrastructure/motor_task.hpp`:
  - Remove `SetValve` from `MotorCommandType` enum
  - Remove `domain::ValvePosition valvePos;` from `MotorCommand` struct
- `components/infrastructure/src/motor/task.cpp` — remove `case MotorCommandType::SetValve:` block (lines 224-227)

**Verification:**
1. `scripts/idf.sh smoke` — compiler catches stale references
2. `scripts/testing/serial_api_test.py` — full pass

#### Step 5 — Route REST API valve through dispatch (H2)

**Files:**
- `components/interface/src/rest_api.cpp`:
  - `valve_get_handler` → construct `{"cmd":"valve.getState"}`, call `handleCommandCore()`, return result
  - `valve_post_handler` → parse JSON body for `"position"`, construct `{"cmd":"valve.setPosition","position":"%s"}`, call `handleCommandCore()`, return result
  - **Delete** `handleValveGetCore()` (lines 73-81) and `handleValvePostCore()` (lines 83-121) — dead code, duplicate code path eliminated
  - Remove `#include "nlohmann/json.hpp"` if no longer needed elsewhere

**HTTP response format changes** (REST API not covered by COMMS_PROTOCOL.md):
| Endpoint | Before | After |
|---|---|---|
| `GET /api/valve` | `{"valve":"input"}` | `{"status":"ok","data":{"position":"input"}}` |
| `POST /api/valve` | `{"valve":"output"}` | `{"status":"ok","data":{"position":"output"}}` |

- `scripts/testing/http_api_test.py` — update expectations to match new format:
  - Line ~549: `j.get("valve") in ("input", "output")` → `j.get("status") == "ok" and j["data"]["position"] in ("input", "output")`
  - Line ~558: `j.get("valve") == "output"` → `j["status"] == "ok" and j["data"]["position"] == "output"`
  - Line ~565: similar for input
  - The `validate_http_status` function (line 291-326) already validates `valve.position` via the `/api/status` endpoint which is unchanged — ok.

**Verification:**
1. `scripts/idf.sh smoke`
2. `scripts/testing/serial_api_test.py` — valve broadcast `vlv` must reflect real GPIO state after POST/GET
3. `scripts/testing/http_api_test.py` — updated expectations pass

### Phase 2 — State ownership (Steps 6–7)

#### Step 6 — Motor task sole writer of `gBuretteState` (H3)

**Files:**
- `components/application/src/handlers/burette_ops.cpp` — remove `.store()` calls on lines 24, 46, 90, 116, 166, 189-190

**Rationale:** The motor task (`task.cpp`) already writes `gBuretteState` when it processes each command. Removing the premature `.store()` from handlers eliminates the stuck-state bug: if `xQueueSend` fails, `gBuretteState` stays `Idle` instead of permanently showing "Working".

**Behavior change:** After sending a command, `gBuretteState` updates when the motor task processes it (up to 100ms delay) instead of immediately. The ACK response tells clients the command was accepted; the state reflects actual execution.

**Verification:**
1. `scripts/idf.sh smoke`
2. `scripts/testing/serial_api_test.py` — burette.getStatus (id=2)
3. Manual: rapid-fire 5 fill commands, 6th should return `burette_busy` while `gBuretteState` remains `Idle`

#### Step 7 — OneWire driver stops writing `gTempCX100` (H4)

**Files:**
- `components/infrastructure/src/drivers/onewire.cpp` — delete 4 lines: `gTempCX100.store(...)` at lines 96, 107, 120, 124
  - Remove `#include "domain/types.hpp"` (no longer needed)
  - Remove `#include <limits>` (no longer needed — sentinel values replaced by `std::nullopt`)

**No change to `temp_thread.cpp`** — it already handles `readSensor()` return value correctly:
```cpp
auto tempOpt = drivers::readSensor(bus);
if (tempOpt.has_value()) {
    gTempCX100.store(static_cast<int32_t>(tempOpt.value() * 100.0f));
} else {
    gTempCX100.store(-99999);
}
```

**Verification:**
1. `scripts/idf.sh smoke`
2. `scripts/testing/serial_api_test.py` — broadcast `temp` field correct (step 8)
3. `GET /api/status` — `sensors.temperature.celsius_val` correct

### Phase 3 — Layering (Steps 8–10)

#### Step 8 — StallGuard threshold via motor task (H7)

**Files:**
- `components/infrastructure/include/infrastructure/motor_task.hpp`:
  - Add `SetStallThreshold` to `MotorCommandType` enum (after `StartCalSpeedSeq`)
  - Add `uint8_t stallThreshold;` to `MotorCommand` struct
- `components/infrastructure/src/motor/task.cpp`:
  - Add switch case `SetStallThreshold` that writes `gStallGuardThreshold` + calls `gTmcUart.writeRegister(TMC_REG_SGTHRS, threshold)`
- `components/application/src/handlers/sensors.cpp` (lines 323-326):
  - Remove direct `gTmcUart.writeRegister()` call
  - Send `SetStallThreshold` command to motor queue instead
  - Keep atomic update + NVS persist in handler (these are state, not hardware access)
  - Add `#include "esp_log.h"` (for `ESP_LOGW` on queue-full)
  - If queue is full, log warning but accept the atomic + NVS update (register set on next boot)

**Verification:**
1. `scripts/idf.sh smoke`
2. Manual: `stallGuard.setThreshold` + `stallGuard.get` — threshold persists and TMC register updates

#### Step 9 — Move `SmResult` to domain layer (H5)

**Files:**
- **New:** `components/domain/include/domain/sm_result.hpp` — define `SmResult` struct
- `components/infrastructure/include/infrastructure/motor_task.hpp`:
  - Remove `SmResult` struct definition
  - Add `#include "domain/sm_result.hpp"`
  - Add `using SmResult = domain::SmResult;` in infrastructure namespace
- `components/application/include/application/response.hpp`:
  - Change `#include "infrastructure/motor_task.hpp"` → `#include "domain/sm_result.hpp"`
  - Change `const infrastructure::SmResult&` → `const domain::SmResult&`
- `components/application/src/response.cpp`:
  - Change `#include "infrastructure/motor_task.hpp"` → `#include "domain/sm_result.hpp"`
  - Update `infrastructure::SmResult::Type` → `domain::SmResult::Type`
  - Update `infrastructure::SmResult` → `domain::SmResult`

**ABI-safe:** struct layout unchanged. FreeRTOS queues using `sizeof(SmResult)` unaffected.

**Verification:**
1. `scripts/idf.sh smoke` — compiler catches all stale references
2. `scripts/testing/serial_api_test.py`

#### Step 10 — `IMotorController` interface (H5, Phase 3 completion)

**Files:**
- **New:** `components/application/include/application/motor_controller.hpp` — abstract interface:
  - `sendCommand()`, `peekResult()`, `waitResult()`, `writeTmcRegister()`, `readTmcRegister()`
- **New:** `components/infrastructure/src/motor/motor_controller_impl.cpp` — wraps `gMotorCmdQueue`, `gSmResultQueue`, `gTmcUart`
- `components/application/src/dispatch.cpp` — inject controller reference
- `components/application/src/handlers/*.cpp` — use controller instead of raw queue handles
- `components/interface/src/rest_api.cpp` — use controller instead of `gSmResultQueue` poll
- `main/main.cpp` — instantiate controller after motor task creates queues

**Verification:**
1. `scripts/idf.sh smoke`
2. `scripts/testing/serial_api_test.py`
3. `scripts/testing/http_api_test.py`
4. `scripts/testing/ble_test.py`

## Dependency order

```
Phase 1 ─────────────────────────────────────────────
Step 1 (Valve global) ──→ Step 2 (handler uses Valve) ──→ Step 4 (remove SetValve)
                            │
                            └─→ Step 3 (motion uses Valve)
                                    │
                                    └────── Step 5 (REST via dispatch)
                                              needs Step 2

Phase 2 ─────────────────────────────────────────────
Step 6 (gBuretteState) — independent of Phase 1
Step 7 (OneWire)       — independent of Phase 1

Phase 3 ─────────────────────────────────────────────
Step 8 (StallGuard)    — independent of Phase 1-2
Step 9 (SmResult)      — prerequisite for Step 10
Step 10 (IMotorController) — depends on Step 9
```

## Edge cases

### Valve settle timing via `Valve::setPosition()`
The `Valve` class itself does NOT include the 50ms settle delay (it's a pure GPIO wrapper). The delay must be applied by callers:
- `handleSetPosition` (Step 2) adds `vTaskDelay(VALVE_SETTLE_MS)` after `gValve.setPosition()`
- `move_fill()` / `move_empty()` (Step 3) add `vTaskDelay(VALVE_SETTLE_MS)` after `set_valve()` — **pre-existing bug fix**: the settle was missing in these paths, risking stepper movement before valve fully opened/closed

### REST API synchronous wait via dispatch
After Step 5, both GET and POST /api/valve go through `handleCommandCore()` → `dispatch()` → handler. Since valve commands are `ResponseKind::Single` (not `AckThen`), the synchronous result queue wait in `handleCommandCore` (lines 206-237) is never entered for valve commands — the response is returned directly from the handler. The result queue wait only applies to `AckThen` commands (fill, empty, dose, rinse).

### Thread safety of `gTempCX100` after Step 7
Only `temp_thread.cpp` writes `gTempCX100`. The OneWire driver no longer writes the global. `temp_thread.cpp` runs once per second, so consumers see at most 1s stale data — same as before.

### Queue-full for StallGuard (Step 8)
If the motor command queue is full when `SetStallThreshold` is sent, the atomic (`gStallGuardThreshold`) and NVS are already updated. The TMC register is not written until the next successful `SetStallThreshold` command or on next boot (motor task init reads from NVS). This is acceptable — threshold changes are infrequent.

### HTTP API test update (Step 5)
The simplest approach with minimum code duplication: remove `handleValvePostCore`/`handleValveGetCore` and route both handlers through `handleCommandCore()` (existing generic dispatch wrapper). Update `http_api_test.py` expectations to match the new COMMS_PROTOCOL-consistent response format `{"status":"ok","data":{"position":"..."}}` instead of the legacy `{"valve":"..."}`. This eliminates 40 lines of duplicate code and unifies the response format across all valve access paths.

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
