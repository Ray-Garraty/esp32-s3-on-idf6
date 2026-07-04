---
type: CrashReport
title: "Phase 6 Post-Mortem — LL-006 through LL-009: Crash cascade from DRAM fragmentation to stack overflow on error path"
description: >
  Session fixed two production crashes (LL-006 Invalid mbox, LL-007 NimBLE NULL deref)
  but introduced a regression: adding wifi_deinit_retry() + heavy format! logging in the
  net_owner error path caused stack overflow (LL-008). WiFi init remains broken due to
  persistent MALLOC_CAP_INTERNAL fragmentation (LL-009).
tags: ["post-mortem", "dram-fragmentation", "stack-overflow", "wifi", "esp-idf-v6"]
timestamp: 2026-07-04
version: "1.0"
task_id: "phase6_postmortem"
crash_signature: "IllegalInstruction + stack overflow in task pthread — preceded by wifi:malloc buffer fail + ESP_ERR_HTTPD_TASK"
---

# Phase 6 Post-Mortem

## Session Summary

| Activity | Result |
|----------|--------|
| Debug LL-006 (Invalid mbox) | ✅ Fixed — unconditional `esp_netif_init()` in `esp_safe.rs` |
| Debug LL-007 (NimBLE NULL deref) | ✅ Fixed — BLE heap guard + `nimble_port_init()` return check |
| Plan post-LL-007 remediation | ✅ 8-step plan (FIX-A through FIX-E) |
| Implement 8 steps | ✅ All coded, verified compilation |
| Smoke test after fixes | ❌ **FAIL** — net_owner stack overflow, reboot loop |

## Timeline

1. **LL-006 discovery**: Crash log `serial_2026-07-03_21-48-51.log` showed `assert failed: tcpip_send_msg_wait_sem (Invalid mbox)`. Root cause: `esp_netif_init()` only called inside `EspWifi::wrap()`, never reached when WiFi init fails.

2. **LL-006 fix**: `src/esp_safe.rs:netif_init()` — safe FFI wrapper. Called unconditionally before `WifiManager::new()` in `main.rs`.

3. **LL-007 discovery**: After LL-006 fix, firmware still crashed — `LoadProhibited (EXCVADDR=0x00000000)` in NimBLE host task. Root cause: `esp32-nimble`'s `BLEDevice::init()` ignores `nimble_port_init()` return value, spawns host task even when event queue is NULL.

4. **LL-007 fix**: Patch `esp32-nimble/ble_device.rs` — check return, skip host task on failure. Add heap precondition `largest >= 20K` before `ble_mgr.init()` in project `ble.rs`. Reduce WiFi dynamic buffer counts 32→16.

5. **Smoke test after LL-007**: ✅ 70+ sec uptime, no crashes. WiFi STA failed, HTTP failed (DRAM), BLE skipped gracefully. Main loop slow (62-78ms).

6. **Remediation planning**: 8-step plan — FIX-A (WiFi DRAM: buffers 16→8, UART 8K→4K), FIX-C (WS guard, scheduler::tick, loop timing), FIX-D (misleading log), FIX-E (ADC diag).

7. **Implementation + smoke test**: All 8 steps applied. Build verified. **Smoke test failed** — net_owner thread stack overflow.

## Crash Analysis: LL-008 — Error Path Stack Overflow

### Crash Signature
```
IllegalInstruction + stack overflow in task pthread
net_owner watermark=0
```

### Root Cause
The `net_owner` thread (32 KB stack, `config::NET_OWNER_STACK`) executes the WiFi error path when `WifiManager::new()` fails. The error path was augmented with:

```rust
log::error!("WiFi init failed: {e:?}. Heap: free={}K, largest={}K, ...");
ecotiter_fw::esp_safe::wifi_deinit_retry(3);  // ← stack-heavy
```

The `wifi_deinit_retry(3)` function:
- Allocates stack for a loop (3 iterations)
- Each iteration calls `esp_wifi_deinit()` (C FFI, ~1-2KB stack)
- Calls `record_exit()` with format args
- Calls `log::warn!()` with format args
- Calls `esp_rom_delay_us()` (FFI)

Combined with the `log::error!` already in the error branch (which uses `format_args!` with 5 temporaries), the total stack usage exceeds 32 KB.

### Lesson
**Error paths consume MORE stack than happy paths.** `format!`/`log::error!` with many temporaries allocate on the stack, not heap. Adding FFI calls (retry loops) on an already-stressed error path pushes stack over budget.

### Fix
- Removed `wifi_deinit_retry(3)` from the error path
- The function stays in `esp_safe.rs` for manual use when needed
- Net_owner thread stack remains 32 KB

## Crash Analysis: LL-009 — WiFi `malloc buffer fail` Despite Adequate DRAM

### Crash Signature
```
W (1915) wifi:malloc buffer fail
E (1925) wifi:Expected to init 3 rx buffer, actual is 0
```

### Root Cause
The WiFi static RX buffer allocation (`CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM=3`) requires `MALLOC_CAP_INTERNAL` memory. Even though `heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT)` reports 108 KB, the `MALLOC_CAP_INTERNAL` pool is more constrained after thread stack allocations consume DRAM.

The WiFi driver's internal allocation path in ESP-IDF v6.0.1:
1. Allocates task stack (3.5 KB, `MALLOC_CAP_INTERNAL`)
2. Allocates static RX buffers (3×1600, `MALLOC_CAP_INTERNAL`)
3. Allocates dynamic buffer pools (8×1600 each, `MALLOC_CAP_INTERNAL`)

Step 2 fails because the static RX buffer pool requires contiguous `MALLOC_CAP_INTERNAL` that TLSF cannot satisfy after stacking Motor(16K)+Temp(16K)+UART(4K)+net_owner(32K).

### Lesson
**Reducing dynamic buffer counts does not help when static RX buffer allocation fails first.** The `CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM=3` is already at minimum. The root fix requires either:
- Moving thread stack allocations to `MALLOC_CAP_8BIT` non-internal memory
- Or increasing pre-WiFi available DRAM by reducing other allocations
- Or upgrading ESP-IDF to v6.1+ which may have improved allocation

### Current Status
**WiFi STA init remains broken.** AP fallback works (uses different netif stack).

## What Works (Green)

| Feature | Status |
|---------|--------|
| `esp_netif_init()` unconditional | ✅ LL-006 fix |
| BLE graceful degradation | ✅ LL-007 fix |
| NimBLE host task guard | ✅ ll-007 fix |
| ADC diagnostics | ✅ FIX-E |
| Scheduler `tick()` call | ✅ FIX-C2 |
| Main loop timing instrumentation | ✅ FIX-C3 |
| WS broadcast guard (`G_HTTP_SERVER_ALIVE`) | ✅ FIX-C1 |
| UART stack 8K→4K | ✅ FIX-A2 |
| WiFi buffers 32→16→8 | ✅ FIX-A1 (partial) |

## What's Broken (Red)

| Feature | Status | Owner |
|---------|--------|-------|
| WiFi STA init | ❌ `malloc buffer fail` on static RX | ESP-IDF v6 quirk |
| HTTP server | ❌ `ESP_ERR_HTTPD_TASK` — no 12K contiguous | Blocked by WiFi |
| BLE full init | ❌ Heap < 20K largest | Blocked by WiFi |
| ADC raw > 0 | ❌ Reads 0 (hardware or config) | Needs investigation |

## Lessons Added

| ID | Title |
|----|-------|
| LL-006 | `esp_netif_init()` must be called unconditionally, not only inside `EspWifi::wrap()` |
| LL-007 | `esp32-nimble` ignores `nimble_port_init()` return — BLE host task crashes with NULL eventq |
| LL-008 | Error paths consume more stack than happy paths — adding FFI retry loops + format! logging to error branch overflows budgeted stack |
| LL-009 | WiFi static RX buffer allocation fails despite adequate free DRAM — `MALLOC_CAP_INTERNAL` fragmentation from thread stacks |
