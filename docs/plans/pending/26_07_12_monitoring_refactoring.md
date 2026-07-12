---
type: Plan
title: Monitoring stack refactoring — hybrid model
description: Keep custom CI monitor for ResultCode + BootDetector + DedupTracker; use idf.py monitor for interactive work
tags: [monitoring, ci, refactoring]
timestamp: 2026-07-12
status: pending
---

# Monitoring stack refactoring — hybrid model

## Summary

The project currently has a hand-written serial monitoring stack (`monitor.py` +
`monitor_classifier.py` + `boot_detect.py` + `crash_analyzer.py`) that overlaps
significantly with Espressif's official tools (`idf.py monitor`, `espcoredump.py`).

Analysis shows the stack is **three-tiered**:

```
Level 0: Serial I/O + port detection   → monitor.py, find_port.py
Level 1: Session classification        → monitor_classifier.py, boot_detect.py
Level 2: Crash forensics               → crash_analyzer.py
Level 3: Business-logic validation     → broadcast_validator.py
```

Levels 2–3 are **unique IP** — no equivalent in any Espressif tool.
Level 0 is a near-complete reimplementation of `idf.py monitor`.
Level 1 partially overlaps (boot detection) but adds unique `ResultCode` CI exit codes.

### Decision: Hybrid — keep Level 0 for CI, but document boundaries

| Context | Tool | Rationale |
|---------|------|-----------|
| CI / automation (`idf.sh smoke`, `serial_api_test.py`) | Custom `monitor.py` | Needs `ResultCode`, `BootDetector`, `DedupTracker` — these don't exist in `idf.py monitor` |
| Interactive debugging (human) | `idf.py monitor` | Auto addr2line, GDBStub, print-filter, embedded commands |
| Crash post-mortem | `crash_analyzer.py` + optional `espcoredump` | `crash_analyzer.py` matches known patterns (LL-*) and classifies; `espcoredump` for deep GDB analysis |

Rationale for keeping Level 0 in CI:
- CI pipelines never read coloured output or invoke GDB — they need a **boolean result** (boot OK / crash / hung / no output)
- `idf.py monitor` always exits 0 on normal disconnect — it cannot distinguish
  "clean boot with JSON" from "ROM output only, app hung"
- `BootDetector` catches reboot-loops invisible to `idf.py monitor` (count > 1 in one session)

### What this plan does NOT do
- No replacement of `monitor.py` I/O with `idf.py monitor`
- No replacement of `crash_analyzer.py` with `espcoredump.py`
- No introduction of `pytest-embedded`
- These remain separate tracks for future evaluation

### What this plan DOES

1. **Documents the architecture** (this document) for future developers
2. **Completes the outstanding improvements** already started (reboot detection warning, `_clean()` filter)
3. **Adds minor grooming** to `find_port.py` (PID comment) and `broadcast_validator.py` (magic-number comment)
4. **Optimizes `smoke`** — skips rebuild when binary is already fresh,
   saving 60–120 s per iteration (optional `--force-build` for clean rebuild)
5. **Ensures consistency** — all scripts in `scripts/utils/` that are pure-logic
   remain importable without hardware dependencies

---

## Steps / Execution log

### Step 1: Document architecture in AGENTS.md
Add a brief note that the monitoring stack is hybrid:
- CI/smoke tests → `scripts/monitor.py` (custom)
- Interactive debugging → `idf.py monitor` / `scripts/idf.sh monitor`
- Post-mortem → `crash_analyzer.py` + optionally `espcoredump.py`

### Step 2: Minor grooming of find_port.py
Add a comment clarifying that VID 0x303A covers ESP32-S2/S3/C3/C6/H2 native USB.
✅ Done.

### Step 3: Minor grooming of broadcast_validator.py
Add a comment explaining `delta_ts * 10` — the magic factor 10 comes from
legacy firmware `configTICK_RATE_HZ=100` (FreeRTOS tick = 10 ms).
✅ Done.

### Step 4: Verify `_clean()` integration
Already done in commit `675e423`. Confirm it applies in `writeline()` for both
terminal and log output.

### Step 5: Verify `BootDetector` integration
Already done in commit `675e423`. Confirm it feeds from the same line stream
as `SerialClassifier`, and emits visible WARNING on reboot > 1.

### Step 6: Run existing tests
```bash
python3 -m unittest scripts/utils/test_monitor.py -v
python3 -m unittest scripts/utils/test_monitor_clean.py -v
python3 -m unittest scripts/utils/test_boot_detect.py -v
```

### Step 7: Smoke test on hardware
```bash
timeout 30 python3 scripts/monitor.py --timeout 20
```
Expected: WARNING if reboot loop exists, or RESULT: BOOT OK / CRASH / HUNG.

### Step 8: Optimize `smoke` command to skip build when binary is fresh
The `smoke` command currently always calls `do_build` (clean build), even if
`scripts/idf.sh build` was just run. This adds ~60-120 s of unnecessary
recompilation to every smoke test cycle.

**Change:** Before calling `do_build`, check if `build/ecotiter.bin` exists
and is newer than all source files. If yes → skip build, just flash + monitor.

Implementation outline in `scripts/idf.sh`:
```bash
smoke)
    BINARY="$PROJECT_DIR/build/ecotiter.bin"
    if [ -f "$BINARY" ]; then
        STALE=$(find "$PROJECT_DIR" \
            -path "$PROJECT_DIR/build" -prune -o \
            -path "$PROJECT_DIR/build-tests" -prune -o \
            -path "$PROJECT_DIR/.git" -prune -o \
            -path "$PROJECT_DIR/logs" -prune -o \
            -path "$PROJECT_DIR/legacy" -prune -o \
            \( -name '*.cpp' -o -name '*.c' -o -name '*.h' -o -name '*.hpp' \
               -o -name 'CMakeLists.txt' -o -name 'sdkconfig.defaults' \) \
            -newer "$BINARY" -print -quit 2>/dev/null)
        if [ -z "$STALE" ]; then
            echo "Binary is up-to-date — skipping build"
        else
            do_build
        fi
    else
        do_build
    fi
    ...
```
The stale-source check is already implemented in the `flash` case (lines 148-159) —
reuse the same logic.

Add `--force-build` flag for cases where a clean build is explicitly desired:
```bash
./scripts/idf.sh smoke --force-build
```

---

## Verification

| Criterion | How | Expected |
|-----------|-----|----------|
| Existing tests pass | `python3 -m unittest` | All green |
| Serial log readable by Read tool | `head -c 1000 logs/serial_*.log` | No `\0` bytes |
| Reboot detected | `BootDetector.count > 1` → WARNING | Printed to terminal |
| CI exit code on reboot | `return 2` | Pipeline detects failure |
| `idf.py monitor` still works interactively | `scripts/idf.sh monitor` (manual) | Normal operation |
| `smoke` skips build when binary fresh | `scripts/idf.sh smoke` after `build` | No recompilation, directly flashes |

---

## Files affected

| File | Change | Status |
|------|--------|--------|
| `scripts/monitor.py` | Add `_clean()`, `BootDetector`, reboot WARNING, exit 2 | ✅ Done in `675e423` |
| `scripts/utils/test_monitor_clean.py` | 11 tests for `_clean()` | ✅ Done in `675e423` |
| `AGENTS.md` | Update §6.3: binary log rejection + usage guide | ✅ Done in `675e423` |
| `docs/plans/pending/26_07_12_monitoring_refactoring.md` | This document | 📝 Current |
| `scripts/find_port.py` | Add comment: 0x303A covers multiple Espressif chips | ✅ Done |
| `scripts/utils/broadcast_validator.py` | Add `* 10` comment explaining legacy tick | ✅ Done |
| `scripts/utils/monitor_classifier.py` | No change (pure logic, clean) | ❌ No change needed |
| `scripts/utils/boot_detect.py` | No change (pure logic, clean) | ❌ No change needed |
| `scripts/utils/crash_analyzer.py` | No change (Level 2, out of scope) | ❌ No change needed |
| `scripts/utils/broadcast_validator.py` | No structural change | ⏳ Comment only |
| `scripts/idf.sh` | `smoke` skips build if binary is fresh; `--force-build` flag | ⏳ Step 8 |

---

## Future considerations (not in scope)

1. **`pytest-embedded` integration** — would replace `serial_api_test.py`
   infrastructure with official ESP-IDF test fixtures. Worth evaluating when
   the test suite grows beyond ~10 test cases.
2. **`espcoredump` integration** — if CONFIG_ESP_COREDUMP is enabled,
   `crash_analyzer.py` could feed into `espcoredump-info` for deep GDB
   analysis. Currently CONFIG_ESP_COREDUMP is not set.
3. **GDBStub on panic** — adding `CONFIG_ESP_SYSTEM_PANIC=GDBStub` would give
   interactive debug on crash. Useful for development, risky for CI (would hang).
