---
type: Plan
title: Architecture tech debt — cycles, SRP outliers, Tier-A violations
description: Document and resolve 3 circular dependencies, 2 SRP threshold violations,
  and 3 Tier-A public header leaks discovered by check_arch.py
tags: [architecture, tech-debt, cycles, srp]
timestamp: 2026-07-21
updated: 2026-07-21
status: pending
---

# Architecture tech debt

## Summary

The `check_arch.py` initial scan (2026-07-21) found the following issues
beyond the accepted baseline of 31 cross-include edges across 14 files:

| Category | Count | Severity |
|----------|-------|----------|
| Circular dependencies | 3 | HIGH |
| SRP threshold exceeded | 2 | MEDIUM |
| Tier-A public header leaks | 3 | HIGH (sunset 2026-10-01) |

## Circular dependencies

### Cycle 1: application ↔ infrastructure

```
application → infrastructure → application
```

**Edges:**
- application → infrastructure: 10 baselined includes
  (send_motor_command.hpp, handlers/*.cpp → config, NVS, drivers)
- infrastructure → application:
  `infrastructure/network/src/wifi.cpp` includes `application/command.hpp`
  `infrastructure/network/src/ble.cpp` includes `application/command.hpp`
  `infrastructure/network/src/http_server.cpp` includes `application/dispatch.hpp`

**Root cause:** HTTP/BLE/WiFi handlers in infrastructure need to format
`Command` structs, which live in application. This creates a back-edge.

**Fix options:**
- Move `Command`/`CommandType`/`CommandResponse` types to domain
- Move handler logic (formatting commands) from infrastructure to application

### Cycle 2: application → infrastructure → interface → application

**Edges:** Cycle 1 + interface → application:
- `interface/src/rest_api.cpp` includes `application/command.hpp`,
  `application/dispatch.hpp`, `application/state_machine.hpp`

**Root cause:** REST API handler dispatches commands. Same as Cycle 1
but through the interface layer.

**Fix:** Same as Cycle 1 — fixing application↔infrastructure breaks
this cycle transitively.

### Cycle 3: infrastructure → interface → infrastructure

**Edges:**
- infrastructure → interface: `http_server.cpp` includes `interface/rest_api.hpp`,
  `interface/webui.hpp` (baselined Tier-C)
- interface → infrastructure: `interface/src/serial.cpp` includes
  `infrastructure/drivers/stepper.hpp`, etc. (intentional — serial needs to read
  driver status)

**Root cause:** Interface layer reads hardware state directly.

**Fix options:**
- Move serial status queries to application layer
- Have interface call through application instead of directly to infrastructure

## SRP threshold violations

### stepper.hpp — 28 public methods (threshold 25)

**File:** `components/infrastructure/include/infrastructure/drivers/stepper.hpp`

The StepperMotor class combines:
- RMT channel configuration
- Step pulse generation
- Microstepping control
- Direction control
- Enable/disable

**Fix option:** Split into `RmtChannel` (low-level pulse), `StepperDriver`
(microstepping + enable), `StepperMotor` (high-level move interface).

### webui.hpp — 814 LOC (threshold 800)

**File:** `components/interface/include/interface/webui.hpp`

Likely a large inline template or embedded HTML. Check if it's generated
content. If embedded HTML, move to a separate `.html` file and embed at
build time via `xxd` or `embed` directive.

## Tier-A public header leaks (sunset 2026-10-01)

These have a hard sunset date and will become NEW violations after that date:

| File | Includes | Action needed |
|------|----------|---------------|
| `domain/include/domain/log_buffer.hpp` | `freertos/FreeRTOS.h`, `freertos/queue.h` | Extract queue behind callback/sink in infrastructure |
| `application/include/application/send_motor_command.hpp` | `infrastructure/motor_task.hpp` | Abstract motor task queue access |
| `application/include/application/esp_check.hpp` | `esp_err.h`, `esp_log.h` | Remove direct ESP-IDF includes; application must not depend on esp_system |

## Resolution plan

1. **Cycle 1 (application↔infrastructure):** Move `Command` types to domain.
   Highest priority — breaks 2 of 3 cycles in one change.
2. **Cycle 3 (infrastructure↔interface):** Move serial status reads to application.
3. **stepper.hpp:** Split into 3 classes.
4. **webui.hpp:** Check if generated; if inline HTML, extract at build time.
5. **Tier-A (3 items):** Revisit before 2026-10-01.

Each item should be its own implementation plan with acceptance criteria.
