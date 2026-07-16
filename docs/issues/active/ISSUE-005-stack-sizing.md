---
type: Known Issue
title: No systematic task stack sizing process — sizes determined reactively after crashes
description: All 6 task stack sizes were bumped reactively after stack overflow crashes. No proactive measurement, no documented budgets, no CI gate. StackMonitor gaps leave 2 tasks untracked.
tags: [stack, architecture, process, diagnostic]
timestamp: 2026-07-16
status: active
---

# No systematic task stack sizing process

## Problem

Every task stack size in this firmware was determined reactively: write code → crash (stack overflow) → double the stack → test → forget. This pattern repeated 5 times across 6 tasks (LL-001, LL-010, LL-038, LL-043, LL-048, LL-050). There is no proactive methodology.

### Current stack sizes (all defined ad-hoc)

| Task | Current size | History (evolution) | Defined in |
|------|-------------|---------------------|------------|
| Main (`app_main`) | 32 KB | 2304 → 32768 (LL-001) | `sdkconfig.defaults` |
| Motor | 16 KB | single value, never crashed | `domain/types.hpp:76` |
| Temperature | 16 KB | single value, never crashed | `domain/types.hpp:80` |
| Net Owner | 20 KB | 16384 → 20480 | `domain/types.hpp:78` |
| Log Worker | 12 KB | 4096 → 8192 → 12288 (LL-043, LL-048) | `domain/types.hpp:79` |
| HTTP Server | 16 KB | 12288 → 16384 (LL-050) | `http_server.hpp:52` + `domain/types.hpp:83` |
| BLE Notify | 8 KB | single value | `domain/types.hpp:81` |

### Concrete gaps

1. **StackMonitor does not track all application tasks.** Motor and temperature tasks are created with `xTaskCreate(..., nullptr)` (no handle stored). Neither self-registers. Their stack watermarks never appear in crash dumps.

2. **No periodic watermark logging.** `StackMonitor::logAllWatermarks()` is only called from the panic handler (`crash_handler.cpp`). Gradual stack degradation (like LL-048's progressive watermark decline) goes undetected until crash.

3. **No CI gate for stack usage.** No step checks that stack watermarks stay above a threshold across builds. A PR that adds 2 KB of stack frames to a task with 1 KB headroom will pass CI silently.

4. **No per-task stack budget documentation.** There is no document listing worst-case call chain depth per task. When adding new handler code, developers have no way to estimate stack impact.

5. **MAX_THREADS = 8 may be exceeded.** `StackMonitor` allocates a static array of 8 slots. With 5 internal tasks + 6 application tasks + BLE notify, up to 12 tasks need tracking. Registrations beyond slot 8 are silently dropped.

6. **ResponseBuffer (2048 B) allocated on HTTP handler stack.** Three `ResponseBuffer` instances in the POST /api/valve call chain consume ~6 KB of the 16 KB HTTP server stack. Large stack-local buffers are a recurring pattern in every stack overflow crash.

7. **No formal process for stack impact review.** When adding or modifying code that runs in a task context, there is no requirement to measure watermark impact or update the task's budget.

## Root cause

1. **Historical:** The firmware evolved through three rewrites (Arduino → Rust → C++). Stack sizes were carried forward from each rewrite without re-validation. Each overflow was treated as an isolated incident rather than a symptom of a missing process.

2. **Architectural:** `StackMonitor` was designed for post-mortem analysis only (dump watermarks on crash). It was never intended for proactive monitoring or CI validation. The static 8-slot array cannot grow with the application.

3. **Process:** No review checklist requires stack impact analysis. The rule "After moving code between threads, verify with `uxTaskGetStackHighWaterMark()`" exists in `docs/refs/project.md` but is not enforced by CI or code review.

4. **Cultural:** "Double the stack and move on" is the path of least resistance. A proper worst-case analysis takes 30-60 minutes per task and requires instrumented builds — no one does it unless forced by a crash.

## Solution

### Phase 1 — Fix StackMonitor gaps

1. **Register motor and temperature tasks.** Each must call `StackMonitor::instance().registerThread(name, stackSize)` at entry. Currently both create themselves with `nullptr` handle, so no external code can register them — self-registration from the task body is the only option.

2. **Increase `MAX_THREADS`** from 8 to 16 (or switch to a dynamic container). Must account for: main, Tmr Svc, ipc0, ipc1, wifi, phy_init, motor, temp, net_owner, log_worker, ble_notify, http_server (internal), plus 3 spare.

3. **Add periodic watermark logging.** In the main loop, every 60 seconds:
   ```cpp
   StackMonitor::instance().logAllWatermarks();
   ```
   This logs to ESP_LOGI. The output is parseable by CI and makes gradual degradation visible in normal logs.

**Verification:**
- `scripts/idf.sh smoke` + 60s monitor shows watermarks for all 12 tasks
- Every task's watermark appears in the log
- Running `rg "watermark" logs/serial_*.log` shows all tasks

### Phase 2 — Document stack budgets

1. **Produce a worst-case call chain analysis for each task.** For each task, list:
   - Entry point function
   - Deepest call chain with estimated stack usage per frame
   - Large stack locals (>256 B) with sizes
   - Measured watermark from Phase 1
   - Safety margin (current size - measured watermark)

2. **Document in `docs/refs/project.md` §Thread Architecture.** Add a table:
   ```markdown
   | Task | Stack | Watermark | Margin | Deepest chain | Largest locals |
   |------|-------|-----------|--------|---------------|----------------|
   | HTTP | 16384 | 7200 | 9184 | valve_post_handler → handleCommandCore → dispatch → handleSetPosition | ResponseBuffer[3] × 2048 |
   ```

3. **Set a per-task target watermark.** Minimum 25% headroom. If any task falls below, it must be bumped before merging new code.

**Verification:**
- `docs/refs/project.md` updated with full budget table
- Every task has ≥25% headroom

### Phase 3 — CI gate

1. **Add a `scripts/check_watermarks.py` script** that:
   - Parses `StackMonitor::logAllWatermarks()` output from a log file
   - Fails if any task shows >75% usage (i.e., <25% headroom)
   - Fails if any expected task is missing from the log
   - Exit code 0 = pass, 1 = fail

2. **Add to `scripts/pre_commit.sh --fast`:**
   ```bash
   python3 scripts/check_watermarks.py < logs/serial_*.log
   ```

**Verification:**
- `scripts/pre_commit.sh --fast` runs watermark check
- Intentional stack regression produces CI failure

### Phase 4 — Process enforcement

1. **Code review checklist (add to `docs/refs/coding_style.md` or `docs/reviews/`):**
   - [ ] Does this change add stack frames to an existing task? If yes, measure watermark before and after.
   - [ ] Does this change add a new task? If yes, register with StackMonitor and set documented budget.
   - [ ] Does this change allocate a buffer > 512 B on the stack? If yes, justify or move to heap/PSRAM.

2. **PR template comment:** Every PR that touches a handler or task function must include a stack impact note:
   ```
   ## Stack impact
   - Task affected: HTTP server (16384 B)
   - Watermark before: 7200 B (56% used)
   - Watermark after: 7800 B (61% used)
   - Headroom: 8584 B (39%) — OK (≥25%)
   ```

**Verification:**
- Code reviews check the checklist
- Stack impact note is present in PRs with handler/task changes

## Edge cases

### MAX_THREADS capacity after increase
Bumping from 8 to 16 consumes an additional 8 × (pointer + uint8 + char[16]) ≈ 8 × 20 B = 160 B of DRAM. Acceptable for the diagnostic subsystem. If dynamic allocation is preferred, use `heap_caps_malloc(MALLOC_CAP_INTERNAL)` in DRAM.

### Motor and temp task self-registration
Both tasks are created with `xTaskCreate(..., nullptr)` — the handle is not returned. Self-registration from the task body (calling `StackMonitor::instance().registerThread()`) is the only option. The registration call must happen early in the entry function, before any work that might overflow.

### Periodic logging overhead
`StackMonitor::logAllWatermarks()` calls `uxTaskGetStackHighWaterMark()` for each of 12 tasks and prints 12 lines via `printf`. At 115200 baud, this takes ~10 ms and consumes ~1 KB of stack. Must be called from main loop where stack is abundant (32 KB).

### CI gate false positives
A task at 76% usage (just over the 75% threshold) on a cold boot might drop to 60% after warm-up (caches, lazy init). The CI check should allow a tolerance band (e.g., 75% ± 5%).

### ResponseBuffer on stack
Three `ResponseBuffer` (2048 B each) in the HTTP handler chain are the single largest contributor to HTTP server stack usage. Per Constitution Art. VIII, buffers > 1 KB should use PSRAM/heap. This is a separate issue from sizing — even with adequate stack, placing large arrays there wastes DRAM. Consider allocating `ResponseBuffer` from `heap_caps_malloc(MALLOC_CAP_SPIRAM)`.

## Related files

- [Constitution Art. VI (RAII/stack budget)](../refs/CONSTITUTION.md)
- [Constitution Art. VIII (Memory philosophy: no stack in PSRAM)](../refs/CONSTITUTION.md)
- [Thread architecture + stack constraints](../refs/project.md)
- [Memory spec — stack placement rules](../refs/memory_spec.md)
- [Diagnostic spec — StackMonitor + instrumentation](../refs/diagnostic_spec.md)
- [Stack overflow protocol](../protocols/stack_overflow.md)
- [StackMonitor header](../../components/diag/include/diag/stack_monitor.hpp)
- [StackMonitor source](../../components/diag/src/stack_monitor.cpp)
- [Domain types — all stack constants](../../components/domain/include/domain/types.hpp)
- [main.cpp — task creation](../../main/main.cpp)
- [LL-001: main stack overflow](../lessons_learned/LL-001.yaml)
- [LL-010: UART FFI stack overflow](../lessons_learned/LL-010.yaml)
- [LL-038: system event task overflow](../lessons_learned/LL-038.yaml)
- [LL-043: log_worker overflow (4K→8K)](../lessons_learned/LL-043.yaml)
- [LL-048: log_worker overflow (8K→12K)](../lessons_learned/LL-048.yaml)
- [LL-050: HTTP server valve handler overflow (12K→16K)](../lessons_learned/LL-050.yaml)
- [SRP violations](ISSUE-004-srp-violations.md)
