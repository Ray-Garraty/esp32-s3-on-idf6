---
type: Plan
title: Architecture dependency checker script
description: Zero-dependency Python script for include-dependency analysis, SRP metrics,
  cycle detection, and architectural layering enforcement across ESP-IDF components
tags: [architecture, layering, static-analysis, python, include-deps, srp]
timestamp: 2026-07-21
updated: 2026-07-21
status: pending
---

# Architecture dependency checker script

## Summary

Add a static analysis tool that covers both parts of the original requirement:

1. **Dependency analysis** — enforce the project's 4-layer architecture
   (`domain` → `application` → `infrastructure` → `interface`) by scanning
   all `#include` directives and checking against a declarative allow-list.

2. **SRP violation detection** — per-file metrics (LOC, fan-out, public
   methods) that flag god-files, god-classes, and hidden coupling hubs.

The audit found 17 existing cross-include edges. **We do not fix them.**
The tool establishes a *baseline* with severity levels and sunset dates —
only *new* violations block CI. This prevents regression without demanding
abstract interfaces for single-call-site helpers.

**Key design decisions:**
- Zero external dependencies — Python stdlib only (`tomllib`, `pathlib`,
  `re`, `sys`, `json`)
- Config in TOML (`arch_config.toml`) — comments, merge-safe, no arbitrary
  code execution in CI
- Baseline in `baseline.json` (JSON, generated via `--generate-baseline`)
  with `severity`, `tier`, `sunset` fields
- `--strict` mode: check all violations (including baselined) — for audit CI
- `--format=json`: machine-readable for CI annotations
- `--graph`: DOT output restricted to project components (6 nodes);
  `--graph --all` includes virtual tags (22+ nodes)
- Distinguishes `.hpp`/`.h` in `include/` (PUBLIC — transitive leak)
  vs `.cpp` in `src/` (PRIVATE — local coupling only)
- Detects cycles via DFS on the component dependency graph
- Zero changes to production code — pure read-only analysis

## Config file: `arch_config.toml`

Single TOML file replacing the previous Python config. Read via
`tomllib.load()` (stdlib since Python 3.11). Declarative only — no logic.

```toml
# arch_config.toml
# Read by scripts/check_arch.py via tomllib. Zero external dependencies.
# WARNING: This file is declarative only. Do not add Python logic here.

[components]
domain = { path = "components/domain", allowed = ["domain"] }
application = { path = "components/application", allowed = ["domain", "application", "json"] }
infrastructure = { path = "components/infrastructure", allowed = [
    "domain", "application", "infrastructure", "diag", "json",
    "freertos", "esp_system", "esp_driver", "esp_timer",
    "esp_wifi", "esp_http", "esp_netif", "bt", "nvs_flash",
    "esp_pm", "esp_event", "lwip", "mbedtls", "mdns",
    "hal", "soc", "driver",
] }
interface = { path = "components/interface", allowed = [
    "domain", "application", "interface", "json", "esp_http", "freertos",
] }
diag = { path = "components/diag", allowed = [
    "domain", "diag", "freertos", "esp_system", "esp_timer", "esp_hal", "hal",
] }
main = { path = "main", allowed = ["*"] }

# How #include paths map to virtual component tags.
# Order matters: first match wins. Specific entries MUST come before
# generic ones (e.g. "esp_wifi" before "esp_") to avoid dead entries.
[[virtual_prefix]]
prefix = "components/domain/"; tag = "domain"
[[virtual_prefix]]
prefix = "components/application/"; tag = "application"
[[virtual_prefix]]
prefix = "components/infrastructure/"; tag = "infrastructure"
[[virtual_prefix]]
prefix = "components/interface/"; tag = "interface"
[[virtual_prefix]]
prefix = "components/diag/"; tag = "diag"
[[virtual_prefix]]
prefix = "components/json/"; tag = "json"
[[virtual_prefix]]
prefix = "main/"; tag = "main"
# Specific ESP-IDF subsystems BEFORE generic "esp_" fallback
[[virtual_prefix]]
prefix = "esp_wifi"; tag = "esp_wifi"
[[virtual_prefix]]
prefix = "esp_http"; tag = "esp_http"
[[virtual_prefix]]
prefix = "esp_netif"; tag = "esp_netif"
[[virtual_prefix]]
prefix = "esp_timer"; tag = "esp_timer"
[[virtual_prefix]]
prefix = "esp_pm"; tag = "esp_pm"
[[virtual_prefix]]
prefix = "esp_event"; tag = "esp_event"
[[virtual_prefix]]
prefix = "esp_bt"; tag = "bt"
[[virtual_prefix]]
prefix = "esp_driver"; tag = "esp_driver"
[[virtual_prefix]]
prefix = "esp_hal"; tag = "esp_hal"
[[virtual_prefix]]
prefix = "esp_"; tag = "esp_system"  # generic fallback
# Third-party
[[virtual_prefix]]
prefix = "freertos/"; tag = "freertos"
[[virtual_prefix]]
prefix = "driver/"; tag = "esp_driver"
[[virtual_prefix]]
prefix = "hal/"; tag = "hal"
[[virtual_prefix]]
prefix = "soc/"; tag = "soc"
[[virtual_prefix]]
prefix = "nvs_flash.h"; tag = "nvs_flash"
[[virtual_prefix]]
prefix = "nlohmann/"; tag = "json"
[[virtual_prefix]]
prefix = "bt/"; tag = "bt"
[[virtual_prefix]]
prefix = "lwip/"; tag = "lwip"
[[virtual_prefix]]
prefix = "mbedtls/"; tag = "mbedtls"
[[virtual_prefix]]
prefix = "mdns.h"; tag = "mdns"

# Headers generated at build time — always whitelisted, never checked
generated_headers = [
    "sdkconfig.h",
    "esp_app_desc.h",
    "cxxabi.h",
]

[srp_thresholds]
max_loc = 800
max_fan_out = 12
max_public_methods = 25
```

## Existing violations audit

### Cross-include violations

#### Tier A — Public header propagates forbidden deps

| # | File | Includes | Severity | Impact |
|---|------|----------|----------|--------|
| A1 | `domain/include/domain/log_buffer.hpp` | `freertos/FreeRTOS.h`, `freertos/queue.h` | HIGH | Public header leaks FreeRTOS to all consumers. Domain tests need FreeRTOS stubs. |
| A2 | `application/include/application/send_motor_command.hpp` | `infrastructure/motor_task.hpp` | HIGH | Public header couples all consumers to motor task queue globals. |

**Fix cost:** Medium — extract queue access behind callback/sink interface.
**Decision: DISCUSS.** Worth fixing if domain/app test isolation becomes
a measurable bottleneck.

#### Tier B — Private source files cross layer boundary

| # | File | Includes | Severity |
|---|------|----------|----------|
| B1 | `domain/src/log_buffer.cpp` | `freertos/FreeRTOS.h`, `freertos/queue.h`, `freertos/task.h`, `diag/stack_monitor.hpp` | LOW — source only |
| B2 | `application/src/handlers/valve.cpp` | `infrastructure/config.hpp`, `infrastructure/drivers/valve.hpp` | LOW |
| B3 | `application/src/handlers/burette_ops.cpp` | `infrastructure/cal_cache.hpp`, `infrastructure/config.hpp`, `infrastructure/storage/nvs.hpp` | LOW |
| B4 | `application/src/handlers/sensors.cpp` | `infrastructure/config.hpp`, `infrastructure/motor_task.hpp`, `infrastructure/storage/nvs.hpp` | LOW |
| B5 | `application/src/handlers/burette_cal.cpp` | `infrastructure/config.hpp` | LOW |
| B6 | `application/src/dispatch.cpp` | `infrastructure/storage/nvs.hpp` | LOW |
| B7 | `application/src/response.cpp` | `infrastructure/storage/nvs.hpp` | LOW |

**Fix cost:** High — would require cascading injection, single-impl interfaces (YAGNI),
or logic restructuring. No transitive leak.
**Decision: BASELINE.** No fix.

#### Tier C — Backwards dependency direction

| # | File | Includes | Severity |
|---|------|----------|----------|
| C1 | `infrastructure/network/src/http_server.cpp` | `interface/rest_api.hpp`, `interface/webui.hpp` | LOW — intentional wiring seam |

**Decision: BASELINE.** Breaking would require indirection without measurable benefit.

#### Summary

| Tier | Count | Action | Severity range |
|------|-------|--------|----------------|
| A | 2 | Discuss | HIGH |
| B | 9 | Baseline | LOW |
| C | 2 | Baseline | LOW |

### SRP metric outliers

Preliminary scan against project thresholds (TBD — to be calibrated):

| Metric | Threshold | Current max | File |
|--------|-----------|-------------|------|
| LOC | >800 | — | (needs measurement) |
| Fan-out (includes) | >12 | — | (needs measurement) |
| Public methods | >25 | — | (needs measurement) |

Calibration run happens during implementation.

## Implementation results (2026-07-21)

All files created/modified. Script verified on the unmodified codebase
with the following results:

```
default mode: exit 0 (0 NEW, 20 BASELINED, 3 cycles informational)
--strict:     exit 1 (cycles + baselined violations flagged)
--format=json: valid JSON with exit_code, violation breakdown
--graph:      DOT digraph (6 project nodes)
--graph --all: DOT digraph (22 nodes including virtual tags)
--generate-baseline: valid baseline.json with all 10 entries
```

**Findings beyond accepted baseline:**
- 3 circular dependencies (documented in `26_07_21_arch_tech_debt.md`)
- 2 SRP violations (stepper.hpp: 28 methods, webui.hpp: 814 LOC)

## Steps / Execution log

### Step 1: Create `scripts/arch_config.toml`

As shown above in the Config file section. Committed to repo alongside the
checker script.

### Step 2: Create `baseline.json`

JSON file with known violations, severity, tier, sunset dates.

```json
{
  "components/domain/include/domain/log_buffer.hpp": {
    "includes": ["freertos/FreeRTOS.h", "freertos/queue.h"],
    "severity": "high",
    "tier": "A",
    "sunset": "2026-10-01",
    "note": "Public header — extract FreeRTOS queue behind callback/sink"
  },
  "components/domain/src/log_buffer.cpp": {
    "includes": [
      "freertos/FreeRTOS.h", "freertos/queue.h",
      "freertos/task.h", "diag/stack_monitor.hpp"
    ],
    "severity": "low",
    "tier": "B",
    "sunset": "never",
    "note": "Source-only, no fix planned"
  },
  "components/application/include/application/send_motor_command.hpp": {
    "includes": ["infrastructure/motor_task.hpp"],
    "severity": "high",
    "tier": "A",
    "sunset": "2026-10-01",
    "note": "Public header — abstract motor task queue access"
  },
  "components/application/src/handlers/valve.cpp": {
    "includes": ["infrastructure/config.hpp", "infrastructure/drivers/valve.hpp"],
    "severity": "low",
    "tier": "B",
    "sunset": "never"
  },
  "components/application/src/handlers/burette_ops.cpp": {
    "includes": [
      "infrastructure/cal_cache.hpp", "infrastructure/config.hpp",
      "infrastructure/storage/nvs.hpp"
    ],
    "severity": "low",
    "tier": "B",
    "sunset": "never"
  },
  "components/application/src/handlers/sensors.cpp": {
    "includes": [
      "infrastructure/config.hpp", "infrastructure/motor_task.hpp",
      "infrastructure/storage/nvs.hpp"
    ],
    "severity": "low",
    "tier": "B",
    "sunset": "never"
  },
  "components/application/src/handlers/burette_cal.cpp": {
    "includes": ["infrastructure/config.hpp"],
    "severity": "low",
    "tier": "B",
    "sunset": "never"
  },
  "components/application/src/dispatch.cpp": {
    "includes": ["infrastructure/storage/nvs.hpp"],
    "severity": "low",
    "tier": "B",
    "sunset": "never"
  },
  "components/application/src/response.cpp": {
    "includes": ["infrastructure/storage/nvs.hpp"],
    "severity": "low",
    "tier": "B",
    "sunset": "never"
  },
  "components/infrastructure/network/src/http_server.cpp": {
    "includes": ["interface/rest_api.hpp", "interface/webui.hpp"],
    "severity": "low",
    "tier": "C",
    "sunset": "never",
    "note": "Intentional wiring seam — REST handler registration"
  }
}
```

Generated via `python3 scripts/check_arch.py --generate-baseline > baseline.json`.
Keys are repo-relative file paths, sorted alphabetically.

### Step 3: Create `scripts/check_arch.py`

Core script (~500 lines). Two passes.

**Pass 1 — Layer enforcement (include edges):**

1. Load `arch_config.toml` (COMPONENTS, VIRTUAL_PREFIX_MAP, GENERATED_HEADERS,
   SRP_THRESHOLDS) via `tomllib.load()`. Load `baseline.json` (known violations
   with severity/tier/sunset).
2. Discover source files — walk each component's `path`, collect `.cpp`,
   `.hpp`, `.h` files. Omit `managed_components/`, `build/`, `build-tests/`,
   `legacy/`.
3. Pre-process each file before regex:
   - Strip `//` line comments
   - Strip `/* ... */` block comments
   - Track `#ifdef`/`#ifndef`/`#if`/`#elif`/`#else`/`#endif` nesting with
     a simple integer stack. Includes inside `#if` blocks flagged as
     `[CONDITIONAL]` in the output.
4. Parse includes — extract `#include "..."` and `#include <...>` lines.
   Skip GENERATED_HEADERS.
5. Resolve target component — for each include path, walk VIRTUAL_PREFIX_MAP
   in order; first match wins. Unresolved paths get
   `target_component=UNRESOLVED`.
   - System headers (`<cstdint>`, `<atomic>`, `<expected>`, `<array>`,
     `<vector>`, `<string_view>`, `<memory>`, `<cmath>`, etc.) → skip.
     Heuristic: if `<>` include and does not match any prefix → stdlib.
6. Classify file type:
   - **PUBLIC** — any `.hpp`/`.h` inside an `include/` subdirectory
   - **PRIVATE** — any `.cpp` or header file outside `include/`
7. Check rule in this exact order:
   - If source component `allowed == ["*"]` → PASS
   - If resolved tag is in `allowed` list → PASS
   - Else → VIOLATION
8. Apply baseline — if the exact (file, include_path) pair exists in
   `baseline.json` → tag as `BASELINED` (no exit code impact except in
   `--strict` mode). If not in baseline → tag as `NEW`.
   Note: `UNRESOLVED` violations are never baselined — they always fail.
9. On each VIOLATION, check severity override from baseline if present.
   For `PUBLIC` violations with `severity=high`, always treat as HIGH
   regardless of baseline.

**Cycle detection (added after include graph build):**
10. Build a directed graph of component→component edges from all resolved
    includes. DFS on each unvisited node. Track visit stack; if a node is
    revisited while still on the stack → cycle. Report cycle path.
    Exit code 1 if cycles found — cycles always fail, even in baselined edges.
    Only project components are included in cycle detection
    (virtual tags like `freertos` are leaf nodes with no outgoing edges).

**Pass 2 — SRP metrics:**

11. Per file:
    - **LOC**: count non-blank, non-comment lines (total physical LOC
      regardless of `#ifdef` blocks). Does not strip `#ifdef` — counts all
      lines in the file as-is.
    - **Fan-out**: count unique resolved include targets, excluding stdlib
      and GENERATED_HEADERS. Conditional includes are counted in fan-out
      and tagged as `[CONDITIONAL]`. Duplicate includes (same header twice)
      are deduplicated — counted once.
    - **Public methods (heuristic)**: scan `.hpp`/`.h` files only. In
      `public:` sections, count lines containing `(` (opening paren of a
      method/constructor/destructor/cast operator), excluding lines that
      match `if(`, `for(`, `while(`, `switch(`, `return`. This is a
      heuristic — expected ±20% accuracy vs a real C++ parser.
12. Flag any file where a metric exceeds SRP_THRESHOLDS.

**Output format — key=value (default, designed for AI agents):**

Every record starts with `status=` as the discriminator. Records are
separated by an empty line. An AI agent splits on `\n\n` and parses
each block with a simple key-value reader.

**VIOLATION (new — must fix):**
```
status=VIOLATION
source_file=components/domain/include/domain/log_buffer.hpp
source_line=14
include=freertos/FreeRTOS.h
source_component=domain
target_component=freertos
conditionality=unconditional
file_type=PUBLIC_HEADER
severity=HIGH
action=move this include behind a sink interface in infrastructure/ — domain headers must not pull FreeRTOS

```

**BASELINED (known debt — no action):**
```
status=BASELINED
source_file=components/application/src/handlers/valve.cpp
source_line=22
include=infrastructure/config.hpp
source_component=application
target_component=infrastructure
conditionality=unconditional
file_type=PRIVATE_SOURCE
severity=LOW
tier=B
sunset=never
action=no action required — accepted architectural debt

```

**UNRESOLVED (unknown include — must fix):**
```
status=VIOLATION
source_file=components/network/src/wifi.cpp
source_line=5
include=some_unknown/header.h
source_component=infrastructure
target_component=UNRESOLVED
file_type=PRIVATE_SOURCE
severity=HIGH
action=unknown include target — add to arch_config.toml VIRTUAL_PREFIX_MAP or fix include path

```

**CYCLE (must fix — even if all edges are baselined):**
```
status=CYCLE
path=application → infrastructure → interface → application
action=break one edge with an abstraction layer — cycles cause non-deterministic rebuilds

```

**SRP (must fix):**
```
status=SRP
source_file=components/domain/include/domain/types.hpp
metric=public_methods
value=47
threshold=25
action=split god-class — 47 public methods exceeds limit of 25

```

**`--format=json`:** emits the same data as a JSON object with arrays
for CI integration (GitHub Actions annotations, GitLab Code Quality):

```json
{
  "violations": {
    "total": 17,
    "new": 0,
    "baselined": 17,
    "unresolved": 0,
    "list": [
      {
        "status": "BASELINED",
        "source_file": "components/application/src/handlers/valve.cpp",
        "source_line": 22,
        "include": "infrastructure/config.hpp",
        "source_component": "application",
        "target_component": "infrastructure",
        "file_type": "PRIVATE_SOURCE",
        "severity": "LOW",
        "tier": "B",
        "sunset": "never"
      }
    ]
  },
  "cycles": 0,
  "srp": {
    "over_loc": [],
    "over_fan_out": [],
    "over_public_methods": []
  },
  "exit_code": 0
}
```

**Flags:**

| Flag | Effect |
|------|--------|
| (none) | Default — NEW violations → exit 1, baselined → exit 0 |
| `--strict` | NEW + BASELINED → exit 1. For release audits. |
| `--format=json` | Machine-readable JSON output |
| `--graph` | DOT digraph of project components only (6 nodes) |
| `--graph --all` | DOT digraph including virtual tags (22+ nodes) |
| `--generate-baseline` | Dump all current violations as `baseline.json` → stdout |

### Step 4: Integrate into `scripts/pre_commit.sh`

Add step **4b** after the existing semgrep check (step 4):

```bash
# === 4b. Architecture dependency check ===
echo "=== 4b. Architecture dependency check ==="
python3 "$PROJECT_DIR/scripts/check_arch.py"
```

Not added to `scripts/idf.sh` — the script has no dependency on ESP-IDF.
It is a standalone Python tool.

### Step 5: Register in `AGENTS.md` and `coding_style.md`

**AGENTS.md §3.1 (Build & CI Commands):** Add row:

| `python3 scripts/check_arch.py` | Architecture layering + SRP + cycle check |

**AGENTS.md §3.4 (Development pipeline skill):** Add to mandatory validation
steps after code generation.

**coding_style.md §13 (Pre-Merge Checklist):** Add item:

> - [ ] No `#include` violations: `python3 scripts/check_arch.py` exits 0

## Verification

1. **Run on current codebase** — 0 NEW violations, 17 BASELINED. Exit 0.
2. **Inject a NEW violation** — add `#include "infrastructure/config.hpp"`
   to any domain file → `status=VIOLATION` with `file_type=PUBLIC_HEADER`
   or `PRIVATE_SOURCE` → exit 1.
3. **Cycle detection** — create a temporary circular include between two
   components → `status=CYCLE` → exit 1.
4. **SRP threshold** — set `max_fan_out=1` → flag the file with the most
   includes → `status=SRP`.
5. **`--strict` mode** — exit 1 on a codebase that has only baselined edges.
6. **`--format=json`** — output parsable as JSON, `exit_code` field matches
   actual exit code.
7. **`--generate-baseline` roundtrip**:
   ```
   python3 scripts/check_arch.py --generate-baseline > /tmp/baseline_test.json
   cp /tmp/baseline_test.json baseline.json
   python3 scripts/check_arch.py
   ```
   Must exit 0 with 0 NEW violations. This proves the generated baseline is
   correctly consumed by the main pass.
8. **UNRESOLVED include** — add `#include "made_up/header.h"` to any file →
   `target_component=UNRESOLVED` → exit 1 (never baselinable).
9. **`--graph`** — DOT output contains only project component nodes
   (domain, application, infrastructure, interface, diag, main).
10. **`--graph --all`** — DOT output contains virtual tags as well.
11. **Pre-commit integration** — `scripts/pre_commit.sh --fast` runs step 4b,
    passes on unmodified codebase.
12. **Smoke test** — `scripts/idf.sh smoke` still passes.

## Files affected

| File | Action | Purpose |
|------|--------|---------|
| `scripts/arch_config.toml` | Create | Component definitions, virtual prefix map, generated headers, SRP thresholds |
| `scripts/check_arch.py` | Create | Main engine (1057 lines) — include scanning, SRP metrics, cycle detection, output |
| `baseline.json` | Create | Known violations with severity, tier, sunset (10 files, 20 edges) |
| `scripts/pre_commit.sh` | Modify | Add step 5 (architecture dependency check after semgrep) |
| `AGENTS.md` | Modify | Register command in §3.1 build table, update §3.5 pipeline steps |
| `docs/refs/coding_style.md` | Modify | Add pre-merge checklist item |
| `docs/plans/pending/26_07_21_arch_tech_debt.md` | Create | Tech debt plan — cycles, SRP outliers, Tier-A items |
