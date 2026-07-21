---
type: Plan
title: Migrate JSON command/response and broadcast from UART0 to UART1 (GPIO8/9)
description: >
  Replace the USB Serial/JTAG approach with UART1 (GPIO8/9) for JSON command/response
  and broadcast data between ESP32-S3 and Raspberry Pi 3B (Tauri client). UART0 stays
  exclusively for debug logging. UART2 (GPIO16/17) stays for TMC2209. USB-JTAG stays
  free for OpenOCD debugging.
tags: [uart, serial, dual-port, architecture, rpi]
timestamp: 2026-07-14
status: pending
---

# Dual-Port UART Architecture: UART1 (GPIO8/9) for JSON Command Traffic

## 1. Overview

### Objective
Move all JSON command/response and broadcast data from UART0 (GPIO1/3, external
USB-UART bridge) to **UART1 (GPIO8/9)**, leaving UART0 exclusively for debug output
(ESP_LOG, printf, core dumps, panic handler). The RPi 3B client connects to UART1
via its PL011 UART on GPIO14/15. BLE NUS and WiFi WebSocket channels remain unchanged.

### Motivation

- **RPi 3B compatibility:** UART1 connects directly to the RPi PL011 UART (GPIO14/15),
  avoiding the LAN9514 shared USB/ethernet controller used by USB Serial/JTAG. The
  PL011 is a dedicated UART peripheral — no bus contention, no SOF flapping.
- **No 50ms busy-wait:** Unlike USB Serial/JTAG (which has a hardware busy-wait of
  50ms when TX FIFO is full), UART TX uses a ring buffer. Data is never lost and
  the main loop never blocks on TX.
- **USB-JTAG freed:** GPIO19/20 remain available for OpenOCD debugging via the
  native USB-JTAG peripheral — impossible if USB Serial/JTAG is used for data.
- **Proven API:** UART is the same `esp_driver_uart` already used by UART0 and
  UART2 (TMC2209). No new driver dependencies, no VFS compatibility risk.
- **Hardware reliability:** 3-wire connection (TX/RX/GND) with optoisolator option —
  simpler to galvanically isolate than USB.
- **Handshake-based detection:** Two-level link state (PhysicalOnly → LogicalReady
  via Tauri `serial.ping`) is more robust than SOF-based `usb_serial_jtag_is_connected()`
  which has a known issue (IDFGH-12984).

### Design

```
RPi 3B (Tauri App)                    ESP32-S3
┌──────────────────────┐             ┌─────────────────────────────────────┐
│  /dev/serial0 (UART)  │             │  UART1 (GPIO8 TX, GPIO9 RX)        │
│  ┌────────────────┐   │ 921600 baud │  ┌───────────────────────────────┐ │
│  │ JSON commands   │──┼─────────────┼─>│ RX: dataSerial.process()      │ │
│  │ JSON responses  │<─┼─────────────┼──│ TX: uart_tx_task              │ │
│  │ Broadcast 300ms │<─┼─────────────┼──│   ├─ gUartBroadcastQueue(ovwr)│ │
│  └────────────────┘   │             │   │   └─ gUartResponseQueue(send)│ │
│  ┌────────────────┐   │             │  └───────────────────────────────┘ │
│  │ BLE (bluez)     │   │  BLE NUS   │  ┌───────────────────────────────┐ │
│  │ always open     │<──┼────────────┼──│ bleManager (independent)      │ │
│  └────────────────┘   │             │  └───────────────────────────────┘ │
│  ┌────────────────┐   │             │  ┌───────────────────────────────┐ │
│  │ USB-UART (PC)   │   │  UART0     │  │ debugSerial (console, no JSON)│ │
│  │ debug logs      │<──┼────────────┼──│ ESP_LOG, printf, core dumps   │ │
│  └────────────────┘   │             │  └───────────────────────────────┘ │
└──────────────────────┘             │  UART2 (GPIO16/17) → TMC2209        │
                                      │  USB-JTAG (GPIO19/20) → OpenOCD     │
                                      └─────────────────────────────────────┘
```

### Architecture Changes

```
┌──────────────────────────────────────────────────────────────────────────┐
│ Before (current)                                                         │
│                                                                          │
│ UART0 (GPIO1/3): JSON commands + responses + broadcast + ESP_LOG + dumps │
│ BLE: NUS secondary, transport priority switching                         │
│ UART2 (GPIO16/17): TMC2209                                               │
│ USB-JTAG (GPIO19/20): unused                                             │
└──────────────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌──────────────────────────────────────────────────────────────────────────┐
│ After                                                                    │
│                                                                          │
│ UART0 (GPIO1/3): ESP_LOG, printf, core dumps, panic handler only         │
│                 via debugSerial (one SerialReader instance)              │
│ UART1 (GPIO8/9): JSON commands, responses, broadcast                    │
│                 via dataSerial (separate SerialReader instance)          │
│                 uart_tx_task drains dual queues (broadcast overwrite)    │
│ UART2 (GPIO16/17): TMC2209 (unchanged)                                   │
│ BLE: independent channel, always open (no disconnect on UART connect)    │
│ USB-JTAG (GPIO19/20): OpenOCD debugging                                  │
└──────────────────────────────────────────────────────────────────────────┘
```

### Transport Priority — Always Open (No Channel Switching)

Both channels are always open and independent. No BLE disconnect on UART connect,
no UART disable on BLE active. The LinkState only affects LED indication and the
broadcast metadata field `usbSerialConnected`.

```
UART1 LogicalReady?  → usbSerialConnected=true, LED OFF
UART1 PhysicalOnly?  → usbSerialConnected=false, LED status pattern
BLE connected?       → bleConnected=true, LED independent of UART state
```

Command routing: responses go back to the channel they arrived on (UART1 → UART1,
BLE → BLE). Broadcast is sent to both channels (UART1 + BLE + WebSocket).

This architecture eliminates all race conditions between UART and BLE: no stop/start
advertising, no disconnect timers, no transport state machine transitions.

## 2. Scope — All Files and Changes

### 2.1 New Files

#### A. `components/infrastructure/include/infrastructure/uart_tx_queue.hpp`

Queue entry struct shared by the broadcast and response queues.

```cpp
#pragma once

#include <cstdint>
#include <cstddef>
#include <array>
#include "domain/memory.hpp"

namespace ecotiter::infrastructure {

struct UartTxItem {
    std::array<char, domain::memory::MAX_RSP_SIZE> data;
    uint16_t len;
};

} // namespace ecotiter::infrastructure
```

#### B. `sdkconfig.monoblock`

Kconfig overrides file for monoblock builds. Placed at project root alongside `sdkconfig.defaults`.

```ini
# Monoblock: BLE disabled, UART1 is the only data channel
CONFIG_ECOTITER_ENABLE_BLE=n
```

Used via: `SDKCONFIG_DEFAULTS="sdkconfig.defaults:sdkconfig.monoblock" idf.py build`

#### C. `components/infrastructure/src/uart_tx_task.cpp`

Dedicated FreeRTOS task that drains both queues and writes to UART1 fd. Never
blocks the main loop. TWDT must be fed since this task sleeps on `xQueueReceive`.

```cpp
#include "infrastructure/uart_tx_queue.hpp"
#include "interface/serial.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_task_wdt.h"

static constexpr auto TAG = "uart_tx";

extern QueueHandle_t gUartBroadcastQueue;
extern QueueHandle_t gUartResponseQueue;
extern ecotiter::interface::SerialReader* gDataSerial;

namespace ecotiter::infrastructure {

void uartTxTaskEntry(void* param) {
    (void)param;
    UartTxItem item;
    while (true) {
        esp_task_wdt_reset();
        // Prefer responses (lower latency) over broadcast
        if (xQueueReceive(gUartResponseQueue, &item, pdMS_TO_TICKS(50)) == pdTRUE) {
            if (gDataSerial && gDataSerial->isInitialized()) {
                gDataSerial->write({item.data.data(), item.len});
            }
            continue;
        }
        // Then broadcast (lossy, latest frame only)
        if (xQueueReceive(gUartBroadcastQueue, &item, 0) == pdTRUE) {
            if (gDataSerial && gDataSerial->isInitialized()) {
                gDataSerial->write({item.data.data(), item.len});
            }
        }
    }
}

} // namespace ecotiter::infrastructure
```

Note: Two queues instead of one:
- **Broadcast queue** (depth=1): `xQueueOverwrite` — always keeps the latest frame.
  Main loop overwrites before the task reads → old frame lost, latest frame always sent.
- **Response queue** (depth=4): `xQueueSend` with timeout 0. If full, the response is
  dropped (acceptable — client re-sends command on timeout).

The task feeds TWDT every iteration with finite `pdMS_TO_TICKS(50)` timeout on
`xQueueReceive`. This avoids TWDT panic while waiting for work, and ensures
TWDT is fed at least every 50ms (well within the 10s default TWDT timeout).

### 2.2 Files to Modify

#### C. `components/infrastructure/include/infrastructure/config.hpp`
| Lines | Change | Reason |
|-------|--------|--------|
| New after line 20 | `PIN_RPI_UART_TX = GPIO_NUM_8`, `PIN_RPI_UART_RX = GPIO_NUM_9`, `RPI_UART_BAUD = 921600` | UART1 pin and baud definitions |
| New after line 83 | `UART_TX_TASK_STACK = 3072` | Stack for uart_tx_task (3 KB with margin for ::write call chain + ESP_LOG) |
| New after line 83 | `UART_BROADCAST_QUEUE_DEPTH = 1` | Depth 1 + xQueueOverwrite = always latest frame |
| New after line 83 | `UART_RESPONSE_QUEUE_DEPTH = 4` | Depth 4 for command responses + SM results |

#### D. `components/infrastructure/CMakeLists.txt`
| Line | Change | Reason |
|------|--------|--------|
| 15 (before network) | Add `"src/uart_tx_task.cpp"` to SRCS | New worker thread |

`REQUIRES` unchanged — `esp_driver_uart` already present.

#### E. `components/interface/include/interface/serial.hpp`

Refactor SerialReader to handle a single UART port via a config struct.

| Lines | Change | Reason |
|-------|--------|--------|
| Before class | Add `#include <cstdint>` + `enum class LinkState` | Two-level connection state |
| Before class | Add `struct SerialConfig { uart_port_t port; gpio_num_t txPin; gpio_num_t rxPin; int baudRate; int rxBufSize; };` | Configuration for one UART instance |
| Constructor | Change from `SerialReader() = default` to `explicit SerialReader(SerialConfig cfg)` | Parameterized per port |
| `init()` | Remove hardcoded UART0 usage; use `cfg_.port`, `cfg_.txPin`, etc. | Generic init for any UART |
| Public | Add `void acknowledgeHandshake() noexcept;` | Set handshake received flag (called from application layer) |
| Public | Add `[[nodiscard]] LinkState getLinkState() const noexcept;` | Query connection state |
| Public | Add `[[nodiscard]] bool isConnected() const noexcept { return fd_ >= 0; }` | Quick physical check (replaces `isInitialized()`) |
| Private | Add `std::atomic<bool> handshakeReceived_{false};`, `std::atomic<uint32_t> lastPingMs_{0};` | Handshake state tracking |
| Private | Add `static constexpr uint32_t HANDSHAKE_TIMEOUT_MS = 3000;` | Timeout for logical link |
| Private | Add `SerialConfig cfg_;` | Stored config for init |
| Remove | `isInitialized()` method (replaced by `isConnected()`) | — |
| Keep | `process()`, `write()`, `setSilent()`, `splitBuffer()` | Signatures unchanged |

**SRP design:** Each SerialReader instance manages exactly one UART port. Two
instances in main.cpp: `debugSerial(UART0)` and `dataSerial(UART1)`. No shared
state. No `select()` on multiple fds.

#### F. `components/interface/src/serial.cpp`

| Lines | Change | Reason |
|-------|--------|--------|
| Constructor | Store `cfg_` member; no init yet | — |
| `init()` | Use `cfg_.port`, `cfg_.txPin`, `cfg_.rxPin`, `cfg_.baudRate`, `cfg_.rxBufSize` instead of hardcoded UART0 | Generic init for any UART |
| `process()` | Unchanged — `select()` on single fd_ | One fd per instance — SRP |
| `write()` | Unchanged — `::write(fd_, ...)` | — |
| New | `acknowledgeHandshake()` | Set flag + timestamp |
| New | `getLinkState()` | Returns Disconnected / PhysicalOnly / LogicalReady |

**Generic `init()` implementation:**

```cpp
// No longer hardcoded to UART_NUM_0 — uses cfg_ members
Result<void> SerialReader::init() noexcept {
    uart_config_t uart_config = {
        .baud_rate = cfg_.baudRate,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0,
        .rx_glitch_filt_thresh = 8,   // hardware glitch filter (< 8 UART bit clocks)
        .source_clk = UART_SCLK_DEFAULT,
        .flags = {.allow_pd = 0, .backup_before_sleep = 0},
    };

    esp_err_t err = uart_driver_install(
        cfg_.port,
        cfg_.rxBufSize,
        config::UART_TX_RINGBUF_SIZE,
        10, nullptr, 0
    );
    if (err != ESP_OK) { /* ... error handling ... */ }

    err = uart_param_config(cfg_.port, &uart_config);
    if (err != ESP_OK) { /* ... */ }

    err = uart_set_pin(cfg_.port,
                       cfg_.txPin,
                       cfg_.rxPin,
                       UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) { /* ... */ }

    // VFS registration — global, called once from the first SerialReader init
    static bool vfsRegistered = false;
    if (!vfsRegistered) {
        uart_vfs_dev_register();
        vfsRegistered = true;
    }
    uart_vfs_dev_use_driver(cfg_.port);

    // Build VFS path: "/dev/uart/0", "/dev/uart/1"
    char vfsPath[16];
    std::snprintf(vfsPath, sizeof(vfsPath), "/dev/uart/%d", cfg_.port);
    fd_ = open(vfsPath, O_RDWR | O_NONBLOCK);
    if (fd_ < 0) { /* ... */ }

    // Flush RX FIFO: discard any stale bytes from power-up glitch
    uint8_t flushBuf[64];
    while (uart_read_bytes(cfg_.port, flushBuf, sizeof(flushBuf), 0) > 0) {}

    ESP_LOGI(TAG, "SerialReader initialized on %s at %d baud, fd=%d",
             vfsPath, cfg_.baudRate, fd_);
    return {};
}
```

**Note on `uart_vfs_dev_register()`:** Global call — must be called only once.
The `static bool vfsRegistered` guard ensures this. The ESP-IDF system init also
calls this automatically (at level 110, see `uart_vfs.c:1227-1231`), so the guard
is belt-and-suspenders.

**Handshake logic — layer separation:**

```cpp
// SerialReader provides ONLY the state machine and accessors.
// The application layer decides WHEN to acknowledge the handshake.

void SerialReader::acknowledgeHandshake() noexcept {
    handshakeReceived_.store(true, std::memory_order_release);
    lastPingMs_ = static_cast<uint32_t>(esp_timer_get_time() / 1000);
}

void SerialReader::resetHandshake() noexcept {
    handshakeReceived_.store(false, std::memory_order_release);
}

LinkState SerialReader::getLinkState() const noexcept {
    if (fd_ < 0) return LinkState::Disconnected;
    if (!handshakeReceived_.load(std::memory_order_acquire))
        return LinkState::PhysicalOnly;
    auto now = static_cast<uint32_t>(esp_timer_get_time() / 1000);
    auto last = lastPingMs_.load(std::memory_order_acquire);
    if (now - last > HANDSHAKE_TIMEOUT_MS) {
        // Stale — Tauri not responding; allow next ping to re-arm
        return LinkState::PhysicalOnly;
    }
    return LinkState::LogicalReady;
}
```

**The `process()` method does NOT search for `"cmd":"ping"`** — that would be a
layering violation. Handshake is acknowledged from the application layer when it
parses a valid ping command:

```cpp
// application/src/handlers/serial.cpp
Result<void, ProtocolError> handlePing(const JsonCommand& cmd, SerialReader& dataSerial) {
    dataSerial.acknowledgeHandshake();  // interface layer API
    return sendResponse(cmd.id, {{"status", "ok"}});
}
```

**Destructor:** Remove hardcoded `UART_NUM_0` — use `cfg_.port`:
```cpp
SerialReader::~SerialReader() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
    uart_driver_delete(cfg_.port);
}
```

**ESP_LOGE suppression in `write()`:** When the client is not connected,
`::write()` returns -1 with `errno = ENOTCONN` or `EAGAIN`. Since broadcast
fires every 300ms, this would flood the LogBuffer. Guard with `getLinkState()`:

```cpp
void SerialReader::write(std::string_view s) const noexcept {
    if (fd_ < 0 || s.empty()) return;
    // Suppress errno log when no client is listening
    if (getLinkState() == LinkState::Disconnected) return;
    ssize_t written = ::write(fd_, s.data(), s.size());
    if (written < 0 && getLinkState() == LinkState::LogicalReady) {
        // Only log write errors when we expect the client to be listening
        ESP_LOGE(TAG, "write failed: errno=%d", errno);
    }
}
```

#### G. `main/main.cpp`

| Lines | Change | Reason |
|-------|--------|--------|
| ~142-151 | Replace single `SerialReader serial` with two instances: `SerialReader debugSerial({.port=UART_NUM_0, ...})` and `SerialReader dataSerial({.port=UART_NUM_1, ...})` | SRP — one per UART |
| ~160-165 | Create `gUartBroadcastQueue` (depth=1) and `gUartResponseQueue` (depth=4) | Two queues: broadcast overwrite, response send |
| ~165 | `xTaskCreate(uartTxTaskEntry, "uart_tx", config::UART_TX_TASK_STACK, ...)` | Start worker thread |
| ~245-265 | `sendResponse()` lambda: push to `gUartResponseQueue` (`xQueueSend` timeout 0) | Response is important — don't overwrite |
| ~286-353 | Broadcast block: push to `gUartBroadcastQueue` (`xQueueOverwrite`) | Latest frame always wins |
| ~450 | Keep `gUsbHandshakeReceived.store(true, ...)` on serial RX for now but mark as deprecated | Remove in cleanup step |
| ~444-476 | Replace `serial.process()` on UART0 with `dataSerial.process()` on UART1 only | UART0 is debug-only, no JSON parsing |
| ~360-395 | LED SM: `dataSerial.getLinkState() == LogicalReady → UsbActive` | Use UART1 link state |
| ~398-404 | Transport SM: use `dataSerial.getLinkState()` | No BLE stop/start — both channels always open |
| New | Command routing: if command came from UART1 → respond via `gUartResponseQueue`; if from BLE → respond via BLE notify queue | Multi-channel routing |
| ~478-501 | SM result block: push to `gUartResponseQueue` | Non-blocking |
| Add | `#include "driver/uart.h"` (remove `driver/gpio.h` if no longer needed) | — |

**New globals (main.cpp scope):**
```cpp
QueueHandle_t gUartBroadcastQueue = nullptr;
QueueHandle_t gUartResponseQueue = nullptr;
ecotiter::interface::SerialReader* gDataSerial = nullptr;
```

**SerialReader construction:**
```cpp
// Debug console (UART0) — ESP_LOG, core dumps, panic handler
interface::SerialReader debugSerial({
    .port = UART_NUM_0,
    .txPin = UART_PIN_NO_CHANGE,   // keep bootloader pins (GPIO1/3)
    .rxPin = UART_PIN_NO_CHANGE,
    .baudRate = 115200,
    .rxBufSize = 256,
});
auto debugResult = debugSerial.init();

// Data channel (UART1) — JSON commands/responses/broadcast
interface::SerialReader dataSerial({
    .port = UART_NUM_1,
    .txPin = GPIO_NUM_8,           // explicit GPIO8/9
    .rxPin = GPIO_NUM_9,
    .baudRate = config::RPI_UART_BAUD,  // 921600
    .rxBufSize = 1024,             // 4x larger for 921600 baud
});
auto dataResult = dataSerial.init();
```

**Handshake calling from application layer (not in SerialReader read path):**
```cpp
// In the command dispatch section (~line 452-475 in current code):
auto line = dataSerial.process();
if (line.has_value()) {
    auto cmd = application::parseCommand(*line);
    if (cmd) {
        // Acknowledge handshake on ANY valid command (not just ping)
        dataSerial.acknowledgeHandshake();
        auto rsp = application::dispatch(*cmd);
        if (rsp) sendResponse(*rsp);
    }
}
```

#### H. `components/domain/include/domain/types.hpp`
| Lines | Change | Reason |
|-------|--------|--------|
| 103 | Add deprecation comment to `gUsbHandshakeReceived` | Not referenced after main.cpp changes |

#### I. `components/interface/include/interface/broadcast.hpp`
| Lines | Change | Reason |
|-------|--------|--------|
| 21 | Keep `usbSerialConnected` field name | Backward-compatible JSON schema; populate from `getLinkState() == LogicalReady` in main.cpp (minor: value now reflects UART1 LogicalReady) |

#### J. `docs/refs/gpio_pins_spec.md`
| Lines | Change | Reason |
|-------|--------|--------|
| 21 (table) | Add row: `"8 | RPi UART TX | uart_set_pin(UART_NUM_1) | ESP TX → RPi RX"` | New pin usage |
| 22 (table) | Add row: `"9 | RPi UART RX | uart_set_pin(UART_NUM_1) | ESP RX ← RPi TX"` | New pin usage |
| 96 | Change USB-JTAG usage: `"Free for OpenOCD debugging (not used for data)"` | USB-JTAG freed |
| 105 | Split UART0 description: `"UART0: debug logging only"` | Reflect new role |

#### K. `main/Kconfig.projbuild`
| Lines | Change | Reason |
|-------|--------|--------|
| New after `ECOTITER_ENABLE_BLE` | Add `ECOTITER_ENABLE_UART1` bool default y | Kconfig flag for conditional compilation of UART1 channel |

#### L. `docs/refs/project.md`
| Lines | Change | Reason |
|-------|--------|--------|
| 60-62 | Split communication table: UART0 → debug, UART1 → JSON data | New architecture |
| 79 (pin table) | Add GPIO8/9 rows to pin assignments | Document new pins |
| 224-230 | Transport priority: change USB → UART | Reflect new primary channel |
| 367-371 | Transport SM: change USB_ACTIVE → UART_ACTIVE | Reflect new state names |
| 379 | Safety: change "USB takeover" → "UART takeover" | Update terminology |

### 2.3 Conditional Compilation — Hardware Config Selection

The project supports two hardware configurations via Kconfig flags:

| Flag | `sdkconfig.defaults` (modular) | `sdkconfig.monoblock` (monoblock) |
|------|-------------------------------|-----------------------------------|
| `ECOTITER_ENABLE_BLE` | `y` (NimBLE active) | `n` (BLE fully excluded) |
| `ECOTITER_ENABLE_UART1` | `y` (UART1 active) | `y` (UART1 active) |
| `ECOTITER_ENABLE_WIFI` | `y` | `y` |

When `ECOTITER_ENABLE_BLE=n`:
- The `#ifndef CONFIG_ECOTITER_ENABLE_BLE` guards in `main.cpp` skip all BLE init, queue drain, and BLE notify logic
- `CMakeLists.txt` conditionally excludes `ble.cpp` and `ble_notify_thread.cpp` from the build
- NimBLE component is not linked — saves ~30 KB DRAM, 8 KB notify thread stack
- No race conditions, no BLE state machine, no BLE broadcast overhead

The flag is set via a separate sdkconfig defaults file:

`sdkconfig.monoblock` (CONFIG_ECOTITER_ENABLE_BLE=n only):
```
CONFIG_ECOTITER_ENABLE_BLE=n
```

Build command for monoblock:
```bash
SDKCONFIG_DEFAULTS="sdkconfig.defaults:sdkconfig.monoblock" ./scripts/idf.sh build
```

No runtime auto-detection — the config is determined at build time. Two binaries,
one source tree. The flag propagates through Kconfig and generates `CONFIG_ECOTITER_ENABLE_UART1`
in `sdkconfig`, accessible as `#ifdef CONFIG_ECOTITER_ENABLE_UART1` in C++ code.

**CMakeLists.txt conditional includes:**
```cmake
# components/infrastructure/CMakeLists.txt
if(CONFIG_ECOTITER_ENABLE_BLE)
    list(APPEND SRCS "network/src/ble.cpp" "network/src/ble_notify_thread.cpp")
endif()
if(CONFIG_ECOTITER_ENABLE_UART1)
    list(APPEND SRCS "src/uart_tx_task.cpp")
endif()
```

**main.cpp pattern:**
```cpp
// BLE init — excluded in monoblock builds
#ifdef CONFIG_ECOTITER_ENABLE_BLE
    infrastructure::network::BleManager bleManager;
#endif
```

### 2.4 Backpressure and Lost Frames

UART1 is a **lossy channel by design** — broadcast telemetry is fire-and-forget.
No adaptive rate, no ACK, no retransmission.

**Broadcast flow:**
```
main loop (10ms):
  every 300ms:
    formatBroadcastPayload() → UartTxItem{data, len}
    xQueueOverwrite(gUartBroadcastQueue, &item)  // always succeeds

uart_tx_task (priority 1):
  xQueueReceive(gUartResponseQueue, 50ms)  → write to UART1
  xQueueReceive(gUartBroadcastQueue, 0)    → write to UART1
```

**`lostFrames_` counter:**
```cpp
// Inside the broadcast block in main.cpp:
static uint32_t lostBroadcastFrames = 0;

auto prevCount = uxQueueMessagesWaiting(gUartBroadcastQueue);
xQueueOverwrite(gUartBroadcastQueue, &item);
if (prevCount > 0) {
    lostBroadcastFrames++;  // frame was replaced before the task sent it
}

// lostBroadcastFrames can be exposed in extended broadcast JSON for diagnostics
```

**Client-side gap detection:**
The RPi Tauri monitors the `ts` (tick) field in broadcast JSON. If consecutive
values jump by more than 300ms worth of ticks, a frame was lost. No adaptive
backpressure — the client simply logs diagnostic counters.

### 2.5 Files Unchanged

- `components/interface/CMakeLists.txt` — still needs `esp_driver_uart` (UART0, UART1, UART2 all use it)
- `tests/src/test_serial.cpp` — uses `process(string_view)` overload, no hardware dependency
- `components/interface/src/broadcast.cpp` — broadcast format unchanged
- `components/interface/src/rest_api.cpp` — HTTP API unchanged
- `components/application/` — command dispatch unchanged
- `components/infrastructure/network/` — BLE, WiFi, HTTP unchanged (except BLE stop/start from main.cpp)
- `scripts/find_port.py` — UNCHANGED (flash/monitor still on UART0)
- `scripts/idf.sh` — UNCHANGED
- `scripts/monitor.py` — UNCHANGED
- `partitions.csv` — no partition changes
- `sdkconfig.defaults` — `CONFIG_ESP_CONSOLE_UART_DEFAULT=y` stays; core dumps stay on UART0
- `components/infrastructure/src/drivers/tmc_uart.cpp` — TMC2209 on UART2 unchanged

## 3. Implementation Steps (Ordered)

### Step 1: Add UART1 pins + task/queue constants to config.hpp
**Files:** `components/infrastructure/include/infrastructure/config.hpp`
**Changes:**
- Add `PIN_RPI_UART_TX = GPIO_NUM_8`
- Add `PIN_RPI_UART_RX = GPIO_NUM_9`
- Add `RPI_UART_BAUD = 921600`
- Add `UART_TX_TASK_STACK = 3072`
- Add `UART_BROADCAST_QUEUE_DEPTH = 1`
- Add `UART_RESPONSE_QUEUE_DEPTH = 4`
**Effort:** 5 min
**AC:** Build succeeds

### Step 2a: Update Kconfig.projbuild
**Files:** `main/Kconfig.projbuild`
**Changes:**
- Add `ECOTITER_ENABLE_UART1` entry (bool, default y)
**Effort:** 5 min
**AC:** `idf.py reconfigure` shows new option in sdkconfig

### Step 2b: Create sdkconfig.monoblock
**Files:** `sdkconfig.monoblock`
**Changes:** Create file with `CONFIG_ECOTITER_ENABLE_BLE=n`
**Effort:** 2 min
**AC:** File exists at project root

### Step 2c: Create uart_tx_queue.hpp
**Files:** `components/infrastructure/include/infrastructure/uart_tx_queue.hpp`
**Change:** Write header with `UartTxItem` struct
**Effort:** 5 min
**AC:** Build succeeds

### Step 3: Create uart_tx_task.cpp
**Files:** `components/infrastructure/src/uart_tx_task.cpp`
**Change:** Write worker thread that drains queue and calls `serial.write()`
**Effort:** 15 min
**AC:** Build succeeds

### Step 4: Add uart_tx_task.cpp to infrastructure CMakeLists
**Files:** `components/infrastructure/CMakeLists.txt`
**Change:** Add `"src/uart_tx_task.cpp"` to SRCS
**Effort:** 5 min
**AC:** Build succeeds

### Step 5: Add SerialConfig struct + LinkState to serial.hpp
**Files:** `components/interface/include/interface/serial.hpp`
**Changes:**
- Add `SerialConfig` struct with port, txPin, rxPin, baudRate, rxBufSize
- Change constructor: `explicit SerialReader(SerialConfig cfg)`
- Add `LinkState` enum (`Disconnected`, `PhysicalOnly`, `LogicalReady`)
- Add `getLinkState()`, `acknowledgeHandshake()`, `resetHandshake()` declarations
- Add `isConnected()` (replaces `isInitialized()`)
- Add private handshake state members (`handshakeReceived_`, `lastPingMs_`, `HANDSHAKE_TIMEOUT_MS`)
- Add `cfg_` member
**Effort:** 20 min
**AC:** Build succeeds

### Step 6: Refactor serial.cpp to generic init + handshake
**Files:** `components/interface/src/serial.cpp`
**Changes:**
- Rewrite `init()` to use `cfg_.port`, `cfg_.txPin`, `cfg_.rxPin`, `cfg_.baudRate`, `cfg_.rxBufSize`
- Remove hardcoded `UART_NUM_0` — all UART references use `cfg_.port`
- Add `rx_glitch_filt_thresh = 8` to uart_config_t
- Add RX FIFO flush after init (discard power-up stale bytes)
- Add `static bool vfsRegistered` guard for `uart_vfs_dev_register()`
- Add `acknowledgeHandshake()` and `getLinkState()` implementations
- No handshake detection in `process()` — application layer calls `acknowledgeHandshake()`
- Destructor uses `cfg_.port` instead of hardcoded `UART_NUM_0`
- Suppress `ESP_LOGE` in `write()` when not `LogicalReady`
**Effort:** 60 min
**AC:** Build succeeds; host unit tests pass

### Step 7: Add BroadcastManager with lostFrames_ tracking
**Files:** `components/interface/include/interface/broadcast.hpp`, `components/interface/src/broadcast.cpp`
**Changes:**
- Add `uint32_t lostFrames_{0}` to broadcast logic or as a static in main.cpp
- Increment when `xQueueOverwrite` replaces a frame before uart_tx_task reads it
- Expose via extended broadcast JSON for diagnostics
**Effort:** 15 min
**AC:** Build succeeds

### Step 8: Rewrite main.cpp for two SerialReader instances + dual queues
**Files:** `main/main.cpp`
**Changes:**
- Two SerialReader instances: `debugSerial(UART_NUM_0, ...)` and `dataSerial(UART_NUM_1, ...)`
- Create `gUartBroadcastQueue` (depth=1, for xQueueOverwrite) and `gUartResponseQueue` (depth=4, for xQueueSend)
- `xTaskCreate(uartTxTaskEntry, "uart_tx", UART_TX_TASK_STACK, ...)`
- Replace `serial.write()` in `sendResponse()` → `xQueueSend(gUartResponseQueue, ...)`
- Replace `serial.write()` in broadcast block → `xQueueOverwrite(gUartBroadcastQueue, ...)`
- Replace `serial.write()` in SM result block → `xQueueSend(gUartResponseQueue, ...)`
- Command routing: UART1 commands → respond via `gUartResponseQueue`; BLE commands → respond via BLE notify queue
- Replace `gUsbHandshakeReceived` → `dataSerial.getLinkState() == LogicalReady` in:
  - Broadcast event
  - LED SM
  - Transport SM
- `dataSerial.process()` reads commands; `debugSerial` is console-only (no JSON parsing)
- Call `dataSerial.acknowledgeHandshake()` on every valid parsed command (not just ping — avoids layering violation)
- Wrap BLE init, queue drain, and notify in `#ifdef CONFIG_ECOTITER_ENABLE_BLE`
- Wrap UART1 init, queues, and uart_tx_task in `#ifdef CONFIG_ECOTITER_ENABLE_UART1`
**Effort:** 90 min
**AC:** Build succeeds in both `sdkconfig.defaults` (modular) and `sdkconfig.monoblock` modes

### Step 9: Deprecate gUsbHandshakeReceived in domain types
**Files:** `components/domain/include/domain/types.hpp`
**Change:** Add deprecation comment to `gUsbHandshakeReceived`
**Effort:** 5 min
**AC:** Build succeeds

### Step 10: Update documentation
**Files:** `docs/refs/gpio_pins_spec.md`, `docs/refs/project.md`
**Changes:** Reflect new port split as described in §2.2 items J-K
**Effort:** 15 min
**AC:** Documentation accurately describes new architecture

### Step 11: Smoke test on real hardware
**Command:** `scripts/idf.sh smoke`
**What to expect:**
1. Build succeeds (0 errors, 0 warnings)
2. Flash succeeds (via UART0 bootloader)
3. 30s serial monitor on UART0 shows:
   - `BOOT OK: ecotiter v...`
   - `UART1 initialized on GPIO8/9 at 921600 baud, fd=...`
   - All ESP_LOG debug output on UART0
   - No JSON data on UART0
   - No Guru Meditation, no WDT panic
4. UART1 on RPi (`/dev/serial0`) receives broadcast events every ~300ms
5. Handshake ping from RPi → ESP32-S3 → LinkState transitions to LogicalReady
**Effort:** 30 min
**AC:** All conditions above met

## 4. Risks & Mitigations

### R1: UART1 VFS path conflict with UART0
| Risk | Level | Description | Mitigation |
|------|-------|-------------|------------|
| Both UARTs register VFS on same path | **Low** | `uart_vfs_dev_register()` is global — called once. `open("/dev/uart/1")` vs `open("/dev/uart/0")` are distinct paths | Verify both fds open correctly. Test with esp console on UART0 and UART1 VFS on UART1 simultaneously |

### R2: RX buffer overflow at 921600 baud
| Risk | Level | Description | Mitigation |
|------|-------|-------------|------------|
| Main loop runs at 10ms; at 921600 baud, ~1150 bytes arrive between ticks. Current INPUT_BUF_SIZE=256 overflows | **Medium** | RX FIFO (128 bytes HW) + ring buffer must be large enough to hold inter-tick data | Increase UART1 RX buffer to 1024 or 2048 bytes in `uart_driver_install()`. Also increase `readBuf` in `process()` to 1024 |

### R3: Handshake timeout false-positive during high load
| Risk | Level | Description | Mitigation |
|------|-------|-------------|------------|
| If `process()` is delayed by >3s (long command execution), handshake timeout may trigger incorrectly | **Low** | `getLinkState()` checks if last ping was within 3s. A busy command could skip reads for >3s | Tauri sends ping every 1s. On reconnect, next ping restores LogicalReady within 1s. Transient false-positive is acceptable |

### R4: `select()` on single fd per SerialReader instance
| Risk | Level | Description | Mitigation |
|------|-------|-------------|------------|
| Each SerialReader instance selects on one fd. Two instances = two select() calls per tick | **Low** | Each `select()` with `{0,0}` timeout is a syscall; two per tick is ~1-2µs overhead at 10ms pacing | Negligible. Both select() calls are zero-timeout polls. If profiling shows overhead, merge into one select() on maxFd+1 with both FD_SETs |

### R4b: Handshake acknowledged by application layer (not by SerialReader)
| Risk | Level | Description | Mitigation |
|------|-------|-------------|------------|
| `acknowledgeHandshake()` must be called from the application dispatch layer every time a valid command is received | **Low** | If dispatch misses calling acknowledge (e.g., unknown command path), handshake timeouts | Make `acknowledgeHandshake()` unconditional in the process() loop: any valid parsed command → acknowledge. Invalid JSON (parse error) → no acknowledge |

### R5: RPi 3B UART routing — PL011 vs mini UART
| Risk | Level | Description | Mitigation |
|------|-------|-------------|------------|
| RPi 3B `/dev/serial0` may point to mini UART (`/dev/ttyS0`) by default, which does NOT support 921600 baud | **High** | The mini UART is clocked from the VPU frequency (varies with CPU load/GPU activity) and has no break detection. At 921600 baud, bit errors are guaranteed. The PL011 UART (on GPIO14/15) supports 921600 but requires explicit configuration | **Mandatory RPi config:** `config.txt` must contain `enable_uart=1` and `dtoverlay=disable-bt` (or `dtoverlay=uart0=pl011`). This routes the PL011 UART to GPIO14/15 and disables Bluetooth on the PL011. After this, `/dev/serial0` → PL011 at full speed. Document this as a prerequisite. Without it, UART1 will not work at 921600 baud. |

### R6: RPi not sending handshake during boot (30-60s)
| Risk | Level | Description | Mitigation |
|------|-------|-------------|------------|
| System stuck at PhysicalOnly — BLE stays active but user expects UART | **Low** | RPi takes 30-60s to boot and start Tauri. During this time, UART is PhysicalOnly, BLE is active | Correct behavior. PhysicalOnly means "cable connected but client not ready." BLE remains available. When Tauri starts and sends ping, transition to LogicalReady |

### R6: BLE disconnect late on UART connect
| Risk | Level | Description | Mitigation |
|------|-------|-------------|------------|
| Race: UART connects (LogicalReady) but BLE is still processing a command | **Low** | BLE disconnect while command queued may lose response | Accept — UART takeover is immediate. In-flight BLE command can fail. The RPi Tauri re-sends if needed. |

### R7: GPIO pin-level issues (GPIO8/9)
| Risk | Level | Description | Mitigation |
|------|-------|-------------|------------|
| GPIO8/9 on the specific hardware may have unexpected pull-ups/downs or be used by another external peripheral | **Very Low** | Verified safe per `gpio_pins_spec.md` §5-6: GPIO8 is in safe range 5-13, GPIO9 also safe | Visual check of actual devkit PCB. No PSRAM, no strapping, no IOMUX default conflicts |
| GPIO9 power-up glitch (60µs low) may cause stale data in RX FIFO | **Low** | Per `esp32-s3_gpio_pins.md:99-101`, GPIO1-14 have a typical 60µs low-level glitch at power-up. At 921600 baud (~6.5µs/bit), this equals ~9 bit times — a ~1-byte stale byte in the HW RX FIFO | **Two mitigations:** (1) `.rx_glitch_filt_thresh = 8` in `uart_config_t` filters spikes < 8 UART bit clocks (~8.7µs at 921600). (2) After `init()`, flush RX FIFO with `uart_read_bytes()` in a loop until empty — discards any power-up garbage before the first `process()` call |
| GPIO9 lacks internal pull-up at reset | **Low** | Unlike GPIO1-3 (which have WPU+IE at reset), GPIO9 has no weak pull-up. If RPi TX (GPIO14) is tri-stated during RPi boot (30-60s), GPIO9 may float and pick up noise | GPIO9 has a glitch filter enabled (rx_glitch_filt_thresh=8) that suppresses noise shorter than 8 bit clocks. For production, add external 10kΩ pull-up on GPIO9 to 3.3V |

### R8: `uart_vfs_dev_register()` called twice
| Risk | Level | Description | Mitigation |
|------|-------|-------------|------------|
| If `init()` (UART0) calls `uart_vfs_dev_register()` and `initUart1()` also calls it | **Low** | ESP-IDF VFS allows multiple calls — subsequent calls are safe no-ops | Guard with a `static bool vfsRegistered = false;` or simply call once in `init()` and skip in `initUart1()` |

### R9: Queue overflow — broadcast vs response isolation
| Risk | Level | Description | Mitigation |
|------|-------|-------------|------------|
| Broadcast or response queue fills up, blocking the other channel | **Low** | Two separate queues isolate broadcast from responses. Broadcast (depth=1, `xQueueOverwrite`) never blocks. Response (depth=4, `xQueueSend` timeout 0) drops new responses if full | **Broadcast:** `xQueueOverwrite` always succeeds — replaces old frame with latest. If uart_tx_task hasn't sent the old frame yet, it reads the latest. Broadcast is lossy telemetry — stale frames are irrelevant. **Responses:** `xQueueSend` with timeout 0. If queue is full (unlikely — 4 entries at <1 Hz), the response is dropped. Client re-sends on timeout. **Stack watermark:** uart_tx_task logs `uxTaskGetStackHighWaterMark(nullptr)` every 100 iterations to detect stack pressure |

### R10: uart_tx_task stack overflow
| Risk | Level | Description | Mitigation |
|------|-------|-------------|------------|
| `uart_tx_task` stack too small for `::write()` + `ESP_LOGE` + `esp_task_wdt_reset()` call chain | **Low** | `::write()` may have deep call stack through VFS → UART driver → HAL | Configure `UART_TX_TASK_STACK = 3072` (3 KB). Add periodic `uxTaskGetStackHighWaterMark()` logging. Guard with `configCHECK_FOR_STACK_OVERFLOW=1` |

## 5. Edge Cases

### EC-1: RPi Not Connected (No UART1 Wiring)
- `dataSerial.isConnected()` returns true (driver installed)
- `getLinkState()` returns `PhysicalOnly` (no handshake received)
- `dataSerial.process()` returns nothing (no data on RX)
- Transport SM has no effect on BLE — both channels always open
- **Expected behavior:** System works via BLE/WiFi only. UART1 writes go nowhere (silently dropped by non-blocking VFS). Debug logs on UART0.

### EC-2: RPi Boots Mid-Operation (Hot Plug)
- Cable connected at boot — `dataSerial.init()` succeeds
- RX FIFO flushed (init discards stale bytes from power-up glitch)
- `getLinkState()` returns `PhysicalOnly` (no handshake yet)
- Tauri starts, sends any command → application dispatch calls `acknowledgeHandshake()` → LogicalReady
- LED transitions from blue/green → OFF
- **Expected behavior:** Seamless transition within 1-2s of Tauri startup. BLE stays connected throughout.

### EC-3: RPi Crashes (Tauri Dies, UART Data Stops)
- Last valid command was >3s ago
- `getLinkState()` transitions from LogicalReady → PhysicalOnly
- LED resumes status pattern (not OFF)
- BLE stays connected (both channels always open)
- When Tauri restarts, next command re-establishes LogicalReady
- **Expected behavior:** No disconnect/reconnect cycle. BLE is available immediately. Channel transition is invisible to the user.

### EC-4: Both UART0 and UART1 Active (Development)
- UART0 → debug logs (PC USB-UART bridge)
- UART1 → JSON data (RPi)
- Host PC has two ports visible. Flash/monitor on UART0.
- **Expected behavior:** Clean split. Monitor on UART0 shows debug logs. RPi communicates over UART1.

### EC-5: UART1 Driver Init Failure
- `initUart1()` returns `SerialError::InitFailed`
- `uart1Fd_` stays -1
- `getLinkState()` returns `Disconnected`
- System works via BLE/WiFi only
- **Expected behavior:** Graceful degradation.

### EC-6: UART Data Corruption (Noise on Lines)
- At 921600 baud over 3-wire, noise may introduce bit errors
- UART has no parity or CRC at hardware level (8N1)
- Garbled JSON → parser returns error response `{"error":"invalid_params"}`
- Handshake ping corrupted → missed → next ping (1s later) re-establishes LogicalReady
- **Expected behavior:** Self-healing within 1s. No crash.

### EC-7: Flashing Still Needs UART0
- Bootloader ROM uses UART0 (GPIO1/3) for download
- `idf.py flash` targets UART0 — UNCHANGED
- **Expected behavior:** No change to flashing workflow.

### EC-8: Core Dumps Still Go to UART0
- `CONFIG_ESP_COREDUMP_ENABLE_TO_UART=y`
- **Expected behavior:** Core dump workflow unchanged.

## 6. Dependencies

| Step | From | To | Type |
|------|------|-----|------|
| 1 | config.hpp | All steps | Must come first |
| 2a | Kconfig.projbuild | Build system | Add `ECOTITER_ENABLE_UART1` flag |
| 2b | sdkconfig.monoblock | Build system | BLE=n override |
| 3 | uart_tx_queue.hpp | Step 4 (uart_tx_task.cpp) | Header must exist |
| 4 | uart_tx_task.cpp | Step 5 (CMakeLists) | Source must be listed |
| 5 | CMakeLists.txt (infra) | Build | Build breaks otherwise |
| 6 | serial.hpp | Step 7 (serial.cpp) | Header must be correct |
| 7 | serial.cpp | Step 9 (main.cpp) | Impl must exist |
| 8 | broadcast.hpp/cpp | Step 9 (main.cpp) | lostFrames tracking |
| 9 | main.cpp | Step 12 (smoke) | Core logic change |
| 10 | types.hpp | - | Optional cleanup |
| 11 | docs/ | - | Final documentation |
| 12 | Hardware | All | Real ESP32-S3 + RPi |

**Implementation order:** 1 → 2a → 2b → 3 → 4 → 5 → 6 → 7 → 8 → 9 → 10 → 11 → 12
(With verification at each step: build + host tests)

## 7. Acceptance Criteria

| ID | Description | Verification Method |
|----|-------------|-------------------|
| AC-001 | `scripts/idf.sh build` succeeds with 0 errors, 0 warnings — all new files compile, UART1 linked correctly | `automated` (build) |
| AC-002 | All host unit tests pass: `scripts/idf.sh test` — particularly test_serial.cpp which uses the `process(string_view)` overload (no hardware dependency) | `automated` (host test) |
| AC-003 | On real hardware: UART0 shows all ESP_LOG output including "UART1 initialized on GPIO8/9 at 921600 baud" — no JSON data appears on UART0 | `integration` (flash + monitor on UART0 for 30s) |
| AC-004 | UART1 (RPi `/dev/serial0`) receives broadcast events every ~300ms with correct JSON format | `manual` (user monitors RPi UART, sees `{"ts":...` every ~300ms) |
| AC-005 | JSON command `{"cmd":"ping"}` sent from RPi to UART1 receives correct JSON response on UART1 | `manual` (user sends ping, confirms `{"status":"ok"}` response) |
| AC-006 | `serial.getLinkState()` returns LogicalReady when RPi Tauri is running and sending pings | `manual` (check LED: OFF = LogicalReady) |
| AC-007 | `serial.getLinkState()` returns PhysicalOnly when RPi cable is connected but Tauri is dead | `manual` (LED blue/green = PhysicalOnly or Disconnected) |
| AC-008 | Transport SM: `TransportState::UartActive` when LogicalReady, `BleConnected` when PhysicalOnly + BLE on | `inspection` (logs on UART0 show transport state transitions) |
| AC-009 | BLE disconnects when UART transitions to LogicalReady; BLE reconnects when UART drops to PhysicalOnly | `manual` (connect BLE, start Tauri on RPi, verify BLE disconnects within 3s; kill Tauri, verify BLE reconnects) |
| AC-010 | Core dump on panic still goes to UART0 (not UART1) | `inspection` (verify `CONFIG_ESP_COREDUMP_ENABLE_TO_UART=y` is unchanged) |
| AC-011 | `scripts/idf.sh flash` still works targeting UART0 bootloader | `automated` (smoke test flash step succeeds) |
| AC-012 | `scripts/idf.sh smoke` succeeds: build + flash + 30s monitor on UART0, no Guru/panic/WDT | `integration` (smoke test) |
| AC-013 | No Guru Meditation, WDT panic, or crash during 120-second dual-port operation | `integration` (120s test with UART0 monitor + UART1 data) |
| AC-014 | uart_tx_task stack does not overflow (8192 bytes allocated, actual usage < 2048) | `automated` (run `FreeRTOS` stack watermark check in logs) |

## 8. Effort Estimate

| Step | Description | Files | Effort (hours) |
|------|-------------|-------|----------------|
| 1 | Add UART1 pins + task stack to config.hpp | 1 | 0.1 |
| 2a | Update Kconfig.projbuild | 1 | 0.1 |
| 2b | Create sdkconfig.monoblock | 1 (new) | 0.1 |
| 3 | Create uart_tx_queue.hpp | 1 (new) | 0.1 |
| 4 | Create uart_tx_task.cpp (dual queue, TWDT feed, watermark check) | 1 (new) | 0.3 |
| 5 | Add uart_tx_task.cpp to infrastructure CMakeLists | 1 | 0.1 |
| 6 | Refactor serial.hpp (SerialConfig, LinkState, acknowledgeHandshake, SRP per-port) | 1 | 0.3 |
| 7 | Rewrite serial.cpp (generic init, glitch filter, RX flush, handshake API, ESP_LOGE guard) | 1 | 1.0 |
| 8 | Add BroadcastManager lostFrames tracking | 2 | 0.3 |
| 9 | Rewrite main.cpp (dual SerialReader, dual queues, ifdef guards, routing) | 1 | 1.5 |
| 10 | Deprecate gUsbHandshakeReceived | 1 | 0.1 |
| 11 | Update documentation | 2 | 0.3 |
| 12 | Smoke test + verification | N/A | 0.5 |
| **Buffer** | Debug & unexpected issues | N/A | 1.0 |
| **Total** | | **6 modified + 3 new + 2 docs = 11 files** | **6.0 hours** |

**Estimated effort:** M (1-2 days of focused work, ~5 hours coding + testing)

## 9. Rollback Plan

**Tier 1 — Quick revert (single component issue):**
- If UART1 init fails at runtime: UART1 is optional. Delete `initUart1()` call from main.cpp. System falls back to BLE-only data channel. UART0 stays for debug. No data channel regression.
- If uart_tx_task has bugs: revert queue push in main.cpp to direct `serial.write()` calls. UART1 still works (direct writes from main loop are also non-blocking with UART ring buffer — no Art. I violation).

**Tier 2 — Full rollback:**
```bash
git checkout -- components/infrastructure/src/uart_tx_task.cpp
git checkout -- components/infrastructure/include/infrastructure/uart_tx_queue.hpp  # (after git rm)
git checkout -- components/infrastructure/include/infrastructure/config.hpp
git checkout -- components/infrastructure/CMakeLists.txt
git checkout -- components/interface/include/interface/serial.hpp
git checkout -- components/interface/src/serial.cpp
git checkout -- components/interface/include/interface/broadcast.hpp
git checkout -- components/interface/src/broadcast.cpp
git checkout -- main/main.cpp
git checkout -- main/Kconfig.projbuild
git checkout -- docs/refs/gpio_pins_spec.md
git checkout -- docs/refs/project.md
git checkout -- components/domain/include/domain/types.hpp
```

Then verify:
```bash
./scripts/idf.sh build && ./scripts/idf.sh test && ./scripts/idf.sh smoke
```

**Tier 3 — Fallback to original single-port UART0:**
If UART1 approach has systemic issues, revert to the original single-port architecture
(UART0 carries both debug and JSON). The old USB Serial/JTAG plan is also available
as an alternative migration path.

## 10. Comparison: UART1 vs USB Serial/JTAG

| Criteria | USB Serial/JTAG (superseded) | UART1 GPIO8/9 (selected) |
|----------|------------------------------|--------------------------|
| **RPi 3B compatibility** | ⚠️ LAN9514 shared USB bus | ✅ Dedicated PL011 UART |
| **50ms busy-wait (Art. I)** | ❌ Present (HW TX FIFO full) | ✅ Ring buffer, no blocking |
| **USB-JTAG availability** | ❌ Used for data | ✅ Free for OpenOCD |
| **Connection detection** | ⚠️ `isConnected()` via SOF (bug IDFGH-12984) | ✅ `getLinkState()` handshake |
| **New driver dependency** | `esp_driver_usb_serial_jtag` | None (reuses `esp_driver_uart`) |
| **VFS risk** | `select()` on `/dev/usbserjtag` untested | `select()` on `/dev/uart/1` proven |
| **Port scripts** | `find_port.py` + `idf.sh` changes | **No changes** |
| **New files** | 0 | 2 (task + queue header) |
| **Total files modified** | 10 | 9 (5 existing + 2 new + 2 docs) |
| **Implementation risk** | Medium (new API, VFS, 50ms bug) | Low (copy-paste-proven pattern) |
| **Wiring** | 2 wires (USB D+/D-) | 3 wires (TX/RX/GND) |
| **Speed** | 12 Mbps | 921600 baud (~100 KB/s) |

## Appendix A: Physical Wiring

```
ESP32-S3                    RPi 3B
GPIO8  (U1TXD) ──────────── GPIO15 (RXD)
GPIO9  (U1RXD) ──────────── GPIO14 (TXD)
GND    ───────────────────── GND

No level shifting needed — both are 3.3V logic.
Max cable length: ~1m at 921600 baud (twisted pair recommended).

### Mandatory RPi 3B Configuration

The RPi 3B has two UARTs: PL011 (hardware UART, supports up to ~4 Mbps) and mini UART
(`/dev/ttyS0`, clocked from VPU, unreliable at high baud). By default, `/dev/serial0`
may point to the mini UART. To use PL011 at 921600 baud:

1. Edit `/boot/config.txt`:
```
enable_uart=1
dtoverlay=disable-bt
```

This disables Bluetooth on the PL011 and routes it to GPIO14/15 (TX/RX).
Alternatively, `dtoverlay=uart0=pl011` if Bluetooth must stay enabled (less common).

2. Verify:
```bash
ls -l /dev/serial0
# Should point to /dev/ttyAMA0 (PL011), NOT /dev/ttyS0 (mini UART)
```

3. Test baud rate:
```bash
stty -F /dev/serial0 921600
# Verify with loopback test
```

**Without this config, UART1 will NOT work at 921600 baud.** The mini UART's clock
is derived from the VPU frequency which varies with CPU load, causing bit errors.
```

## Appendix B: Key API Reference

### ESP-IDF UART API (driver/uart.h)

```c
// Install UART driver
esp_err_t uart_driver_install(uart_port_t uart_num,
                              int rx_buffer_size,
                              int tx_buffer_size,
                              int queue_size,
                              QueueHandle_t* queue,
                              int intr_alloc_flags);

// Configure UART parameters
esp_err_t uart_param_config(uart_port_t uart_num,
                            const uart_config_t* uart_config);

// Set UART pins (GPIO matrix routing) — variadic macro
// 5-arg form: uart_set_pin(uart_num, tx, rx, rts, cts)
// 7-arg form: uart_set_pin(uart_num, tx, rx, rts, cts, dtr, dsr)
// Use UART_PIN_NO_CHANGE (-1) for unused signals

// Delete UART driver
esp_err_t uart_driver_delete(uart_port_t uart_num);

// VFS registration (global — call once)
void uart_vfs_dev_register(void);
void uart_vfs_dev_use_driver(uart_port_t uart_num);

// UART config struct
typedef struct {
    int baud_rate;
    uart_word_length_t data_bits;
    uart_parity_t parity;
    uart_stop_bits_t stop_bits;
    uart_hw_flowcontrol_t flow_ctrl;
    uint8_t rx_flow_ctrl_thresh;
    uint32_t rx_glitch_filt_thresh;          // filter for glitch spikes; 0 = disabled
    union {
        uart_sclk_t source_clk;
    };
    struct { uint32_t allow_pd: 1; uint32_t backup_before_sleep: 1; } flags;
} uart_config_t;
```

### UART Port Enumeration (ESP32-S3)

| Port | Default IOMUX TX | Default IOMUX RX | Project usage |
|------|-----------------|-----------------|---------------|
| `UART_NUM_0` | GPIO1 | GPIO3 | Debug console (ESP_LOG) |
| `UART_NUM_1` | GPIO17 | GPIO18 | **JSON data channel** (remapped to GPIO8/9) |
| `UART_NUM_2` | GPIO18 | GPIO19 | TMC2209 (remapped to GPIO17/16) |

### FreeRTOS Queue API

```c
// Create queue
QueueHandle_t xQueueCreate(UBaseType_t uxQueueLength,
                           UBaseType_t uxItemSize);

// Send to queue (non-blocking in main loop)
BaseType_t xQueueSend(QueueHandle_t xQueue,
                      const void* pvItemToQueue,
                      TickType_t xTicksToWait);

// Receive from queue (blocking in uart_tx_task)
BaseType_t xQueueReceive(QueueHandle_t xQueue,
                         void* pvBuffer,
                         TickType_t xTicksToWait);
```

## Appendix C: LinkState State Machine

Both channels (UART1 + BLE) are always open — no disconnect on LogicalReady.
LinkState only affects LED indication and broadcast metadata.

```
                ┌─────────────────────────────────────┐
                │  Disconnected                       │
                │  fd_ < 0 (init failed)              │
                │  - All writes silently dropped      │
                │  - BLE is the only data channel     │
                └──────────┬──────────────────────────┘
                           │ init() succeeds
                           ▼
                ┌─────────────────────────────────────┐
          ┌────>│  PhysicalOnly                        │
          │     │  fd_ >= 0                            │
          │     │  handshakeReceived_ == false         │
          │     │  - Cable connected, client not ready │
          │     │  - LED: status pattern (not OFF)    │
          │     │  - Broadcast usbSerialConnected=false│
          │     └──────────┬──────────────────────────┘
          │                │ acknowledgeHandshake() called
          │                │ by application dispatch
          │                ▼
          │     ┌─────────────────────────────────────┐
          │     │  LogicalReady                        │
          │     │  handshakeReceived_ == true          │
          │     │  lastPing < HANDSHAKE_TIMEOUT_MS ago │
          │     │  - Client ready                     │
          │     │  - LED OFF                          │
          │     │  - Broadcast usbSerialConnected=true │
          │     │  - Writes enabled (active path)     │
          │     └──────────┬──────────────────────────┘
          │                │ No ping for >3000ms
          └────────────────┘
```

Transitions:
- `Disconnected → PhysicalOnly`: `init()` succeeds (UART driver installed, fd open)
- `PhysicalOnly → LogicalReady`: Application dispatch calls `acknowledgeHandshake()` after parsing any valid command
- `LogicalReady → PhysicalOnly`: No command received for >3000ms (stale — client may be dead)
- `PhysicalOnly → Disconnected`: Only on driver uninstall (runtime error recovery)

Note: `acknowledgeHandshake()` is called by `application/handlers/serial.cpp` for
EVERY valid parsed command, not just ping. This means the first valid JSON command
transitions PhysicalOnly → LogicalReady. Handshake ping is just one possible command.

## Appendix D: Broadcast Bandwidth Calculation

| Field | Size (bytes) | Notes |
|-------|-------------|-------|
| JSON overhead (keys) | ~120 | `{"ts":N,"tmp":N,"mv":N,...}` |
| Values (numbers + bools) | ~40 | Integers, floats, booleans |
| Newline | 1 | `\n` terminator |
| **Total per event** | **~161 bytes** | Compact format |

At 921600 baud (8N1 = 10 bits/byte):
- **92160 bytes/sec** effective throughput
- **Broadcast: 161 bytes × 3.3 Hz = 531 bytes/sec** → 0.6% of bandwidth
- **Response (worst case): 2048 bytes × ~1 Hz = 2048 bytes/sec** → 2.2% of bandwidth
- **Total data channel load: < 3% of UART1 bandwidth**

**Conclusion:** 921600 baud is massively over-provisioned. Even 115200 baud would
be sufficient (11520 bytes/sec, ~22% load). However, 921600 is chosen for safety
margin and because both ESP32-S3 and RPi PL011 support it comfortably.

## Appendix E: UART0 vs UART1 vs UART2 — Complete Matrix

| Property | UART0 | UART1 | UART2 |
|----------|-------|-------|-------|
| **Port** | `UART_NUM_0` | `UART_NUM_1` | `UART_NUM_2` |
| **Physical pins** | GPIO1 (TX), GPIO3 (RX) | GPIO8 (TX), GPIO9 (RX) | GPIO16 (RX), GPIO17 (TX) |
| **Baud rate** | 115200 | 921600 | 115200 |
| **Purpose** | Debug console (ESP_LOG, core dumps, panic handler) | JSON command/response + broadcast (RPi client) | TMC2209 PDN_UART half-duplex |
| **RX buffer** | 256 bytes | 1024 bytes (at 921600) | 256 bytes |
| **TX buffer** | 1024 bytes (ringbuf) | 1024 bytes (ringbuf) | None (sync writes) |
| **VFS** | Yes (`/dev/uart/0`) | Yes (`/dev/uart/1`) | No (raw API) |
| **Driver** | `esp_driver_uart` | `esp_driver_uart` | `esp_driver_uart` |
| **Flow control** | None | None | None (half-duplex) |
| **Host** | PC (USB-UART bridge, CP2102/CH340/FTDI) | RPi 3B (PL011 on GPIO14/15) | TMC2209 (on-board PDN_UART) |
| **Criticality** | Debug only | **Primary data channel** | Stepper motor config |
| **Fallback if fails** | Console lost (acceptable) | BLE NUS takes over | No runtime TMC config (uses defaults) |
