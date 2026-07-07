---
type: Architecture Reference
title: Coding Style Guide
description: Coding conventions, error hierarchy, state machine patterns, memory budget, and concurrency rules for ecotiter C++23 firmware
tags: [coding-style, conventions, architecture, c++23]
timestamp: 2026-07-07
---

# Coding Style Guide (C++23 / ESP-IDF v6)

Based on proven conventions from the Rust project, adapted for C++23 + ESP-IDF v6,
and extended with ESP32-S3 embedded best practices.

## 1. Layered Architecture

Four layers with strict one-way dependency: `domain` -> `application` -> `infrastructure` -> `interface`.

**Golden rule:** `domain/` must NEVER include `esp_*.h` headers. Only `infrastructure/` talks to hardware. `application/` coordinates using domain types and infrastructure traits.

## 2. Error Hierarchy

Three-level typed errors with `std::expected`:

```cpp
// domain/errors.hpp
enum class StepperError { InitFailed, Rmt, LimitSwitchTriggered, Timeout };
enum class SensorError { AdcReadFailed, TempSensorNotDetected, TempReadGlitch };
enum class NetworkError { WifiConnectionFailed };

enum class HardwareError { StepperMotor, Sensor, Network };
enum class ProtocolError { InvalidJson, UnknownCommand, MissingParam };
enum class StateError { Busy, InvalidTransition, AlreadyRunning };
enum class ResourceError { NvsOpenFailed, OutOfMemory };

enum class AppError { Hardware, Protocol, State, Resource };
```

```cpp
// Domain-layered error type with std::expected
using Result = std::expected<T, E>;  // alias in domain/errors.hpp

[[nodiscard]] Result<void, StepperError> moveSteps(Steps steps, Hz speed);
[[nodiscard]] Result<float, SensorError> readTemperature();
```

### Recovery Strategy

| Error | Recoverable | Action |
|-------|-------------|--------|
| `NetworkError` | Yes | Retry after STA_RECONNECT_INTERVAL |
| `StepperError::Rmt` | Yes | Retry chunk transmission |
| `StepperError::Timeout` | No | Enter error state, require manual reset |
| `StateError` | No | Fix caller logic |
| `ProtocolError` | No | Log and ignore malformed command |

## 3. Enum over Inheritance (Choose Deliberately)

### When to use enums

```cpp
// GOOD: Enum for state machines, commands, errors
enum class Direction : uint8_t { Cw, Ccw };
enum class BuretteState { Idle, Homing, Filling, Emptying, Dosing, Rinsing, Stopping, Error };
```

### When to use classes / interfaces

```cpp
// GOOD: Abstract interface for hardware abstraction (mockable in tests)
class StepperMotor {
public:
    virtual ~StepperMotor() = default;
    virtual Result<void, StepperError> moveSteps(Steps steps, Hz speed) = 0;
    virtual Result<void, StepperError> stop() = 0;
    virtual Steps position() const noexcept = 0;
};

// BAD: Class hierarchy for what should be an enum
class BuretteState { virtual void process() = 0; };
class IdleState : public BuretteState { /* use enum instead! */ };
```

## 4. State Machine: Explicit Enum + Exhaustive switch

```cpp
// Type-safe state
enum class BuretteState {
    Idle,
    Homing,
    Filling,
    Emptying,
    Dosing,
    Rinsing,
    Stopping,
    Error
};

enum class BuretteCommand {
    Fill,
    Dose,
    Stop,
    Reset
};
```

Validation pipeline order:
1. **Safety** -- limit switches, valve position
2. **Concurrency** -- return `Busy` if already moving
3. **State Logic** -- reject invalid transitions via exhaustive switch
4. **Parameter Validation** -- bounds, ranges
5. **Execution** -- perform the action

```cpp
Result<void, AppError> handleCommand(BuretteState state, BuretteCommand cmd) {
    switch (state) {
    case BuretteState::Idle:
        switch (cmd) {
        case BuretteCommand::Fill: return doFill();
        case BuretteCommand::Dose: return doDose();
        default: return std::unexpected(AppError::State(StateError::InvalidTransition));
        }
    case BuretteState::Filling:
        if (cmd == BuretteCommand::Fill) {
            return std::unexpected(AppError::State(StateError::AlreadyRunning));
        }
        [[fallthrough]];
    default:
        if (cmd == BuretteCommand::Stop) return doStop();
        return std::unexpected(AppError::State(StateError::InvalidTransition));
    }
}
```

## 5. Memory: Budget Enforcement

Fixed-size buffers for all hot paths. No heap allocations in main loop or motor thread.

```cpp
// domain/memory.hpp -- central budget definition
namespace ecotiter::domain::memory {
    inline constexpr size_t MAX_CMD_SIZE   = 256;
    inline constexpr size_t MAX_RSP_SIZE   = 512;
    inline constexpr size_t LOG_BUF_SIZE   = 100;
    inline constexpr size_t ADC_BUF_SIZE   = 64;
    inline constexpr size_t DNS_BUF_SIZE   = 512;

    using CommandBuffer  = std::array<uint8_t, MAX_CMD_SIZE>;
    using ResponseBuffer = std::array<uint8_t, MAX_RSP_SIZE>;
}
```

```cpp
// GOOD: Fixed-size buffers for hot paths
std::array<uint16_t, ADC_BUF_SIZE> adc_buf;
std::array<uint8_t, DNS_BUF_SIZE> dns_buf;

// ALLOWED: std::vector at init or config-change time only
auto intervals = computeRamp(total_steps, config); // std::vector -- computed once

// BAD: std::vector/std::string allocation in main loop or motor thread
loop {
    auto data = std::vector<uint32_t>{};  // NO -- use fixed buffer
}
```

## 6. Concurrency: Atomics + Queues, Never Block Main Loop

### Golden Rule

The main loop (FreeRTOS `main` task) must **never** execute a blocking operation.

| Context | Allowed | Forbidden |
|---------|---------|-----------|
| Main loop | `std::atomic` read/write, `try_lock()`, poll functions, `vTaskDelayUntil(10ms)` | `rmt_tx_wait_all_done()`, `lock()`, `xQueueReceive()`, `vTaskDelay()` > 10ms |
| Motor thread | `rmt_tx_wait_all_done()`, blocking RMT | Mutex contention |
| HTTP server task | Blocking writes to socket fd | Long computations |
| BLE notify thread | `xQueueReceive()`, `ble_gatts_notify()` | Main loop blocking |

### Atomic Ordering

```cpp
struct MotorState {
    std::atomic<int32_t> target{0};
    std::atomic<int32_t> current{0};
    std::atomic<bool> enabled{false};

    void updateCurrent(int32_t pos) noexcept {     // motor thread (writer)
        current.store(pos, std::memory_order_release);
    }
    int32_t getCurrent() const noexcept {           // main loop (reader)
        return current.load(std::memory_order_acquire);
    }
    bool tryEnable() noexcept {                     // compare-and-swap
        bool expected = false;
        return enabled.compare_exchange_strong(expected, true,
            std::memory_order_acq_rel, std::memory_order_acquire);
    }
};
```

### Mutex Safety
- `try_lock()` only in main loop (never `lock()`)
- `std::mutex` only for shared resources between tasks
- Never hold a mutex across a blocking call

```cpp
// GOOD:
std::scoped_lock lock(wifi_mutex, std::try_to_lock);
if (lock) { wifi.process(); }

// BAD:
std::scoped_lock lock(wifi_mutex); // may block HTTP task
```

## 7. Types and Constants

```cpp
// GOOD: Strongly-typed wrappers for domain units
struct Steps { int32_t value; };
struct Hz { uint32_t value; };
struct Ml { float value; };
struct MlMin { float value; };

// GOOD: Named constants, no magic numbers
inline constexpr auto AP_SSID = "EcoTiter-AP";
inline constexpr size_t MOTOR_THREAD_STACK = 16384;
inline constexpr size_t HTTP_SERVER_STACK = 12288;
inline constexpr uint32_t RMT_RESOLUTION = 1'000'000;

// BAD: Magic numbers
stepper.moveSteps(Steps{64}, Hz{2000});  // what is 64? what is 2000?
```

## 8. Logging

```cpp
// ESP-IDF esp_log wrapped in C++ logger
static constexpr auto TAG = "stepper";
ESP_LOGI(TAG, "Move completed: %ld steps", steps.value);
ESP_LOGW(TAG, "ADC read error on channel %d", channel);
ESP_LOGE(TAG, "Limit switch triggered, emergency stop");
```

## 9. ESP32-S3 Special Rules

### 9.1 WDT
Must be disabled at boot -- `rmt_tx_wait_all_done()` blocks > 250 ms.

```cpp
// Use safe wrapper only
esp_safe::disableWdt();  // calls esp_task_wdt_deinit() once at boot
```

### 9.2 RMT Stepper
- `rmt_new_tx_channel()` wrapped in `RmtChannel` RAII class
- `rmt_tx_wait_all_done()` **only in motor thread**, never in main loop

### 9.3 GPIO Pins
- TMC2209 EN is active LOW -- call `gpio_set_level(en, 0)` in constructor

### 9.4 Low-Level Operations
- All MMIO/ISR/raw-pointer functions: `_raw` or `_isr` suffix
- Preceding `// CONTRACT:` comment explaining invariants
- RAII wrappers for all ESP-IDF handles

### 9.5 Thread Stack Sizes

| Thread | Stack | Notes |
|--------|-------|-------|
| Main loop | 32 KB | CONFIG_ESP_MAIN_TASK_STACK_SIZE=32768 |
| Motor (RMT stepper) | 16 KB | Increased from 4 KB -- stack overflow on homing |
| Temperature (DS18B20) | 16 KB | Bitbang call chain |
| BLE notify | 8 KB | |
| HTTP server | 12 KB | stack_size: 12288 |
| std::thread default | 8 KB | CONFIG_PTHREAD_TASK_STACK_SIZE_DEFAULT=8192 |

## 10. Testing

See `docs/guides/testing.md` for the 3-tier strategy.

## 11. Documentation

### When to write comments
- Incorrect usage can **damage hardware** or cause WDT reset
- **Semantics differ from signature** (side effects, ordering)
- Function has **preconditions** the caller must satisfy

```cpp
/// Feed the Task Watchdog Timer to prevent ESP32-S3 reset.
/// Must be called at least every ~5 s (default WDT timeout).
void feedWatchdog();
```

### When NOT to write comments
- Trivial getters (`bool isEnabled() const noexcept`)
- Self-explanatory enum variants (`Direction::Cw`, `Error::Busy`)

## 12. Anti-Patterns (Forbidden Patterns)

```cpp
// BAD: Global mutable state
static AppState globalState;  // no!

// GOOD: Pass via queues or std::shared_ptr<Mutex>
```

```cpp
// BAD: Blocking in main loop
loop { http_client.get("/api/data"); /* BLOCKS! */ }

// GOOD: Non-blocking polling
loop {
    if (auto response = http_client.tryPoll()) { process(response); }
    vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(10));
}
```

```cpp
// BAD: Ignoring errors
void readSensor() { adc.read(); }

// GOOD: Proper error propagation
Result<float, SensorError> readSensor() {
    auto result = adc.read();
    if (!result) return std::unexpected(SensorError::AdcReadFailed);
    return *result;
}
```

```cpp
// BAD: Magic numbers
stepper.moveSteps(Steps{64}, Hz{2000});

// GOOD: Named constants
stepper.moveSteps(STEPS_HOMING, Hz{HOMING_SPEED_HZ});
```

## 13. Pre-Merge Checklist

### Architecture
- [ ] Code follows layered architecture (`domain/` -> no `esp_*.h` includes)
- [ ] State machine uses exhaustive switch on enum
- [ ] No circular dependencies between components

### Memory
- [ ] No `std::vector`/`std::string` in main loop or motor thread
- [ ] `std::array` for all hot paths
- [ ] All buffers have fixed sizes (defined in `memory.hpp`)

### Concurrency
- [ ] Main loop has NO blocking operations
- [ ] Correct `std::memory_order` semantics (Release/Acquire/AcqRel)
- [ ] `try_lock()` used in main loop, never `lock()`

### Error Handling
- [ ] No ignored return values (use `[[nodiscard]]`)
- [ ] Errors use typed hierarchy (`AppError` -> typed enums)
- [ ] Every `// CONTRACT:` comment present on low-level operations

### Testing
- [ ] `ctest --output-on-failure` passes
- [ ] `clang-tidy -p build/` -- 0 warnings
- [ ] `idf.py build` -- 0 errors, 0 warnings

## Summary

| Principle | Rule |
|-----------|------|
| **Layers** | `domain` -> `application` -> `infrastructure` -> `interface`. One-way deps only. |
| **Error Handling** | Typed enums + `std::expected`. No ignored errors. |
| **Memory** | Fixed-size `std::array` for hot paths. `std::vector` only at init/config-change. |
| **State Machine** | Explicit enum + exhaustive switch. Pipeline: Safety -> Busy -> State -> Params -> Execute. |
| **Concurrency** | `std::atomic` for ISR->task. FreeRTOS queues for task->task. **Never block main loop.** `try_lock()`. |
| **Types** | Strong-typed wrappers for domain units (`Steps`, `Hz`, `Ml`). Named constants -- no magic numbers. |
| **Testing** | Host units (Catch2) + on-device integration + pytest HIL. |
| **Anti-patterns** | No global state, no blocking in main loop, no ignored errors, no magic numbers. |
