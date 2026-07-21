---
type: Known Issue
title: "Continuous mode (burette.moveToStop) calls handleStop() instead of starting motion"
description: "WebUI Continuous mode button sends burette.moveToStop. dispatch.cpp routes it to handleStop() — the same handler as burette.stop. Motor never starts."
tags: [webui, stepper, dispatch, burette, regression]
timestamp: 2026-07-21
status: resolved
resolved_at: 2026-07-21
---

# Continuous mode (burette.moveToStop) calls handleStop() instead of starting motion

**Severity:** Critical  
**Detected:** 2026-07-21, audit vs legacy Arduino  
**Legacy handler:** `legacy/arduino/src/handlers/stepper_cmd.cpp:34` → `stepper_move_to_stop()`

## Problem

In WebUI, select mode "Continuous", enter frequency 300, click Start:

1. HTTP POST `{"cmd":"burette.moveToStop","direction":"LIQ_IN","freq":300}` is sent
2. Server returns `{"status":"accepted"}` (AckThen)
3. Motor does not move
4. After `STOP_SETTLE_MS`, state machine returns to idle

The "Continuous" mode is completely non-functional.

## Root cause

In `dispatch.cpp:91-92`:
```cpp
case CommandType::MoveToStop:
    return withId(burette_ops::handleStop());
```

This is a **migration regression**: `burette.moveToStop` was incorrectly mapped to `handleStop()` — the same handler that processes `burette.stop`.

In legacy Arduino, `handle_burette_move_to_stop` (`stepper_cmd.cpp:34`) called `stepper_move_to_stop()` which started continuous motion in the given direction until a stop command or limit switch was triggered.

The current codebase already has the infrastructure for continuous motion:
- `motion.cpp:52-83` — `move_to_endstop()` runs continuous motion until `stopFlag` is set
- `motion.cpp:85-101` — `move_fill()` and `move_empty()` use `move_to_endstop()` internally

But there is:
- **No `MotorCommandType::MoveContinuous`** in `motor_command.hpp`
- **No motor task handler** that calls `move_to_endstop()` directly
- **No `handleMoveContinuous()`** in `burette_ops.cpp`

## Solution applied

### Files modified

| File | Change |
|---|---|
| `components/domain/include/domain/motor_command.hpp` | +`MoveContinuous` to `MotorCommandType` enum |
| `components/infrastructure/src/motor/task.cpp` | +`handleMoveContinuous()` function + case in motor dispatch |
| `components/application/src/handlers/burette_ops.cpp` | +`handleMoveContinuous()` — validates params, sends `MoveContinuous` motor command, returns `AckThen` |
| `components/application/include/application/handlers/burette_ops.hpp` | +`handleMoveContinuous()` declaration |
| `components/application/src/dispatch.cpp` | Changed `MoveToStop` from `handleStop()` → `handleMoveContinuous()` with direction and freqHz |
| `components/application/src/command.cpp` | +`freq` field fallback for WebUI compatibility |
| `tests/src/test_command.cpp` | +2 tests: `freq` field parsing, `freq_hz` field parsing |
| `tests/src/test_dispatch.cpp` | +1 test: `MoveToStop` returns `AckThen` (not `Single`) |
| `tests/src/test_handlers.cpp` | +3 tests: success, missing params, zero freq |

### Verification

- **Pre-commit** (`scripts/pre_commit.sh`): **`=== PRE_COMMIT_VERDICT: PASS ===`**
  - Build: SUCCESS
  - Unit tests: 817 assertions in 256 test cases — all pass
  - Smoke test: BOOT OK, no Guru Meditation, no WDT
  - Serial API hardware test: 16/16 ALL CHECKS PASSED
  - clang-tidy: ✅ Clean
  - sdkconfig constraint: ✅
  - Stack watermark: ✅

## Test coverage

### Unit test — dispatch routing (`test_dispatch.cpp`)

```cpp
TEST_CASE("dispatch: MoveToStop returns AckThen (not Single)", "[dispatch]")
{
    Command cmd{CommandType::MoveToStop};
    cmd.direction = Direction::LiqIn;
    cmd.freqHz = 300.0f;
    auto rsp = dispatch(cmd);
    REQUIRE(rsp);
    REQUIRE(rsp->kind == ResponseKind::AckThen);
}
```

### Unit test — handler correctness (`test_handlers.cpp`)

```cpp
TEST_CASE("handler: handleMoveContinuous returns AckThen", "[handlers][burette]")
{
    auto rsp = burette_ops::handleMoveContinuous(Direction::LiqOut, 500);
    REQUIRE(rsp);
    REQUIRE(rsp->kind == ResponseKind::AckThen);
}

TEST_CASE("handler: handleMoveContinuous missing param returns error", "[handlers][burette]")
{
    auto rsp = burette_ops::handleMoveContinuous(std::nullopt, std::nullopt);
    REQUIRE(rsp);
    REQUIRE(rsp->kind == ResponseKind::Error);
}

TEST_CASE("handler: handleMoveContinuous zero freq returns error", "[handlers][burette]")
{
    auto rsp = burette_ops::handleMoveContinuous(Direction::LiqIn, 0);
    REQUIRE(rsp);
    REQUIRE(rsp->kind == ResponseKind::Error);
}
```

### Unit test — command parser (`test_command.cpp`)

```cpp
TEST_CASE("parseCommand: burette.moveToStop parses freq field", "[command]")
{
    auto cmd = parseCommand(
        R"({"cmd":"burette.moveToStop","direction":"LIQ_IN","freq":300})");
    REQUIRE(cmd);
    REQUIRE(cmd->type == CommandType::MoveToStop);
    REQUIRE(cmd->direction == Direction::LiqIn);
    REQUIRE(cmd->freqHz);
    REQUIRE(*cmd->freqHz == Catch::Approx(300.0f));
}

TEST_CASE("parseCommand: burette.moveToStop parses freq_hz field", "[command]")
{
    auto cmd = parseCommand(
        R"({"cmd":"burette.moveToStop","direction":"liq_out","freq_hz":500})");
    REQUIRE(cmd);
    REQUIRE(cmd->type == CommandType::MoveToStop);
    REQUIRE(cmd->direction == Direction::LiqOut);
    REQUIRE(cmd->freqHz);
    REQUIRE(*cmd->freqHz == Catch::Approx(500.0f));
}
```

## Edge cases

- **Zero frequency**: handleMoveContinuous returns `"invalid_params"`
- **Busy state**: `sendMotorCommand()` with `timeout=0` returns false → `"busy"`
- **Stop during continuous motion**: `handleStop()` sets `gStopFull=true` → `move_to_endstop()` checks flag on every chunk → exits cleanly
- **Limit switch during continuous motion**: `stepper.moveStepsIntervals()` returns `StepperError::LimitSwitchTriggered` → `move_to_endstop()` breaks loop
