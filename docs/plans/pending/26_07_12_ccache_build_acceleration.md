---
type: Plan
title: Build acceleration via ccache
description: Reduce clean-build time from 5–10 min to 1–2 min using ESP-IDF native ccache integration
tags: [build, ccache, optimization, ci]
timestamp: 2026-07-12
status: pending
---

# Build acceleration via ccache

## Summary

A clean build (`rm -rf build/` + `idf.py build`) currently takes **5–10 minutes**.
This is the bottleneck in the debug cycle: every `scripts/idf.sh build` (or
`smoke` with `--force-build`) recompiles every `.cpp` from scratch.

The root cause is **not** the clean-build policy — removing `build/` guarantees
reproducibility and prevents stale-binary bugs that sub-agents would chase.
The root cause is that every clean build actually compiles everything.

**Solution:** `ccache` (compiler cache). ESP-IDF has built-in support via
`IDF_CCACHE_ENABLE=1`. When enabled, `ccache` hashes each `.cpp` file + compiler
flags, and reuses the cached `.o` if unchanged — even after `rm -rf build`.

| Scenario | Before | After ccache |
|----------|--------|-------------|
| First clone / first build | 5–10 min | 5–10 min (cold cache) |
| Consecutive clean builds | 5–10 min | **1–2 min** (hot cache) |
| `build` → `smoke` (skip) | 0 (skip) | 0 (skip) |
| `smoke --force-build` | 5–10 min | **1–2 min** |

## Steps

### Step 1: Install ccache

```bash
sudo apt-get update && sudo apt-get install -y ccache
ccache -M 5G
```

`-M 5G` sets a 5 GB cache limit — sufficient for the project (~500 MB per
full build with all intermediate targets, ~3x headroom).

### Step 2: Enable in scripts/idf.sh

After sourcing `export.sh` (line 13), add:

```bash
export IDF_CCACHE_ENABLE=1
```

This tells the ESP-IDF build system to wrap the compiler with `ccache`.
No changes to CMakeLists.txt or Kconfig are needed.

### Step 3: Show ccache stats after build

In `do_build()`, after `idf.py build` completes, add:

```bash
# ccache stats (quiet if not installed)
ccache -s 2>/dev/null | grep -E "cache hit rate|cache size" || true
```

This gives immediate feedback that ccache is working, e.g.:
```
cache hit rate                  92.3 %
cache size                      1.2 GB
```

### Step 4: Verify

```bash
# Cold build (fill cache)
ccache -z && scripts/idf.sh build
ccache -s | grep "cache hit rate"    # ~0%

# Hot build (reuse cache)
scripts/idf.sh build
ccache -s | grep "cache hit rate"    # >90%
```

### Step 5: Document in AGENTS.md

Add a note under `## 4. BUILD & CI`:

```
- **ccache** used automatically (IDF_CCACHE_ENABLE=1). Clean builds after the
  first one take 1–2 min instead of 5–10. To verify: `ccache -s | grep rate`.
  Stats printed at end of every build.
```

## Verification

| Criterion | How | Expected |
|-----------|-----|----------|
| ccache installed | `which ccache` | `/usr/bin/ccache` |
| ccache enabled in build | `idf.sh build` first line | No error |
| Cold build works | First build after `ccache -z` | 5–10 min, hit rate ~0% |
| Hot build works | Second build without changes | 1–2 min, hit rate >90% |
| Incremental works | Touching one `.cpp` + rebuild | Only changed file recompiled |
| `smoke --force-build` fast | After hot build | ~1 min rebuild |

## Files affected

| File | Change | Status |
|------|--------|--------|
| System (`apt install`) | Install ccache, set 5 GB cache | ⏳ Step 1 |
| `scripts/idf.sh` | Add `export IDF_CCACHE_ENABLE=1` after `export.sh` source | ⏳ Step 2 |
| `scripts/idf.sh` | Add `ccache -s` stats after build | ⏳ Step 3 |
| `AGENTS.md` | Document ccache in Build section | ⏳ Step 5 |
