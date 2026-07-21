---
type: Known Issue
title: "Valve settle delay and WS broadcast handled in motor task — fundamental architecture flaw"
description: "handleSetPosition() sends SetValvePosition MotorCommand to motor task queue. Motor task runs vTaskDelay(VALVE_SETTLE_MS) and emits valve_settled WS event. GPIO write happens immediately. No mutual exclusion between valve and motor operations."
tags: [architecture, srp, valve, motor-task, mutual-exclusion, safety]
timestamp: 2026-07-21
status: resolved
resolved_at: 2026-07-21
---

# Valve settle delay and WS broadcast handled in motor task — fundamental architecture flaw

**Severity:** High  
**Detected:** 2026-07-21, serial log `serial_2026-07-21_18-20-48.log`  
**Updated:** 2026-07-21 — architectural review identified deeper SRP + mutual exclusion + timing issues

## Problem

### 1. Hardware reality does not match code

| Parameter | Code (`config.hpp:113`) | Actual hardware (solenoid) | Future hardware (rotary valve) |
|---|---|---|---|
| `VALVE_SETTLE_MS` | 50 ms | **~500 ms** | **~3-5 seconds** |

At 50ms, blocking the motor task for settle is invisible. At 500ms it's painful; at 3-5 seconds it's **catastrophic** — the motor task cannot process EmergencyStop or Stop for multiple seconds during valve switching.

### 2. No mutual exclusion between valve and motor

**Legacy Arduino** had bidirectional busy checks:

```
// valve.setPosition — checks motor
handlers/valve.cpp:36:
    if (pending_id != 0 || stepper_is_busy())
        → "burette_busy"

// All motor commands — check valve + pending
handlers/burette_ops.cpp:24,85,122,160,253,292:
    bool busy = (pending_id != 0 || stepper_is_busy())
    if (busy) → "burette_busy"
```

**Current code has NO checks:**

- `valve.cpp:handleSetPosition()` calls `sendMotorCommand()` without checking `gBuretteState`. If motor is filling/emptying, the command silently queues behind the motor operation.
- Motor handlers (`handleFill`, `handleMoveSteps`, `handleMoveContinuous`, etc.) call `sendMotorCommand()` which is just `xQueueSend(..., 0)` — if the queue has space, the command is accepted regardless of whether the motor is already running.
- Safety-critical: you could send `burette.moveSteps` while the valve is mid-settle, and the motor would start before the valve has physically switched.

### 3. Valve uses motor task as a deferred timer (SRP violation)

```
Current flow — standalone valve toggle (e.g. WebUI button):

valve.cpp:handleSetPosition()
    │
    ├── sendMotorCommand(SetValvePosition)  ← goes to motor queue
    ├── GPIO write ← valve switches NOW
    └── HTTP 200 response ← client thinks it's done
                              │
                    ┌─────────▼──────────────┐
                    │     Motor Task          │
                    │  (when dequeued)        │
                    │                         │
                    │  vTaskDelay(50ms)       │
                    │  push valve_settled WS  │
                    │  ← if motor is busy     │
                    │    running fill/empty,  │
                    │    this waits SECONDS   │
                    └────────────────────────↕
                              │
                    ┌─────────▼──────────────┐
                    │     net_owner           │
                    │  drainWsBroadcastQueue()│
                    │  → WS "valve_settled"   │
                    └────────────────────────↕
                              │
                    ┌─────────▼──────────────┐
                    │     WebUI client        │
                    │  Confirms valve moved   │
                    │  — but GPIO happened    │
                    │    seconds ago!         │
                    └─────────────────────────┘
```

### 4. `gBuretteState` and `gMotorIsMoving` exist but are unused for locking

`domain/types.hpp:133,145`:
```cpp
inline std::atomic<BuretteState> gBuretteState{BuretteState::Idle};
inline std::atomic<bool> gMotorIsMoving{false};
```

Seven states defined on `gBuretteState`, plus `gMotorIsMoving`, but only motor task writes to them and nobody reads them for admission control.

## Root cause analysis

Three separate problems conflated into one bad design:

```
Problem A: Need valve settle delay before motor starts
    → motion.cpp, sm_runners.cpp call vTaskDelay(VALVE_SETTLE_MS)
    → This is CORRECT — inside an atomic motor operation

Problem B: Need valve_settled WS event for WebUI
    → Created as side-effect in motor task handler
    → Wrong owner: valve lifecycle has nothing to do with motor

Problem C: Need non-blocking standalone valve toggle
    → Can't vTaskDelay in HTTP handler (would block server)
    → So queued to motor task as a "free timer"
    → This is the WRONG abstraction
```

## Corrected architecture (post-review v2)

### Gatekeeper design

Two orthogonal atomic flags instead of polluting `BuretteState`:

```cpp
// domain/types.hpp — BuretteState stays clean (no ValveSwitching)
enum class BuretteState : uint8_t
{
    Idle, Homing, Filling, Emptying, Dosing, Rinsing, Stopping, Error
};

// Orthogonal mutual-exclusion flag (domain/types.hpp)
inline std::atomic<bool> gValveIsSettling{false};

// gBuretteState != Idle already serves as gMotorIsBusy
// (gMotorIsMoving at line 145 can be removed as redundant)
```

`gValveIsSettling` is orthogonal to `gBuretteState`:
- Valve can settle independently of burette state machine
- No changes needed in `cal_dose_sm.hpp`, `rinse_sm.hpp`, `sm_runners.cpp`
- `BuretteState` remains flat and readable

**Gatekeeper predicate (return `"burette_busy"` if true):**
```cpp
bool systemBusy() {
    return gBuretteState.load(std::memory_order_acquire) != BuretteState::Idle
        || gValveIsSettling.load(std::memory_order_acquire);
}
```

### CAS (compare-and-swap) mandated — never load+store

A naive load+store sequence has a race window:

```cpp
// WRONG — race condition
auto state = gBuretteState.load();  // sees Idle
// concurrent BLE request also sees Idle
gBuretteState.store(Filling);       // both pass!
```

**All gatekeeper transitions MUST use CAS:**

```cpp
// valve.cpp — acquire valve settle slot
bool expected = false;
if (!gValveIsSettling.compare_exchange_strong(expected, true,
        std::memory_order_acq_rel, std::memory_order_acquire)) {
    return makeErrorResponse("burette_busy");
}

// burette_ops.cpp — acquire motor slot
auto expected = BuretteState::Idle;
if (!gBuretteState.compare_exchange_strong(expected, BuretteState::Filling,
        std::memory_order_acq_rel, std::memory_order_acquire)) {
    return makeErrorResponse("burette_busy");
}
```

### Correct flow — standalone valve toggle

```
valve.cpp:handleSetPosition()
    │
    ├── CAS(gValveIsSettling, false→true)
    │     ↓ fail → "burette_busy"
    ├── GPIO write ← valve switches now
    ├── HTTP 200 response ← immediate
    └── esp_timer one-shot(VALVE_SETTLE_MS)
              │
              └── timer callback:
                    ├── ESP_LOGI("Valve settled: position=%s")
                    ├── xQueueSend(gWsBroadcastQueue, ...) ← non-blocking
                    │     ↓ errQUEUE_FULL → ESP_LOGW + drop counter
                    └── gValveIsSettling = false

Motor handlers — during standalone settle:

handleFill/handleMoveSteps/handleMoveToStop/handleStop:
    ├── systemBusy() → "burette_busy"  ← REJECT, not queue
    └── CAS(gBuretteState, Idle→newState)
          ↓ fail → "burette_busy"

EmergencyStop:
    ├── cancelValveSettleTimer() if gValveIsSettling
    ├── gValveIsSettling = false
    └── proceed unconditionally
```

### Correct flow — motor operation with integral valve switch

```
move_fill():
    │
    ├── CAS(gBuretteState, Idle→Filling)  ← inside motor operation atomically
    ├── set_valve(Input)
    ├── settle_loop():
    │     for i in 0..VALVE_SETTLE_MS/10:
    │         if gStopFull → break (EmergencyStop!)
    │         vTaskDelay(10ms)
    └── move_to_endstop(LiqIn, ..., gStopFull)

During settle_loop, EmergencyStop IS processed
(checked every 10ms via gStopFull flag)
gValveIsSettling is NOT touched here — integral operation.
```

### System context

```
┌──────────────────────────────────────────────────────────────┐
│                   Application Layer                          │
│  (burette_ops.cpp, valve.cpp, dispatch.cpp)                  │
│                                                              │
│  ┌─────────────────────────┐   ┌──────────────────────────┐  │
│  │ systemBusy() gatekeeper  │   │ emergencyStop bypasses  │  │
│  │ = gBuretteState != Idle │   │ all checks + cancels    │  │
│  │ || gValveIsSettling     │   │ pending timer            │  │
│  └────────┬────────────────┘   └──────────┬───────────────┘  │
│           │                               │                   │
│           ▼                               ▼                   │
│    ┌──────────────┐              ┌──────────────┐            │
│    │  Motor Task  │              │  esp_timer   │            │
│    │              │              │  (valve)     │            │
│    │ MoveSteps    │              │              │            │
│    │ MoveCont.    │              │ callback:    │            │
│    │ Stop/EStop   │              │ valve_settled│            │
│    │ Home         │              │ → WS queue   │            │
│    │ SetDirection │              │ (drop on     │            │
│    │ ...          │              │  full)       │            │
│    │              │              │ → gValveIs-  │            │
│    │ fill():      │              │   Settling=0 │            │
│    │  set_valve() │              └──────────────┘            │
│    │  settle_loop │                                          │
│    │  move_to_..  │                                          │
│    └──────────────┘                                          │
│           │                                                  │
│           ▼                                                  │
│    ┌────────────────────────────────────────────────────┐    │
│    │              gWsBroadcastQueue                     │    │
│    └───────────────────────┬────────────────────────────┘    │
│                            ▼                                 │
│    ┌────────────────────────────────────────────────────┐    │
│    │              net_owner (drain)                     │    │
│    │       motor_complete, valve_settled,               │    │
│    │       stallguard_result → WS clients               │    │
│    └────────────────────────────────────────────────────┘    │
└──────────────────────────────────────────────────────────────┘
```

## Detailed design decisions

### A. Orthogonal atomics — NOT `BuretteState::ValveSwitching`

`gValveIsSettling` is a separate `std::atomic<bool>` because:

1. **BuretteState models burette operational state** (dosing, filling, emptying).
   Valve switching is a prerequisite, not a burette mode. Adding it to
   `BuretteState` would violate SRP at the state-machine level.

2. **Two distinct use cases, one flag would conflate them:**
   ```
   Idle → ValveSwitching → Idle       (standalone toggle — no burette op)
   Idle → ValveSwitching → Filling    (integral — fill starts after settle)
   ```
   With a boolean flag the integral case simply does not touch `gValveIsSettling`
   at all — the burette state machine transitions directly `Idle → Filling`.

3. **No changes needed in state machine files** (`cal_dose_sm.hpp`,
   `rinse_sm.hpp`, `sm_runners.cpp`) — they know nothing about `gValveIsSettling`.

4. **Easier to test** — each atomic is isolated.

### B. `VALVE_SETTLE_MS` updated in config

`infrastructure/config.hpp:113`:
```cpp
inline constexpr uint32_t VALVE_SETTLE_MS = 500; // solenoid 500ms
// Future rotary valve: change to 5000
```

### C. `valve.cpp:handleSetPosition()` — new implementation with CAS

MUST check both `gBuretteState` (burette not busy) AND `gValveIsSettling` (no concurrent settle):

```cpp
std::expected<CommandResponse, domain::AppError>
handleSetPosition(std::optional<domain::ValvePosition> pos)
{
    if (!pos)
        return makeErrorResponse("invalid_params");

    // Mutual exclusion: burette must be idle AND no concurrent settle
    if (domain::gBuretteState.load(std::memory_order_acquire)
            != domain::BuretteState::Idle) {
        return makeErrorResponse("burette_busy");
    }
    bool expected = false;
    if (!domain::gValveIsSettling.compare_exchange_strong(expected, true,
            std::memory_order_acq_rel, std::memory_order_acquire)) {
        return makeErrorResponse("burette_busy");
    }

    // GPIO write — immediate
    infrastructure::drivers::gValve.setPosition(*pos);
    domain::gValvePosition.store(*pos, std::memory_order_release);

    // Arm settle timer
    armValveSettleTimer(*pos);

    const char* posStr = ...;
    CommandResponse rsp;
    rsp.kind = ResponseKind::Single;
    rsp.bodySize = snprintf(..., R"({"status":"ok","data":{"position":"%s"}})", posStr);
    return rsp;
}
```

### D. `burette_ops.cpp` — all handlers add CAS gatekeeper

Every handler that sends a MotorCommand to the motor queue must first
atomically claim the burette state via CAS:

```cpp
auto expected = domain::BuretteState::Idle;
if (!domain::gBuretteState.compare_exchange_strong(expected,
        domain::BuretteState::Filling,  // or appropriate state
        std::memory_order_acq_rel, std::memory_order_acquire)) {
    return makeErrorResponse("burette_busy");
}
```

Also check `gValveIsSettling` — valve settle is orthogonal:
```cpp
if (domain::gValveIsSettling.load(std::memory_order_acquire))
    return makeErrorResponse("burette_busy");
```

**Exception: EmergencyStop** — always allowed, cancels pending settle:
```cpp
cancelValveSettleTimer();
domain::gValveIsSettling.store(false, std::memory_order_release);
// then proceed with emergency stop (unconditionally)
```

**Stop during standalone settle:** `Stop` is a motor operation. If valve is
settling (standalone toggle), Stop has no motor to stop — it returns
`"valve_settling_try_again"`. `EmergencyStop` cancels the settle timer
immediately.

### E. `esp_timer` callback — non-blocking, handles queue full

Must clear `gValveTimerArmed` to prevent stale-arm race on next
`armValveSettleTimer()` call. Matches existing pattern from `task.cpp`
(lines 220-224) where WS broadcast entries are declared static:

```cpp
void valveSettleCallback(void* arg)
{
    auto pos = static_cast<ValvePosition>(reinterpret_cast<uintptr_t>(arg));
    const char* posStr = (pos == ValvePosition::Input) ? "input" : "output";
    ESP_LOGI(TAG, "Valve settled: position=%s", posStr);

    // Push valve_settled WS event — non-blocking
    // (Article I Constitution: no blocking operations in timer context)
    static WsBroadcastEntry entry;
    int n = snprintf(entry.data, sizeof(entry.data),
                     R"({"event":"valve_settled","position":"%s"})", posStr);
    if (n > 0 && static_cast<size_t>(n) < sizeof(entry.data)) {
        entry.len = static_cast<size_t>(n);
        if (xQueueSend(gWsBroadcastQueue, &entry, 0) != pdPASS) {
            ESP_LOGW(TAG, "WS queue full, dropping valve_settled event");
        }
    }

    gValveIsSettling.store(false, std::memory_order_release);
    gValveTimerArmed.store(false, std::memory_order_release); // clear stale-arm flag
}
```

TODO: add diagnostics counter (e.g. `gDroppedWsEvents`) after initial implementation.

### F. `esp_timer` handle lifecycle — static handle + atomic armed flag

```
NOTE: Constitution Art. VI (RAII is Law). A naked esp_timer_handle_t
is used because the timer lives for the lifetime of the program and is
created once (lazy init). Adding a RAII wrapper would add complexity
with no benefit for a permanent singleton. If the timer were dynamic
(create/delete per toggle), a wrapper would be required.
```

```cpp
// valve.cpp — module-level static
static esp_timer_handle_t s_valveTimer = nullptr;
static std::atomic<bool> gValveTimerArmed{false};

void armValveSettleTimer(ValvePosition pos)
{
    if (!s_valveTimer) {
        esp_timer_create_args_t args = {};
        args.callback = valveSettleCallback;
        args.arg = reinterpret_cast<void*>(static_cast<uintptr_t>(
            pos == ValvePosition::Input ? 0 : 1));
        args.name = "valve_settle";
        ESP_ERROR_CHECK(esp_timer_create(&args, &s_valveTimer));
    } else {
        // Cancel previous if re-arming (rapid toggle)
        // TOCTOU race: timer may fire between exchange and stop.
        // ESP_ERR_INVALID_STATE from esp_timer_stop is benign.
        if (gValveTimerArmed.exchange(true, std::memory_order_acq_rel)) {
            esp_err_t err = esp_timer_stop(s_valveTimer);
            if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
                ESP_ERROR_CHECK(err); // unexpected failure
            }
        }
    }
    // exchange() already set gValveTimerArmed=true — no redundant store needed
    ESP_ERROR_CHECK(esp_timer_start_once(s_valveTimer, VALVE_SETTLE_MS * 1000));
}

void cancelValveSettleTimer()
{
    if (gValveTimerArmed.exchange(false, std::memory_order_acq_rel)) {
        if (s_valveTimer) {
            esp_err_t err = esp_timer_stop(s_valveTimer);
            if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
                ESP_ERROR_CHECK(err);
            }
        }
    }
}
```

### G. `motion.cpp` / `sm_runners.cpp` — settle_loop

Replace `vTaskDelay(VALVE_SETTLE_MS)` with a polling loop that checks
the stop flag. This is inside an atomic motor operation, so
`gValveIsSettling` is NOT touched here:

```cpp
void settle_valve(std::atomic<bool>& stopFlag)
{
    int steps = VALVE_SETTLE_MS / 10;
    for (int i = 0; i < steps; ++i)
    {
        if (stopFlag.load(std::memory_order_acquire))
            break;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
```

### H. Removing `SetValvePosition` from motor dispatch — requires audit

Before removing, audit all callers of `sendMotorCommand(SetValvePosition)`:

```bash
grep -rn "SetValvePosition" --include="*.cpp" --include="*.hpp"
```

Expected matches:
- `valve.cpp:handleSetPosition()` ← will be removed (replaced by esp_timer)
- `motor_command.hpp` ← enum definition (to be removed or deprecated)
- `task.cpp` ← dispatch case and handler (to be removed)

**This is a protocol change.** Must:
1. Verify WebUI/BLE/REST clients do not expect motor task response for `valve.setPosition`
2. Update JSON Command Set documentation in `project.md`
3. Add migration note in commit message

## Resolved design decisions

1. **Queue or Reject?** → **REJECT.** Lab device needs immediate operator feedback.
   Silent queuing = unpredictable behavior = risk for experiment.
   Compatible with legacy Arduino (`burette_busy`).

2. **`valve_settled` WS event retention?** → **Keep, generated by `esp_timer` callback.**
   WebUI currently waits for it. Timer runs in `esp_timer` task (not ISR),
   `xQueueSend` is safe.

3. **Stop during valve settle?** → `Stop` only applies to motor operations.
   Standalone valve toggle has no motor — `Stop` returns `"valve_settling_try_again"`.
   `EmergencyStop` cancels the settle timer immediately.

## Edge cases

- **Multiple rapid valve toggles**: `armValveSettleTimer()` cancels previous timer
  via `gValveTimerArmed.exchange(true)` + `esp_timer_stop()` before re-arming.

- **EmergencyStop during settle**: `cancelValveSettleTimer()` stops the timer,
  `gValveIsSettling` is set to `false`, motor is stopped unconditionally.

- **System restart during settle**: `esp_timer` does not survive restart —
  timer is lost, but on next boot everything reinitialises. `gValveIsSettling`
  is a plain `std::atomic` in BSS → initialises to `false`.

- **`esp_timer` callback context**: runs in `esp_timer` task (not ISR) —
  `xQueueSend` and `ESP_LOGI` are safe.

- **`gWsBroadcastQueue` full in callback**: `xQueueSend(..., 0)` returns
  `errQUEUE_FULL` → event dropped with `ESP_LOGW`. Timer callback never blocks
  (Article I Constitution).

- **Race condition on gatekeeper**: Two concurrent requests (BLE + HTTP, Article V
  Constitution) both see `Idle`. CAS prevents double-acquisition. The second
  request gets `"burette_busy"`.

## Implementation order

1. Add `gValveIsSettling` to `domain/types.hpp`; remove `gMotorIsMoving` (redundant)
2. Add `armValveSettleTimer()` / `cancelValveSettleTimer()` / `valveSettleCallback()` to `valve.cpp`
3. Rewrite `handleSetPosition()` in `valve.cpp` — CAS gatekeeper + esp_timer, no MotorCommand
4. Update all motor handlers in `burette_ops.cpp` — add systemBusy() check + CAS
5. Update `handleEmergencyStop()` — cancel settle timer
6. Update `handleStop()` — settle-agnostic, returns "valve_settling_try_again" during standalone settle
7. Replace `vTaskDelay(VALVE_SETTLE_MS)` with `settle_loop()` in `motion.cpp` and `sm_runners.cpp`
8. Remove `SetValvePosition` from `motor_command.hpp` and `task.cpp` dispatch
9. Update documentation in `project.md` (JSON Command Set)
10. Add/update tests (see below)

## Test coverage

### Unit tests (to be added)

| # | Location | Test | Verification |
|---|---|---|---|
| 1 | `test_handlers.cpp` | `handleSetPosition` rejects when `gValveIsSettling` is true | Returns `"burette_busy"` |
| 2 | `test_handlers.cpp` | `handleSetPosition` does NOT send MotorCommand | `sendMotorCommand` spy shows 0 calls |
| 3 | `test_handlers.cpp` | `handleFill` rejects when `gValveIsSettling` is true | Returns `"burette_busy"` |
| 4 | `test_handlers.cpp` | `handleEmergencyStop` bypasses all checks | Succeeds even when `gValveIsSettling` is true |
| 5 | `test_handlers.cpp` | Race condition: two concurrent `handleSetPosition` | CAS: one succeeds, one gets `"burette_busy"` |
| 6 | `test_dispatch.cpp` | `MoveToStop` returns `"burette_busy"` when valve settling | Returns error immediately |
| 7 | `test_timer.cpp` | Valve timer cancellation on EmergencyStop | Timer stopped, `gValveIsSettling` = false |
| 8 | `test_timer.cpp` | WS queue full in timer callback | `xQueueSend` fails, `ESP_LOGW` emitted, no crash |

### Regression tests (source-code checks)

| # | Location | Verification |
|---|---|---|
| 9 | `test_logging.cpp` | `Valve settled` log is in `valve.cpp` (not `task.cpp`) |
| 10 | `test_logging.cpp` | `SetValvePosition` case removed from motor dispatch in `task.cpp` |
| 11 | `test_srp.cpp` | `handleSetValvePosition` function removed from `task.cpp` |

## Resolution

**Date:** 2026-07-21  
**Verified by:** `scripts/pre_commit.sh --full` — all 12 stages passed. Smoke test on real ESP32-S3 confirmed boot and serial API protocol compliance.

**Changes (19 files):**

| Area | File | Change |
|------|------|--------|
| Types | `domain/types.hpp` | Added `gValveIsSettling`, removed `gMotorIsMoving` |
| Config | `infrastructure/config.hpp` | `VALVE_SETTLE_MS` 50 → 500 |
| Infrastructure | `infrastructure/drivers/valve.cpp` | Added valve settle timer (`armValveSettleTimer`, `cancelValveSettleTimer`, `valveSettleCallback`) via `esp_timer` one-shot |
| Infrastructure | `infrastructure/drivers/valve.hpp` | Declared timer functions |
| Infrastructure | `infrastructure/src/motor/motion.cpp` | Added `settle_valve()` polling loop; replaced `vTaskDelay(VALVE_SETTLE_MS)` |
| Infrastructure | `infrastructure/src/motor/sm_runners.cpp` | Replaced `vTaskDelay` → `settle_valve(gStopFull)` |
| Infrastructure | `infrastructure/src/motor/task.cpp` | Removed `handleSetValvePosition()` + dispatch case; updated `handleEmergencyStop()` |
| Motor command | `domain/motor_command.hpp` | Removed `SetValvePosition` enum value |
| Application | `application/handlers/valve.cpp` | Rewrote `handleSetPosition` — CAS gatekeeper + `esp_timer`, no `MotorCommand` |
| Application | `application/handlers/burette_ops.cpp` | Added `gValveIsSettling` + CAS gatekeeper to ALL handlers |
| Application | `application/handlers/burette_cal.cpp` | Added CAS gatekeeper |
| Broadcast | `interface/broadcast.*`, `domain/broadcast_helpers.hpp`, `main/main.cpp` | Removed `motorIsMoving` field |
| Tests | `tests/src/test_handlers.cpp` | 6 new gatekeeper tests; `resetState()` helper |
| Tests | `tests/src/test_dispatch.cpp` | `resetState()` helper |
| Tests | `tests/src/test_logging.cpp` | Updated regression checks |
| Stubs | `tests/stubs/esp_timer.h`, `tests/src/stub_valve.cpp`, `tests/src/stub_motor_task.cpp` | Added stubs for test build |

**Key architectural outcomes:**
1. Valve settle runs on `esp_timer` (not motor task) — motor task no longer blocked for 500ms
2. CAS gatekeeper prevents double-acquisition (race-free mutual exclusion)
3. `EmergencyStop` unconditionally cancels settle timer
4. `Stop` during standalone settle returns `"valve_settling_try_again"`
5. Motor operations with integral valve switch use `settle_loop()` with stop-flag polling
6. Architecture checker (`check_arch.py`) — no violations
