---
type: Known Issue
title: AckThen commands block HTTP worker task, violating Articles I and II
description: burette.moveSteps (and other AckThen commands) block the HTTP worker task on waitResult(60000), preventing new HTTP/WS connections and violating task sovereignty
tags: [http, motor, architecture, constitution, blocking]
timestamp: 2026-07-17
status: active
---

# AckThen commands block HTTP worker task

## Problem

Any command that returns `ResponseKind::AckThen` (e.g., `burette.moveSteps`, `burette.fill`, `burette.empty`, `burette.doseVolume`, `burette.rinse`, `burette.cal.run`) blocks the HTTP worker task for the entire duration of the motor operation.

Evidence from `http_api_test.py` run on 2026-07-17_15-01-13:

```
15:01:22.760  POST /api/command burette.moveSteps steps=3000
15:01:23.261  WS connect ws://172.16.130.39/ws/stream
                 🟡 2.56 seconds — no response from HTTP server
15:01:25.822  WS {type:'sub'} sent
15:01:25.823  moveSteps HTTP 200 ← motor done, HTTP worker free
15:01:25.965  WS session finally registered
```

During the 2.56-second window, the HTTP server **cannot accept new WebSocket connections** because the single HTTP worker task is stuck in `waitResult(60000)`. The main loop continues broadcasting to existing WS clients via `httpd_ws_send_frame_async` (direct socket write), but any new HTTP request (status poll, new WS upgrade, second command) is queued behind the blocked worker.

### Affected commands

All handlers returning `makeAckThenResponse()` in `components/application/src/handlers/burette_ops.cpp`:

| Function | Line | Type |
|----------|------|------|
| `handleFill()` | 33 | Fill |
| `handleEmpty()` | 55 | Empty |
| `handleDoseVolume()` | 92 | DoseVolume |
| `handleRinse()` | 110 | Rinse |
| `handleCalRun()` | 168 | CalRun (speed/dose) |
| `handleMoveSteps()` | 215 | MoveSteps |

Also `handleCalGetResult()` in `burette_cal.cpp:193`.

---

## Root cause

### Code path

`rest_api.cpp:127-218` — `command_handler()`:

```cpp
// L161: For Single/Error — return immediately
if (rsp->kind != application::ResponseKind::AckThen) {
    // ... serialize and return ...
    return ESP_OK;
}

// L191: AckThen — BLOCK
auto result = controller->waitResult(60000);
```

`motor_controller_impl.cpp:418-434` — `waitResult()`:

```cpp
std::optional<domain::SmResult> waitResult(uint32_t timeoutMs) {
    // ...
    if (xQueueReceive(q, &result, pdMS_TO_TICKS(timeoutMs)) == pdTRUE) {
        return result;
    }
    return std::nullopt;
}
```

`xQueueReceive` with a 60-second timeout blocks the **calling task** (the HTTP worker) until the motor task produces a result via `store_result()`. The motor task only calls `store_result()` after `execute_move_steps()` completes — which can take seconds.

### Why WS is partially affected

- `httpd_ws_send_frame_async` sends data **directly to the socket** via `sess->send_fn()` from the calling task (main loop). This works because the socket is asynchronous at the TCP level.
- But **new WebSocket connections** require an HTTP upgrade handshake (`HTTP_GET`) which must be processed by a **free HTTP worker task**. If the only worker is blocked on `waitResult`, the handshake stalls.

### Constitutional violations

**Article I — Main Loop is Sacred (Non-Blocking):** While the blocked task is the HTTP worker (not `app_main`), the spirit of Article I applies to any system task whose blockage starves network services. The HTTP worker is a system-critical task — its blockage prevents HTTP request processing.

**Article II — Task Sovereignty:** The HTTP handler task **directly waits** for the motor task via `xQueueReceive(resultQueue, ..., 60000)`. Tasks must communicate via queues without synchronous waiting. The correct pattern is fire-and-forget: return `accepted` immediately, the motor task reports completion via WS broadcast.

---

## Solution (proposed)

Replace the synchronous `waitResult` pattern with an asynchronous fire-and-forget model:

1. **HTTP handler** returns `{"status":"accepted"}` immediately for all AckThen commands (never blocks)
2. **Motor task** sends a completion event via WS broadcast when done (already has `store_result()` infrastructure)
3. **HTTP worker task** remains free to process other requests during motor operation
4. **Client** learns about completion via WS messages (`brt.sts` transition or explicit `{"event":"motor_complete",...}`)

### Implementation sketch

```
rest_api.cpp:command_handler():
  if (kind == AckThen) {
      httpd_resp_send(req, R"({"status":"accepted"})", ...);
      return ESP_OK;  // ← never block
  }

Motor task → store_result() → notify via WS broadcast
```

This eliminates the `waitResult(60000)` call entirely and restores full HTTP/WS availability during motor operations.

---

## Edge cases

1. **Backward compatibility**: WebUI JS (`stepper.js`) currently expects a synchronous HTTP response for AckThen commands. It must be updated to handle `{"status":"accepted"}` and listen for WS completion events instead.

2. **Client timeout**: If no WS is connected, the client never learns about completion. Fallback: client polls `/api/status` (which works because HTTP worker is never blocked).

3. **Multiple AckThen in flight**: Currently impossible (queue system rejects if busy → returns `makeErrorResponse("busy")`). With async model, the queue check still applies — motor busy returns error immediately. No change needed.

4. **Existing WS connections**: Continue to receive broadcasts during motor ops (already proven working — `httpd_ws_send_frame_async` bypasses HTTP worker).

---

## Related modules

| File | Role |
|------|------|
| `components/interface/src/rest_api.cpp` | HTTP handler with `waitResult(60000)` blocking call |
| `components/infrastructure/src/motor/motor_controller_impl.cpp` | `waitResult()` implementation |
| `components/application/include/application/motor_controller.hpp` | `IMotorController` interface |
| `components/infrastructure/src/motor/motion.cpp` | `store_result()` — sets Idle state and pushes to result queue |
| `components/interface/include/interface/webui.hpp` | WebUI JS — expects synchronous AckThen responses |

## Evidence

Test log: `scripts/testing/logs/http_api_test_2026-07-17_15-01-13.log` lines 183-188 (2.56s WS connect delay during moveSteps).

Homing timeout (60s → 2s) already fixed in `components/infrastructure/src/motor/homing.cpp:29` — separate issue, same constitutional violation pattern.

---

## Citations

[1] Constitution: `docs/refs/CONSTITUTION.md` — Article I (Non-Blocking), Article II (Task Sovereignty)
[2] ESP-IDF `httpd_ws_send_frame_async` source: `components/esp_http_server/src/httpd_ws.c:512-570`
