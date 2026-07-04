---
type: CrashReport
version: "1.0"
task_id: "uart-stdin-panic-2026-07-04"
timestamp: "2026-07-04"
crash_signature: "UART reader thread panic on std::io::stdin().read() — first call panics in spawned std::thread with 8KB stack"
title: "UART stdin panic on ESP32"
description: "std::io::stdin().read() panics on ESP32 because ESP-IDF boot console does not install UART driver for interrupt-driven input; only polled output is configured"
tags: [uart, stdin, panic, esp-idf, vfs, driver]
---

# Crash Report

## Verdict

- **Status:** root_cause_found
- **Root Cause:** ESP-IDF's boot console only sets up polled-mode **output** (`printf`/`println!` via direct UART register writes) but does NOT install the UART driver required for interrupt-driven blocking **input**. When `std::io::stdin().read()` is called on UART0 (fd 0), the VFS layer has no UART driver backing the read path, causing a panic.
- **Confidence:** high

## Evidence Chain

### Step 1: Triage

**Problem:** The UART reader thread (spawned with `std::thread::Builder::new().stack_size(8192)`) panics on the first call to `std::io::stdin().read(&mut buf)`.

**Observation:** `println!()` works fine throughout — it writes to stdout (fd 1) using the boot console's polled-mode UART0 register writes. But `std::io::stdin().read()` calls `libc::read(0, ...)` which goes through ESP-IDF's VFS layer, requiring the UART driver to be installed.

**Pattern matched:** Commented-out code in `esp-idf-sys/src/start.rs` (lines 53-77) shows exactly this initialization was intended:
```rust
// #[cfg(all(feature = "uart0_driver_init", esp_idf_comp_driver_enabled, esp_idf_comp_vfs_enabled))]
// mod uart_init {
//     pub(super) fn init_uart0() {
//         uart_driver_install(0, 512, 512, 10, null_mut(), 0);
//         esp_vfs_dev_uart_use_driver(0);
//     }
// }
```

The `uart0_driver_init` feature was never implemented — the code was left commented out.

### Step 2: S1–S5 Protocol

| Step | Result | Action |
|------|--------|--------|
| S1 (stack watermark) | 8192 bytes | Unclear — thread panics before watermark check runs. LL-010 already addressed stack=8KB. |
| S2 (heap integrity) | Not at fault | Heap OK before thread spawn. Issue is not heap corruption. |
| S3 (smoke test) | Not applicable | `stdin().read()` is the crash point, not boot-level init. |
| S4 (delta analysis) | Bindings analysis | ESP-IDF v6 bindings have `uart_vfs_dev_register()`, `uart_vfs_dev_use_driver()`, `uart_driver_install()` — all available but not called. |
| S5 (red flags) | Missing UART driver init | The UART driver is NOT installed at boot. The `esp-idf-sys` crate has the init code commented out. |

### Step 3: Elimination

**Technique A — Binary search via documentation:**
- `println!()` works → stdout connected, UART0 hardware works
- `std::io::stdin().read()` panics → stdin not connected through VFS
- `libc::read(0, ...)` would also fail → not a Rust std wrapper issue

**Technique D — `init_array` Inspection (bindings check):**
- Verified bindings include all needed functions:
  - `uart_vfs_dev_register()` at bindings:79277
  - `uart_vfs_dev_use_driver(uart_num: c_int)` at bindings:79299
  - `uart_driver_install(...)` at bindings:78915
  - `uart_is_driver_installed(uart_num)` at bindings:79287
- These are NOT called during the boot process

### Step 4: Root Cause

**Category:** `api_misuse` — missing mandatory UART driver/VFS initialization

**Explanation:**
ESP-IDF v6 on the ESP32 has two separate I/O paths for the UART console:

1. **Polled-mode output** (boot console): `printf()` / `esp_log_write()` / `write(1, ...)` writes directly to UART0 FIFO registers. This is set up by default during `esp_system_startup()` → `esp_vfs_console_register()`. It works for output WITHOUT installing the UART driver.

2. **Driver-mode input** (stdin): `read(0, ...)` goes through the VFS layer which dispatches to the UART driver. This REQUIRES:
   - `uart_vfs_dev_register()` — register the UART VFS device (creates `/dev/uart/0`)
   - `uart_driver_install(UART_NUM_0, ...)` — install the UART0 driver with RX buffers and interrupt handlers
   - `uart_vfs_dev_use_driver(UART_NUM_0)` — switch VFS from polled to driver mode

Without steps 2a–2c, `std::io::stdin().read()` panics because the VFS layer has no driver backing fd 0 for reads.

**Evidence from `esp-idf-sys` source:**
```rust
// src/start.rs:42-43 (COMMENTED OUT — never executed)
// uart_init::init_uart0();

// src/start.rs:63-76 (COMMENTED OUT — never executed)
// fn init_uart0() {
//     uart_driver_install(0, 512, 512, 10, null_mut(), 0);
//     esp_vfs_dev_uart_use_driver(0);
// }
```

## Fix

### Category: Trivial (< 10 lines of new code)

### Summary
Add `uart_init_stdin()` function in `esp_safe.rs` that performs the 3-step UART driver/VFS initialization. Call it from `main()` before spawning the UART reader thread.

### Files Modified

| File | Change |
|------|--------|
| `src/esp_safe.rs` | Added `uart_init_stdin()` — registers UART VFS, installs UART0 driver with 256-byte RX buffer, switches VFS to driver mode |
| `src/main.rs` | Added call to `ecotiter_fw::esp_safe::uart_init_stdin()` before UART thread spawn (line 336) |
| `src/diag/ffi_guard.rs` | Added `FFI_UART_INIT = 23` constant for diag tracing |

### Detailed Fix

**`src/esp_safe.rs`** — new function:
```rust
pub fn uart_init_stdin() {
    diag::ffi_guard::record_enter(diag::ffi_guard::FFI_UART_INIT);
    unsafe {
        // Step 1: Register UART VFS driver (idempotent)
        esp_idf_sys::uart_vfs_dev_register();

        // Step 2: Install UART0 driver if not already installed
        if !esp_idf_sys::uart_is_driver_installed(esp_idf_sys::uart_port_t_UART_NUM_0) {
            let ret = esp_idf_sys::uart_driver_install(
                esp_idf_sys::uart_port_t_UART_NUM_0, // UART0
                256,   // rx_buffer_size
                0,     // tx_buffer_size (0 = no TX buffering — println! handles TX)
                10,    // queue_size
                core::ptr::null_mut(), // uart_queue
                0,     // intr_alloc_flags
            );
            if ret != 0 {
                log::error!("UART: uart_driver_install failed: {ret:#x}");
            }
        }

        // Step 3: Switch VFS to driver mode for blocking interrupt-driven I/O
        esp_idf_sys::uart_vfs_dev_use_driver(esp_idf_sys::uart_port_t_UART_NUM_0 as i32);
    }
    diag::ffi_guard::record_exit(diag::ffi_guard::FFI_UART_INIT, 0);
}
```

**`src/main.rs`** — added call before UART thread:
```rust
// Initialize UART0 VFS driver + install driver for blocking stdin reads.
// Without this, `std::io::stdin().read()` panics because the VFS layer has
// no driver backing fd 0 — ESP-IDF's boot console only sets up polled-mode
// output (printf/println), not interrupt-driven input.
ecotiter_fw::esp_safe::uart_init_stdin();
```

### Verification

| Test | Result |
|------|--------|
| `cargo check --target xtensa-esp32-espidf` | ✅ Passes (0 errors, 0 new warnings) |
| `cargo clippy --target xtensa-esp32-espidf` | ✅ Passes (only pre-existing warnings) |
| `cargo test --lib` | ✅ 245/245 tests pass |
| `cargo build --target xtensa-esp32-espidf` | ✅ Full build succeeds |
| Flash + smoke test (30s) | ⏳ Needs hardware — should show no UART panic, commands processed |

### Verification Steps for Hardware

1. `scripts/build.sh flash /dev/ttyUSB0` — flash firmware
2. `timeout 45 python3 scripts/serial_monitor.py` — monitor boot
3. Expected: No panic in UART thread. UART receives serial commands, main loop processes them.
4. Type `{"cmd":"ping"}` over serial → should get `{"ok":true}` response.
5. No "UART reader thread disconnected" warnings.

## Investigation Artifacts

| File | Status |
|------|--------|
| `[INVESTIGATION]` markers | ✅ Not needed (no probe markers added) |
| Lessons learned | ✅ LL-014 added (this file documents it) |

## Remaining Issues

None — existing issues (DRAM fragmentation, thread spawn order) are addressed by LL-013 and existing code.

## Lessons Learned

- **LL-014:** ESP-IDF's boot console only sets up polled output via direct UART register writes (`printf`/`println!`). For `read(0, ...)` / `std::io::stdin().read()` to work, three FFI calls are needed: `uart_vfs_dev_register()`, `uart_driver_install()`, and `uart_vfs_dev_use_driver()`. The `esp-idf-sys` crate has this code commented out in `start.rs` — it was intended as a feature (`uart0_driver_init`) that was never implemented. Always check `start.rs` for commented-out init code when a peripheral function panics on first use.
