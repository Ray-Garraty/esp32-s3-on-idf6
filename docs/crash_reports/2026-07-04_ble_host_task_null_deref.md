---
type: CrashReport
title: "BLE Host Task NULL Dereference (NimBLE Eventq)"
description: "NimBLE BLE host FreeRTOS task launches with uninitialized event queue after nimble_port_init() fails — return value ignored in esp32-nimble crate's BLEDevice::init(), causing NULL pointer dereference in npl_freertos_eventq_get()"
tags: ["ble", "nimble", "null-dereference", "dram-fragmentation", "esp32-nimble"]
timestamp: 2026-07-04
version: "1.0"
task_id: manual
crash_signature: "PC=0x402cf9d7 EXCVADDR=0x00000000 A2=0x00000000"
---

# Crash Report: BLE Host Task NULL Dereference (NimBLE Eventq)

## Verdict

- **Status:** root_cause_found
- **Root Cause:** NimBLE BLE host FreeRTOS task launches with uninitialized event queue after `nimble_port_init()` fails — return value ignored in `esp32-nimble` crate's `BLEDevice::init()`
- **Confidence:** high

## Crash Signature

| Field | Value |
|-------|-------|
| PC | `0x402cf9d7` |
| EXCVADDR | `0x00000000` (NULL dereference) |
| EXCCAUSE | 28 (LoadProhibited) |
| A2 | `0x00000000` (first argument = `evq` = NULL) |
| A3 | `0xffffffff` (second argument = `tmo` = BLE_NPL_TIME_FOREVER = -1) |
| Crash thread | t255 (NimBLE host FreeRTOS task) |
| Determinism | **Fully deterministic** — same crash on every boot cycle |

## Decoded Backtrace

```
0x402cf9d4: npl_freertos_eventq_get
    at npl_os_freertos.c:367
    → ret = xQueueReceive(eventq->q, &ev, tmo);
    → eventq is NULL because evq->eventq was never initialized

0x40084322: ble_npl_eventq_get (inlined)
    at nimble_npl_os.h:167
    (inlined by) nimble_port_run
    at nimble_port.c:420
    → ev = ble_npl_eventq_get(&g_eventq_dflt, BLE_NPL_TIME_FOREVER);

0x4017ef9e: <esp32_nimble::ble_device::BLEDevice>::blecent_host_task
    at ble_device.rs:365
    → esp_idf_sys::nimble_port_run();

0x403670da: vPortTaskWrapper
    at port.c:143
    → FreeRTOS task entry wrapper
```

## Root Cause

### Primary: Unchecked Return Value in `esp32-nimble`'s `BLEDevice::init()`

**File:** `~/.cargo/git/checkouts/esp32-nimble-2f1452d0552dfadc/f60d7c5/src/ble_device.rs:75`

```rust
esp_idf_sys::nimble_port_init();  // ← RETURN VALUE DISCARDED
```

`nimble_port_init()` (in ESP-IDF's `nimble_port.c:274`) calls `esp_nimble_init()` which internally calls `esp_nimble_hci_init()` at line 175. When HCI init fails (due to DRAM exhaustion), `esp_nimble_init()` returns `ESP_FAIL` **before** reaching line 192 (`ble_npl_eventq_init(&g_eventq_dflt)`). The event queue is never initialized:

```c
// nimble_port.c / esp_nimble_init()
#if CONFIG_BT_CONTROLLER_ENABLED
    if(esp_nimble_hci_init() != ESP_OK) {
        ESP_LOGE(NIMBLE_PORT_LOG_TAG, "hci inits failed\n");
        return ESP_FAIL;  // ← EARLY RETURN! ble_npl_eventq_init() at line 192 never reached
    }
#endif
    /* Initialize default event queue */
    ble_npl_eventq_init(&g_eventq_dflt);  // ← NEVER REACHED
```

Despite the failure, execution continues to line 92:
```rust
esp_idf_sys::nimble_port_freertos_init(Some(Self::blecent_host_task));  // ← SPAWNS TASK ANYWAY
```

This spawns the FreeRTOS host task (`nimble_port_freertos.c:43`):
```c
xTaskCreatePinnedToCore(host_task, "nimble_host", NIMBLE_HS_STACK_SIZE, ...);
```

The spawned task immediately calls `nimble_port_run()` → `ble_npl_eventq_get(&g_eventq_dflt, ...)` → `npl_freertos_eventq_get()`.

The `g_eventq_dflt` macro expands to `ble_npl_ctx->eventq` (with `BLE_STATIC_TO_DYNAMIC`). While `ble_npl_ctx` was allocated (`ble_npl_ensure_ctx()` runs BEFORE the early return at line 157), `ble_npl_eventq_init()` was never called, so `eventq.eventq` is NULL (zeroed by calloc). The dereference at `npl_os_freertos.c:356`:

```c
struct ble_npl_eventq_freertos *eventq = (struct ble_npl_eventq_freertos *)evq->eventq;
// eventq = NULL because evq->eventq was never initialized
...
ret = xQueueReceive(eventq->q, &ev, tmo);  // ← LoadProhibited at 0x00000000
```

**Additionally**, the calling `net_owner` thread spins forever in the `SYNCED` waiting loop at `ble_device.rs:95-101` — `SYNCED` will never be set because `on_sync()` is never called (stack init failed).

### Contributing: DRAM Fragmentation (same as LL-003 pattern)

The root cause of `esp_nimble_hci_init()` failure is DRAM fragmentation:

| Phase | Free | Largest | DMA Largest |
|-------|------|---------|-------------|
| Boot | 159K | 108K | 108K |
| Pre-WiFi (after Motor 16K + Temp 16K + UART 4K) | 136K | 108K | 108K |
| After WiFi init failure | 14K | **6K** | **6K** |
| After HTTP server failure | 22K | 9K | — |
| After BLE controller init | — | — | — |

Thread stacks fragment DRAM:
- Motor: 16K
- Temperature: 16K
- UART: 4K
- Net_owner: 32K
- Main: 32K (FreeRTOS)
- **Total stacks: ~100K of internal SRAM**

The WiFi STA init fails (`create wifi task: failed to create task`) because `esp_wifi_init()` cannot allocate its internal structures from the fragmented heap.

HTTP server fails (`ESP_ERR_HTTPD_TASK` 45064) because it needs 12K contiguous `MALLOC_CAP_INTERNAL` but only 6K is available.

BLE controller init partially succeeds (we see the MAC address), but `esp_nimble_hci_init()` fails because HCI transport buffer allocation cannot find contiguous memory.

### Contributing: NVS First-Boot Warning (Red Herring)

```
[WARN] WiFi: NVS open failed: NvsOpenFailed
```

Per **LL-003**, this is a red herring. The project's `nvs::nvs_init()` at `main.rs:152` successfully initializes the NVS flash. The warning comes from `WifiManager::load_credentials_from_nvs()` trying to open the "wifi" namespace on a fresh/empty NVS. It does NOT affect WiFi or BLE init.

### Skipped S1: Stack Watermarks (Watermarks unavailable due to LL-005 gap)

The stack watermark values read `0` for all threads because:
- The crash is a hardware exception (LoadProhibited), NOT a Rust panic
- Per **LL-005**, `std::panic::set_hook()` does NOT fire for hardware exceptions
- The diag `__wrap_esp_panic_handler` (in `esp_safe.rs`) exists but may not be fully wired to the NimBLE crash context (the crash is in a FreeRTOS task, not a Rust `std::thread`)

**However, stack overflow is NOT suspected here.** The main task already has `CONFIG_ESP_MAIN_TASK_STACK_SIZE=32768` and the `net_owner` thread has 32K. The crash is a deterministic NULL pointer dereference, not a stack overflow.

## Fix Recommendations

### Fix 1: Check `nimble_port_init()` Return Value (CRITICAL)

**File:** `esp32-nimble` crate (local patch or fork at `~/.cargo/git/checkouts/esp32-nimble-2f1452d0552dfadc/f60d7c5/src/ble_device.rs`)

Change `BLEDevice::init()` to check the return value of `nimble_port_init()` and **skip spawning the host task** if initialization failed:

```rust
pub fn init() -> Result<(), EspError> {
    unsafe {
        let initialized = INITIALIZED.load(Ordering::Acquire);
        if !initialized {
            // ... existing NVS init ...
            
            // CHECK RETURN VALUE
            esp_nofail!(esp_idf_sys::nimble_port_init())?;
            
            // ... existing ble_hs_cfg setup ...
            esp_idf_sys::nimble_port_freertos_init(Some(Self::blecent_host_task));
        }
        loop {
            let syncd = SYNCED.load(Ordering::Acquire);
            if syncd {
                break;
            }
            esp_idf_sys::vPortYield();
        }
        INITIALIZED.store(true, Ordering::Release);
    }
    Ok(())
}
```

**If patching upstream is not feasible**, add a post-init guard in the project's `ble.rs::BleManager::init()`:

```rust
pub fn init(&mut self) -> Result<(), NetworkError> {
    BLEDevice::init();  // Ignored error — but we can't fix the crate
    
    // Post-init check: verify the NimBLE host task didn't crash
    // by checking if INITIALIZED flag was set
    // (requires adding accessor to esp32-nimble or checking BLE state)
    
    // Safer approach: add pre-condition check for heap
    let (_, largest, _) = ecotiter_fw::esp_safe::heap_stats();
    if largest < 12 * 1024 {
        return Err(NetworkError::BleInitFailed);  // Skip BLE entirely
    }
    
    // ... rest of init ...
}
```

### Fix 2: Reduce DRAM Fragmentation

The `CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM=3` is already set (LL-003 fix). Additional steps:

**a) Reduce Dynamic RX Buffer Count** in `sdkconfig.defaults`:
```
CONFIG_ESP_WIFI_DYNAMIC_RX_BUFFER_NUM=16  # Was 32
```

**b) Reduce Dynamic TX Buffer Count** in `sdkconfig.defaults`:
```
CONFIG_ESP_WIFI_DYNAMIC_TX_BUFFER_NUM=16  # Was 32
```

**c) Consider moving some threads to smaller stacks:**
- UART thread: 4K → check if this is sufficient
- Temperature: 16K → could potentially use 8K

**d) Reorder BLE init to check heap pre-condition:**
Move BLE init to only when `heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) >= 20K`.

### Fix 3: Graceful Init Degradation (Resilience Pattern)

In the `net_owner` thread (`main.rs:321-326`), wrap BLE init with heap pre-check:

```rust
// Init BLE only if heap has enough contiguous memory
let (_, largest, _) = ecotiter_fw::esp_safe::heap_stats();
if largest >= 20 * 1024 {
    match ble_mgr.init() {
        Ok(()) => info!("BLE: init OK"),
        Err(e) => log::error!("BLE init failed: {e:?}"),
    }
} else {
    log::warn!("BLE: skipped — insufficient contiguous heap (largest={largest}K, need 20K)");
}
```

This prevents the NimBLE stack from being initialized at all when DRAM is too fragmented, avoiding the crash cascade.

### Fix 4: NVS First-Boot Handling

While per LL-003 the NVS warning is a red herring, properly initializing NVS on first boot would clean up the log and avoid confusion. The existing `nvs::nvs_init()` at `main.rs:152` handles this correctly — no action needed.

## Relevant Rules Violated

| Rule | Description | How Violated |
|------|-------------|--------------|
| GR-7 | Mandatory diagnostic instrumentation — pre-init guards for large allocs | No `heap_snapshot::assert_can_allocate()` before BLE init's HCI alloc |
| §5.3 | Pre-init guard for BLE: `initialized: bool` in `BleManager`; `process()`/`is_connected()` early-return if not initialized | The `initialized` flag exists but only protects against user calls, not against the NimBLE host task crash itself |
| §5.3 | "Never call `BLEDevice::take().get_server()` before `init()` — global statics with internal mutexes, will panic" | Related: the host task crash demonstrates that `BLEDevice::init()` can fail silently |

## LL-00x Designation

**Suggested: LL-007** — "NimBLE BLE host FreeRTOS task launches with uninitialized state if `nimble_port_init()` fails; return value is ignored in `esp32-nimble` crate, causing NULL pointer dereference in `npl_freertos_eventq_get()`."

### Trigger Patterns
- `exccause=28 name=LoadProhibited` AND
- `pc=0x402cf9d7` (or `npl_freertos_eventq_get`) AND
- Backtrace includes `blecent_host_task` at `ble_device.rs:365` AND
- Preceded by `BLE_INIT: hci inits failed` / `nimble host init failed`

### Lesson
> The `esp32-nimble` crate's `BLEDevice::init()` ignores the return value of `nimble_port_init()`. When this function fails (e.g., from DRAM exhaustion causing `esp_nimble_hci_init()` to fail), the BLE host FreeRTOS task is still spawned via `nimble_port_freertos_init()`. The task immediately crashes with a NULL dereference because the default event queue (`g_eventq_dflt`) was never initialized (the init happens after the early return).
>
> Always check `nimble_port_init()` return value before proceeding to spawn the host task. Additionally, add a heap pre-condition check before attempting BLE init: verify `heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) >= 20K`.

### Diagnostic
1. Look for `BLE_INIT: hci inits failed` in the boot log.
2. Look for `BLE_INIT: nimble host init failed`.
3. Confirm the crash backtrace includes `npl_freertos_eventq_get` called from `blecent_host_task`.
4. The crash immediately follows "BLE Host Task Started".
5. Check heap trajectory: largest contiguous block < 12K before BLE init.

### Fix
1. Patch `esp32-nimble`'s `BLEDevice::init()` to check `nimble_port_init()` return value and skip `nimble_port_freertos_init()` on failure.
2. Add heap pre-condition check (`largest >= 20K`) before calling `ble_mgr.init()` in the project code.
3. Reduce WiFi dynamic buffer counts to minimize DRAM fragmentation.

## Investigation Artifacts

| File | Status |
|------|--------|
| `src/bin/smoke_test.rs` | N/A — not created (crash is deterministic, S1-S5 partially applicable) |
| `[INVESTIGATION]` markers | N/A — no markers added |
| Lessons learned | ✅ LL-007 suggested |

## Verification

To verify fixes:
1. After applying Fix 1 (heap pre-check), flash to fresh hardware.
2. Confirm no Guru Meditation on boot.
3. Confirm log shows "BLE: skipped — insufficient contiguous heap" (when DRAM fragmented).
4. After applying Fix 2 (reduced WiFi buffers), confirm HTTP server init succeeds.
5. Full init sequence: WiFi STA → HTTP → BLE all healthy.
