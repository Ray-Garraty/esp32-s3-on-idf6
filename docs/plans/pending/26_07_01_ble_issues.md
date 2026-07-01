---
type: Known Issue
title: BLE activation breaks WebUI + mDNS — HTTP latency regression
description: >
  Enabling BLE (removing `ESP_COEX_PREFER_BT`, restoring 12 KB NimBLE stack,
  reordering init to HTTP→BLE→WiFi) successfully starts BLE advertising and
  fixes the four original issues, but introduces severe HTTP latency (7+ s
  per request) and mDNS init failure. WebUI pages never load; `ecotiter.local`
  does not resolve. Root cause of the new regression is still under investigation.
tags: [ble, wifi, coexistence, http, internal-dram, esp32, webui, mdns]
timestamp: 2026-07-01
status: pending
---

# BLE activation breaks WebUI + mDNS — HTTP latency regression

## Session summary

Goal was to enable BLE, build, flash, and verify BLE advertising + HTTP server
+ WiFi STA working simultaneously on ESP32 (ESP-IDF v6.0.1, esp32-nimble).

**Result:** Four original issues were fixed, but two new regressions appeared
that break the WebUI.

---

## Original issues (fixed this session)

### Issue 1: `ESP_ERR_HTTPD_TASK` — HTTP server task creation fails

**Symptom:**
```
EspHttpServer::new() failed: EspIOError(ESP_ERR_HTTPD_TASK (error code 45064))
```
`httpd_start()` could not create the FreeRTOS task for the HTTP server.

**Initial misdiagnosis:** internal DRAM exhaustion (Qwen hint). The heap showed
`free=167 KB, largest=108 KB`, but `xTaskCreate` for the HTTP server (12 KB
stack) requires `MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL`. After BLE + WiFi
initialization, the internal DRAM pool was too fragmented to satisfy the
allocation.

**Fix applied:** moved BLE init (`ble_mgr.init()`) from the main task
to the `net_owner` thread, **after** `HttpServer::new()`. When the HTTP
server task is created while DRAM is still pristine, `xTaskCreate` succeeds.

### Issue 2: NimBLE host task creation hangs (spin loop on SYNCED)

**Symptom:**
```
BLE: calling BLEDevice::init()...
BTDM_INIT: Bluetooth MAC: b4:bf:e9:09:ff:ee
(no "BLE Host Task Started" — hangs forever)
```
`BLEDevice::init()` enters a spin loop waiting for `SYNCED=true`, which is
set by the `on_sync` callback running in the `nimble_host` FreeRTOS task.
The task was never created — `nimble_port_freertos_init()` → `xTaskCreate`
failed silently.

**Root cause:** same as Issue 1 — after WiFi init, internal DRAM was too
fragmented for a 12 KB NimBLE host task stack.

**Fix applied:**
- Changed init order so BLE runs **before** WiFi (DRAM still pristine)
- `CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE=12288` (was 8192, restored)
- `CONFIG_BT_NIMBLE_MSYS_1_BLOCK_COUNT=12` (was 24, kept reduced)
- `CONFIG_BT_NIMBLE_ACL_BUF_COUNT=4` (was 20, kept reduced)
- `CONFIG_BT_NIMBLE_ACL_BUF_SIZE=256` (was 512, kept reduced)
- `CONFIG_BT_NIMBLE_MAX_CONNECTIONS=1`

### Issue 3 (critical): `ESP_COEX_PREFER_BT` kills WiFi TCP

**Symptom:** WiFi STA associates, DHCP gets `192.168.1.103`, but:
- `ping 192.168.1.103` — 100% packet loss
- `curl http://192.168.1.103/` — TCP connect timeout
- HTTP server task is alive (route registration logs visible)
- BLE advertising visible on phone scan

**Root cause:** `ble.rs:102` called `set_coex_ble_preferred()` →
`esp_coex_preference_set(ESP_COEX_PREFER_BT)`. The ESP32 BT/WiFi coexistence
arbitrator starves the WiFi L2 interface of airtime, breaking TCP/IP even
though the STA link is established and an IP is assigned. Additionally,
`esp_coex_preference_set()` is **deprecated** in ESP-IDF v6.0
(use `esp_coex_status_bit_set/clear` instead) and is only intended for
BLE MESH scenarios, not for BLE peripheral.

**Fix applied:** Removed the call. Default coexistence (`ESP_COEX_PREFER_BALANCE`)
gives 50/50 airtime to WiFi and BLE automatically.

### Issue 4 (secondary): NimBLE host task stack overflow on connect

**Symptom (observed after the DRAM workaround in the previous session):**
```
***ERROR*** A stack overflow in task nimble_host has been detected.
```
When a BLE client connects, the NimBLE host stack (8 KB) overflows.

**Root cause:** direct consequence of reducing the stack to 8 KB to fit in
fragmented DRAM. The original 12 KB is required for connection processing
(GATT, MTU update, notification queues).

**Fix applied:** restored `CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE=12288`.
With BLE init before WiFi, the 12 KB allocation succeeds.

---

## New regressions (introduced this session)

### Issue 5: Severe HTTP latency — all requests delayed 7+ seconds

**Symptom:**
```
$ curl -w "total: %{time_total}s\n" http://192.168.1.103/api/ping
total: 7.2s
{"status":"ok"}

$ curl -w "total: %{time_total}s\n" http://192.168.1.103/
[timeout after 30s — headers arrive, body never completes]
```

- TCP connection establishes instantly
- Request sent immediately
- **Response headers arrive 7+ seconds later** (for simple endpoints)
- Response body for large payloads (28 KB index.html) **never completes**
- All HTTP endpoints affected (ping, status, WebUI)
- **Not** a WebUI-specific issue — even `/api/ping` is slow

**Observed response headers (curl -v):**
```
< HTTP/1.1 200 OK
< Content-Type: application/json
< Transfer-Encoding: chunked
```

Headers use `Transfer-Encoding: chunked` (not `Content-Length: 0`), indicating
that `httpd_resp_send_chunk` DOES get called eventually but is severely delayed.

**Boot log (relevant timestamps):**
```
[15:58:08.745] HTTP: server started on port 80
[15:58:09.195] BLE: init OK, advertising
[15:58:09.226] WiFi: trying STA connect
[15:58:14.237] wifi:connected with TP-Link_29D4
[15:58:15.953] sta ip: 192.168.1.103
```

HTTP server starts **before** any WiFi netif is available (5+ seconds before
WiFi connects, 7+ seconds before DHCP assigns IP).

**Hypothesis:** `httpd_start()` binds to `0.0.0.0:80` while no netif exists.
When the WiFi STA netif later comes up, the pre-existing listening socket may
not properly route incoming TCP connections, causing lwIP core lock contention
or delayed accept processing. The 7-second delay matches the WiFi-connect
latency window.

**Affected files (changes made in this session):**
- `src/main.rs` — reordered init: `HttpServer::new()` → `ble_mgr.init()` → `wifi_mgr.init()`
- `src/infrastructure/network/ble.rs` — removed `set_coex_ble_preferred()`
- `sdkconfig.defaults` — stack 8192→12288, removed duplicate NimBLE config
- `src/config.rs` — `BLE_NOTIFY_THREAD_STACK` 8192→6144

**Status: NOT FIXED.** Root cause still under investigation.

### Issue 6: mDNS init fails — `ecotiter.local` does not resolve

**Symptom:**
```
[WARN] mDNS: init failed: ESP_FAIL
```
`EspMdns::take()` returns `ESP_FAIL` when called from `start_mdns()` inside
`wifi.init()`.

**Observed timing:**
```
[15:58:14.944] WiFi: STA connected to 'TP-Link_29D4'
[15:58:14.966] Initializing MDNS
[15:58:14.987] [WARN] mDNS: init failed: ESP_FAIL
[15:58:15.953] sta ip: 192.168.1.103  ← IP arrives AFTER mDNS failure
```

mDNS is initialized **before** DHCP completes and the IP is assigned.
`EspMdns::take()` likely requires at least one netif with a valid IP address,
which is not yet available.

**Hypothesis:** In the original init order (WiFi → HTTP), `start_mdns()` was
called after `BlockingWifi::connect()` returned, which waits for the `GOT_IP`
event. In the new order (HTTP → BLE → WiFi), the `wifi.init()` flow may return
from `wifi.connect()` before the IP is assigned — the STA link is up but DHCP
is still in progress.

**Status:** NOT FIXED. Related to Issue 5's init-order change.

---

## Current state (end of session)

| Component | Status |
|-----------|--------|
| HTTP server | ✅ Starts on port 80, all 17 routes registered |
| WiFi STA | ✅ Associates to TP-Link_29D4, gets 192.168.1.103/24 |
| BLE advertising | ✅ "EcoTiter-" visible on BLE scanner (RSSI -46) |
| BLE connect | ✅ No stack overflow (12 KB stack works) |
| TCP/IP over WiFi | ⚠️ Works but 7+ second latency (Issue 5) |
| WebUI in browser | ❌ Never loads — HTTP body never completes (Issue 5) |
| Curl /api/ping | ❌ 7+ second delay before response (Issue 5) |
| Curl GET / | ❌ Timeout — 30s+ no body data (Issue 5) |
| mDNS — ecotiter.local | ❌ `EspMdns::take()` returns `ESP_FAIL` (Issue 6) |

---

## Root cause analysis

### Original problems (all fixed)

**Factor A — Coexistence policy:**
`ESP_COEX_PREFER_BT` was inappropriate and deprecated. Default balance works.

**Factor B — Internal DRAM fragmentation:**
BLE init before WiFi ensures 12 KB contiguous block for NimBLE host task.

### Remaining problems (unfixed)

**Factor C — Init order side effect (Issue 5 & 6):**
Changing the init order from `WiFi→HTTP` to `HTTP→BLE→WiFi` means:
1. `httpd_start()` runs before any netif exists — possible lwIP routing issue
2. `wifi.init()` → `start_mdns()` may run before DHCP completes — mDNS fails

Both are direct consequences of the init reorder that was needed for Fix B
(DRAM ordering). A solution must address both DRAM ordering AND the netif
dependency.

---

## Investigation notes

### Curl diagnostics (observed)

- TCP handshake: **instant** (no delay)
- Request send: **instant**
- First response byte: **7+ seconds** (the bottleneck)
- Response body for `api/ping` (15 B): arrives after headers
- Response body for `/` (28 KB): **never arrives** within 30 s timeout
- Consecutive requests: equally slow (no caching/warmup effect)

### What was ruled out

- **Not** WiFi TCP connectivity — `lwIP` establishes connections, IP is valid
- **Not** handler code — `/api/ping` is a trivially simple handler (no locks, no I/O)
- **Not** BLE/WiFi coexistence RF arbitration — BLE is only advertising, no active connection
- **Not** `ESP_COEX_PREFER_BT` — already removed
- **Not** NimBLE host task CPU starvation — HTTP task has higher priority (22 vs 20)
- **Not** static content — `handle_api_ping()` is computed, not static
- **Not** a specific route — ALL endpoints affected

### Suspected root cause

`httpd_start()` (called by `EspHttpServer::new()`) creates a TCP server task
that binds to `0.0.0.0:80`. When called **before any WiFi netif exists**, the
listening socket may experience:
1. lwIP core lock contention with the WiFi driver task (priority 23 > HTTP 22)
2. Delayed TCP accept processing due to missing netif routing
3. Incomplete lwIP PCB setup requiring netif-attach after binding

### To verify

1. Check if reverting init order (WiFi→HTTP→BLE) while keeping BLE → WiFi
   order fixes the latency
2. Or: keep HTTP first but verify `httpd_start()` with explicit netif config
3. Check `EspMdns::take()` error reason — netif not ready vs already taken

---

## References

- AGENTS.md — ESP32 crash investigation, NimBLE patch notes
- `docs/plans/completed/26_06_30_phase4_network_report.md` — Phase 4 report
- `src/infrastructure/network/ble.rs` — BLE manager
- `src/esp_safe.rs` — `set_coex_ble_preferred()` wrapper (now dead code)
- `docs/plans/pending/26_07_01_webui_transfer.md` — WebUI transfer plan
- ESP-IDF coexistence docs: `docs.espressif.com/…/coexist.html` — default
  coexistence (no preference) gives 50/50 WiFi/BLE time slices automatically
