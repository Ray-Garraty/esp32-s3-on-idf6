# AGENTS.md — AI Agent Rules (ESP32-S3 + C++23 + ESP-IDF v6)

This file defines non-negotiable rules for AI coding agents working on this
firmware. Violations of CRITICAL rules invalidate all changes and require
immediate revert. Every rule is derived from a real post-mortem or hardware
failure — read them before generating any code.

## 1. CRITICAL RULES (Auto-Revert)

These rules are non-negotiable. Any generated code violating them must be
rejected by the agent itself before being shown to the user.

### GR-1: NEVER BLOCK THE MAIN LOOP

The main loop (FreeRTOS task `main`) must NEVER execute a blocking operation.
Any call that can block for > 10 ms MUST live in a dedicated thread/task.

**Forbidden in main loop:**
- `rmt_tx_wait_all_done()` (RMT)
- `vTaskDelay()` > 10 ms (10 ms pacing tick is the only exception)
- Blocking receive on any queue (`xQueueReceive` with `portMAX_DELAY`)
- `std::mutex::lock()` — use `try_lock()` only
- Any synchronous I/O (blocking `read()`, blocking `send()`)

**Allowed in main loop:**
- `std::atomic<T>::load/store/fetch_*`
- `std::mutex::try_lock()` with immediate `false` handling
- Non-blocking `poll()` / `process()` functions
- `vTaskDelay(pdMS_TO_TICKS(10))` — pacing tick only
- Writes to pre-opened non-blocking file descriptors

**Violation -> all changes invalidated, immediate revert.**

Failure (2026-07-03): Homing called `rmt_tx_wait_all_done()` in main,
blocking for 11 s. WiFi/BLE init never ran.

---

### GR-2: MANDATORY RMT STOP FLAGS

Every RMT motion function MUST accept and check a `std::atomic<bool>*`
stop flag between chunks. If set -> motion aborts immediately.

```cpp
// infrastructure/drivers/stepper.hpp
[[nodiscard]] std::expected<void, StepperError> move_steps_intervals(
    std::span<const uint32_t> intervals,
    std::atomic<bool>* stop_flag = nullptr
) {
    for (auto chunk : split_into_chunks(intervals, CHUNK_SIZE)) {
        if (stop_flag != nullptr &&
            stop_flag->load(std::memory_order_acquire)) {
            std::ignore = emergency_stop();
            return std::unexpected(StepperError::LimitSwitchTriggered);
        }
        auto result = rmt_transmit_wait(chunk);
        if (!result) return std::unexpected(result.error());
    }
    return {};
}
```

Failure (2026-07-03): Homing omitted stop flag. Motor ran through FULL limit switch.

---

### GR-3: DRAM INIT ORDER (The Triangle)

ESP-IDF v6 on ESP32 has a three-way DRAM conflict. Init order MUST be:
1. WiFi driver init (low DRAM cost, ~3.5 KB)
2. HTTP server task (needs 12 KB contiguous MALLOC_CAP_INTERNAL)
3. BLE NimBLE host (needs 12 KB contiguous MALLOC_CAP_INTERNAL)

WiFi must obtain IP before HTTP server binds to 0.0.0.0:80.

Forbidden reorderings:
- HTTP -> BLE -> WiFi (7+ s HTTP latency, lwIP routing delay)
- BLE -> WiFi -> HTTP (ESP_ERR_HTTPD_TASK — xTaskCreate fails)
- WiFi -> BLE -> HTTP (wifi:fail to alloc timer, type=9)

Required pattern in net_owner thread:

```cpp
// infrastructure/network/net_owner.cpp
auto result = wifi_.init();
if (!result) return std::unexpected(result.error());

result = wifi_.wait_for_ip(std::chrono::seconds(5));
if (!result) return std::unexpected(result.error());

http_server_ = HttpServer::create();
if (!http_server_) return std::unexpected(http_server_.error());

result = ble_.init();
if (!result) return std::unexpected(result.error());
```

Failures (2026-07-01): Each reordering fixed one issue but broke another.

---

### GR-4: COEXISTENCE POLICY — NEVER PREFER BT

Forbidden:

```cpp
esp_coex_preference_set(ESP_COEX_PREFER_BT);  // deprecated in ESP-IDF v6
```

Use default `ESP_COEX_PREFER_BALANCE` — the ESP32 coexistence arbitrator
automatically gives 50/50 airtime.

Failure (2026-07-01): `ESP_COEX_PREFER_BT` starved WiFi L2 of airtime.
STA associated, got IP, but 100% TCP packet loss.

---

### GR-5: NO RAW ESP-IDF POINTERS ACROSS TASK BOUNDARIES

Forbidden: Storing `httpd_req_t*`, `httpd_ws_frame_t*`, or any ESP-IDF
opaque pointer in `std::shared_ptr`, `std::atomic<T*>`, or passing them
across thread boundaries.

Once a C-side HTTP handler returns, ESP-IDF frees or recycles the request
structure. Any stored pointer -> dangling -> Guru Meditation: StoreProhibited.

Required: Use native WebSocket API (`ws_handler` + `httpd_ws_send_frame_async`)
for real-time streaming, OR copy data into `std::array<char, N>` before
leaving the C callback.

Failure (2026-07-02): SSE stored `std::shared_ptr<std::atomic<httpd_req_t*>>`
in main loop. Crash with EXCVADDR=0x28. Replaced with WebSocket API.

---

### GR-6: STACK BUDGET IS LAW

Every task creation MUST specify stack size from the approved budget table
below. No exceptions. No defaults.

| Thread | Stack Size | Notes |
|--------|-----------|-------|
| Main loop (FreeRTOS main task) | 32768 (32 KB) | CONFIG_ESP_MAIN_TASK_STACK_SIZE |
| net_owner (WiFi/BLE/HTTP lifecycle) | 16384 (16 KB) | GR-3 init order owner thread |
| Motor (RMT stepper + homing) | 16384 (16 KB) | Was 4 KB -> stack overflow on homing |
| Temperature (DS18B20 bitbang) | 16384 (16 KB) | Bitbang call chain is deep |
| BLE notify | 8192 (8 KB) | Host stack is 12288 (separate FreeRTOS task) |
| HTTP server (FreeRTOS internal) | 12288 (12 KB) | stack_size: 12288 mandatory |
| std::thread default | 8192 (8 KB) | CONFIG_PTHREAD_TASK_STACK_SIZE_DEFAULT |

Forbidden inside threads with stack <= 8 KB:
- `std::format()` / `std::print()` in loops (allocates internally)
- `nlohmann::json::dump()` without pre-allocated buffer
- Large stack-local arrays (`uint8_t buf[4096]`)
- Deep recursion

Mandatory check: After moving any code path between threads, verify with
`uxTaskGetStackHighWaterMark()` or at minimum log watermark before/after.

Failure (2026-07-03): Moving homing to 8 KB motor task caused stack
overflow. Increased to 16 KB.

---

### GR-7: MANDATORY DIAGNOSTIC INSTRUMENTATION

Every new function MUST have diagnostic instrumentation:

| What | Instrumentation | Rule |
|------|----------------|------|
| FFI boundary | `FfiGuard guard(boundary_id)` RAII | Must wrap every ESP-IDF C API call |
| RMT motion | `assert_rmt_preconditions()` | Before any `move_steps_intervals()` call |
| New thread | `StackMonitor::register_thread(name, stack_size)` | At thread creation |
| State transition | `StateTracer::log_burette_transition(old, new)` | On every state change |
| Large alloc (>4 KB) | `HeapSnapshot::assert_can_allocate(size)` | Before allocating contiguous DRAM |
| Main loop | `TickWatchdog watchdog` RAII | Wrap each iteration body |

Code generated without these instrumentation points is INCOMPLETE and must
be rejected.

Rationale (2026-07-03): Every crash in the post-mortem log could have been
detected pre-mortem by a diagnostic event. The black box costs 1 KB SRAM and
5 us per event, which is negligible for the insight gained.

---

## 2. PRE-FLIGHT CHECKLIST (Copy Before Codegen)

Before generating any code that touches: threads, network, RMT, FFI, mutexes,
queues, GPIO ISR, NVS, WiFi, BLE, or HTTP — copy and fill this block:

### Pre-Flight Checklist
1. **Thread:** \_\_\_\_\_\_\_\_ (Main/Motor/Temp/BLE/HTTP/net_owner)
2. **Blocking >10ms?** \_\_\_\_\_\_\_\_ (Yes -> move to worker: \_\_\_\_\_\_\_\_)
3. **Stack impact:** std::format/std::string/arrays/recursion? \_\_\_\_ Budget: \_\_\_\_ KB
4. **Init order dep:** WiFi IP / NimBLE / HTTP / none? \_\_\_\_
5. **FFI boundary:** Stores C pointers? \_\_\_\_ Copies before return? \_\_\_\_
6. **Stop flag:** RMT/motion? \_\_\_\_ (if yes: GR-2 REQUIRED)
7. **DRAM:** MALLOC_CAP_INTERNAL? \_\_\_\_ Position in init order? \_\_\_\_

If you cannot confidently fill this -> stop and ask the user.

---

## 3. EMBEDDED HARDWARE INVARIANTS

### 3.1 GPIO Pinout

| GPIO | Function | Constraint |
|------|----------|-----------|
| 1 | U0TXD | Serial output — DO NOT TOUCH |
| 3 | U0RXD | Serial input — DO NOT TOUCH |
| 2 | Status LED | `gpio_set_level()` — Active HIGH |
| 4 | ADC (pH electrode) | `adc_oneshot_read()` (ADC1_CH3) — 0-2900 mV range |
| 14 | Valve | `gpio_set_level()` — LOW=input, HIGH=output |
| 21 | TMC2209 STEP | `rmt_new_tx_channel()` — pulse train |
| 26 | TMC2209 DIR | `gpio_set_level()` — HIGH=CW |
| 27 | TMC2209 EN | `gpio_set_level()` — Active LOW |
| 32 | Endstop FULL | `gpio_install_isr_service()` + PosEdge ISR -> `std::atomic<bool>` |
| 33 | DS18B20 | OneWire bitbang — 4.7k pull-up |
| 35 | Endstop HOME | `gpio_install_isr_service()` + PosEdge ISR -> `std::atomic<bool>` |

### 3.2 RMT Stepper API (ESP-IDF v6 C API)

```cpp
// Creation (infrastructure/drivers/stepper.cpp)
rmt_channel_handle_t channel = nullptr;
rmt_tx_channel_config_t tx_config = { /* ... */ };
ESP_ERROR_CHECK(rmt_new_tx_channel(&tx_config, &channel));

rmt_encoder_handle_t encoder = nullptr;
rmt_copy_encoder_config_t enc_config = {};
ESP_ERROR_CHECK(rmt_new_copy_encoder(&enc_config, &encoder));

// Blocking transmit (MOTOR THREAD ONLY — never in main loop)
rmt_transmit_config_t tx_cfg = {};
ESP_ERROR_CHECK(rmt_transmit(channel, encoder, signal, signal_size, &tx_cfg));
ESP_ERROR_CHECK(rmt_tx_wait_all_done(channel, portMAX_DELAY));

// Cleanup (RAII destructor)
rmt_del_channel(channel);
rmt_del_encoder(encoder);

// EN pin active LOW: call gpio_set_level(en_pin, 0) in constructor.
```

### 3.3 WDT & Brownout

WDT must be disabled at boot — `rmt_tx_wait_all_done()` blocks > 250 ms.

```cpp
// Use safe wrapper only — never call esp_task_wdt_deinit() directly
#include "esp_safe.hpp"
esp_safe::disable_wdt();
```

Brownout detector: Disabled via sdkconfig.defaults:

```
CONFIG_BROWNOUT_DET=n
```

NOT via `WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0)` at runtime.

### 3.4 Peripheral Drivers

| Driver | File | Notes |
|--------|------|-------|
| TMC2209 | infrastructure/drivers/stepper.cpp | EN active LOW -> `gpio_set_level(en, 0)` in constructor |
| Endstops | infrastructure/drivers/limitswitch.cpp | GPIO32/35, PosEdge ISR, atomic flag |
| DS18B20 (1-Wire) | infrastructure/drivers/onewire.cpp | MMIO bitbang |
| NVS | infrastructure/storage/nvs.cpp | C API wrappers with RAII handles |

---

## 4. THREAD MODEL & STACK BUDGET

| Thread | Role | Stack | Constraint |
|--------|------|-------|-----------|
| main (FreeRTOS) | Pacing tick, dispatch, atomic reads | 32768 | GR-1: no blocking |
| net_owner | GR-3 init, WiFi/BLE/HTTP lifecycle | 16384 | Must follow GR-3 order |
| motor | RMT stepper, homing | 16384 | GR-2: mandatory stop flag |
| temp | DS18B20 bitbang reads | 16384 | Blocking reads OK here |
| ble_notify | BLE notify pushes | 8192 | No std::format / JSON dumps |
| httpd (FreeRTOS) | HTTP handlers | 12288 | GR-5: no stored C pointers |

Cross-thread communication:
- `std::atomic<bool>` for stop flags / status flags
- FreeRTOS QueueHandle_t (wrapped in RAII `Queue<T>` class) for event passing
- `std::mutex` only with `try_lock()` in main loop

---

## 5. NETWORK STACK

### 5.1 Init Order (GR-3)
See GR-3 above. WiFi -> HTTP -> BLE. Never reorder.

### 5.2 WiFi
Custom `esp_netif_t` for AP mode with `esp_netif_destroy_default_wifi()` +
custom netif creation before `esp_wifi_start()` — fixes DHCP on
192.168.4.1/24.

mDNS requires valid IP — call `mdns_init()` only after `IP_EVENT_STA_GOT_IP`
event / `wait_for_ip()` returns.

DNS responder: `bind()` UDP socket to AP_IP:53 first, fallback 0.0.0.0:53.

### 5.3 BLE / NimBLE
No local patches needed in C++ (the C API is stable across IDF v6).
Pre-init guard mandatory: `bool initialized_` in `BleManager`;
`process()` / `is_connected()` early-return if not initialized.

Never call `nimble_port_init()` before all GATT services are registered —
global state with internal mutexes, will deadlock.

3-level zombie defense:
- L1: 5 consecutive notify failures -> disconnect
- L2: `ble_gap_conn_active()` returns 0 but local flag set -> cleanup
- L3: immediate kill on notify with zero connections but flag set

### 5.4 HTTP Server
`stack_size: 12288` mandatory — prevents handler stack overflow.
Use WebSocket (`httpd_ws_send_frame_async`) for real-time streaming.
Never store `httpd_req_t*` across handler returns (GR-5).

### 5.5 Coexistence (GR-4)
Default `ESP_COEX_PREFER_BALANCE`. Never prefer BT (GR-4).

---

## 6. BUILD & CI

### 6.1 Commands

| Command | Purpose | Timeout |
|---------|---------|---------|
| `idf.py set-target esp32s3` | Configure target (run once) | 30 s |
| `idf.py build` | Build firmware | >= 120 s |
| `idf.py flash -p /dev/ttyACM0` | Flash firmware | >= 60 s |
| `timeout 30 idf.py monitor -p /dev/ttyACM0` | Smoke test | 30 s |
| `clang-tidy -p build/ src/**/*.cpp` | Linter — 0 warnings | 60 s |
| `clang-format -i -n src/**/*.cpp` | Format check | 15 s |
| `cd tests && ctest --output-on-failure` | Host unit tests (Catch2) | 60 s |

Build output: `build/ecotiter.elf`, `build/ecotiter.bin`

Flash verification: Confirmed only by "Flashing has completed!" message
from `idf.py flash` or successful boot sequence on serial monitor.

### 6.2 sdkconfig Policy
Edit only `sdkconfig.defaults` — never `sdkconfig` (auto-generated).
Never run `idf.py menuconfig` (not reproducible).
After changing defaults, run `idf.py reconfigure` to regenerate sdkconfig.

Key defaults:
```
CONFIG_IDF_TARGET="esp32s3"
CONFIG_ESP_MAIN_TASK_STACK_SIZE=32768
CONFIG_BROWNOUT_DET=n
CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE=12288
CONFIG_FREERTOS_HZ=1000
```

### 6.3 Partitions & Dependencies
Partition table in `partitions.csv` — do not change without explicit
approval (affects OTA, NVS layout).

Component dependencies (managed by CMake):
- `main` — application code
- `nlohmann_json` — header-only JSON (via FetchContent or IDF component registry)
- `catch2` — header-only tests (host build only)

No external package manager needed. ESP-IDF components via `idf_component.yml`
only when strictly required.

---

## 7. CRASH INVESTIGATION

Any ESP32 crash (Guru Meditation, StoreProhibited, LoadProhibited, stack
overflow, abort, WDT) is a RED ALERT.

### Diagnostic System
The firmware has a built-in diagnostic subsystem (`components/diag/`) that
intercepts ALL crash types via `__wrap_esp_panic_handler` (linker `--wrap`).
Crash output has a machine-parseable format:

```
=== CRASH ===
exccause=0 name=IllegalInstruction pc=0x40091100 excvaddr=0x0 ps=0x60730 sp=0x3fffcee0
=== REGISTERS ===
a0=0x800910c8 a1=0x3fffcee0 a2=0x0 a3=0x0 ...
=== BACKTRACE ===
0x400910fd:0x3fffcee0
0x400910c5:0x3fffcf00
...
=== BLACK BOX (64 events, newest first) ===
[822us] t4 FfiExit { boundary: 20, result: 0 }
[821us] t4 FfiEnter { boundary: 20 }
...
=== STACK ===
t0 main watermark=0
t1 motor watermark=0
...
!!! EXCEPTION END !!!
```

### Triage Pipeline
Step 1: Run the quickest available analysis:

| Scenario | Command |
|----------|---------|
| Serial log exists | `./scripts/analyze_last_crash.sh` |
| Live capture | `timeout 60 python3 scripts/serial_monitor.py` |
| Raw crash text | `cat crash.txt \| python3 scripts/crash_analyzer.py` |

### Known Patterns (from docs/lessons_learned.yaml)

| Signature | Real Cause | Fix |
|-----------|-----------|-----|
| A2=0xFFFFFFFC, EXCVADDR=0x0, tlsf_check, heap_caps_\*free | Stack overflow, NOT heap corruption (LL-001) | CONFIG_ESP_MAIN_TASK_STACK_SIZE=32768; check uxTaskGetStackHighWaterMark(NULL) FIRST |
| ESP_ERR_HTTPD_TASK (45064) | DRAM fragmentation | Init order GR-3; move BLE to net_owner |
| wifi:fail to alloc timer, type=9 | WiFi timer alloc after BLE+HTTP ate DRAM | Reduce WiFi buffer counts in sdkconfig.defaults |
| StoreProhibited EXCVADDR=0x28 | Dangling httpd_req_t (GR-5) | WebSocket API; never store C pointers |
| IllegalInstruction PC=0x40091100 + heap snapshot 6KB largest | DRAM fragmentation -> HTTP alloc failure -> lwIP after event loop drop (LL-004) | Keep event loop handle alive |

---

## 8. PROJECT CONVENTIONS

### 8.1 Coding Style (C++23)
- DRY, KISS, YAGNI.
- Prefer cohesive units over fragmented micro-modules.
- Extract non-obvious constants to `config.hpp` or top of file.
- Never break existing functionality or business logic.
- Never invent methods/hooks/APIs — verify against official ESP-IDF docs.
- Never fix symptoms; always find root cause.
- Never assert physical-world events — ask the user what they observe.
- Show only changed fragments with file paths.
- LF (`\n`) line endings. No trailing whitespace.
- All pipelines: 0 errors, 0 warnings — no "pre-existing" excuses.
- Two-strike rule: 2 attempts per task, then stop and consult user.
- Every `// NOLINT` suppression MUST have an English `// CONTRACT:` comment
  within the preceding 3 lines explaining why the lint cannot be satisfied
  by changing the code.

C++23 specific:
- Use `std::expected<T, E>` for error handling (C++23 `<expected>`).
- Use `std::print` / `std::format` (C++23 `<print>`, `<format>`) instead of printf.
- Use `enum class` (strongly typed) for all enumerations.
- Use `std::atomic<T>` with explicit `std::memory_order` (no default seq_cst).
- Use RAII for all ESP-IDF handles (`rmt_channel_handle_t`, `httpd_handle_t`, etc.).
- Use `[[nodiscard]]` on all functions returning `std::expected` or status codes.
- Use `std::span<T>` instead of pointer+size pairs.
- Use `constexpr` for compile-time constants.
- Use `std::unique_ptr` for sole ownership, `std::shared_ptr` only when shared ownership is genuinely required.
- Use `const` correctness everywhere possible.
- Use `noexcept` on functions that cannot throw (most embedded code).

### 8.2 Error Handling
Typed errors (`enum class` + `std::expected`) in library code.

```cpp
// errors.hpp
enum class StepperError {
    InitFailed,
    Rmt,
    LimitSwitchTriggered,
    Timeout
};

enum class HardwareError {
    StepperMotor,
    Sensor,
    Network
};

enum class AppError {
    Hardware,
    Protocol,
    State,
    Resource
};

// Function signatures
[[nodiscard]] std::expected<void, StepperError> move_steps(Steps steps, Hz speed);
[[nodiscard]] std::expected<float, SensorError> read_temperature();
```

Forbidden in production code:
- `std::abort()`, `std::terminate()`, `assert()` (use `ESP_ERROR_CHECK` for IDF calls)
- Raw `throw` without catch at top level
- Magic return codes (-1, 0, 1)

Error propagation: use `.transform_error()` or explicit checks:

```cpp
auto result = stepper.move_steps(steps, speed);
if (!result) {
    log_error("Stepper failed: {}", static_cast<int>(result.error()));
    return std::unexpected(AppError::Hardware);
}
```

Layer boundary rule: No ESP-IDF headers (`#include "esp_*.h"`) in `domain/` layer.
Only `infrastructure/` talks to hardware.

### 8.3 Low-Level Operations Policy
C++ does not have Rust's `unsafe` keyword. Instead, low-level operations
(raw pointers, MMIO, ISR handlers, ESP-IDF C API calls) are controlled via:

- Naming convention: All functions doing MMIO/ISR/raw-pointer work have
  `_raw` or `_isr` suffix (e.g., `write_register_raw()`, `gpio_isr_handler()`).
- `// CONTRACT:` comments: Every low-level operation MUST have a preceding
  comment explaining:
  - The invariant being maintained
  - The context (ISR, panic handler, boot-time)
  - The risk if violated
- RAII wrappers: All ESP-IDF handles MUST be wrapped in RAII classes
  with destructors that call the corresponding `*_del_*` / `*_deinit` function.

Enforcement:
- `clang-tidy` checks for `// CONTRACT:` presence on `_raw` / `_isr` functions.
- New low-level operations require justification in the commit message.
- Avoid low-level operations whenever possible. Prefer ESP-IDF's high-level APIs.

### 8.4 Logging & Firmware Versioning
- Backend: ESP-IDF `esp_log` API wrapped in C++ logger class.
- Levels: `ESP_LOG_ERROR`, `ESP_LOG_WARN`, `ESP_LOG_INFO`, `ESP_LOG_DEBUG`, `ESP_LOG_VERBOSE`.
- Tag: module path or component name convention.
- Firmware version: `PROJECT_VER` from `CMakeLists.txt`. Pushed to NVS
  `firmware_version` on first boot after OTA.

### 8.5 RAII for ESP-IDF Handles

```cpp
// CORRECT — RAII wrapper
class RmtChannel {
    rmt_channel_handle_t handle_ = nullptr;
public:
    explicit RmtChannel(const rmt_tx_channel_config_t& config) {
        ESP_ERROR_CHECK(rmt_new_tx_channel(&config, &handle_));
    }
    ~RmtChannel() {
        if (handle_) rmt_del_channel(handle_);
    }
    RmtChannel(const RmtChannel&) = delete;
    RmtChannel& operator=(const RmtChannel&) = delete;
    RmtChannel(RmtChannel&& other) noexcept : handle_(other.handle_) {
        other.handle_ = nullptr;
    }
    [[nodiscard]] rmt_channel_handle_t get() const noexcept { return handle_; }
};

// FORBIDDEN — naked handle
rmt_channel_handle_t channel = nullptr;
rmt_new_tx_channel(&config, &channel);
// ... forgot to call rmt_del_channel -> resource leak
```

---

## 9. SERIAL & PYTHON SAFETY

### 9.1 Serial Port Safety
- ABSOLUTELY FORBIDDEN to launch background processes that hold serial ports.
- Any blocking/serial tool MUST be run with an explicit timeout via
  `timeout <seconds>` prefix.
- Never leave a process occupying a serial port (ttyUSB/ttyACM) after exit.

```
# CORRECT
timeout 30 python3 scripts/serial_monitor.py

# FORBIDDEN — hangs forever
python3 -m serial.tools.miniterm /dev/ttyACM0 115200
```

### 9.2 Python Script Rules
- NEVER use `python -c "..."` inline scripts — bash on Windows mangles quotes/backslashes.
- ALWAYS write Python code to a temp file first, then run it.
- Use `/tmp/opencode` for temp scripts.

```
# CORRECT
cat > "$TMPDIR/test_serial.py" << 'PYEOF'
import serial
PYEOF
python "$TMPDIR/test_serial.py"

# FORBIDDEN
python -c "import serial; print(serial.Serial('/dev/ttyACM0'))"
```

---

## 10. FINAL COMMIT CHECKLIST

Before submitting any change, verify:
- [ ] `idf.py build` — 0 errors, 0 warnings
- [ ] `clang-tidy -p build/ src/**/*.cpp` — 0 warnings
- [ ] `cd tests && ctest --output-on-failure` — all host tests pass
- [ ] No `std::abort()` / `std::terminate()` / `assert()` in production
- [ ] No `std::string` / `std::vector` allocation in main loop or motor thread hot paths (use `std::array` + fixed buffers)
- [ ] Every `// NOLINT` has an English `// CONTRACT:` comment within preceding 3 lines
- [ ] No ESP-IDF headers in `domain/` layer
- [ ] Main loop has NO blocking operations (GR-1)
- [ ] Every RMT motion has a stop flag (GR-2)
- [ ] Init order follows GR-3 triangle (WiFi -> HTTP -> BLE)
- [ ] CONFIG_ESP_MAIN_TASK_STACK_SIZE=32768
- [ ] Motor thread stack = 16384 (not 4096/8192)
- [ ] Pre-Flight Checklist (x2) was filled out BEFORE code generation
- [ ] No new low-level operations without `// CONTRACT:` comment + commit justification
- [ ] No raw ESP-IDF pointers cross thread boundaries (GR-5)
- [ ] Frozen docs/API/SERIAL_API.md contract respected
- [ ] 30-second serial smoke test: no Guru Meditation, no WDT, no panics

---

## Appendix A: Forbidden Patterns Quick Reference

| Forbidden | Required | Rule |
|-----------|----------|------|
| `rmt_tx_wait_all_done()` in main loop | Move to motor thread | GR-1 |
| `std::mutex::lock()` in main loop | `try_lock()` with false handling | GR-1 |
| `std::format()` in hot paths | `std::array<char, N>` + `std::format_to` | GR-6 |
| `httpd_req_t*` across threads | WebSocket API | GR-5 |
| `esp_coex_preference_set(PREFER_BT)` | Default PREFER_BALANCE | GR-4 |
| Homing in main thread | Motor task (16 KB stack) | GR-1, GR-6 |
| BLE init before HTTP | GR-3: WiFi -> HTTP -> BLE | GR-3 |
| NimBLE init before service registration | Register all GATT services first | x5.3 |
| `mdns_init()` before IP assigned | Wait for `IP_EVENT_STA_GOT_IP` | x5.2 |
| `python -c "..."` inline | Write temp file, then run | x9.2 |
| Guessing stack size | Budget table in GR-6 | GR-6 |
| RMT motion without stop flag | Stop flag before start | GR-2 |
| `WRITE_PERI_REG` for brownout | `CONFIG_BROWNOUT_DET=n` | x3.3 |
| HTTP bind without IP | `wait_for_ip()` first | GR-3 |
| `esp_task_wdt_deinit()` direct | `esp_safe::disable_wdt()` | x3.3 |
| Naked `rmt_channel_handle_t` | RAII `RmtChannel` class | x8.5 |
| Functions returning -1 on error | `std::expected<T, Error>` | x8.2 |

## Appendix B: Reference Documentation

| Document | Purpose |
|----------|---------|
| docs/refs/project.md | Hardware pinout, thread model, error hierarchy, state machines, NVS layout |
| docs/refs/coding_style.md | 4-layer architecture, enum class over inheritance, memory budget, concurrency rules |
| docs/lessons_learned.yaml | Crash patterns & fixes (LL-001, LL-002, etc.) |
| docs/protocols/embedded_boot_crash.md | S1-S5 Occam's Razor Protocol |
| docs/protocols/heap_corruption.md | Heap triage (often misdiagnosed stack overflow) |
| docs/protocols/stack_overflow.md | Stack triage + watermark checks |
| docs/guides/testing.md | 3-tier testing strategy (Catch2) |
