---
type: Known Issue (Resolved)
title: "HTTP POST /api/command handler does not log received commands or responses"
description: "command_handler() in rest_api.cpp parses and dispatches HTTP commands without ESP_LOGI. Commands from WebUI invisible in serial monitor, LogBuffer, and WebUI log panel."
tags: [webui, rest_api, logging, http]
timestamp: 2026-07-21
status: resolved
resolved_at: 2026-07-21
---

# HTTP POST /api/command handler does not log received commands or responses

**Severity:** High  
**Detected:** 2026-07-21, serial log `serial_2026-07-21_18-20-48.log`  
**Related rules:** GR-0.1 (green unit tests prove nothing — only hardware/serial logs matter)

## Problem

Pressing WebUI stepper Start/Stop produces zero output in:
- Serial monitor (`logs/serial_*.log`)
- In-memory LogBuffer → WebUI "System Log" textarea
- Any ESP_LOG level stream

Compare with USB Serial path (`main.cpp:480` — `ESP_LOGI(TAG, "RX: %s", ...)`) and BLE path (`main.cpp:443` — `ESP_LOGI(TAG, "BLE RX: %s", ...)`) — both log every command.

The HTTP response (`{"status":"accepted"}` or `{"status":"stopped"}`) is also not logged, so the operator cannot determine whether the command was accepted, rejected, or what result it produced.

## Root cause

`command_handler()` in `rest_api.cpp:128-184`:
- Reads HTTP body via `httpd_req_recv`
- Parses JSON via `application::parseCommand()`
- Dispatches via `application::dispatch()`
- Sends HTTP response via `httpd_resp_send()`
- **Has zero `ESP_LOGI` calls at any point**

```cpp
esp_err_t ecotiter::interface::command_handler(httpd_req_t* req)
{
    domain::memory::CommandBuffer body{};
    // ... httpd_req_recv ...
    auto sv = std::string_view(body.data(), bodyLen);
    auto cmd = application::parseCommand(sv);       // ← no log before
    auto rsp = application::dispatch(*cmd);          // ← no log before
    if (rsp->kind != domain::ResponseKind::AckThen) {
        // synchronous response
        httpd_resp_send(req, rspBuf.data(), ...);   // ← no log before
    } else {
        httpd_resp_send(req, R"({"status":"accepted"})", ...); // ← no log before
    }
}
```

Legacy Arduino had two log calls in `command.cpp:63,85`:
```cpp
logger.debug("cmd_rx: %s", json_str.c_str());
logger.debug("cmd_dispatch: %s", cmd);
```

## Solution applied

### Files modified

| File | Change |
|---|---|
| `components/interface/src/rest_api.cpp` | +3 `ESP_LOGI` in `command_handler()`: HTTP RX after body recv, HTTP RSP before sync response, HTTP RSP before AckThen `{"status":"accepted"}` |
| `components/infrastructure/src/motor/motion.cpp` | +1 `ESP_LOGI` in `store_result()` — logs motor completion type and steps |
| `main/main.cpp` | +1 `ESP_LOGI` after `formatSmResult()` — logs SM result in main loop |
| `tests/src/test_rest_api.cpp` | +regression test verifying `ESP_LOGI` calls with `HTTP RX`/`HTTP RSP` exist in source |

### Verification

- **Smoke test** (`scripts/idf.sh smoke`): BOOT OK, no Guru Meditation, no WDT
- **HTTP API test** (`scripts/testing/http_api_test.py`): **30/30 ALL CHECKS PASSED**
- **Pre-commit** (`scripts/pre_commit.sh --fast`): all 8 checks passed
- **Serial log confirmation** — every POST /api/command now produces:
  ```
  I (3923) rest_api: HTTP RX: {"id": 1, "cmd": "serial.ping"}
  I (3928) rest_api: HTTP RSP: {"id":1,"status":"ok","data":{"status":"ok"}}
  I (9458) rest_api: HTTP RX: {"id": 100, "cmd": "burette.setDirection", "direction": "liq_in"}
  I (12738) rest_api: HTTP RX: {"id": 101, "cmd": "burette.moveSteps", "steps": 3000}
  I (12744) rest_api: HTTP RSP: {"status":"accepted"}
  I (15890) rest_api: HTTP RX: {"id": 102, "cmd": "burette.stop"}
  I (15895) rest_api: HTTP RSP: {"id":102,"status":"ok","data":{"status":"stopped"}}
  ```

## Test coverage

### Source-code regression test (`test_rest_api.cpp`)

Following the pattern from `test_ble_init.cpp`:

```cpp
TEST_CASE("rest_api.cpp: command_handler logs HTTP commands", "[rest_api][logging][regression]")
{
    auto path = TESTS_SOURCE_DIR "/../components/interface/src/rest_api.cpp";
    auto lines = readFileLines(path);
    REQUIRE_FALSE(lines.empty());

    bool hasRxLog = false;
    bool hasRspLog = false;
    for (const auto& line : lines)
    {
        if (line.find("ESP_LOGI") != std::string::npos && line.find("HTTP RX") != std::string::npos)
            hasRxLog = true;
        if (line.find("ESP_LOGI") != std::string::npos && line.find("HTTP RSP") != std::string::npos)
            hasRspLog = true;
    }
    REQUIRE(hasRxLog);
    REQUIRE(hasRspLog);
}
```

### HIL test (`scripts/testing/http_api_test.py`)

After sending any `POST /api/command`, serial log output contains:
- `HTTP RX:` line with the command body
- `HTTP RSP:` line with the response

## Edge cases

- **Body not null-terminated**: `%.*s` with explicit length handles this safely
- **Large payloads**: `CommandBuffer` is 256 bytes — `%.*s` caps output
- **Re-entrancy**: `ESP_LOGI` in HTTP handler context may trigger `wsLogCallback` → `gWsSendQueue` → `drainWsSendQueue` → `broadcastWsEvent`. This is the same pattern used by `ESP_LOGI` in other HTTP handlers and is already safe.
