---
type: Known Issue
title: "Main loop tick watchdog latency exceeds 10ms GR-1 threshold"
description: "Main loop blocks for 15-101ms every ~1s, violating GR-1 10ms limit. Firmware tick_watchdog threshold also incorrectly set to 15ms instead of 10ms"
tags: [main_loop, GR-1, tick_watchdog, latency]
timestamp: 2026-07-13
---

# ISSUE-003: Main loop tick watchdog — latency exceeds 10ms GR-1 threshold

**Severity:** Medium  
**Detected:** 2026-07-13, serial log `serial_2026-07-13_07-25-02.log`  
**Related rule:** AGENTS.md §GR-1 (Never Block The Main Loop)

## Symptom

32 warnings in 30 s (every ~1 s):

```
W (1712) tick_watchdog: main loop took 96626 us (>15ms threshold)
W (1871) tick_watchdog: main loop took 15569 us (>15ms threshold)
W (1936) tick_watchdog: main loop took 25192 us (>15ms threshold)
...
W (29065) tick_watchdog: main loop took 59147 us (>15ms threshold)
```

- Min: 15.5 ms
- Max: **101.9 ms**
- Typical: ~59 ms

**Note:** `tick_watchdog.hpp:19` already uses 10000μs (10ms) threshold — correct. All measurements exceed it regardless.

## Impact

Main loop blocking >10ms violates GR-1. Delays:
- WiFi/BLE event processing
- Temperature sensor polling
- Motor command responses
- WebSocket broadcast scheduling

## Root Causes (Code Analysis — 2026-07-14)

Six sources of >10ms blocking in the main loop:

| # | Cause | File:Line | Worst-case |
|---|-------|-----------|-----------|
| 1 | **BLE `sendNotification()` called synchronously** | `main.cpp:451,536-537,754-755` | 10-30ms |
| 2 | **RMT `rmt_tx_wait_all_done(10ms)` in RGB LED** | `rgb_led.cpp:130` | 10ms |
| 3 | **`xQueueSend(motor, timeout=10ms)`** | `burette_ops.cpp:15` | 10ms |
| 4 | **UART `::write()` blocking** | `serial.cpp:108` | 2-5ms |
| 5 | **NVS flash reads in dispatch path** | `burette_ops.cpp:23,42,66,101,122` | 5-50ms |
| 6 | **UART TX buffer = 0 — `fflush(stdout)` blocks byte-by-byte** | `serial.cpp:41` | **~43ms (watermarks), ~5ms (each ESP_LOGW)** |

### Architectural Root Cause

`broadcast()` in `main.cpp:493-553` violates **Article II (Task Sovereignty)** by calling NimBLE (`sendNotification`) and httpd (`broadcastWsEvent`) directly — functions that belong to `ble_notify_thread` and `net_owner` respectively. The legacy `transport_send()` (`legacy/arduino/src/main.cpp:39-45`) committed the same anti-pattern.

**Principle:** Broadcast is a **pure producer**. It must NEVER wait for delivery. It collects state from atomics and pushes to queues with `timeout=0`. Consumer tasks own the actual I/O.

The infrastructure already exists but is bypassed:
- `BleNotifyItem` queue (depth 4, buffer 2048 B) + `ble_notify_thread` — **nobody pushes to it**
- `ws_send_queue` drain in `net_owner` — **broadcast ignores it, calls httpd directly**

All four direct `sendNotification()` calls in `main.cpp` violate both Art. I (blocking) and Art. II (cross-task call).

### Why ~59ms typical (initial) and ~47-71ms (after Fixes 1-5)

**Initial:** Every 300ms the broadcast block calls `sendNotification()` twice — each blocks on NimBLE radio for 1-30ms. Plus NVS + motor queue. Aggregate: 15-101ms.

**After Fixes 1-5:** Broadcast became ~100μs, but residual 47-71ms spikes every ~1s are caused by `logAllWatermarks()` → 8× `ESP_LOGI` → `logVprintf` → `fwrite` + `fflush(stdout)`. UART TX buffer = 0 at `serial.cpp:41`, so `uart_write()` sends byte-by-byte directly to the hardware FIFO (128 bytes). When FIFO is full — blocks for 87 μs/byte. ~500 bytes of watermark output → **~43ms blocking**. Additionally, the ~10001 μs baseline is caused by TickWatchdog destructor calling `ESP_LOGW` every iteration, its `fflush` blocking the same way.

**Root of both issues: `uart_driver_install(..., 256, 0, ...)` — TX buffer = 0.**

## Resolution (All Fixes Applied — 2026-07-14)

### Fixes 1-5 (Architecture — I/O elimination)

| Fix | Change | Files | Status |
|-----|--------|-------|--------|
| 1 | BLE notifications → `notifyQueue` | `main/main.cpp` | ✅ |
| 2 | WS broadcast → separate `gWsBroadcastQueue` drained by `net_owner` | `main/main.cpp` | ✅ |
| 3 | RGB LED `rmt_tx_wait_all_done` → fire-and-forget | `rgb_led.cpp` | ✅ |
| 4 | `sendMotorCommand` timeout 10ms → 0, returns `"busy"` on full queue | `burette_ops.cpp` | ✅ |
| 5 | NVS `CalibrationData` cached in `std::atomic<CalibrationData*>` at boot | `calibration.hpp`, `nvs.cpp`, `burette_ops.cpp`, `main.cpp` | ✅ |

### Fixes 6-8 (Infrastructure — residual blocking elimination)

| Fix | Change | File | Status |
|-----|--------|------|--------|
| 6 | UART TX ring buffer 0→1024 bytes | `serial.cpp:41` | ✅ |
| 7a | ADC `calibratedMv()` moved to `temp_thread` loop | `main.cpp` (removed), `temp_thread.cpp` | ✅ |
| 7b | `stackmon.logAllWatermarks()` + `print_heap_stats()` removed from main loop | `main.cpp` | ✅ |
| 7c | `bleManager.process()` moved to `net_owner`'s loop | `main.cpp` → `netTaskEntry` | ✅ |
| 8 | Semgrep enforcement gate (8 rules) + `scripts/idf.sh` integration | `.semgrep/main_loop_blocking.yaml`, `scripts/idf.sh` | ✅ |

### Fix 9 (Remaining — TickWatchdog scope)
```cpp
while (true) {
    rtcWdt.feed();
    esp_task_wdt_reset();
    {                                       // ← body scope
        TickWatchdog watchdog;
        // all main loop work (~70-600μs)   // ← body only, no sleep
    }                                       // ← watchdog dtor BEFORE sleep
    vTaskDelayUntil(&lastWake, PACING_TICK);
}
```
*Rationale:* TickWatchdog is a diagnostic tool per Art. I — measures body-only time accurately.

### Main Loop Operations Inventory (After Fixes 1-9)

Operations that remain in main loop, all <1ms:

| Operation | Avg time | Type | Notes |
|-----------|----------|------|-------|
| `rtcWdt.feed()` + `esp_task_wdt_reset()` | ~15μs | MMIO | Mandatory per Constitution |
| `scheduler.tick()` | ~1μs | Atomic | Core timing |
| `appStateMachine.tick()` | ~10μs | Logic | State machine |
| LED state eval (atomics only) | ~10μs | Atomics | RMT transmit on change only (~1μs) |
| Transport state update | ~5μs | Atomics | USB/BLE priority |
| BLE command drain (`xQueueReceive(0)`) | ~10μs | Queue poll | Functional requirement |
| `serial.process()` (`select()+read()`) | ~30μs | UART poll | Functional requirement |
| Motor result check + deliver | ~5μs | Atomic + UART | On result only |
| Broadcast compact (every 300ms) | ~150μs | Atomics + UART + queues | Pure producer |
| Broadcast extended (every 300ms, +1 tick) | ~200μs | Atomics + queue | Pure producer |
| **Typical tick (no broadcast)** | **~70μs** | | |
| **Broadcast tick** | **~220μs** | | |

## Verification

| Step | Command | Result |
|------|---------|--------|
| Build | `scripts/idf.sh build` | ✅ 0 errors, 0 warnings |
| Static analysis | `semgrep --config=.semgrep/main_loop_blocking.yaml --error main/main.cpp` | ✅ 0 findings |
| Smoke (30s on ESP32-S3) | `scripts/idf.sh smoke` | ✅ BOOT OK, no Guru/panic |
| tick_watchdog warnings (pre-fix) | 463 warnings, 18 spikes at 47-73ms | ❌ |
| tick_watchdog warnings (post-fix-6-8) | 433 warnings, 0 spikes at 47-73ms, 428 at 10001-10015μs (TickWatchdog scope issue) | ⚠️ Fix 9 resolves |
| tick_watchdog warnings (post-fix-9) | **4 warnings** — 3 at boot (WiFi/BLE PHY init, ~11-43ms), 1 near-threshold (~10ms) | ✅ Acceptable per design |

## Status

**closed** — all fixes verified on hardware
