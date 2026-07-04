---
type: CrashReport
version: "1.0"
task_id: manual
timestamp: "2026-07-04"
---

# Crash Report: HTTP Server Dropped + Motor OOM

## Verdict

- **Status:** root_cause_found
- **Root Cause #1:** `HttpServer` created in `net_owner` thread but immediately discarded — `Ok(_)` binds to underscore, Drop triggers `httpd_stop()` at end of match arm
- **Root Cause #2:** `compute_ramp()` called `Vec::with_capacity(10000)` = 40 KB allocation in motor thread during homing, when only 20 KB largest contiguous block remained after BLE init
- **Confidence:** high (both causes confirmed by code inspection)

---

## Crash #1: HTTP Server Immediately Dropped

### Evidence Chain

**Step 1: Log observation**
```
[INFO] HTTP: server started on port 80
[INFO] HTTP: server started (stack watermark: 19716 bytes)
[INFO] Unregistered Httpd server handler 1 for URI "/js/init.js"
... all 20+ handlers unregistered one by one ...
[INFO] Httpd server stopped
[INFO] [HEAP] http_started : free=69KB largest=42KB
W (2934) BTDM_INIT: esp_bt_controller_rom_mem_release already released
[INFO] BLE: init OK
[INFO] Main: ble_mgr received
```

The server starts, registers handlers, then **immediately** unregisters all handlers. These ESP-IDF logs ("Unregistered Httpd server handler", "Httpd server stopped") are emitted by `httpd_stop()` when the `EspHttpServer` is dropped.

**Step 2: Source code inspection — `src/main.rs` lines 243–253**

```rust
// BEFORE (broken):
let http_ok = match HttpServer::new(wifi_mgr_for_http) {
    Ok(_) => {                         // <--- `_` discards the HttpServer!
        let watermark = ecotiter_fw::esp_safe::stack_watermark();
        info!("HTTP: server started (stack watermark: {watermark} bytes)");
        true
    }
    Err(e) => {
        log::error!("HTTP: server init failed: {e:?}");
        false
    }
};
```

The `Ok(_)` pattern binds the created `HttpServer` to **nothing**. It goes out of scope at the end of the match arm, triggering `Drop`:
1. `HttpServer::drop()` sets `G_HTTP_SERVER_ALIVE = false`
2. Inner `EspHttpServer::drop()` calls `httpd_stop()`
3. ESP-IDF prints "Unregistered" for each handler, then "Httpd server stopped"

**Step 3: Confirmation**

The `Drop` impl at `http_server.rs:594`:
```rust
impl Drop for HttpServer {
    fn drop(&mut self) {
        G_HTTP_SERVER_ALIVE.store(false, Ordering::Release);
    }
}
```

And `EspHttpServer::drop()` at `esp-idf-svc/src/http/server.rs:717`:
```rust
impl Drop for EspHttpServer<'_> {
    fn drop(&mut self) {
        self.stop().expect("Unable to stop the server cleanly");
    }
}
```

**Fix applied:** Bind to `_http_server` (kept alive for thread lifetime):
```rust
let _http_server = match HttpServer::new(wifi_mgr_for_http) {
    Ok(server) => {
        let watermark = ecotiter_fw::esp_safe::stack_watermark();
        info!("HTTP: server started (stack watermark: {watermark} bytes)");
        Some(server)   // stored → NOT dropped
    }
    ...
};
```

---

## Crash #2: Motor OOM — "memory allocation of 40000 bytes failed"

### Evidence Chain

**Step 1: Crash analyzer output**

Backtrace:
```
ecotiter_fw::stepper::ramp::compute_ramp  (src/stepper/ramp.rs:91)
ecotiter_fw::motor_task::run              (src/motor_task.rs:108)
ecotiter_fw::motor_task::spawn::{closure#0} (src/motor_task.rs:68)
```

Error: `memory allocation of 40000 bytes failed`
Heap at crash: free=30KB, largest=20KB

**Step 2: Root cause trace**

In `motor_task.rs:108`:
```rust
let nominal_steps = calibration::volume_to_steps(Ml(cal_config.nominal_vol), cal_config.steps_per_ml)
    .abs()
    .min(config::HOMING_MAX_STEPS);  // 10,000

let intervals = compute_ramp(nominal_steps, &ramp_cfg);
```

`compute_ramp(10000, ...)` → `Vec::with_capacity(10000)` = **40,000 bytes**.

Default calibration: `steps_per_ml=7730`, `nominal_vol=8.14` → full volume = 62,922 steps.
Capped by `HOMING_MAX_STEPS=10000` (was already a fix for the original ~250KB).

**Step 3: Heap timeline**

1. Boot: ~140KB free, largest ~140KB
2. Thread stacks allocated: Motor(16K) + Temp(16K) + UART(8K) + net_owner(32K) → 72K consumed
3. WiFi init: ~9KB consumed
4. HTTP init: ~12KB allocated (stack for httpd task)
5. **BLE init: ~30KB consumed** → free=30KB, largest=20KB
6. Motor thread homing: attempts `Vec::with_capacity(10000)` = **40KB** → **FAILS** (largest=20KB)

The 40KB allocation cannot fit in any contiguous block.

**Step 4: Root cause**

`compute_ramp()` allocates a `Vec<u32>` with capacity = `total_steps`, which is 40KB for 10,000 steps (and would be ~250KB for a full 62,922-step motion without the 10K cap). This allocation happens inside the motor thread AFTER BLE has consumed ~30KB of heap, leaving only 20KB largest contiguous block.

**Fix applied:** Replaced `compute_ramp` with `RampIter` — a lazy iterator that computes intervals on-the-fly in O(1) per step, with zero heap allocation:

1. Added `RampIter` struct to `ramp.rs` — `Iterator<Item = u32>`, produces same values as `compute_ramp`
2. Changed `move_steps_intervals()` signature from `&[u32]` to `impl IntoIterator<Item = u32>` — accepts slices, Vec, or `RampIter`
3. Changed `move_steps()` to use `RampIter::new(steps.abs(), &ramp_config)` directly — no Vec allocation
4. Changed homing to use `RampIter::new(nominal_steps, &ramp_cfg)` — no Vec allocation

Also fixes the same problem for regular motions (Fill/Empty/Dose with 62,922 steps = 250KB allocation) which would also OOM if homing had succeeded.

---

## S1–S5 Protocol

| Step | Result | Action |
|------|--------|--------|
| S1 (stack watermark) | 19716 bytes (HTTP), 16KB (motor) | ✅ Pass — not stack overflow |
| S2 (heap integrity) | Pre-init OK | ✅ Pass — not heap corruption |
| S3 (smoke test) | N/A — deterministic boot | ✅ Not needed |
| S4 (delta analysis) | Both crashes are deterministic | ✅ Code inspection sufficient |
| S5 (red flags) | `Ok(_)` discards HttpServer; `Vec::with_capacity(10000)` in motor thread | ❌ Both flags confirmed |

---

## Fix Summary

### Fix #1: HttpServer Dropped (Trivial — Applied)

**File:** `src/main.rs`

Before (line 243):
```rust
let http_ok = match HttpServer::new(wifi_mgr_for_http) {
    Ok(_) => { ... true }
    Err(e) => { ... false }
};
```

After:
```rust
let _http_server = match HttpServer::new(wifi_mgr_for_http) {
    Ok(server) => { ... Some(server) }
    Err(e) => { ... None }
};
let http_ok = _http_server.is_some();
```

### Fix #2: Motor OOM (Moderate — Applied)

**File:** `src/stepper/ramp.rs`
- Added `RampIter` struct — lazy iterator, O(1) per step, zero allocation
- `compute_ramp()` now delegates to `RampIter::new(total_steps, config).collect()`
- All 13 ramp tests pass (same values)

**File:** `src/infrastructure/drivers/stepper.rs`
- Changed `move_steps_intervals()` to accept `impl IntoIterator<Item = u32>`
- Changed `move_steps()` (`StepperMotor` trait) to use `RampIter::new()` — no Vec allocation

**File:** `src/motor_task.rs`
- Homing uses `RampIter::new()` directly — no Vec allocation

---

## Investigation Artifacts

| File | Status |
|------|--------|
| `[INVESTIGATION]` markers | ✅ None added |
| `src/bin/smoke_test.rs` | ✅ Does not exist |
| Lessons learned | ✅ LL-011, LL-012 added |

## Remaining Issues

- None discovered during investigation

## Files Modified

| File | Change |
|------|--------|
| `src/main.rs` | Store HttpServer in `_http_server` instead of `_` |
| `src/stepper/ramp.rs` | Added `RampIter` lazy iterator; `compute_ramp` now delegates to it |
| `src/infrastructure/drivers/stepper.rs` | `move_steps_intervals` accepts `impl IntoIterator<Item = u32>`; `move_steps` uses `RampIter` |
| `src/motor_task.rs` | Homing uses `RampIter` instead of `compute_ramp` |
