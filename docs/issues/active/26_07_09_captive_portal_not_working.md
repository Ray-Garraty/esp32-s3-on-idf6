---
type: Known Issue (Active)
title: Captive portal not working on phone connect
description: >
  Phone connects to AP but no captive portal popup. Two sub-problems:
  (A) Blank /wifi page (3 bytes served) — RESOLVED via LL-036.
  (B) Captive portal popup still not appearing after /wifi renders correctly — ACTIVE.
tags: [network, captive-portal, http, dns, wifi]
timestamp: 2026-07-09
status: active
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

## Sub-problem B: `/wifi/connect` returns "Missing ssid or password" — ACTIVE

### Symptom
1. User navigates to `http://192.168.4.1/wifi` ✅ (now serves full HTML after LL-036 fix)
2. User fills SSID + Password in the form
3. Form POSTs to `/wifi/connect`
4. Response: `{"success":false,"message":"Missing ssid or password"}` ❌

### Suspected cause
The `findField` parser in `http_server.cpp:121-135` uses `std::strstr()` to
locate `"ssid"` and `"password"` keys, then manually scans past `":` and quotes.
If the form sends JSON with slightly different formatting (e.g. no space after
colon, or the JavaScript `fetch` sends extra headers), the parser may not find the
fields.

The JavaScript in `webui.hpp` sends:
```js
fetch('/wifi/connect', {
  method: 'POST',
  headers: {'Content-Type': 'application/json'},
  body: JSON.stringify({ssid: ssid, password: pass})
});
```

`JSON.stringify` produces: `{"ssid":"mywifi","password":"mypass"}` (no spaces).

The `findField` function searches for `"ssid"`, then skips `":`, then expects `"`.
This should work with `{"ssid":"mywifi"}`. The issue may be:
- Content length mismatch (`req->content_len` vs actual body length)
- `httpd_req_recv` returning partial data
- Body contains URL-encoded form data instead of JSON (if Content-Type is wrong)

### Attempted fixes

| # | Fix | File | Status |
|---|-----|------|--------|
| 1 | — | — | ❌ Not yet investigated |

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

## Next steps

1. **Debug `/wifi/connect`** — add logging of received body, Content-Type,
   content length. Verify that `httpd_req_recv` returns the full body.
2. **Test captive portal popup** — once POST works, verify that phone
   shows captive portal on AP connect.

## Edge cases

- **Xiaomi MIUI** may use HTTPS-only captive portal detection (port 443).
  If `/wifi` renders correctly but popup still doesn't appear, need HTTPS
  server on port 443.
- **iOS** requires content body in 404 response (already correct).
- **Windows NCSI** uses `/ncsi.txt` — caught by catch-all redirect.
- CDN resources in dashboard (`/`) cause infinite redirect loops if 404
  handler redirects to `/` — fixed by redirecting to `/wifi`.

## Related

- LL-035: Netif init order (TCP not working)
- LL-036: xtensa constexpr sizeof bug (`/wifi` blank page)
- Official ESP-IDF example: `examples/protocols/http_server/captive_portal/`
- ESP-IDF docs: `httpd_register_err_handler()`, `esp_netif_create_default_wifi_ap()`