---
type: Plan
title: Build acceleration via ccache
description: Full debug journey — from 0% hits to 99.91% ccache hit rate on hot builds
tags: [build, ccache, optimization, ci]
timestamp: 2026-07-12
status: completed
---

# Build acceleration via ccache

## Goal

Reduce clean-build time from 5–10 min to 1–2 min using ccache.
Every `scripts/idf.sh build` does `rm -rf build` + `idf.py build`.
ccache should cache `.o` files by source hash + flags, surviving `rm -rf build`.

## Result

| Scenario | Before fix | After fix |
|----------|-----------|-----------|
| Cold build (first after `rm -rf build`) | 3m 0s / 0% | 3m 0s / 0% (expected — cold cache) |
| Hot build (second, no code changes) | 3m 0s / 0% | **1m 8s / 99.91%** |
| Incremental (touch main.cpp) | 3m 0s / 0% | **~1m 10s / >99%** |

**ccache WORKS.** The first build after `rm -rf build` is always cold (3 min, 0% hits).
The second and subsequent builds hit ccache at >99%, completing in ~1 min.

## What was the fix?

Four environment variables in `scripts/idf.sh` `do_build()`, set **before** `idf.py build`:

```bash
export CCACHE_BASEDIR="$PROJECT_DIR"
export CCACHE_NOHASHDIR=1
export CCACHE_SLOPPINESS="time_macros,include_file_mtime,include_file_ctime,file_macro,locale,pch_defines"
export CCACHE_COMPILERCHECK=content
```

| Variable | Purpose |
|----------|---------|
| `CCACHE_BASEDIR` | Normalises absolute paths (makes cache portable across builds) |
| `CCACHE_NOHASHDIR` | Stops hashing the working directory (CWD differs between Ninja and idf.py) |
| `CCACHE_SLOPPINESS` | Ignores mtime of generated headers, `__DATE__`/`__TIME__`, file macro, locale |
| `CCACHE_COMPILERCHECK=content` | Ignores compiler binary mtime (hashes content instead) |

Additionally, BUILD_DATE/GIT_HASH were moved from `target_compile_definitions` (poisoned all files)
to a generated `version_build.h` included only by `version.cpp` — so only one file recompiles on timestamp changes.

---

## Full debug log

### Attempt 1: Sub-agent ses_0a9c — investigation spiral

**Time spent:** ~1 hour, 13 full builds (~39 min of waiting), 5596-line session log.
**Result:** Zero useful output. Agent never measured anything.

1. Ran first build (cold cache) → `Hits: 0/2225 (0.00%)`
2. Instead of running a second build, started investigating the 0% hit rate
3. Discovered BUILD_DATE poisoning (turn 6-7) but instead of reporting, started refactoring
4. Got stuck in debugging loop, never delivered any measurement

**Lesson:** GR-13 added — sub-agents must escalate after 3 failed iterations.

### Attempt 2: Sub-agent ses_0a98 — measurement with BUILD_DATE fix

**Time spent:** ~30 min.
**Result:** Build timings captured, ccache still at 0%.

Confirmed:
- BUILD_DATE fix (`version_build.h` instead of `target_compile_definitions`) ✅ in tree
- `-DBUILD_DATE` no longer in compiler flags
- `cxxflags` / `sdkconfig.h` byte-identical between builds
- Manual ccache test with cross-compiler works

Root cause of 0% hit rate **not found** — escalated after 30 min.

### Attempt 3: Orchestrator + QWEN consultation

Static analysis confirmed:
- CXX compilation rule: `ccache ${LAUNCHER}${CODE_CHECK}/path/to/gcc $DEFINES $INCLUDES $FLAGS`
- `${CODE_CHECK}` is empty (only set for `idf.py clang-tidy`)
- `hash_dir = true` (default), `sloppiness = ` (empty)
- `cxxflags` contains `-specs=/abs/path/to/picolibc.specs` — absolute path poison

**QWEN diagnosis:** Three root causes:
1. `hash_dir=true` + `rm -rf build` → CWD inode changes → hash differs
2. mtime of generated files (sdkconfig.h, version_build.h, picolibc.specs) → invalidates cache
3. Absolute paths in response files → `CCACHE_BASEDIR` needed

### Attempt 4: Sub-agent ses_0a95 — final fix + verify

**Time spent:** ~15 min.
**Result:** ccache verified at 99.91% hit rate.

Added to `do_build()`:
- `CCACHE_BASEDIR`, `CCACHE_NOHASHDIR`, `CCACHE_SLOPPINESS` (already present from earlier attempt)
- `CCACHE_COMPILERCHECK=content`

**Verification:**
1. `ccache -z && scripts/idf.sh build` — 1114 cacheable calls, 0 hits (cold) ✅
2. `scripts/idf.sh build` (immediately after, no changes) — **1113/1114 hits (99.91%)** ✅
3. Confirmed: `idf.py build` does NOT clear CCACHE_* env vars (identical before/after dump)

## Current ccache config in `scripts/idf.sh`

```bash
# Inside do_build(), before idf.py build:
export IDF_CCACHE_ENABLE=1
export CCACHE_BASEDIR="$PROJECT_DIR"
export CCACHE_NOHASHDIR=1
export CCACHE_SLOPPINESS="time_macros,include_file_mtime,include_file_ctime,file_macro,locale,pch_defines"
export CCACHE_COMPILERCHECK=content
```

Build timing is also logged:
```
Build time: 3m 0s  (cold)
Build time: 1m 8s  (hot — 99.91% ccache hit)
```

ccache stats printed after each build.

## Files affected

| File | Change | Status |
|------|--------|--------|
| `scripts/idf.sh` | `IDF_CCACHE_ENABLE=1` + ccache stats + build timing + CCACHE_* env vars | ✅ Done |
| `main/CMakeLists.txt` | `file(WRITE version_build.h)` instead of `target_compile_definitions` | ✅ Done |
| `main/version.cpp` (new) | Include `version_build.h`, expose `const char*` | ✅ Done |
| `main/version.h` (new) | Declare `extern const char*` | ✅ Done |
| `main/main.cpp` | Include `version.h`, use `ecotiter::build_date` | ✅ Done |
| `AGENTS.md` §4.5 | Document ccache usage + ccache stats | ✅ Done |
| `AGENTS.md` §4.3 | ccache build acceleration subsection | ✅ Done |
| `AGENTS.md` GR-13 | Sub-agent escalation rules (from LL-046) | ✅ Done |
| `docs/lessons_learned/LL-046.yaml` | Sub-agent failure analysis | ✅ Done |
