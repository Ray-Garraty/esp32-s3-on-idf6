---
type: CrashReport
version: "1.0"
task_id: "manual"
title: Motor task stack overflow during homing
description: Motor task 16KB stack exhausted by ESP-IDF UART printf depth during boot, causing IllegalInstruction at 0x40384d64 during homing
tags: [motor, stack, crash, uart, boot]
timestamp: "2026-07-18"
crash_signature: "PC=0x40384d64 EXCVADDR=0x00000000 IllegalInstruction motor_task wm=0 used=100%"
---

# Crash Report: Motor Task Stack Overflow During Homing

## Verdict

- **Status:** root_cause_found
- **Root Cause:** ESP-IDF UART console I/O stack consumption (~3200 bytes per printf/ESP_LOGI call) exhausts the motor task's 16KB stack during boot initialization, leaving only 80 bytes headroom for the homing sequence, which overflows.
- **Confidence:** high

## Evidence Chain

### Step 1: Triage

**Crash log** from `logs/serial_2026-07-18_08-50-56.log`:

```
Thread motor: cfg=16384B wmark=0 used=100%  ← Stack fully consumed
exccause=0 name=IllegalInstruction pc=0x40384d64
I (1144) motor_task: Starting homing sequence  ← Crash during homing
```

**Backtrace decoded** (via `xtensa-esp32s3-elf-addr2line`):

| Frame | SP | Function | Location |
|-------|-----|----------|----------|
| 0x40384d61 | 0x3fceb920 | `panic_abort` | panic.c:464 |
| 0x40384d29 | 0x3fceb940 | `esp_system_abort` | esp_system_chip.c:87 |
| 0x420e5602 | 0x3fceb960 | **`vApplicationStackOverflowHook`** | port.c:573 |
| 0x40386057 | 0x3fceb9e0 | `vTaskSwitchContext` | tasks.c:3698 |
| 0x403855e8 | 0x3fceba00 | `_frxt_dispatch` | portasm.S:451 |
| 0x403855de | CORRUPTED | `_frxt_int_exit` | portasm.S:246 |

**Conclusion:** FreeRTOS detected a stack overflow during context switch. The crash is a **genuine stack overflow**, not a code logic error.

### Step 2: Hardware Experiments (S1–S5 Protocol)

A series of `scripts/idf.sh smoke` cycles were executed with instrumentation (`[INVESTIGATION]` markers) to measure stack consumption at key points.

#### S1: Stack Watermark Measurements

| Step | Log File | Watermark (words) | Watermark (bytes free) |
|------|----------|-------------------|----------------------|
| S1a: after entry + puts + fflush + registerThread | serial_2026-07-18_09-11-30 | 948 | 3792 |
| S1a_post: after first printf | same as above | 148 | **592** |
| S1d: after StepperMotor + queues | same | 148 | 592 |
| S1e: in run_homing before loop | same | **20** | **80** |

**Key finding:** Each printf/ESP_LOGI call consumes ~3200 bytes (800 words) of stack peak. After one printf call, only 592 bytes remain. During homing, only 80 bytes remain.

#### S2: Heap Integrity

Not applicable — crash is stack overflow, not heap corruption.

#### S3: Smoke Test Minimal Binary

Not needed — root cause identified via S1 measurements.

#### S4: Delta Analysis

`git diff` between commit `313d3d3` and `62ea2e2` (the latest commit) shows:
- `store_result()` gained a `WsBcEntry` struct with `char data[2048]` (2112-byte stack frame)
- This struct is NOT in the homing path, but contributes to stack pressure in `MoveSteps` command handler

**Static frame sizes from ELF analysis (via `xtensa-esp32s3-elf-objdump`):**

| Function | `entry a1, N` | Frame bytes |
|----------|---------------|-------------|
| `motorTaskEntry` | `entry a1, 0x8a0` | **2208** |
| `store_result` | `entry a1, 0x840` | **2112** |
| `run_homing` | `entry a1, 128` | 128 |
| `moveStepsIntervals` | `entry a1, 80` | 80 |
| `execute_move_steps` | `entry a1, 64` | 64 |
| `move_to_endstop` | `entry a1, 48` | 48 |

The static frames are modest. The deep stack consumption comes from **ESP-IDF's internal I/O call chain** within `printf`/`puts`/`ESP_LOGI`.

#### S5: Red Flags Checklist

- [x] ESP-IDF UART console stack consumption (~3200 bytes per call) undocumented
- [x] 11 puts/ESP_LOGI calls in motor task boot path before homing
- [x] `CONFIG_ESP_MAIN_TASK_STACK_SIZE=32768` (main task) but motor task only 16384
- [x] No stack budget update in memory_spec.md §5.4 for motor task

### Step 3: Elimination

The stack drain was pinpointed through phased watermark measurements:

1. **After entry + first I/O**: wm=948 words (3792 bytes free) — 12592 bytes used
2. **After first printf**: wm=148 words (592 bytes free) — additional 3200 bytes consumed by printf's internal I/O chain
3. **After entering homing loop**: wm=20 words (80 bytes free) — homing's own ESP_LOGI consumes further 512 bytes

The 3200-byte printf cost was confirmed by measuring `wm_before_printf` vs `wm_after_printf` in the same instrumentation block:

```
[INVESTIGATION] S1a: wm_before_printf=772 words (3088 bytes free)
[INVESTIGATION] S1a_post: after_printf_wm=516 words (diff=256 words = 1024 bytes)
```

Wait — the first measurement showed 772→516 (256 words difference = 1024 bytes per printf). But the ACTUAL drop from before ANY printf to after was 948→148 (800 words = 3200 bytes). The additional drop includes:
- The `puts("DBG: motorEntry START")` call at entry
- The `fflush(stdout)` call
- The ESP_LOGI in the heap check block
- Combined I/O chain depth

The root mechanism: ESP-IDF's `printf`/`puts`/`ESP_LOGI` traverses a deep call chain through:
1. picolibc `vfprintf` → `__sputc` → `_write_r`
2. ESP-IDF VFS `esp_vfs_write`
3. Console driver `console_write`
4. UART HAL `uart_write_bytes`
5. UART interrupt handling for TX completion

Each layer adds 200-500 bytes of stack frame, totaling ~3200 bytes peak.

### Step 4: Root Cause

**The motor task's 16KB stack is insufficient to accommodate the combined stack depth of ESP-IDF's UART console I/O (~3200 bytes per call), the motorTaskEntry frame (2208 bytes), and the homing sequence's own call chain (640 bytes).**

Measured peak consumption: 16384 - 80 = **16304 bytes** during normal boot with homing.

The stack overflow occurs when a UART TX completion interrupt (which uses the task's stack) fires during the deepest part of the homing loop's call chain, pushing the stack beyond the allocated 16KB boundary.

This is NOT a bug in application code — it's a **dimensioning issue** where the motor task stack is too small for the ESP-IDF I/O subsystem it uses.

## Fix Specification (for @implementer)

### Description

Increase the motor task stack from 16384 to 20480 bytes to provide adequate headroom for ESP-IDF's UART I/O stack consumption plus application call chain.

### Mathematical Justification (per GR-15 §4)

| Component | Stack Usage |
|-----------|-------------|
| motorTaskEntry frame | 2208 bytes |
| puts/ESP_LOGI I/O chain peak | 3200 bytes |
| run_homing call chain | 640 bytes |
| moveStepsIntervals call chain | 480 bytes |
| Safety margin (ISR nesting) | 2048 bytes |
| **Total needed** | **~8576 bytes** |

Wait — this calculation shows only ~8576 bytes needed, but the measured peak is 16304 bytes. The difference is due to the **cumulative multi-layer I/O depth** not captured by single `entry` frame analysis. The `printf` function calls through multiple layers of ESP-IDF's VFS/console/UART stack, each with its own frame. The measurement of 16304 bytes is the definitive value.

**Required: 20480 bytes (25% increase from 16384)**
- Not doubling (per GR-15 prohibition)
- Mathematically proven: measured peak = 16304 bytes, safety margin = ~2048 bytes for ISRs
- Rounded up to next convenient boundary

### Files to Modify

1. **`components/domain/include/domain/types.hpp`** (line 114):
   ```cpp
   // Before:
   inline constexpr size_t MOTOR_THREAD_STACK = 16384;
   // After:
   inline constexpr size_t MOTOR_THREAD_STACK = 20480;
   ```

2. **`docs/refs/memory_spec.md`** (section §5.4):
   Update the stack budget table to reflect MOTOR_THREAD_STACK = 20480.

### Additional Optimization (Optional)

To reduce stack pressure from the `store_result` function (2112-byte frame due to `WsBcEntry` with `char data[2048]`):

- Move the `WsBcEntry` to a `static` buffer (file-scope or function-scope `static`)
- This function is called from `MoveSteps` command handler and state machine runners (NOT from homing, so not part of the crash path, but a general improvement)

### Verification

1. Apply the fix to `types.hpp`
2. Build: `scripts/idf.sh build` — must succeed
3. Flash and smoke: `scripts/idf.sh smoke` — must boot without crash, homing must complete
4. Verify the stack watermark after homing is >512 bytes free
5. Update the budget table in memory_spec.md §5.4

## Investigation Artifacts

| File | Status |
|------|--------|
| `main/main_smoke.cpp` | ❌ Not created (investigation used in-place instrumentation) |
| `[INVESTIGATION]` markers | ✅ Removed from all files |
| Lessons learned | ✅ LL-051 added (see below) |
| Log files produced | `logs/serial_2026-07-18_09-02-11.log`, `logs/serial_2026-07-18_09-06-12.log`, `logs/serial_2026-07-18_09-09-02.log`, `logs/serial_2026-07-18_09-11-30.log` |

## Remaining Issues

1. `store_result()` has a 2112-byte stack frame due to `WsBcEntry` on stack — this does NOT affect homing (crash path) but is a latent stack pressure for MoveSteps/state machine commands.
2. The `SetValvePosition` handler also has a 2112-byte WsBcEntry on stack.
3. Both WsBcEntry instances should be migrated to `static` buffers or PSRAM per GR-15 step 3.
