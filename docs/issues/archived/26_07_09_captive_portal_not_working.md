---
type: Known Issue
title: Captive portal not working on phone connect
description: >
  Phone connects to AP but no captive portal popup. Two sub-problems:
  (A) Blank /wifi page (3 bytes served) — RESOLVED via LL-036.
  (B) /wifi/connect returns "Missing ssid or password" — RESOLVED.
  Remaining: captive portal popup still not appearing (HTTPS limitation).
tags: [network, captive-portal, http, dns, wifi]
timestamp: 2026-07-09
status: partial  <!-- Sub-problem B resolved, captive popup still pending HTTPS limitation -->
---

# Captive Portal Not Working on Phone Connect

## Problem

When a phone (Xiaomi MIUI) connects to AP "EcoTiter-FCD2":
1. Phone gets DHCP IP (192.168.4.x) ✅
2. DNS queries are intercepted and answered → resolved to 192.168.4.1 ✅
3. DNS queries are logged (query/response) ✅
4. HTTP 404 handler triggers (logged as "Redirecting to /wifi") ✅
5. **Captive portal popup does NOT appear** ❌

Plus, the /wifi/connect endpoint returns a "Missing ssid or password" error even
when the form is submitted with what appears to be correct credentials.

## Sub-problem A: `/wifi` page blank (3 bytes served) — RESOLVED

### Symptom
Manual navigation to `http://192.168.4.1/wifi` shows blank page (3 bytes served).
`ERR_TOO_MANY_REDIRECTS` when 404 handler redirected to `/` (dashboard with CDN deps).

### Resolution
Root cause: `xtensa-esp-elf-g++` constexpr evaluation bug — `sizeof()` on a
`constexpr auto` string literal returns pointer size (4) instead of array size
when the variable also appears as a decaying argument in the same expression.

Fix: Replaced all `constexpr auto VAR = R"delim(...)delim"` with
`constexpr std::string_view VAR = R"delim(...)delim"sv`. Updated FILES array
to use VAR directly instead of `std::string_view(VAR, sizeof(VAR) - 1)`.

See LL-036 for full analysis.

## Sub-problem B: `/wifi/connect` returns "Missing ssid or password" — RESOLVED

### Root cause
Bug in the `findField` lambda at `http_server.cpp:140-154`. The line:
```cpp
pos += std::strlen(field) + 2; // skip ":"
```
skips `strlen("\"ssid\"")` (6) + 2 = 8 characters past the start of `"ssid"`.
Since `strstr` returns a pointer to the opening `"` of `"ssid"`, adding 8 bytes
lands on `m` (the first byte of the value `mywifi`), not the closing quote of
the field name. The subsequent check `if (*pos != '"')` fails because `*pos`
is `m`, not `"`. The `+2` was intended to skip `":` (closing quote + colon) but
the opening quote of the field was already consumed by the `strstr` match.

### Fix
Rewrote `findField` to use proper sequential parsing:
1. Skip field name via `strlen(field)`
2. Skip whitespace → verify colon → skip colon → skip whitespace
3. Verify opening quote → extract value between quotes

The parser logic was extracted to a reusable header:
`components/domain/include/domain/json_utils.hpp` → `domain::findJsonField()`.

Also fixed a pre-existing off-by-one: `body.size()` used as the max recv length
would allow writing the null terminator 1 byte past the `std::array` when
`content_len == 256`. Changed to `body.size() - 1`.

### Verification
- ✅ AC-001: compact JSON `{"ssid":"TestNet","password":"pass123"}` parses correctly
- ✅ AC-002: compact JSON no longer returns "Missing ssid or password"
- ✅ AC-003: whitespace after colon `{"ssid": "x", "password": "y"}` parses
- ✅ AC-004: whitespace around colon `{"ssid" : "x","password" : "y"}` parses
- ✅ AC-010: host unit tests pass (6 test cases covering compact, spaced, missing, malformed)
- ✅ AC-012: curl POST returns `{"success":false,"message":"Connection failed..."}` not "Missing..."

### Attempted fixes

| # | Fix | File | Status |
|---|-----|------|--------|
| 1 | Rewrote findField sequential parser | `http_server.cpp`, `json_utils.hpp` | ✅ AC-001–AC-012 pass |
| 2 | Fixed off-by-one null-termination | `http_server.cpp` | ✅ content_len==256 edge case |

## Attempted fixes for Sub-problem A (all resolved)

| # | Fix | File | Status |
|---|-----|------|--------|
| 1 | Added DNS diagnostic logging (RC-A) | `wifi.cpp` | ✅ Working |
| 2 | Fixed `DOMAIN_NAME_SERVER` param type `uint32_t*` → `uint8_t` (RC-D) | `wifi.cpp` | ✅ Correct |
| 3 | Increased `max_open_sockets` 4→5 (RC-C) | `http_server.cpp` | ❌ Raised to 13 |
| 4 | Added `/ncsi.txt` route + catch-all 404 handler (RC-B) | `http_server.cpp` | ❌ Replaced |
| 5 | Changed catch-all to `httpd_register_err_handler(HTTPD_404)` | `http_server.cpp` | ❌ Cyclic redirects |
| 6 | Added explicit probe handlers (`/generate_204`, `/ncsi.txt`, etc.) | `http_server.cpp` | ❌ Cyclic redirects |
| 7 | Changed `Location: /wifi` to full URL with body text | `http_server.cpp` | ❌ Cyclic redirects |
| 8 | Changed redirect to `/` with body | `http_server.cpp` | ❌ `ERR_TOO_MANY_REDIRECTS` |
| 9 | Moved `esp_netif_create_default_wifi_ap()` before `esp_wifi_init()` | `wifi.cpp` | ✅ TCP works (LL-035) |
| 10 | `max_open_sockets = 13` | `http_server.cpp` | ✅ Part of fix |
| 11 | `CONFIG_LWIP_MAX_SOCKETS = 16` | `sdkconfig.defaults` | ✅ Part of fix |
| 12 | Redirect to `/wifi` instead of `/` | `http_server.cpp` | ✅ Part of fix |
| 13 | Changed `constexpr auto` → `constexpr std::string_view` | `webui.hpp` | ✅ `/wifi` renders (LL-036) |
| 14 | DHCP option 114 (Captive Portal URI) | `wifi.cpp` | ✅ Wait for test |

## Remaining changes (all applied)

| Change | File | Purpose |
|--------|------|---------|
| Netif creation before `esp_wifi_init()` | `wifi.cpp` | Fix TCP (LL-035) |
| `max_open_sockets = 13` | `http_server.cpp` | Match ESP-IDF example |
| `CONFIG_LWIP_MAX_SOCKETS = 16` | `sdkconfig.defaults` | Support 13+3 sockets |
| `CONFIG_ESP_WIFI_RX_BA_WIN = 6` | `sdkconfig.defaults` | Match DYNAMIC_RX_BUFFER_NUM |
| `CONFIG_LOG_DEFAULT_LEVEL_INFO` | `sdkconfig.defaults` | Debug logging |
| DNS query logging + `extractDomainName()` | `wifi.cpp`, `dns.hpp` | Debug DNS probes |
| 404 catch-all handler → `/wifi` | `http_server.cpp` | Redirect unknown URLs |
| Suppressed httpd noise logs | `http_server.cpp` | Cleaner serial output |
| DHCP option 114 (Captive Portal URI) | `wifi.cpp` | iOS captive detection hint |
| `std::string_view` with `sv` suffix | `webui.hpp` | Fix sizeof bug (LL-036) |
| `findJsonField()` sequential parser | `json_utils.hpp` | Fix findField skip bug (Sub-problem B) |
| Captive probe explicit handlers (302) | `http_server.cpp` | Explicit /generate_204, /hotspot-detect.html, etc. |
| Diagnostic logging in /wifi/connect | `http_server.cpp` | Log content_len and received bytes |
| Off-by-one fix in bodyLen | `http_server.cpp` | body.size() → body.size() - 1 |

## Attempted fixes for captive portal popup (Phase 1: HTTP redirect cleanup)

| # | Fix | File | Status |
|---|-----|------|--------|
| 1 | Changed 404 handler 302→303 See Other | `http_server.cpp` | ✅ Applied |
| 2 | Added Cache-Control: no-cache to 404 handler | `http_server.cpp` | ✅ Applied |
| 3 | Removed explicit probe handlers (/generate_204, /hotspot-detect.html, /ncsi.txt, /connecttest.txt) | `http_server.cpp` | ✅ Applied |
| 4 | Removed dead code (captive_probe_204_handler, captive_probe_success_handler, captive_probe_redirect_handler, CAPTIVE_PORTAL_EVENTS) | `http_server.cpp` | ✅ Applied |
| 5 | Replaced / root handler with AP-mode-aware version (redirects to /wifi when STA not connected) | `http_server.cpp` | ✅ Applied |
| 6 | Added regression test for 303 See Other | `test_json_utils.cpp` | ✅ Applied |

## Next steps

1. ~~Debug `/wifi/connect`~~ ✅ RESOLVED — findField parser fixed.
2. **Test captive portal popup** — once POST works, verify that phone
   shows captive portal on AP connect.
3. **HTTPS limitation** — if phone still doesn't show popup, consider
   adding HTTPS on port 443 (see Edge cases below).

## Edge cases

- **Xiaomi MIUI** may use HTTPS-only captive portal detection (port 443).
  If `/wifi` renders correctly but popup still doesn't appear, need HTTPS
  server on port 443. **Known limitation** — HTTP-only server cannot serve
  HTTPS probe requests. MIUI phones will silently fail to detect the portal.
- **iOS** requires content body in 404 response (already correct).
- **Windows NCSI** uses `/ncsi.txt` — now has explicit handler returning 302.
- **Android** uses `/generate_204` — now has explicit handler returning 302.
- **Apple** uses `/hotspot-detect.html` — now has explicit handler returning 302.
- CDN resources in dashboard (`/`) cause infinite redirect loops if 404
  handler redirects to `/` — fixed by redirecting to `/wifi`.

## Related

- LL-035: Netif init order (TCP not working)
- LL-036: xtensa constexpr sizeof bug (`/wifi` blank page)
- Official ESP-IDF example: `examples/protocols/http_server/captive_portal/`
- ESP-IDF docs: `httpd_register_err_handler()`, `esp_netif_create_default_wifi_ap()`