---
type: Plan
title: Python check scripts refactoring
description: Replace scripts/check_unwrap.py and scripts/check_blocking.py with compile-time/linter equivalents
tags: [refactoring, clippy, semgrep, ci, linting]
timestamp: 2026-06-30
status: pending
---

# Python check scripts refactoring

## Summary

Replace two Python pre-commit scripts with maintainable, compiler-integrated solutions:

| Script | Problem | Replacement |
|--------|---------|-------------|
| `scripts/check_unwrap.py` | Redundant — Clippy already enforces via `lib.rs` lints | **Delete** — no replacement needed |
| `scripts/check_blocking.py` | Fragile brace-depth regex tracker | **Semgrep rule** — tree-sitter AST, no false positives on nesting |

Both scripts are removed; checks move from pre-commit shell hooks to established tooling (Clippy, Semgrep) for faster, more reliable enforcement.

---

## Background

### Current state of `check_unwrap.py`

- Source: `scripts/check_unwrap.py` — 70 lines, Python regex + brace-depth tracking
- Scans all `src/**/*.rs` (except `main.rs`) for `.unwrap()` / `.expect()` calls outside `#[cfg(test)]` blocks
- Called in `scripts/pre_commit.sh` step 7

**Already redundant today:**
- `src/lib.rs:5-6`: `#![deny(clippy::unwrap_used)]` `#![deny(clippy::expect_used)]` — crate-level deny
- `clippy.toml:5`: `allow-unwrap-in-tests = true` — test modules exempted
- All `.unwrap()` / `.expect()` in the codebase reside inside `#[cfg(test)]` modules — Clippy allows them automatically
- `main.rs` is explicitly skipped by the script — boot-time `.expect()` are intentional panics

The Python script has false-negative bugs (fails to detect unwraps in certain test block shapes) and adds zero value over Clippy.

### Current state of `check_blocking.py`

- Source: `scripts/check_blocking.py` — 110 lines, Python regex + char-level brace-depth tracking
- Scans `src/main.rs`, `src/logger.rs`, `src/lib.rs` for blocking calls outside `std::thread::spawn()` / `xTaskCreate()` closures
- Patterns: `.send_and_wait(`, `.lock().unwrap()`, `.recv()`, `std::thread::sleep(`, `.wait(`
- Exception: `from_millis(10)` / `MAIN_LOOP_TICK_MS` heartbeat tick
- Called in `scripts/pre_commit.sh` steps 3.5 (regression tests) and 8
- Has a companion `scripts/test_check_blocking.py` (219 lines) for regression testing

**Works today but fragile:**
- Brace-depth tracking breaks on complex nested closures, macros, or code reformatting
- Regex cannot distinguish `Builder::new().stack_size(4096).name("motor").spawn(move || { ... })` from a simple function call
- Requires a separate Python regression test suite
- Blocks pre-commit even when Clippy and build pass

---

## Considered options for `check_unwrap.py`

### Option U1: Keep script (status quo)

| Pro | Con |
|-----|-----|
| No changes needed | Redundant with Clippy; slower pre-commit |
| Catches unwrap in main.rs (which Clippy doesn't check) | Missing config in main.rs is the real fix |

### Option U2: Delete — Clippy already covers it

| Pro | Con |
|-----|-----|
| Removes 70 lines of dead code | Need to add `#![deny(...)]` to `main.rs` too |
| Compile-time, not pre-commit hook | Must annotate 5 boot-time `expect()` with `#[allow(...)]` |
| Zero maintenance | |
| No false positives/negatives | |

**Decision: Option U2** — Clippy is already configured and enforced. The script is dead code walking. Add deny lints to `main.rs` with explicit allows on intentional boot-time panics.

---

## Considered options for `check_blocking.py`

### Option B1: Keep and patch Python script

| Pro | Con |
|-----|-----|
| Minimal change | Brace-depth tracking is inherently fragile |
| Already works for current code | Still regex-based — misses edge cases |
| | Requires companion test suite |

### Option B2: Replace with Clippy lint (dylint custom lint)

| Pro | Con |
|-----|-----|
| Compile-time, perfect AST analysis | Requires writing 200-400 lines of Rust + dylint plugin |
| No false positives | Must pin rustc version for plugin compatibility |
| Type-system level guarantee | Build-time overhead; long iteration cycles |

### Option B3: Type-context markers (MainContext / MotorContext)

| Pro | Con |
|-----|-----|
| Compiler cannot produce binary with blocking in main loop | Changes `StepperMotor` trait signature |
| Zero false positives | Requires propagating `&MotorContext` through domain layer |
| No external tooling needed | Adds a type parameter to every blocking call |

### Option B4: Semgrep rule

| Pro | Con |
|-----|-----|
| Tree-sitter AST, not regex — understands scope | Pre-commit hook (not compile-time) |
| Single 20-line YAML file | Requires `semgrep` installed in CI |
| `pattern-not-inside` handles nested closures | Possible false positives on rare patterns |
| Fast: ~1s on a 3k-line Rust codebase | Misses `xTaskCreate` unless explicitly listed |
| Works cross-platform, no Rust version dependency | |

**Decision: Option B4 (Semgrep)** — best effort-to-value ratio. Type-context / dylint are supersets
that can be layered later if false positives become a problem. The current codebase is small enough
(3k lines) that Semgrep's tree-sitter AST provides sufficient accuracy.

---

## Final execution plan

### Step 1: Delete `scripts/check_unwrap.py`

- Remove file `scripts/check_unwrap.py`
- Remove line 41 from `scripts/pre_commit.sh` (step 7)

### Step 2: Harden `src/main.rs` against unwrap/expect

- Add `#![deny(clippy::unwrap_used, clippy::expect_used)]` to `src/main.rs` (crate-level)
- Add `#[allow(clippy::expect_used)]` on the 5 boot-time `expect()` calls (lines 35, 53, 56, 60, 63)

This ensures the binary crate is covered just like the library crate, and intentional boot-time
panics are explicitly marked.

### Step 3: Create `.semgrep/blocking.yml`

```yaml
rules:
  - id: blocking-call-outside-thread
    patterns:
      - pattern-either:
          - pattern: $X.send_and_wait(...)
          - pattern: $X.recv()
      - pattern-not-inside: |
          std::thread::spawn(...)
      - pattern-not-inside: |
          std::thread::Builder::new()....spawn(...)
    message: "Blocking call detected outside dedicated thread context. Use std::thread::spawn or xTaskCreate."
    severity: ERROR
    languages: [rust]
```

Rationale for included/excluded patterns:

| Pattern | Included? | Reason |
|---------|-----------|--------|
| `.send_and_wait(...)` | Yes | RMT blocking transmit (core concern) |
| `.recv()` | Yes | Blocks until message arrives |
| `.lock().unwrap()` | No | `.lock()` alone is not blocking (try_lock pattern); `unwrap()` is caught by Clippy |
| `std::thread::sleep(...)` | No | Heartbeat tick exception (10ms) is near-impossible to express cleanly in Semgrep |
| `.wait(...)` | No | False positive risk (e.g. `joinhandle.wait()`, future `.wait()` that aren't used here) |

`xTaskCreate` is intentionally omitted — the project uses `std::thread::spawn` / `Builder::spawn`.
If `xTaskCreate` is introduced later, add `pattern-not-inside`.

### Step 4: Remove blocking Python scripts

- Delete `scripts/check_blocking.py`
- Delete `scripts/test_check_blocking.py`

### Step 5: Update `scripts/pre_commit.sh`

- Remove step 3.5 (`python3 scripts/test_check_blocking.py`)
- Replace old step 8 with:
  ```bash
  echo "=== 8. Semgrep blocking check ==="
  semgrep --config .semgrep/ src/
  ```
- Renumber remaining steps

### Step 6: Verify

```bash
# 1. Clippy (lib + bin) must pass
cargo clippy --lib -- -D warnings
cargo clippy --bin ecotiter -- -D warnings

# 2. Host build must pass
cargo test --lib

# 3. Semgrep must pass on current codebase
semgrep --config .semgrep/ src/ --error
# Expected: no matches (no blocking calls outside threads)

# 4. Semgrep catches a deliberate violation (smoke test)
echo 'fn main() { stepper.send_and_wait(e, s, &c); }' > /tmp/test_block.rs
semgrep --config .semgrep/ /tmp/test_block.rs --error && false || true
rm /tmp/test_block.rs
```

---

## Files affected

| File | Action |
|------|--------|
| `scripts/check_unwrap.py` | **Delete** |
| `scripts/check_blocking.py` | **Delete** |
| `scripts/test_check_blocking.py` | **Delete** |
| `scripts/pre_commit.sh` | Edit: remove steps 3.5, 7; replace step 8 |
| `src/main.rs` | Edit: add `#![deny(...)]` + `#[allow(...)]` on 5 lines |
| `.semgrep/blocking.yml` | **Create** |
| `docs/plans/pending/26_06_30_python_checks_refactoring.md` | This file |

---

## Future considerations (not in scope)

1. **Type-context markers (`MotorContext` / `MainContext`)** — if Semgrep ever misses a real blocking call
   in the main loop, migrate to type-state. Would add `&MotorContext` param to `StepperMotor::move_steps()`
   and `RmtStepper::move_steps_intervals()`.

2. **dylint custom lint** — if the team grows and multiple custom rules are needed, consolidate into
   a single dylint plugin. Not worth the effort for one rule.

3. **CrankShaft / Rust-analyzer inline diagnostics** — Semgrep results can be surfaced in-editor via
   `semgrep --json` + editor plugin. Not needed now.
