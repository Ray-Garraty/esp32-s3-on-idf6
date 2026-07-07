---
type: CrashReport
version: "1.0"
task_id: "step4-boot-crash-2026-07-07"
timestamp: "2026-07-07"
crash_signature: "Panic handler entered multiple times — masked assert from xTaskCreatePinnedToCore with wrong core ID — three bugs chained"
title: "Step 4 Boot Crash — Triple Bug Chain"
description: >
  After adding motor task creation to main.cpp, firmware entered a boot loop
  with only "Panic handler entered multiple times" visible. Three independent
  bugs chained: xTaskCreatePinnedToCore core ID 1 with UNICORE=y, NULL
  dereference in xTaskGetHandle(nullptr), and .iram1 panic wrapper calling
  flash functions.
tags: [boot, crash, panic-handler, iram, freertos, unicore, stepper]
---

# Crash Report

## Verdict

- **Status:** root_cause_found
- **Root Cause:** `xTaskCreatePinnedToCore(motorTaskEntry, ..., 1)` passed core ID 1
  but `CONFIG_FREERTOS_UNICORE=y` limits cores to 0. The resulting assert fired
  inside the panic handler wrapper which itself crashed twice (NULL deref +
  .iram1 flash call), producing only "Panic handler entered multiple times".
- **Confidence:** high

## Evidence Chain

### Step 1: Triage

**Problem:** After adding motor task creation in Step 4, firmware enters a boot
loop. Only visible output: `BOOT_OK_MARKER` → ~80ms later → `Panic handler
entered multiple times. Abort panic handling. Rebooting ...` → repeat every
~110ms. No crash details, no backtrace, no Guru Meditation.

Saved PC was `0x4037c098` (first run) then `0x4037c07c` (after first fix
attempt). Both are in ROM boot vector range — secondary crashes.

### Step 2: Systematic Elimination

#### Hypothesis A: Panic handler wrapper crashes (Confirmed)

The `__wrap_esp_panic_handler` in crash_handler.cpp had TWO bugs:

1. **`xTaskGetHandle(nullptr)` (LL-024):** Called with NULL, causing
   `strlen(NULL)` → LoadProhibited. This is NOT a valid FreeRTOS API.
   Fixed by replacing with `uxTaskGetStackHighWaterMark(nullptr)`.

2. **`.iram1` section + flash calls (LL-025):** The function was placed in
   IRAM via `[[gnu::section(".iram1")]]` but called `printf` and
   `BlackBox::dump()` which live in flash. If the original crash corrupted
   the cache, the flash call crashes immediately. Removed `.iram1`.

These two bugs caused the wrapper itself to crash, masking the primary crash
with "Panic handler entered multiple times".

#### Hypothesis B: Primary crash (Confirmed after A fixed)

After neutralizing the wrapper (empty pass-through), the real crash appeared:

```
assert failed: xTaskCreatePinnedToCore freertos_tasks_c_additions.h:161
(( ( BaseType_t ) xCoreID ) >= 0 && ( ( BaseType_t ) xCoreID ) < 1 )
```

The call `xTaskCreatePinnedToCore(motorTaskEntry, "motor", ..., 5, nullptr, 1)`
passed core ID 1, but `CONFIG_FREERTOS_UNICORE=y` means only core 0 exists.
`configNUM_CORES == 1`, so core 1 fails the assertion.

### Step 3: Root Cause Chain

```
main.cpp: xTaskCreatePinnedToCore(..., 1)     ← primary bug
  → FreeRTOS assert(xCoreID < configNUM_CORES) ← configNUM_CORES=1
  → esp_panic_handler()
  → __wrap_esp_panic_handler()  ← .iram1, calls flash
    → xTaskGetHandle(nullptr)   ← NULL deref (LL-024)
    → LoadProhibited            ← secondary crash #1
  → esp_panic_handler()         ← recursive entry
  → __wrap_esp_panic_handler()
    → flash call fails          ← .iram1 flash access crash (LL-025)
    → LoadProhibited            ← secondary crash #2
  → esp_panic_handler()         ← 3rd entry > PANIC_ENTRY_COUNT_MAX
  → "Panic handler entered multiple times"
  → panic_restart()             ← boot loop
```

### Step 4: Fix Applied

1. Changed `xTaskCreatePinnedToCore(..., 1)` to `xTaskCreate(...)` (core
   not important for motor task).
2. Replaced `xTaskGetHandle(nullptr)` with `uxTaskGetStackHighWaterMark(nullptr)`
   in both crash_handler.cpp and stack_monitor.cpp.
3. Removed `.iram1` section attribute from `__wrap_esp_panic_handler`.

### Step 5: Verification

- `idf.py build` — 0 errors, 0 warnings
- `clang-tidy` — Clean
- `./scripts/unit_tests.sh` — 159/159 (442 assertions)
- Smoke test (30s serial) — BOOT_OK_MARKER, no Guru/WDT/panic

## Timeline

| Time | Event |
|------|-------|
| 2026-07-07 20:52 | First smoke test — "Panic handler entered multiple times" |
| 2026-07-07 20:53 | Debugger agent misdiagnoses as NULL deref in xTaskGetHandle |
| 2026-07-07 20:54 | Fix applied — still crashes (same symptom) |
| 2026-07-07 20:55 | Removed `.iram1` from wrapper → real crash visible |
| 2026-07-07 20:56 | `xTaskCreatePinnedToCore(..., 1)` with UNICORE=y identified |
| 2026-07-07 20:57 | Changed to `xTaskCreate()` — boot OK |
| 2026-07-07 21:00 | Restored crash_handler with diagnostics — all tests pass |

## Files Modified

| File | Change |
|------|--------|
| `main/main.cpp:42-49` | `xTaskCreatePinnedToCore(..., 1)` → `xTaskCreate(...)` |
| `components/diag/src/crash_handler.cpp` | Removed `.iram1`, replaced `xTaskGetHandle(nullptr)` |
| `components/diag/src/stack_monitor.cpp` | Replaced `xTaskGetHandle(nullptr)` |

## Related Lessons

- LL-023: xTaskCreatePinnedToCore core ID with UNICORE=y
- LL-024: xTaskGetHandle(nullptr) in panic handler
- LL-025: .iram1 panic wrapper calling flash functions
