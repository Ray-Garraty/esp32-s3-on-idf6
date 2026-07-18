---
type: Known Issue
title: WS session drops during motor operations (LL-052 regression → LL-053)
description: WebSocket session for fd=51 dropped and re-added during burette.moveSteps/burette.stop sequence, causing missed broadcast messages
tags: [websocket, http, motor, session]
timestamp: 2026-07-18
status: resolved
resolved_at: 2026-07-18
fix_commit: (pending)
fix_summary: >
  ws_handler now calls removeSession(fd) and returns ESP_FAIL on
  non-INVALID_SIZE recv failure, aligning handler session tracking
  with ESP-IDF framework socket lifecycle.
---

# WS Session Drops During Motor Operations

## Problem

The `http_api_test.py` reports `WS session added 2x for fd 51 (session drop detected)` during the motor control test sequence (`burette.moveSteps` + `burette.stop`).

Evidence from test run 2026-07-18_12-36-44:

```
WS session added for fd=51  (first registration on {"type":"sub"})
... during burette.moveSteps + stop ...
WS session added for fd=51  (re-registration — session was dropped)
```

Result: **29/30 tests passed, 1 FAIL** — WS session-drop regression.

## Root cause

The session drop coincides with `burette.moveSteps` and `burette.stop` HTTP requests. The motor task commands (`setDirection`, `moveSteps`, `stop`) are fire-and-forget (Phase 0 removed `waitResult(60000)`), so the HTTP worker is not blocked. However, `httpd_ws_recv_frame` in `ws_handler` (`http_server.cpp:540`) may fail with a transient socket error when the ESP32-S3 is busy with motor operations. When recv fails (non-INVALID_SIZE), the handler returns `ESP_OK` but the ESP-IDF HTTP server may internally close the WS session due to the recv error. The client's next message then triggers a re-registration via `addSession`.

The problem is that `ws_handler` returns `ESP_OK` on recv failure (line 550) but does not actively remove the session — yet the framework removes it anyway. The gap between framework-side session teardown and handler-side re-registration causes missed broadcast messages.

## Investigation

### Candidates ruled out

- **ws_handler refactoring:** git diff shows only NOLINT comment removal — no logic change to `ws_handler`.
- **captive_wifi_connect_handler unique_ptr refactoring:** unrelated code path.
- **buildHttpdConfig() extraction:** the config values are identical, verified by debug log in `startHttpdWithConfig()`.

### Candidates still open

- Transient `httpd_ws_recv_frame` failure during motor I/O (RMT transmit, TMC UART traffic on shared bus).
- ESP-IDF v6 WebSocket implementation behavior on recv errors — does the framework close the session when `ws_handler` returns `ESP_OK` after a recv failure?

## Reproduction

```bash
python3 scripts/testing/http_api_test.py
```

The test reliably triggers the session drop during the motor control sequence (steps 3–6), though the exact timing varies.

## Workarounds

- Increase `config.max_open_sockets` to tolerate transient closures.
- Add retry logic on the client side for missed WS messages.

## Solution (proposed)

Two options, in order of preference:

1. **Handle recv failure by reconnecting:** In `ws_handler`, on recv failure (non-INVALID_SIZE), call `removeSession(fd)` before returning `ESP_FAIL`. This prevents the framework from silently closing the session while the handler believes it's still active.

2. **Poll-based fallback:** If WS is unreliable during motor ops, the WebUI JavaScript already has a `startMotorPollFallback()` polling `/api/status` every 500ms (added in Phase 0). Ensure it is triggered when WS messages are missed.

## Edge cases

- **Multiple concurrent WS clients:** Each fd is tracked independently. The drop affects only the affected fd.
- **Rapid connect/disconnect cycles:** Sequential tests that open/close WS connections may accumulate stale fd entries if cleanup is missed.

## Related modules

| File | Role |
|------|------|
| `components/infrastructure/network/src/http_server.cpp` | `ws_handler()` — session management + frame recv |
| `scripts/testing/http_api_test.py` | `check_ws_session_drops()` — detection logic |

## Resolution

### Fix applied (2026-07-18)

**Root cause (corrected):** The bug was introduced in commit `66d9029` as part of the frame.len fix. That commit changed the recv-failure path from `return err + removeSession(fd)` to `return ESP_OK` (without removeSession), based on the incorrect assumption that `broadcastWsEvent()` would clean up stale sessions. In fact, returning `ESP_OK` prevents the ESP-IDF framework from closing the broken socket, causing a desync between handler bookkeeping and framework state.

**Fix (2 lines in 1 file):**
- `components/infrastructure/network/src/http_server.cpp` — On non-INVALID_SIZE `httpd_ws_recv_frame` failure, call `server->removeSession(fd)` and return `ESP_FAIL` instead of `ESP_OK`.

**Test fix:**
- `scripts/testing/http_api_test.py` — `check_ws_session_drops()` now distinguishes real drops (recv failure preceding reconnection) from graceful fd recycling across independent connections.

**Lesson learned (LL-053):**
- Added to `AGENTS.md` Pre-Flight Checklist: handler return-value changes must be verified against ESP-IDF framework source.
- `docs/lessons_learned/LL-053.yaml`

**Validation:**
- `scripts/idf.sh smoke` — 70s, BOOT OK, no crashes/WDT
- `http_api_test.py` — **30/30 PASS**, 0 session drops
- Zero `recv failed` messages in serial log during motor ops

## Changelog

| Date | Change |
|------|--------|
| 2026-07-18 | Initial issue filed after http_api_test.py 29/30 failure |
| 2026-07-18 | Fixed: removeSession(fd) + return ESP_FAIL in ws_handler; hardened check_ws_session_drops() |
| 2026-07-18 | Archived — resolved |
