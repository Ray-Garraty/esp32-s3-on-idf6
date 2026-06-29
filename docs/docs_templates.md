---
type: Docs Rule
title: OKF Documentation Standards
description: Mandatory formatting rules for all .md documentation following OKF v0.1
tags: [documentation, okf, standards]
timestamp: 2026-06-27
---

# OKF Documentation Standards

This document defines the mandatory formatting rules for all `.md` documentation, following [OKF v0.1](https://github.com/GoogleCloudPlatform/knowledge-catalog/blob/main/okf/SPEC.md).

---

## General rules (all documents)

| Rule | Standard |
|------|----------|
| **Language** | English. Exception: literal UI strings in code examples (e.g., button labels) may remain in Russian |
| **Frontmatter** | YAML block delimited by `---`, required for every concept document |
| `type` | Required. One of: `Architecture Decision`, `Architecture Reference`, `Algorithm Reference`, `User Journey`, `Known Issue`, `Plan`, `Testing Guide`, `Metric`, `UI Rule`, `ESP32 Reference`, `Build Guide`, `Code Review`, `Docs Rule` |
| `title` | Human-readable display name |
| `description` | One-line summary |
| `tags` | YAML list for cross-cutting categorization |
| `timestamp` | ISO 8601 date of last meaningful change |
| `status` | Producer-defined: `pending`/`awaiting_validation`/`completed`/`cancelled`/`solved`/`active` |
| **Date format** | ISO 8601: `2026-06-15` |
| **Heading style** | ATX (`#`, `##`, ...). No emoji in headings. ASCII only |
| **First heading** | `# Title` for all documents |
| **Code blocks** | Always specify language: ` ```rust `, ` ```typescript `, ` ```json `, ` ```bash ` |
| **Table style** | Minimal: `|---|---|` |
| **HTML comments** | Encouraged as grep anchors: `<!-- grep: my-anchor -->` |
| **Horizontal rules** | Optional before sections |

---

## Standard frontmatter template

```yaml
---
type: Known Issue
title: BLE connection fails on weak adapters
description: First attempt always fails, succeeds on retry
tags: [ble, connection, hardware]
timestamp: 2026-06-15
status: solved
resolved: 2026-06-15
---
```

---

## Profile A — Known Issue

**Used in:** `docs/issues/`

```yaml
---
type: Known Issue
title: BLE connection fails intermittently
description: First attempt always fails on weak BT adapters, succeeds on retry
tags: [ble, connection]
timestamp: 2026-06-15
status: solved
resolved: 2026-06-15
---
```

Body sections: `## Problem`, `## Root cause`, `## Solution`, `## Edge cases`.

---

## Profile B — Plan

**Used in:** `docs/plans/`

```yaml
---
type: Plan
title: RMT driver refactoring
description: Refactor RMT stepper driver to use interrupt-based dispatch
tags: [stepper, rmt, refactoring]
timestamp: 2026-06-09
status: pending
---
```

Body sections: `## Summary`, `## Steps / Execution log`, `## Verification`, `## Files affected`.

---

## Profile C — Reference

**Used in:** `docs/refs/`, `docs/esp_idf_v6/`

```yaml
---
type: ESP32 Reference
title: RMT Stepper API
description: Pinout, timing, and API usage for the stepper motor driver
tags: [stepper, rmt, hardware]
timestamp: 2026-06-19
---
```

Body sections: `## Overview`, `## Commands`, `## Code structure`, `## Related modules`.

---

## Profile D — Build Guide

**Used in:** `docs/guides/`

```yaml
---
type: Build Guide
title: Flashing firmware via espflash
description: Step-by-step guide for building and flashing firmware to ESP32
tags: [build, flash, esp32]
timestamp: 2026-06-19
---
```

Body sections: `## Prerequisites`, `## Steps`, `## Verification`, `## Troubleshooting`.

---

## Profile E — Testing Guide

**Used in:** `docs/guides/`

```yaml
---
type: Testing Guide
title: Stepper ramp unit tests
description: How to run and write host-based unit tests for the stepper ramp module
tags: [testing, stepper, ramp]
timestamp: 2026-06-19
---
```

Body sections: `## Scope`, `## Setup`, `## Running tests`, `## Known issues`.

---

## Profile F — Code Review

**Used in:** `docs/reviews/`

```yaml
---
type: Code Review
title: RMT transmit safety review
description: Review checklist for RMT channel usage and unsafe code
tags: [review, rmt, safety]
timestamp: 2026-06-19
---
```

Body sections: `## Review checklist`, `## Common issues`, `## Notes`.

---

## Cross-linking

Use standard markdown links. Related files in the same or sibling directories:

```markdown
Detailed algorithm: [dynamic titration](../refs/titration.md)
```

---

## Citations

When a document references external sources, list them under `## Citations` at the bottom:

```markdown
## Citations

[1] [ESP-IDF RMT documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/rmt.html)
```

---

## Validation

Run before commit:

```bash
python docs/validate_okf.py
```

---

## File lifecycle

| Event | Action |
|-------|--------|
| New `.md` created | Add YAML frontmatter per profiles above |
| `.md` content changed | Update `timestamp` in frontmatter |
