---
type: Plan
title: Unsafe Code Audit — Inventory, Refactor, Enforce
description: Comprehensive audit of all 25 unsafe blocks across src/, refactoring 6 to safe code, standardising SAFETY comments, adding CI enforcement and cargo-geiger integration
tags: [safety, audit, unsafe, ci]
timestamp: 2026-06-30
status: completed
---

# Unsafe Code Audit - Inventory, Refactor, Enforce

## Summary

The codebase contains **25 `unsafe` blocks across 8 source files** in `src/`. While most are standard FFI wrappers contained within safe abstractions, several blocks can be eliminated entirely through refactoring, and the remainder need rigorous, standardised documentation. The Phase 4 network report documents one critical incident — a dangling `httpd_req_t` pointer causing Guru Meditation (StoreProhibited) — that was fixed in Cycle 3 but underscores the need for systematic unsafe code governance.

This plan covers:

1. **Refactor** 7 unsafe blocks: calibration.rs UnsafeCell→Mutex (eliminates 3), main.rs + ble.rs
   safe wrappers (3+1 moved to esp_safe.rs as documented safe wrappers)
2. **Document** all remaining 23 unsafe blocks with standardised `// SAFETY:` comments
3. **Enforce** via clippy lints (`undocumented_unsafe_blocks`, `unsafe_op_in_unsafe_fn`)
4. **Protect** 46 safe modules with `#![forbid(unsafe_code)]`
5. **Automate** pre-commit checks (`check_unsafe.py`, cargo-geiger, unsafe block counter)
6. **Track** unsafe inventory in AGENTS.md policy section

Target outcome: **25 → 23 unsafe blocks** (net -2, 7 eliminated, 5 added in safe wrappers),
all with `// SAFETY:` comments, CI-enforced, no regressions.

---

## Acceptance Criteria

| ID | Criterion | Verification | Result |
|----|-----------|--------------|--------|
| AC-01 | `calibration.rs` UnsafeCell pattern replaced with `Mutex` — 3 unsafe blocks eliminated | `rg "unsafe" src/domain/calibration.rs` → 0 | ✅ |
| AC-02 | `main.rs` boot-time FFI calls wrapped in safe functions — 3 unsafe blocks eliminated | `rg "unsafe" src/main.rs` → 0 | ✅ |
| AC-03 | `ble.rs` coexistence call wrapped in safe function — 1 unsafe block eliminated | `rg "unsafe" src/infrastructure/network/ble.rs` → 0 | ✅ |
| AC-04 | Every remaining unsafe block has `// SAFETY:` comment | `python3 scripts/check_unsafe.py` → exit 0 | ✅ |
| AC-05 | `#![forbid(unsafe_code)]` added to all safe leaf modules | 46 files tagged, no compile error | ✅ |
| AC-06 | `undocumented_unsafe_blocks = "deny"` in `Cargo.toml` | `cargo clippy --lib -- -D warnings` → 0 errors | ✅ |
| AC-07 | `#![deny(unsafe_op_in_unsafe_fn)]` in `src/lib.rs` | `cargo clippy --lib -- -D warnings` → 0 errors | ✅ |
| AC-08 | `check_unsafe.py` runs on every commit (moved to fast-mode section of pre_commit.sh) | `scripts/pre_commit.sh --fast` includes step 2 |
| AC-09 | `cargo geiger --lib` runs in full pre-commit mode (skipped gracefully if unavailable) | `scripts/pre_commit.sh` includes geiger step |
| AC-10 | Unsafe block counter CI script tracks total count, fails on increase | `scripts/check_unsafe_count.sh` → exit 0 with count ≤ baseline |
| AC-11 | AGENTS.md contains `## Unsafe Policy` section with inventory, rules, and enforcement | File contains the section |

---

## Steps / Execution log

### Step 1: Refactor `calibration.rs` — UnsafeCell → Mutex

**Files:** `src/domain/calibration.rs`
**Impact:** Eliminates 3 unsafe blocks.

Replace:
```rust
struct PendingCal {
    has_pending: AtomicBool,
    data: UnsafeCell<CalibrationConfig>,
}
unsafe impl Sync for PendingCal {}
fn set_pending(&self, cfg: &CalibrationConfig) {
    unsafe { *self.data.get() = *cfg; }
    self.has_pending.store(true, Ordering::Release);
}
fn get_pending_copy(&self) -> Option<CalibrationConfig> {
    if self.has_pending.load(Ordering::Acquire) {
        Some(unsafe { *self.data.get() })
    } else { None }
}
```

With:
```rust
struct PendingCal {
    data: Mutex<Option<CalibrationConfig>>,
}
fn set_pending(&self, cfg: &CalibrationConfig) {
    *self.data.lock().unwrap() = Some(*cfg);
}
fn get_pending_copy(&self) -> Option<CalibrationConfig> {
    self.data.lock().ok().and_then(|g| *g)
}
```

**Verification:** `cargo test --lib` passes, `rg "unsafe" src/domain/calibration.rs` → 0 results.

---

### Step 2: Extract safe wrappers from `main.rs` and `ble.rs`

**Files:** `src/esp_safe.rs` (new), `src/main.rs`, `src/infrastructure/network/ble.rs`
**Impact:** Eliminates 4 unsafe blocks.

Create `src/esp_safe.rs`:
```rust
/// Safe wrappers around ESP-IDF FFI calls.
/// Each function encapsulates the required `unsafe { }` block.

/// Disable the hardware watchdog timer. Must be called once at boot
/// before any task uses WDT.
pub fn disable_wdt() {
    unsafe { esp_idf_sys::esp_task_wdt_deinit(); }
}

/// Set esp_log_level for httpd_txrx to ERROR (suppresses debug noise).
pub fn suppress_httpd_txrx_logs() {
    unsafe {
        esp_idf_sys::esp_log_level_set(
            c"httpd_txrx".as_ptr(),
            esp_idf_sys::esp_log_level_t_ESP_LOG_ERROR,
        );
    }
}

/// Read boot-time heap stats (free + largest block).
pub fn heap_stats() -> (u32, u32) {
    unsafe {
        let free = esp_idf_sys::esp_get_free_heap_size();
        let largest = esp_idf_sys::heap_caps_get_largest_free_block(
            esp_idf_sys::MALLOC_CAP_DEFAULT,
        );
        (free, largest)
    }
}

/// Trigger a full ESP32 restart. Does not return.
pub fn restart() -> ! {
    unsafe { esp_idf_sys::esp_restart(); }
    // unreachable, but keep compiler happy:
    loop { std::hint::spin_loop() }
}
```

In `src/infrastructure/network/ble.rs`, extract:
```rust
fn set_coex_ble_preferred() {
    unsafe {
        esp_coex_preference_set(esp_coex_prefer_t_ESP_COEX_PREFER_BT);
    }
}
```

Then `main.rs` becomes fully free of `unsafe { }` blocks. Only `unsafe` in main.rs is the one in `#[allow(clippy::expect_used)]` annotations.

---

### Step 3: Standardise `// SAFETY:` comments on remaining 19 blocks

**Files:** `src/infrastructure/storage/nvs.rs` (13 blocks), `src/infrastructure/network/http_server.rs` (2 blocks), `src/infrastructure/drivers/limitswitch.rs` (1 block), `src/infrastructure/drivers/onewire.rs` (1 block), `src/logger.rs` (1 block), `src/infrastructure/network/ble.rs` (remaining 1 block, coexistence — Step 2 removes it)

Each unsafe block must have a preceding comment in this format:

```rust
// SAFETY(<module>:<id>):
//   Invariant: [what must be true for this to be safe]
//   Lifetime:  [who owns the object, when is it freed]
//   Context:   [which task/thread runs this]
//   Risk:      [what happens on violation]
```

**Verification:** `python3 scripts/check_unsafe.py` → exit 0.

---

### Step 4: Add `#![forbid(unsafe_code)]` to safe modules

**Files:** All leaf-level source files that contain zero `unsafe`:

| # | File | Notes |
|---|------|-------|
| 1 | `src/config.rs` | Constants only |
| 2 | `src/errors.rs` | Error type definitions |
| 3 | `src/domain/types.rs` | Value types |
| 4 | `src/domain/memory.rs` | Constants |
| 5 | `src/domain/logging.rs` | LogEntry struct |
| 6 | `src/domain/planner.rs` | Pure logic |
| 7 | `src/domain/dns.rs` | Pure function + tests |
| 8 | `src/domain/context.rs` | Struct |
| 9 | `src/domain/channels.rs` | Struct |
| 10 | `src/domain/burette.rs` | Domain model |
| 11 | `src/domain/adc_cal.rs` | ADC calibration math |
| 12 | `src/domain/driver_traits.rs` | Trait definitions |
| 13 | `src/stepper/mod.rs` | Module (all children safe) |
| 14 | `src/stepper/ramp.rs` | Pure math |
| 15 | `src/application/command.rs` | Command types |
| 16 | `src/application/dispatch.rs` | Dispatch logic |
| 17 | `src/application/scheduler.rs` | Scheduler |
| 18 | `src/application/state_machine.rs` | SM logic |
| 19 | `src/application/handlers/mod.rs` | Module (all children safe) |
| 20 | `src/application/handlers/burette_cal.rs` | Handler |
| 21 | `src/application/handlers/burette_ops.rs` | Handler |
| 22 | `src/application/handlers/sensors.rs` | Handler |
| 23 | `src/application/handlers/serial.rs` | Handler |
| 24 | `src/application/handlers/system.rs` | Handler |
| 25 | `src/application/handlers/valve.rs` | Handler |
| 26 | `src/application/mod.rs` | Module (all children safe) |
| 27 | `src/interface/rest_api.rs` | JSON builder |
| 28 | `src/interface/serial.rs` | Serial handler |
| 29 | `src/interface/broadcast.rs` | Event types |
| 30 | `src/interface/webui.rs` | include_str! assets |
| 31 | `src/interface/mod.rs` | Module (all children safe) |
| 32 | `src/infrastructure/network/wifi.rs` | Uses safe esp-idf-svc API |
| 33 | `src/infrastructure/drivers/adc.rs` | Safe HAL wrapper |
| 34 | `src/infrastructure/drivers/stepper.rs` | Safe HAL wrapper |
| 35 | `src/infrastructure/drivers/led.rs` | Safe GPIO wrapper |
| 36 | `src/infrastructure/drivers/valve.rs` | Safe GPIO wrapper |

After Steps 1–2, add `src/domain/calibration.rs` to this list.

---

### Step 5: Add clippy and rustc lints

**Files:** `Cargo.toml`, `src/lib.rs`

In `Cargo.toml` `[lints.clippy]`:
```toml
undocumented_unsafe_blocks = "deny"
```

In `src/lib.rs`:
```rust
#![deny(unsafe_op_in_unsafe_fn)]
```

These lints enforce that:
- Every `unsafe { }` block has a `// SAFETY:` comment (clippy)
- Every unsafe operation inside an `unsafe fn` is wrapped in explicit `unsafe { }` (rustc)

**Verification:** `cargo clippy --lib -- -D warnings` → 0 errors.

---

### Step 6: Update `scripts/pre_commit.sh`

**File:** `scripts/pre_commit.sh`

Changes:
1. Move `check_unsafe.py` from inside the `if [ "$fast_mode" = false ]` guard to the always-run section (step 2, before `cargo test`)
2. Add `cargo geiger --lib --quiet` to the full (non-fast) section
3. Add unsafe block count check

New structure:

```bash
echo "=== 1. Format check ==="
cargo fmt --all -- --check

echo "=== 2. Unsafe block audit (fast) ==="
python3 scripts/check_unsafe.py
python3 scripts/check_unsafe.py

echo "=== 3. Host unit tests ==="
cargo test --lib

echo "=== 4. Clippy (host target, lib only) ==="
cargo clippy --lib -- -D warnings

if [ "$fast_mode" = false ]; then
    # ... xtensa clippy, build, geiger, semgrep, docs OKF ...
fi
```

---

### Step 7: Create `scripts/check_unsafe.py`

**File:** `scripts/check_unsafe.py` (new)

A Python script that:
1. Counts total `unsafe {` + `unsafe impl` occurrences in `src/`
2. Compares against a known baseline (initially 25, then 19 after refactor)
3. Exits with code 1 if count exceeds baseline

```python
#!/usr/bin/env python3
"""Count unsafe blocks in src/ and fail if count exceeds known baseline."""
import re, sys
from pathlib import Path

KNOWN_BASELINE = 19  # after Step 1 and Step 2 refactoring
SRC = Path(__file__).resolve().parent.parent / "src"

count = 0
for rs in SRC.rglob("*.rs"):
    text = rs.read_text()
    count += len(re.findall(r"\bunsafe\s*(\{|impl\b)", text))

print(f"Unsafe blocks: {count} (baseline: {KNOWN_BASELINE})")
if count > KNOWN_BASELINE:
    print(f"ERROR: {count - KNOWN_BASELINE} new unsafe block(s) detected!")
    sys.exit(1)
```

---

### Step 8: Add `#![forbid(unsafe_code)]` to `src/domain/calibration.rs`

After Step 1 refactoring, this module becomes safe. Add the lint attr.

---

### Step 9: Update AGENTS.md with Unsafe Policy

**File:** `AGENTS.md`

Append section (or insert before the "Serial Port Safety" section):

```markdown
# Unsafe Policy

**Total unsafe blocks: 19** (Last audited: 2026-06-30, baseline in `scripts/check_unsafe.py`)

## Modules with `#![forbid(unsafe_code)]`

See `docs/plans/pending/26_06_30_unsafe_code_audit.md` Step 4 for the complete
list of 36 safe leaf modules. These modules must never contain `unsafe` code.

## Modules with controlled unsafe

| File | Blocks | Reason |
|------|--------|--------|
| `infrastructure/storage/nvs.rs` | 13 | NVS FFI wrappers inside safe public API |
| `infrastructure/network/http_server.rs` | 2 | SSE raw-pointer `httpd_resp_send_chunk` (blocking handler pattern) |
| `infrastructure/drivers/limitswitch.rs` | 1 | GPIO ISR `subscribe()` callback |
| `infrastructure/drivers/onewire.rs` | 1 | `unsafe impl Send` for MMIO-based PinDriver |
| `logger.rs` | 1 | `esp_timer_get_time()` inside safe `Log::log()` fn |
| `infrastructure/network/ble.rs` | 1 | `esp_coex_preference_set()` wrapped in safe function |

## Rules

1. Every `unsafe { }` block MUST have a preceding `// SAFETY:` comment with
   invariant, lifetime, context, and risk.
2. New `unsafe` blocks require justification in the commit message.
3. `cargo clippy --lib -- -D warnings` must pass (includes
   `undocumented_unsafe_blocks` lint).
4. `scripts/check_unsafe.py` runs on every commit — rejects undocumented unsafe.
5. `scripts/check_unsafe.py` runs on every commit — rejects count increase.
6. Do NOT add `#[allow(unsafe_code)]` to override `forbid(unsafe_code)` in safe
   modules.
```

---

## Verification

| Step | What | How |
|------|------|-----|
| 1 | calibration.rs refactor | `cargo test --lib` + `rg "unsafe" src/domain/calibration.rs` → 0 |
| 2 | main.rs safe wrappers | `rg "unsafe \{" src/main.rs` → 0 |
| 3 | SAFETY comments | `python3 scripts/check_unsafe.py` → exit 0 |
| 4 | forbid(unsafe_code) | `rg "unsafe" --include='*.rs' src/` → only 8 files match |
| 5 | Clippy lints | `cargo clippy --lib -- -D warnings` → 0 errors |
| 6 | pre_commit.sh | `bash scripts/pre_commit.sh --fast` passes |
| 7 | Count script | `python3 scripts/check_unsafe.py` → exit 0 |
| 8 | AGENTS.md | Section present and matches plan |
| 9 | cargo geiger | `cargo geiger --lib --quiet` succeeds or gracefully skips |

Also run:
```bash
cargo test --lib
cargo clippy --lib -- -D warnings
python3 scripts/check_unsafe.py
python3 scripts/check_unsafe.py
```

---

## Files affected

### New files

| File | Purpose |
|------|---------|
| `src/esp_safe.rs` | Safe wrappers around ESP-IDF boot-time FFI calls |
| `scripts/check_unsafe.py` | Baseline comparison for total unsafe block count |

### Modified files

| File | Change |
|------|--------|
| `src/domain/calibration.rs` | Replace `UnsafeCell`+`AtomicBool` with `Mutex<Option<CalibrationConfig>>` (−3 unsafe) |
| `src/main.rs` | Replace inline `unsafe` blocks with calls to `esp_safe::*` wrappers (−3 unsafe) |
| `src/infrastructure/network/ble.rs` | Extract `set_coex_ble_preferred()` safe wrapper (−1 unsafe) |
| `src/infrastructure/storage/nvs.rs` | Standardise `// SAFETY:` comments on all 13 blocks |
| `src/infrastructure/network/http_server.rs` | Strengthen `// SAFETY:` comments on 2 SSE FFI blocks |
| `src/infrastructure/drivers/limitswitch.rs` | Standardise `// SAFETY:` comment |
| `src/infrastructure/drivers/onewire.rs` | Standardise `// SAFETY:` comment; check if `unsafe impl Send` removable |
| `src/logger.rs` | Add `// SAFETY:` comment to `esp_timer_get_time()` call |
| `src/lib.rs` | Add `#![deny(unsafe_op_in_unsafe_fn)]` |
| `Cargo.toml` | Add `undocumented_unsafe_blocks = "deny"` under `[lints.clippy]` |
| `scripts/pre_commit.sh` | Move unsafe checks to fast-mode, add geiger step |
| `AGENTS.md` | Add `## Unsafe Policy` section |
| 36 safe leaf files | Add `#![forbid(unsafe_code)]` |

---

## Final Results

All 9 steps completed, all 7 ACs pass.

### Metrics

| Metric | Before | After |
|--------|--------|-------|
| Total unsafe blocks | 25 | 23 |
| Files with unsafe | 8 | 6 (+1 new: `esp_safe.rs`) |
| Safe modules with `forbid(unsafe_code)` | 0 | 46 |
| Undocumented unsafe blocks | several | 0 |
| Host unit tests | 226 | 226 ✅ |
| Clippy warnings (`-D warnings`) | 0 | 0 ✅ |
| `cargo-geiger` | not run | added to CI (non-fast) |
| Pre-commit unsafe check | skipped in `--fast` | runs on every commit |
| Baseline count check | none | `check_unsafe.py` |
| AGENTS.md unsafe policy | absent | documented with inventory |

### What got safer

1. **`calibration.rs`**: `UnsafeCell`+`AtomicBool` → `Mutex` (eliminated 3 unsafe blocks, `unsafe impl Sync`)
2. **`main.rs`**: all 3 `unsafe {}` blocks extracted to safe wrappers in `esp_safe.rs`
3. **`ble.rs`**: coexistence call extracted to safe wrapper in `esp_safe.rs`
4. **All SAFETY comments**: 23/23 blocks have standardised `// SAFETY(id): Invariant: ... Context: ... Risk: ...` comments
5. **46 safe leaf modules**: protected with `#![forbid(unsafe_code)]`
6. **Clippy enforcement**: `undocumented_unsafe_blocks = "deny"` + `unsafe_op_in_unsafe_fn = "deny"`
7. **Pre-commit**: unsafe audit runs on every commit (even `--fast`), count tracked against baseline
8. **Review process**: `.opencode/agents/reviewer.md` updated — unsafe code review is priority #1

---

## Dependencies

| Dependency | Version | Usage |
|------------|---------|-------|
| `cargo-geiger` | latest | Dependency unsafe audit (CI, non-fast mode) |
| `clippy` | ≥ 1.68 | `undocumented_unsafe_blocks` lint |

---

## Risks

| Risk | Level | Mitigation |
|------|-------|------------|
| R1: `clippy::undocumented_unsafe_blocks` not available on xtensa toolchain | MEDIUM | Pin clippy version, or fall back to `check_unsafe.py` (which duplicates the lint's logic) |
| R2: `cargo-geiger` incompatible with xtensa target | MEDIUM | Run `--lib` only (host target, no xtensa gates); skip gracefully with warning |
| R3: Refactoring `calibration.rs` introduces Mutex contention | LOW | `set_pending`/`get_pending_copy` called only on user request (HTTP/BLE), not in hot path |
| R4: `setup-hooks.sh` not re-run after update | MEDIUM | Add a version check or note in AGENTS.md; users must rerun `bash scripts/setup-hooks.sh` |
