---
type: CrashReport
title: "Boot-Time HTTP Server Init Crashes with Invalid mbox Assertion"
description: "esp_netif_init() is never called when WiFi init fails, leaving the lwIP TCP/IP thread uninitialized. Any subsequent socket operation (HTTP server creation) crashes with assert failed: tcpip_send_msg_wait_sem (Invalid mbox)."
tags: ["lwip", "init-order", "dram-fragmentation", "wifi", "http"]
timestamp: 2026-07-04
version: "1.0"
task_id: "boot_invalid_mbox_2026-07-04"
crash_signature: "PC=0x40091100 EXCVADDR=0x0 assert_failed: tcpip_send_msg_wait_sem (Invalid mbox)"
---

# Crash Report: Boot-Time HTTP Server Init Crashes with "Invalid mbox" Assertion

## Verdict

- **Status:** root_cause_found
- **Root Cause:** `esp_netif_init()` is never called when WiFi init fails, leaving the lwIP TCP/IP thread uninitialized. Any subsequent socket operation (HTTP server creation) crashes with `assert failed: tcpip_send_msg_wait_sem (Invalid mbox)`.
- **Confidence:** high

## Evidence Chain

### Step 1: Triage

**Crash dump analysis** (from `logs/serial_2026-07-03_21-48-51.log`):

```
exccause=0 name=IllegalInstruction pc=0x40091100 excvaddr=0x00000000 sp=0x3fffcee0
```

The panic handler's decoded backtrace shows the crash originates from:

| Frame | Function | Source Location |
|-------|----------|-----------------|
| 0 | `panic_abort` | `esp-idf/components/esp_system/panic.c:464` |
| 1 | `esp_system_abort` | `esp-idf/components/esp_system/port/esp_system_chip.c:87` |
| 2 | `__assert_func` | `esp-idf/components/esp_libc/src/assert.c:81` |
| 3 | **`tcpip_send_msg_wait_sem`** | **`lwip/lwip/src/api/tcpip.c:454`** — `assert failed: "Invalid mbox"` |
| 4 | `netconn_apimsg` | `lwip/lwip/src/api/api_lib.c:134` |
| 5 | `netconn_new_with_proto_and_callback` | `lwip/lwip/src/api/api_lib.c:164` |
| 6 | `lwip_socket` | `lwip/lwip/src/api/sockets.c:1759` |
| 7 | `socket` | `lwip/include/lwip/sockets.h:70` |
| 8 | `httpd_start` | `esp-idf/components/esp_http_server/src/httpd_main.c:521` |
| 9 | `EspHttpServer::internal_new` | `esp-idf-svc/src/http/server.rs:389` |
| 10 | `EspHttpServer::new` | `esp-idf-svc/src/http/server.rs:348` |
| 11 | `HttpServer::new` | `src/infrastructure/network/http_server.rs:171` |
| 12 | `ecotiter::main::{closure#2}` | `src/main.rs:311` (net_owner thread) |

**Determinism:** The crash occurs on EVERY boot at the same point (HTTP server creation) — 100% deterministic, same PC, same backtrace.

### Step 2: S1–S5 Protocol

| Step | Result | Action |
|------|--------|--------|
| **S1** (stack watermark) | All watermarks = 0 (not yet measured — crash occurs before first periodic check at 10s) | Not stack overflow |
| **S2** (heap integrity) | Passed at boot: `[INFO] Heap integrity OK` | Heap was clean at boot |
| **S3** (smoke test) | Not executed (log is from live firmware) | N/A |
| **S4** (delta analysis) | 3 commits since known-good `f4bcc00`: diagnostic subsystem, crash handler, script rename. No sdkconfig changes affecting WiFi/HTTP init order | Network init unchanged |
| **S5** (red flags) | `_sysloop_keepalive` clone present ✅ — LL-004 pattern applied. No `esp_netif_init()` or `EspNetif::new()` outside WiFi path ❌ | **CRITICAL FINDING** |

### Step 3: Elimination

#### Evidence 1: Heap trajectory

```
Boot:              free=159KB, largest=108KB  (clean DRAM)
Pre-WiFi:          free=136K,  largest=108K   (after Motor+Temp+UART stacks)
Post-WiFi failure: free=18K,   largest=6K     (after esp_wifi_init failure)
```

The WiFi init consumes 118K of DRAM (`esp_wifi_init(&cfg)` allocates 32 dynamic RX buffers, 32 dynamic TX buffers, 32 cache TX buffers, 32 management buffers, lwIP memory pools, and the WiFi driver task). When `esp_wifi_init()` fails (`create wifi task: failed to create task`, code 0x3001 = ESP_ERR_NO_MEM), the cleanup (`esp_wifi_deinit()`) ALSO fails (returns ESP_ERR_NO_MEM), leaving the heap fragmented with only 6K largest block.

#### Evidence 2: The init path (root cause)

In `WifiManager::new()` (`src/infrastructure/network/wifi.rs:91–117`):

```rust
pub fn new<M: WifiModemPeripheral + 'd>(
    modem: M,
    sys_loop: EspSystemEventLoop,
    nvs: Option<EspDefaultNvsPartition>,
    ble_active: Arc<AtomicBool>,
) -> Result<Self, NetworkError> {
    let wifi = EspWifi::new(modem, sys_loop.clone(), nvs)?;  // ← FAILS here
    let wifi = BlockingWifi::wrap(wifi, sys_loop)?;
    // ...
}
```

`EspWifi::new()` calls `WifiDriver::new()` → `Self::init()` → `esp_wifi_init(&cfg)` → **FAILS** (no memory).

On the `?` early return, `EspWifi::wrap()` (which calls `EspNetif::new()` → `initialize_netif_stack()` → `esp_netif_init()`) is **NEVER executed**.

#### Evidence 3: `initialize_netif_stack()` only fires inside `EspNetif::new()`

From `esp-idf-svc/src/netif.rs:310–320`:

```rust
fn initialize_netif_stack() -> Result<(), EspError> {
    let mut guard = INITALIZED.lock();
    if !*guard {
        esp!(unsafe { esp_netif_init() })?;  // ← Creates lwIP tcpip thread
        *guard = true;
    }
    Ok(())
}
```

This is called from `EspNetif::new_with_conf()` (line 335), which is only called from `EspWifi::wrap()` (line 1582-1584 of `wifi.rs`):
```rust
pub fn wrap(driver: WifiDriver<'d>) -> Result<Self, EspError> {
    Self::wrap_all(
        driver,
        EspNetif::new(NetifStack::Sta)?,   // ← Would call esp_netif_init()
        EspNetif::new(NetifStack::Ap)?,
    )
}
```

Since `wrap()` is never reached, `esp_netif_init()` is **never called**. The lwIP TCP/IP thread is never created. Its message queue (mbox) stays in the zero-initialized (invalid) state.

#### Evidence 4: LL-004 `_sysloop_keepalive` is insufficient

The `_sysloop_keepalive` pattern in `src/main.rs:278` correctly clones `EspSystemEventLoop` to prevent the event loop destructor from calling `esp_event_loop_delete_default()`. However, `EspSystemEventLoop::take()` (line 150) only calls `esp_event_loop_create_default()` — it does NOT call `esp_netif_init()`. The lwIP TCP/IP thread is a separate subsystem.

So LL-004's fix was **necessary but not sufficient**. It prevents the event loop from being dropped (which would kill an already-running tcpip thread), but it does not create the tcpip thread in the first place.

#### Evidence 5: No safe wrapper for `esp_netif_init()` exists

`src/esp_safe.rs` has 8 safe FFI wrappers (disable_wdt, suppress_httpd_txrx_logs, check_heap_integrity, heap_stats, stack_watermark, restart, panic_write_str, set_coex_ble_preferred) — but **none for `esp_netif_init()`**.

### Step 4: Root Cause

```yaml
root_cause:
  category: init_order_error
  description: >
    esp_netif_init() is never called because it's only invoked through
    EspNetif::new() → initialize_netif_stack(), which is only reached
    inside EspWifi::wrap(). When WifiDriver::new() fails (due to
    esp_wifi_init() running out of DRAM), the wrap() is skipped,
    leaving the lwIP TCP/IP thread uninitialized. The HTTP server
    creation then crashes on socket() because tcpip_send_msg_wait_sem()
    finds an uninitialized mbox.
  evidence:
    - "Decoded backtrace: assert failed at tcpip_send_msg_wait_sem (Invalid mbox)"
    - "heap trajectory: 136K → 18K after WiFi init failure, largest block = 6K"
    - "esp_netif_init() only called inside EspWifi::wrap(), which is never reached when EspWifi::new() fails"
    - "No explicit esp_netif_init() or EspNetif::new() call exists outside the WiFi init path"
    - "LL-004 _sysloop_keepalive clone is present but only addresses event loop lifecycle, not netif stack init"
  confidence: high
  reproduction: >
    Flash firmware, boot without NVS credentials (first boot).
    WiFi init will fail (NVS empty + DRAM fragmentation).
    HTTP server init crashes every time.
```

## Contributing Factors

### 1. DRAM Fragmentation (LL-003)

Motor (16K) + Temp (16K) + UART (8K) = 40K stack allocation before WiFi init fragments the DRAM. Combined with `CONFIG_ESP_WIFI_DYNAMIC_RX_BUFFER_NUM=32`, `CONFIG_ESP_WIFI_DYNAMIC_TX_BUFFER_NUM=32`, and `CONFIG_ESP_WIFI_CACHE_TX_BUFFER_NUM=32`, the WiFi init allocates numerous dynamic buffers that consume ~118K of heap, leaving only 18K free. The WiFi driver task creation fails due to insufficient contiguous memory.

### 2. WiFi cleanup also fails

`esp_wifi_deinit()` returns `ESP_ERR_NO_MEM` (0x3001), so the massive WiFi init allocations are never freed. This compounds the fragmentation across retries within the same boot cycle.

### 3. Stack watermarks not measured before crash

The `=== STACK ===` section shows all watermarks = 0. The periodic watermark check in the main loop (every 1000 ticks = ~10s) hasn't run yet when the crash occurs at ~15s into boot. The emergency dump in the crash handler reads `slot_watermarks[slot]` which defaults to 0. This is an instrumentation gap — the crash handler should call `uxTaskGetStackHighWaterMark()` directly rather than reading from a slot that may not have been populated yet.

## Fix

### Required Fix (Complex — requires coordination with Implementer)

**Add unconditional `esp_netif_init()` call before any network operations.**

Add a safe wrapper in `src/esp_safe.rs`:

```rust
/// Initialize the ESP-NETIF (lwIP TCP/IP) stack.
///
/// Must be called once at boot before any network operations (WiFi, HTTP, BLE).
/// Safe to call multiple times (internally idempotent via a static flag).
/// Creates the lwIP TCP/IP thread and its message queue.
pub fn netif_init() {
    diag::ffi_guard::record_enter(diag::ffi_guard::FFI_ESP_HEAP);
    // SAFETY:
    //   Invariant: esp_netif_init() is idempotent — subsequent calls return
    //   ESP_OK without reinitializing.
    //   Context: called once at boot before any network operations.
    //   Risk: none — call is guarded by ESP-IDF's internal init flag.
    unsafe {
        esp_idf_sys::esp_netif_init();
    }
    diag::ffi_guard::record_exit(diag::ffi_guard::FFI_ESP_HEAP, 0);
}
```

In `src/main.rs`, call `netif_init()` after `EspSystemEventLoop::take()` and before `WifiManager::new()`:

```rust
// Line 150 (existing)
let sys_loop = EspSystemEventLoop::take().expect("System event loop");

// ADD THIS:
// Initialize lwIP TCP/IP stack unconditionally — needed for HTTP server
// even when WiFi init fails (esp_netif_init() is only called inside
// EspWifi::wrap(), which is skipped on WiFi init failure).
ecotiter_fw::esp_safe::netif_init();
```

### Alternative Fix (Simpler)

Create a lightweight `EspNetif` in the offline WiFi path to trigger `initialize_netif_stack()`. However, this is less clean because it creates a netif handle that must be kept alive.

### Verification

1. Flash firmware with fix applied
2. Observe that WiFi init may still fail (DRAM fragmentation is a separate issue, partially addressed by LL-003)
3. HTTP server now starts successfully even in offline/AP mode — no crash
4. The "Invalid mbox" assertion should be gone

### Files to Modify

| File | Change |
|------|--------|
| `src/esp_safe.rs` | Add `pub fn netif_init()` safe wrapper |
| `src/main.rs` (line ~151) | Call `ecotiter_fw::esp_safe::netif_init()` after `EspSystemEventLoop::take()` |

## Investigation Artifacts

| File | Status |
|------|--------|
| Investigation markers | N/A — no instrumentation added |
| Lessons learned | ❌ LL-004 needs updating — add that `_sysloop_keepalive` alone is insufficient; `esp_netif_init()` must also be called unconditionally |

## Remaining Issues

1. **WiFi init fragility:** With `CONFIG_ESP_WIFI_DYNAMIC_RX_BUFFER_NUM=32`, `CONFIG_ESP_WIFI_DYNAMIC_TX_BUFFER_NUM=32`, `CONFIG_ESP_WIFI_CACHE_TX_BUFFER_NUM=32`, the WiFi driver allocates ~118K of heap during init. If any single allocation fails (e.g., task stack creation with 3.5K+), the entire WiFi init fails. Consider reducing dynamic buffer counts or increasing early-boot DRAM availability.

2. **Stack watermarks unreadable in crash handler:** The `=== STACK ===` section shows all watermarks = 0 because the crash happens before the periodic watermark check runs. The crash handler should call `uxTaskGetStackHighWaterMark()` directly instead of reading from pre-populated slots.

3. **WiFi deinit failure:** When `esp_wifi_init()` fails, `esp_wifi_deinit()` ALSO fails with ESP_ERR_NO_MEM. This means WiFi-allocated memory is never freed during the boot cycle. If the firmware retries WiFi init, it will have even less memory. This should be addressed in a follow-up.

## Relevant Rules Violated

| Rule | Description | Status |
|------|-------------|--------|
| GR-3 (DRAM init order) | WiFi → HTTP → BLE init order observed | ✅ Correct |
| LL-004 (sysloop lifecycle) | `_sysloop_keepalive` clone present | ✅ Applied, but insufficient |
| **Missing init requirement** | `esp_netif_init()` must be called unconditionally | ❌ **New finding — not in existing rules** |

### Recommended new lesson (LL-006):

```yaml
- id: LL-006
  date: 2026-07-04
  crash_signature: "assert failed: tcpip_send_msg_wait_sem ... (Invalid mbox) — preceded by WiFi init failure"
  category: init_order_error
  trigger_patterns:
    - "Invalid mbox"
    - "tcpip_send_msg_wait_sem"
    - "create wifi task: failed to create task"
    - "assert failed: tcpip_send_msg_wait_sem /IDF/components/lwip"
  lesson: >
    esp_netif_init() (which creates the lwIP TCP/IP thread) is only called
    inside EspWifi::wrap() → EspNetif::new() → initialize_netif_stack().
    When EspWifi::new() fails (e.g., from DRAM exhaustion in esp_wifi_init),
    the wrap() is never reached, so the TCP/IP thread is never created.
    Even with the LL-004 _sysloop_keepalive fix (which prevents event loop
    drop), the TCP/IP thread does not exist. Any socket operation — HTTP server
    bind, UdpSocket::bind for DNS, BLE GATT — crashes with:
      assert failed: tcpip_send_msg_wait_sem ... (Invalid mbox)
    
    The fix: call esp_netif_init() unconditionally at boot, before any
    network operations, separate from the WiFi init path. This ensures the
    lwIP TCP/IP thread exists even when WiFi init fails.
  diagnostic: >
    Check if esp_netif_init() is called before WifiManager::new().
    If not, add an explicit call via esp_safe::netif_init() or
    EspNetif::new(NetifStack::Sta) before any network operations.
  fix: >
    Call esp_netif_init() unconditionally after EspSystemEventLoop::take()
    and before WifiManager::new(). Add safe wrapper in src/esp_safe.rs.
```

Note: LL-004's description should also be updated to clarify that `esp_netif_init()` is separate from `EspSystemEventLoop::take()` and must be called explicitly.
