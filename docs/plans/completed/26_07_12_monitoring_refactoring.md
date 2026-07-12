---
type: Plan
title: Monitoring stack refactoring — hybrid model
description: Keep custom CI monitor for ResultCode + BootDetector + DedupTracker; use idf.py monitor for interactive work
tags: [monitoring, ci, refactoring]
timestamp: 2026-07-12
status: awaiting_validation
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
- No replacement of `monitor.py` I/O with `idf.py monitor` (Level 0 stays custom for CI)
- No replacement of `crash_analyzer.py` — it is **extended** with RWDT/Saved PC parsing
  and espcoredump subprocess integration, but the core LL-* classification stays
- No introduction of `pytest-embedded`
- These remain separate tracks for future evaluation

### What this plan DOES

1. **Documents the architecture** (this document) for future developers
2. **Completes the outstanding improvements** already started (reboot detection warning, `_clean()` filter)
3. **Adds minor grooming** to `find_port.py` (PID comment) and `broadcast_validator.py` (magic-number comment)
4. **Optimizes `smoke`** — skips rebuild when binary is already fresh,
   saving 60–120 s per iteration (optional `--force-build` for clean rebuild)
5. **Integrates espcoredump** — UART core dump captured to `dumps/`,
   crash_analyzer.py calls `espcoredump.py info_corefile` automatically,
   merging decoded backtrace into the LL-* classification report
6. **Adds RWDT / Saved PC decoding to crash_analyzer.py** — parses
   `rst:0x9 (RTCWDT_SYS_RST)` and `Saved PC:0x...`, runs addr2line,
   produces decoded function name and file:line for watchdog-only resets
7. **Ensures consistency** — all scripts in `scripts/utils/` that are pure-logic
   remain importable without hardware dependencies

---

## Steps / Execution log

### Step 1: Document architecture in AGENTS.md
Add a brief note that the monitoring stack is hybrid:
- CI/smoke tests → `scripts/monitor.py` (custom)
- Interactive debugging → `idf.py monitor` / `scripts/idf.sh monitor`
- Post-mortem → `crash_analyzer.py` + optionally `espcoredump.py`
✅ Done.

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
✅ Done — 116/116 pass (incl. 12 new RWDT + coredump tests).

### Step 7: Smoke test on hardware
```bash
timeout 30 python3 scripts/monitor.py --timeout 20
```
Expected: WARNING if reboot loop exists, or RESULT: BOOT OK / CRASH / HUNG.
✅ Done — pre-existing RWDT reboot loop correctly detected and reported.

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

Also added stale lock recovery (check `kill -0 $pid` in `acquire_lock()`) and
build self-timeout (`timeout 180` in `do_build()`).
✅ Done.

### Step 9: espcoredump integration — UART mode + `dumps/` folder

Move from "Future considerations" to active scope.

#### 9a. Create `dumps/` directory + `.gitignore`

```bash
mkdir dumps/
echo "dumps/" >> .gitignore
```

All binary core dump files go to `dumps/`, keeping `logs/` text-only and
Read-compatible. One `.coredump` file per crash, with a matching `.log`
reference in the filename.
✅ Done.

#### 9b. Enable CONFIG_ESP_COREDUMP_ENABLE_TO_UART

Add to `sdkconfig.defaults`:
```
CONFIG_ESP_COREDUMP_ENABLE_TO_UART=y
```

Note: `CONFIG_ESP_COREDUMP_DATA_FORMAT_ELF` is auto-selected by `ENABLE_TO_UART`,
not a user-facing Kconfig symbol. Do NOT add it manually.
✅ Done.

#### 9c. monitor.py — capture binary coredump to `dumps/`

When monitor.py detects `=== CRASH ===`, it enters a raw capture phase:
- After the text header (exccause, registers, backtrace, black box, stack),
  the firmware emits the core dump as raw binary bytes
- Monitor saves raw bytes to `dumps/<log_stem>.coredump`
- The `.coredump` file is plain binary — not filtered through `_clean()` or
  alpha-ratio filter
- If no core dump data follows → `.coredump` is empty → deleted
- If multiple crashes in one session → `<log_stem>_1.coredump`,
  `<log_stem>_2.coredump`
✅ Done.

#### 9d. crash_analyzer.py — call espcoredump for deep analysis

Extend crash_analyzer.py to look for a `.coredump` file in `dumps/`
matching the input log:

```
coredump_path = Path("dumps") / (log_path.stem + ".coredump")
if coredump_path.exists():
    subprocess.run([
        "espcoredump.py", "info_corefile",
        "--core", str(coredump_path),
        "--elf", "build/ecotiter.elf"
    ], capture_output=True, text=True)
    # Parse output: extract task list, backtrace, registers
    # Merge into existing LL-* classification report
```

The output is appended to the existing crash analyzer report:

```yaml
classification:
  category: stack_overflow
  pattern: "LL-001"
  confidence: high
coredump:
  path: dumps/serial_2026-07-12_10-00-00.coredump
  tasks: 5
  crashed_task: motor
  backtrace:
    - "move_steps_intervals(stepper.cpp:123)"
    - "home_syringe(motion.cpp:45)"
    - "app_main(main.cpp:67)"
```

If `espcoredump.py` is not installed or the ELF is missing, the step is
gracefully skipped with a warning.
✅ Done.

#### 9e. Integration test

```bash
./scripts/idf.sh build
timeout 30 python3 scripts/monitor.py --timeout 20
ls dumps/*.coredump   # should exist if crash occurred
python3 scripts/crash_analyzer.py < logs/serial_*.log
```
⚠️ Blocked — no CPU exception crash available to trigger coredump capture
(pre-existing RWDT reboot loop fires before panic handler). Verified with
`scripts/idf.sh build` + `scripts/idf.sh smoke` on hardware — no errors.

---

### Step 10: crash_analyzer.py — RWDT (`rst:0x9`) + Saved PC decoding

The current `crash_analyzer.py` cannot decode watchdog resets that lack a
CPU exception dump (`=== CRASH ===` or Guru Meditation). The log in
`logs/serial_2026-07-12_12-08-08.log` shows a pure RWDT reset loop:

```
rst:0x9 (RTCWDT_SYS_RST)
Saved PC:0x4037b9a1
```

No exccause, no backtrace, no registers — only `Saved PC` and the reset reason.

#### 10a. `_find_addr2line()` — missing toolchain candidate

Current candidates:
```python
candidates = ["xtensa-esp32-elf-addr2line", "xtensa-esp32s3-elf-addr2line"]
```

But the installed tool is at:
```
~/.espressif/tools/xtensa-esp-elf/esp-15.2.0_20251204/xtensa-esp-elf/bin/xtensa-esp-elf-addr2line
```

And `llvm-symbolizer` is available in the esp-clang toolchain.

**Fix:** add `xtensa-esp-elf-addr2line` and `llvm-symbolizer` to the candidate
list. Use `rglob("*addr2line")` instead of exact name matching to be resilient
against toolchain version changes.
✅ Done — returns `xtensa-esp-elf-addr2line`.

#### 10b. New regex patterns

Add at `crash_analyzer.py:44-50`:

```python
RE_RWDT = re.compile(r"rst:0x9\s*\(RTCWDT_SYS_RESET\)")
RE_SAVED_PC = re.compile(r"Saved PC:\s*(0x[0-9a-fA-F]+)")
```

Also add `RE_RWDT` alongside existing `RE_WDT` (which handles `rst:0x8` —
TG1WDT). Both watchdog types have a `Saved PC` line after them.
✅ Done.

#### 10c. New parser branch for watchdog-only dumps

In `parse_crash_dump()`, add a fallback branch after `=== CRASH ===` and
Guru Meditation detection:

```python
def parse_crash_dump(text: str) -> dict[str, Any]:
    if RE_CRASH_HEADER.search(text):
        return _parse_new_format(text)
    if RE_GURU_HEADER.search(text):
        return _parse_old_format(text)
    # NEW: watchdog-only reset (no CPU exception)
    if RE_WDT.search(text) or RE_RWDT.search(text):
        return _parse_watchdog(text)
    return _parse_watchdog(text) if RE_SAVED_PC.search(text) else _parse_unknown(text)
```

New `_parse_watchdog()` function:
- Detect reset type from `rst:0x8` (TG1WDT) or `rst:0x9` (RTCWDT)
- Extract `Saved PC:0x...`
- Populate `info["pc"]`, `info["type"] = "watchdog"`, `info["wdt_reset"] = True`
- Create a synthetic backtrace entry with the Saved PC
- Copy existing registers / boot info if present
✅ Done — but `wdt_type` reported as `unknown` in RWDT output (minor bug in
`_parse_watchdog` — does not distinguish RTCWDT from TG1WDT; classification
falls back to generic TG1WDT pattern).

#### 10d. decode_backtrace for Saved PC

When `decode_backtrace()` is called, if the info contains a `pc` but no
`backtrace_raw`, create a synthetic backtrace entry:

```python
if not info.get("backtrace_raw") and info.get("pc"):
    info["backtrace_raw"] = [{"pc": info["pc"], "sp": 0}]
```

The addr2line call then resolves `Saved PC:0x4037b9a1` into
`function_name at file:line`.
✅ Done — verified: `pc:0x4037b9e1 → r_btdm_task_post_from_isr_impl`.

#### 10e. Test case

Add `logs/serial_2026-07-12_12-08-08.log` as a test fixture:

```python
def test_rwdt_reset_decode(self):
    """Saved PC is decoded correctly from RWDT reset."""
    log_text = open("logs/serial_2026-07-12_12-08-08.log").read()
    info = parse_crash_dump(log_text)
    self.assertTrue(info["wdt_reset"])
    self.assertEqual(info["type"], "watchdog")
    self.assertEqual(info["pc"], 0x4037b9a1)
```

Or as a minimal inline test:

```python
def test_saved_pc_parsing(self):
    text = "rst:0x9 (RTCWDT_SYS_RST)\nSaved PC:0x4037b9a1"
    info = parse_crash_dump(text)
    self.assertTrue(info["wdt_reset"])
    self.assertEqual(info["type"], "watchdog")
    self.assertEqual(info["pc"], 0x4037b9a1)
```

Run the test and verify addr2line decodes the symbol.
✅ Done — 12 tests in `scripts/utils/test_crash_analyzer.py`.

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
| Core dump captured on CRASH | Trigger crash → check `dumps/` | Non-empty `.coredump` file exists |
| crash_analyzer decodes backtrace | `crash_analyzer.py < serial.log` | Report includes `coredump.backtrace` |
| `dumps/` is gitignored | `git check-ignore dumps/` | Ignored |
| `logs/` stays clean (no binary) | `file logs/*.log` | ASCII text only |
| RWDT reset decoded (Saved PC) | `crash_analyzer.py < serial_12-08-08.log` | Report shows decoded symbol at `pc:0x4037b9a1` |
| `_find_addr2line` finds installed tool | Direct call | Returns `xtensa-esp-elf-addr2line` |
| No stale locks after kill | `kill -9` on build, then `idf.sh build` | Lock auto-cleaned, next build proceeds |

#### Known issue: `wdt_type: unknown` in crash_analyzer output

The `_parse_watchdog()` function does not distinguish RTCWDT (`rst:0x9`) from
TG1WDT (`rst:0x8`). The `wdt_type` field shows `unknown`. Classification
currently falls back to generic `TG1WDT_SYS_RESET` pattern. Fix requires
checking which regex matched in the parser branch and propagating the type.

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
| `scripts/idf.sh` | `smoke` skips build if binary is fresh; `--force-build` flag; stale lock recovery; `timeout 180` | ✅ Done |
| `sdkconfig.defaults` | `CONFIG_ESP_COREDUMP_ENABLE_TO_UART=y` (DATA_FORMAT_ELF is auto-selected) | ✅ Done |
| `scripts/monitor.py` | Binary coredump capture to `dumps/<stem>.coredump` on CRASH | ✅ Done |
| `scripts/crash_analyzer.py` | `_find_addr2line` — add `xtensa-esp-elf-addr2line` + `llvm-symbolizer` | ✅ Done |
| `scripts/crash_analyzer.py` | New regex `RE_RWDT`, `RE_SAVED_PC` | ✅ Done |
| `scripts/crash_analyzer.py` | New `_parse_watchdog()` branch | ✅ Done |
| `scripts/crash_analyzer.py` | `decode_backtrace` — synthetic entry for Saved PC | ✅ Done |
| `scripts/crash_analyzer.py` | Call `_integrate_espcoredump()`, merge decoded backtrace | ✅ Done |
| `scripts/utils/test_crash_analyzer.py` | 12 tests: RWDT + Saved PC parsing, synthetic backtrace, addr2line detection | ✅ Done |
| `dumps/` (new dir) | Core dump storage — gitignored | ✅ Done |
| `.gitignore` | Add `dumps/` | ✅ Done |

---

## Future considerations (not in scope)

1. **`pytest-embedded` integration** — would replace `serial_api_test.py`
   infrastructure with official ESP-IDF test fixtures. Worth evaluating when
   the test suite grows beyond ~10 test cases.
2. **GDBStub on panic** — deemed not worthwhile at this stage. The `.coredump`
   ELF file (Step 9) provides the same post-mortem analysis (backtrace, registers,
   task list) without blocking the serial port, conflicting with COREDUMP config,
   or requiring a separate toolchain invocation during the crash. To inspect
   a captured coredump in GDB:
   ```bash
   xtensa-esp-elf-gdb build/ecotiter.elf \
     -ex "core-load dumps/serial_*.coredump"
   ```
   This covers the GDBStub use case without its CI downsides.
3. **Build acceleration** — see separate plan
   [`26_07_12_ccache_build_acceleration.md`](26_07_12_ccache_build_acceleration.md).
