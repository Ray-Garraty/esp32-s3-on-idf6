---
type: Code Review
title: Codebase compliance review against updated docs
description: Systematic review of all C++23 source code against the requirements in AGENTS.md, docs/refs/coding_style.md, and docs/refs/project.md
tags: [review, compliance, gpio, wdt, raii, init-order, rmt]
timestamp: 2026-07-10
---

# Code Review: Compliance Against Updated Documentation

Reviewed all source files (`main/`, `components/`) against the updated rules in
AGENTS.md (2026-07-10), docs/refs/coding_style.md (2026-07-10), and
docs/refs/project.md (2026-07-10). Audit date: 2026-07-10.

## Review checklist

### GR-1: Never Block Main Loop ✅ PASS

| Requirement | Status | Evidence |
|-------------|--------|----------|
| No `rmt_tx_wait_all_done()` in main loop | ✅ | Only in `motor_task.cpp` |
| No `vTaskDelay()` > 10 ms in main loop | ✅ | Uses `vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(10))` — periodic, non-blocking |
| No `xQueueReceive(portMAX_DELAY)` | ✅ | BLE queue uses `xQueueReceive(..., 0)` (poll) at `main/main.cpp:415` |
| No `std::mutex::lock()` | ✅ | No mutex usage found in any component |
| Only `try_lock()` if mutex used | ✅ (N/A) | Codebase uses atomics + queues exclusively |
| Blocking ops in dedicated threads | ✅ | `ensureGpioReady()` (1 s delay) in net_owner, `rmt_tx_wait_all_done()` in motor task |

### GR-2: Mandatory RMT Stop Flags ✅ PASS

| Requirement | Status | Evidence |
|-------------|--------|----------|
| `moveStepsIntervals()` accepts `std::atomic<bool>*` | ✅ | `stepper.hpp:45-47`: `stopFlag = nullptr` |
| Check flag between chunks | ✅ | `stepper.cpp` loops check `stopFlag && stopFlag->load(memory_order_acquire)` |
| Call sites pass stop flag | ✅ | `motor_task.cpp` passes `&domain::gStopFull` at both call sites |

### GR-3: DRAM Init Order ✅ PASS

| Step | Actual Code | Line |
|------|-------------|------|
| 1. `wifi.init()` | ✅ `wifiManager.init()` | `main/main.cpp:86` |
| 2. `wifi.startAP()` | ✅ `wifiManager.startAP()` | `main/main.cpp:92` |
| 3. `wifi.tryStartSTA()` | ✅ `wifiManager.tryStartSTA()` | `main/main.cpp:95` |
| 4. HTTP server | ✅ `HttpServer.init()` → `registerRoutes()` | `main/main.cpp:103-105` |
| 5. BLE init | ✅ `bleManager.init()` | `main/main.cpp:115` |
| 6. `ensureGpioReady()` | ✅ after BLE init in net_owner | `main/main.cpp:127` |

All steps execute in the `net_owner` FreeRTOS task (`netTaskEntry`), confirming
the documented "net_owner thread" architecture.

### GR-4: Coexistence — Never Prefer BT ✅ PASS

No `esp_coex_preference_set(ESP_COEX_PREFER_BT)` found anywhere in the C++
codebase. Default `ESP_COEX_PREFER_BALANCE` is used.

### GR-5: No Raw ESP-IDF Pointers Across Tasks ✅ PASS

- WebSocket uses `HttpServer::broadcastWsEvent()` which wraps `httpd_ws_send_frame_async()`
- No stored `httpd_req_t*` or `httpd_ws_frame_t*` across threads
- BLE command data copied into `BleCmdItem` struct (fixed-size `std::array<char, 256>`)
- Serial command data handled via `SerialReader::process()` — completes within callback

### GR-6: Stack Budget Is Law ✅ PASS

All stack sizes match the budget table in `project.md §Thread Architecture`:

| Thread | Constant | Value |
|--------|----------|-------|
| Main loop | `CONFIG_ESP_MAIN_TASK_STACK_SIZE` | 32768 (32 KB) ✅ |
| Motor task | `domain::MOTOR_THREAD_STACK` | 16384 (16 KB) ✅ |
| net_owner | `domain::NET_OWNER_STACK` | 16384 (16 KB) ✅ |
| Temp | `domain::TEMP_THREAD_STACK` | 16384 (16 KB) ✅ |
| BLE notify | `domain::BLE_NOTIFY_STACK` | 8192 (8 KB) ✅ |
| HTTP server | `domain::HTTP_SERVER_STACK` | 12288 (12 KB) ✅ |

All tasks created via `xTaskCreate` with these stack sizes at `main/main.cpp:217-250`.

### GR-7: Mandatory Diagnostic Instrumentation ⚠️ PARTIAL (1 gap)

| Instrumentation | Status | Usage |
|-----------------|--------|-------|
| `FfiGuard` at FFI boundaries | ✅ PASS | Used in wifi.cpp (7), http_server.cpp (16), ble.cpp (7), motor_task.cpp (10+), temp_thread.cpp |
| `assert_rmt_preconditions()` before RMT | ✅ PASS | `motor_task.cpp:89,171` |
| `StackMonitor::register_thread()` for new threads | ✅ PASS | `main/main.cpp:326` (implicit), `netTaskEntry:82`, `temp_thread.cpp:20`, `motor_task.cpp` |
| `StateTracer::logBuretteTransition()` on state change | ✅ PASS | `motor_task.cpp:68,115,147,190,290` |
| `HeapSnapshot::assert_can_allocate()` for large allocs | ❌ **NEVER CALLED** | `HeapSnapshot` class exists but `assert_can_allocate()` is never invoked anywhere |
| `TickWatchdog` in main loop body | ✅ PASS | `main/main.cpp:291` — RAII wrapper wraps each iteration |

**Issue:** `HeapSnapshot::assert_can_allocate()` is documented as mandatory for
every allocation > 4 KB (GR-7) but is never called in any source file. Not a
runtime bug — the HTTP server and BLE handle allocation failures gracefully —
but violates GR-7 requirement.

### RAII for ESP-IDF Handles (§9.5) ⚠️ PARTIAL (2 gaps)

| Handle | Wrapped? | File |
|--------|----------|------|
| `rmt_channel_handle_t` (stepper) | ✅ `RmtChannel` class | `stepper.hpp:18-33` |
| `rmt_channel_handle_t` (RGB LED) | ❌ **`void*` cast** | `rgb_led.hpp:31`: `void* channel_{nullptr}` |
| `rmt_encoder_handle_t` (stepper) | ❌ **Raw pointer** | `stepper.hpp:63`: `rmt_encoder_handle_t encoder_ = nullptr` |
| `rmt_encoder_handle_t` (RGB LED) | ❌ **`void*` cast** | `rgb_led.hpp:32`: `void* encoder_{nullptr}` |
| `httpd_handle_t` | ✅ `HttpServer` class | `http_server.hpp` |
| `adc_oneshot_unit_handle_t` | ✅ destructor calls `adc_oneshot_del_unit()` | `adc.hpp` |
| `nvs_handle_t` | ✅ `NvsHandle` class | `nvs.hpp` |

**Issue 1:** `StepperMotor::encoder_` (`stepper.hpp:63`) is a raw
`rmt_encoder_handle_t`. The destructor at `stepper.cpp:92` does call
`rmt_del_encoder(encoder_)`, but the handle is not wrapped in a standalone
RAII class. Violates `§9.5: "All ESP-IDF C handles MUST be wrapped in RAII
classes — never used naked."`

**Issue 2:** `RgbLed` stores RMT handles as `void*` (`rgb_led.hpp:31-32`),
casting to/from `rmt_channel_handle_t` and `rmt_encoder_handle_t`. This
defeats type safety. The destructor does call `rmt_del_channel()` /
`rmt_del_encoder()`, but the pattern violates §9.5.

### Mutex Safety ✅ PASS (stronger than required)

No `std::mutex::lock()` calls in the codebase. The project uses only
`std::atomic` and FreeRTOS queues for synchronization — a more restrictive
pattern than the docs require. This is acceptable.

### sdkconfig / Brownout ❌ FAIL (1 gap)

`CONFIG_BROWNOUT_DET=n` is **NOT** set in `sdkconfig.defaults`. The brownout
detector is in its default (enabled) state. Both `AGENTS.md §4.3` and
`project.md §WDT & Brownout` document that it should be disabled. Must be
added.

### WDT Configuration ✅ PASS

| Watchdog | State | Config source |
|----------|-------|---------------|
| TWDT (Task WDT) | ✅ Enabled, 10 s | `CONFIG_ESP_TASK_WDT_INIT=y`, `CONFIG_ESP_TASK_WDT_TIMEOUT_S=10` |
| IWDT (Interrupt WDT) | ✅ Enabled, 500 ms | `CONFIG_ESP_INT_WDT=y`, `CONFIG_ESP_INT_WDT_TIMEOUT_MS=500` |
| RWDT (RTC WDT) | ✅ Configured 6 s | `RtcWatchdog` RAII at `main/main.cpp:184` |

No `esp_task_wdt_deinit()` calls — matches the requirement.

### Error Hierarchy ✅ PASS

| Enum | Variants | Status |
|------|----------|--------|
| `StepperError` | `InitFailed`, `Rmt`, `LimitSwitchTriggered`, `LimitSwitchReached`, `Timeout` | ✅ Complete |
| `SensorError` | `AdcReadFailed`, `TempSensorNotDetected`, `TempReadGlitch` | ✅ Complete |
| `NetworkError` | `WifiConnectionFailed` | ✅ Complete |
| `HardwareError` | `StepperMotor`, `Sensor`, `Network` | ✅ Complete |
| `ProtocolError` | `InvalidJson`, `UnknownCommand`, `MissingParam` | ✅ Complete |
| `StateError` | `Busy`, `InvalidTransition`, `AlreadyRunning` | ✅ Complete |
| `ResourceError` | `NvsOpenFailed`, `OutOfMemory` | ✅ Complete |
| `AppError` | `Hardware`, `Protocol`, `State`, `Resource` | ✅ Complete |

`using Result = std::expected<T, E>` — ✅ present in `errors.hpp:58`.
All error-returning functions use `[[nodiscard]]`.

### Memory Budget ✅ PASS

| Buffer | Size | Status |
|--------|------|--------|
| `CommandBuffer` | 256 B | ✅ defined in `memory.hpp:14` |
| `ResponseBuffer` | 512 B | ✅ defined in `memory.hpp:15` |
| `DnsBuf` | 512 B | ✅ defined in `memory.hpp:17` |
| `AdcBuf` | 64×uint16_t | ✅ defined in `memory.hpp:16` |
| `LOG_BUF_ENTRIES` | 100 | ✅ defined in `memory.hpp:10` |

No `std::vector`/`std::string` in hot paths. `std::string` appears only in
NVS storage layer (`getStr()`) which is init/config-path only.

### No Forbidden Patterns

| Forbidden Pattern | Found? |
|-------------------|--------|
| `rmt_tx_wait_all_done()` in main loop | ❌ Not found ✅ |
| `std::mutex::lock()` in main loop | ❌ Not found ✅ |
| `std::format()` in hot paths | ❌ Not used at all ✅ |
| `httpd_req_t*` across threads | ❌ Not found ✅ |
| `esp_coex_preference_set(PREFER_BT)` | ❌ Not found ✅ |
| `python -c "..."` inline | ❌ Not found ✅ |
| `WRITE_PERI_REG` for brownout | ❌ Not found ✅ |
| `mdns_init()` before IP | ❌ Not found ✅ |
| Naked `httpd_handle_t` | ❌ Not found ✅ |

## Common issues

### 1. `rmt_encoder_handle_t` not RAII-wrapped (MEDIUM)

`stepper.hpp:63` stores `rmt_encoder_handle_t` as a raw pointer. While the
destructor correctly calls `rmt_del_encoder()`, the pattern violates §9.5:

```cpp
// stepper.hpp:61-66 (CURRENT — raw pointer)
private:
    RmtChannel channel_;
    rmt_encoder_handle_t encoder_ = nullptr;  // NAKED — violates §9.5
```

Fix: Wrap in a minimal `RmtEncoder` RAII class alongside `RmtChannel`.

### 2. `RgbLed` uses `void*` for RMT handles (MEDIUM)

`rgb_led.hpp:31-32` stores RMT handles as `void*` with casts in the
implementation. Defeats type safety:

```cpp
// rgb_led.hpp:31-32 (CURRENT — void* casts)
void* channel_{nullptr};
void* encoder_{nullptr};
```

Fix: Use proper `rmt_channel_handle_t` / `rmt_encoder_handle_t` types.

### 3. `CONFIG_BROWNOUT_DET=n` missing from `sdkconfig.defaults` (HIGH)

Brownout detector remains enabled, which can cause unexpected resets during
WiFi/BLE transient current spikes. Documented as required in AGENTS.md §4.3
and project.md §WDT & Brownout.

### 4. `HeapSnapshot::assert_can_allocate()` never called (LOW)

GR-7 mandates this call before every allocation > 4 KB, but the function is
never invoked. No runtime risk (allocs fail gracefully), but code is
incomplete per GR-7.

## Notes

**Positive findings:**
- GR-1, GR-2, GR-3, GR-4, GR-5 compliance is strong — all critical safety
  rules are properly implemented
- Diagnostic instrumentation is thorough (FfiGuard, StackMonitor, StateTracer,
  TickWatchdog all present)
- Error hierarchy matched the updated docs exactly
- Stack budget is precise and matches the documented table
- Init order follows the documented 6-step sequence
- No forbidden patterns found
- The codebase is free of `std::mutex::lock()` entirely — more conservative
  than the docs require

**Summary of gaps:**

| # | Severity | Issue | Applies to |
|---|----------|-------|------------|
| 1 | HIGH | `CONFIG_BROWNOUT_DET=n` not set in `sdkconfig.defaults` | sdkconfig |
| 2 | MEDIUM | `rmt_encoder_handle_t` not RAII-wrapped (`stepper.hpp:63`) | stepper |
| 3 | MEDIUM | `RgbLed` stores RMT handles as `void*` (`rgb_led.hpp:31-32`) | rgb_led |
| 4 | LOW | `HeapSnapshot::assert_can_allocate()` never called (GR-7) | diag |
