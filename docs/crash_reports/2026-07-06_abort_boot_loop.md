---
type: CrashReport
version: "2.0"
task_id: manual
timestamp: "2026-07-06"
crash_signature: "abort() at PC 0x4211843a — panic_fmt via println!() in Logger::log — print_to::<Stdout> panics on ESP-IDF v6 early boot"
title: "Boot-Loop: println!() Panic in Logger::log via print_to::<Stdout>"
description: "println!() at logger.rs:77 panics on ESP-IDF v6 early boot because Rust's std I/O stack (Stdout::write → libc::write → VFS) is not fully initialized; replaced with direct FFI write(1, ...)"
tags: [boot-loop, abort, println, logger, std-io, esp-idf-v6, init-order]
---

# Crash Report (Corrected — v2.0)

## Verdict

- **Status:** root_cause_found
- **Root Cause:** `println!()` at `src/logger.rs:77` calls through Rust's `std::io::print_to::<Stdout>` which panics on ESP-IDF v6 at early boot (~110ms). The VFS/TLS layer for Rust's std buffered I/O is not fully initialized yet. `print_to` receives an `Err` from the underlying `write(1, ...)` call and calls `panic!("failed printing to stdout: ...")`.
- **Confidence:** high (backtrace decoded + fix verified on hardware)
- **Previous diagnosis (v1.0):** Incorrectly blamed `disable_wdt()` logging before `logger::init()`. That was a red herring — the real crash was in `println!()` inside `Logger::log`, not in `disable_wdt()`.

## Evidence Chain

### Decoded backtrace (addr2line, 100% reliable)

```
0x4211843a: abort_internal (std/src/sys/pal/unix/mod.rs:305)
0x421a8000: panic_fmt (core/src/panicking.rs:80)
0x4213053b: print_to::<Stdout> (std/src/io/stdio.rs:1165)   ← panics HERE
0x42135d6a: _print (std/src/io/stdio.rs:1275)
0x42020d88: Logger::log (src/logger.rs:77)                    ← calls println!
```

The panic chain: `Logger::log` → `println!()` → `_print` → `print_to::<Stdout>` → `write()` returns `Err` → `panic!("failed printing to stdout")` → `panic_fmt` → `abort`.

### Why `println!()` panics on ESP-IDF v6 at early boot

Rust's `println!()` goes through `std::io::Stdout::write()` → `libc::write(1, buf, len)` → ESP-IDF VFS → UART driver. On ESP-IDF v6 at ~110ms after boot, one of:

- The VFS UART driver backing fd 1 is not fully initialized for buffered std I/O
- The std mutex's lazy-init path fails via TLS
- The `libc::write()` call returns an error because the underlying UART driver hasn't registered with VFS for Rust's buffered mode

The `print_to` function at `stdio.rs:1165` calls `stdout().lock().write(buf)`. When the inner `libc::write()` returns `Err`, `print_to` calls `panic!("failed printing to stdout: ...")`. This is a hard panic — not catchable.

### Why the first debugger diagnosis was wrong

The v1.0 report claimed `disable_wdt()` logged before `logger::init()`. This was based on:

1. `crash_analyzer.py` matched LL-019 (a pattern about logging before logger init)
2. Source inspection showed `disable_wdt()` at line 87, `logger::init()` at line 90

But the actual backtrace shows `Logger::log` — meaning the logger WAS initialized and running. The crash was inside the logger implementation itself (the `println!()` at line 77), not in `disable_wdt()`.

The `log::warn!()` in `disable_wdt()` never executed because `logger::init()` was never called at that point — log output is silently dropped when no logger is set.

## Fix

### Trivial

Replace `println!()` in `Logger::log` with a direct FFI `write(1, ...)` call that doesn't use Rust's std I/O machinery.

### Files Modified

- `src/logger.rs` (lines 76-81):
  ```rust
  // BEFORE (crashes):
  println!("[{}] {}", level, record.args());

  // AFTER (works):
  let mut console_out: heapless::String<{ MAX_LOG_MSG_SIZE + 16 }> = heapless::String::new();
  let _ = write!(console_out, "[{}] {}", level, record.args());
  crate::esp_safe::panic_write_str(console_out.as_str());
  crate::esp_safe::panic_write_str("\n");
  ```

- `src/esp_safe.rs` (line 141) — `panic_write_str()` was already defined:
  ```rust
  pub fn panic_write_str(s: &str) {
      unsafe { esp_idf_sys::write(1, s.as_ptr().cast::<core::ffi::c_void>(), s.len()); }
  }
  ```

### Verification

```
scripts/build.sh check  →  0 errors
scripts/build.sh        →  builds for xtensa-esp32s3-espidf
```

Hardware verification: firmware no longer aborts at boot (see TG1WDT report for follow-up issue).

## Key Insight

`println!()` on ESP-IDF v6 is unreliable at early boot because it goes through Rust's std I/O stack which depends on both VFS and TLS being fully initialized. Direct FFI `write(1, ...)` bypasses all of this and works unconditionally.

The `disable_wdt()` init-order theory was a red herring — `log::warn!()` when no logger is set is silently dropped, it does NOT panic.

## Lessons Learned

```yaml
- id: LL-020
  date: 2026-07-06
  crash_signature: "abort() at PC 0x4211843a — panic_fmt in print_to::<Stdout> from Logger::log"
  category: api_misuse
  lesson: >
    println!() on ESP-IDF v6 panics at early boot because Rust's std I/O
    stack (Stdout::write → libc::write → VFS → UART) is not fully initialized.
    Use direct FFI write(1, ...) instead for early-boot console output.
```

## Investigation Artifacts

| Artifact | Status |
|----------|--------|
| Backtrace decoded | ✅ All frames identified |
| Fix builds | ✅ `scripts/build.sh check` passes |
| Fix verified on hardware | ✅ No more abort() |
| Previous report corrected | ✅ v2.0 replaces v1.0 |

## Remaining Issues

- After this fix, the firmware hit TG1WDT_SYS_RST (Interrupt Watchdog timeout). See `2026-07-06_tg1wdt_bootloop_s3_migration.md`.
- `check_heap_integrity()` (line 85) calls `log::info!()` / `log::error!()` before logger init. It happens to work because the heap is clean at boot, but it's fragile.
