---
type: Architecture Reference
title: WiFi Subsystem Specification
description: Init order, IP visibility timing, coexistence, and captive portal architecture
tags: [wifi, ap, sta, init-order, ip, captive-portal]
timestamp: 2026-07-13
---

# WiFi Subsystem Specification

## Overview

The ESP32-S3 operates a WiFi Soft-AP on `192.168.4.1/24` for device configuration (captive portal) and optionally connects as a STA to a home/office WiFi network for LAN access. Both modes can run concurrently.

**Design constraint:** The device IP address (AP or STA) MUST be visible in the serial log **within 30 seconds** of `BOOT OK`. No other subsystem (motor, temperature, sensors) may block or delay WiFi initialization.

---

## Init Order

### Init order

```
app_main()
  → xTaskCreate(net_owner)   // WiFi in another task
  → xTaskCreate(motor)       // motor in another task

netTaskEntry()
  → wifi.init()
  → startAP()                // LOGS IP within seconds of BOOT OK
  → tryStartSTA()            // non-blocking, async connect
  → HttpServer.init()
  → BLE.init()
```

All subsystems are independent FreeRTOS tasks. No subsystem blocks or waits for another.

### IP Logging — Requirements

| Event | Log line | When |
|-------|----------|------|
| AP started | `AP started: EcoTiter-XXXX (192.168.4.1)` | ≤ 10 s after BOOT OK |
| STA got IP | `STA got IP: 192.168.1.x` | ≤ 10 s after STA connects |
| STA failed | `No saved WiFi credentials` or `STA connection timeout` | ≤ 15 s after BOOT OK |

The user MUST see at least one IP address in the log within 30 seconds of BOOT OK under all conditions:

| Scenario | IP visible in 30s | Source |
|----------|-------------------|--------|
| No STA credentials saved | ✅ `192.168.4.1` | AP start |
| STA credentials saved, connects | ✅ `192.168.4.1` + STA IP | AP start + `IP_EVENT_STA_GOT_IP` |
| STA credentials saved, fails | ✅ `192.168.4.1` + timeout msg | AP start + STA timeout |

---

## Dual-Mode Architecture

### AP Mode (always on)

- SSID: `EcoTiter-{MAC}` (last 4 hex digits)
- Password: `ecotiter123` (from `config::AP_PASSWORD`)
- IP: `192.168.4.1/24`
- DHCP server enabled: leases in `192.168.4.x`
- Captive portal: DHCP option 114 → `http://192.168.4.1/wifi`
- DNS server: resolves `ecotiter.local` → `192.168.4.1`

### STA Mode (optional, from NVS)

- Connects asynchronously via `tryStartSTA()`
- `wifi.process()` in net_owner loop handles reconnection
- On `IP_EVENT_STA_GOT_IP`: log IP, update `WifiManager::staIP`
- On `IP_EVENT_STA_DISCONNECTED`: log disconnect, auto-reconnect

### mDNS

- Registered after IP assignment (`IP_EVENT_STA_GOT_IP` or immediately after AP start)
- Hostname: `ecotiter`
- Resolves to AP IP (`192.168.4.1`) or STA IP, whichever is reachable from the client's network

---

## Coexistence

- `CONFIG_FREERTOS_UNICORE=n` (GR-12): WiFi ISR on CPU0, RMT on CPU1 — no conflict
- `ESP_COEX_PREFER_BALANCE` (GR-4): no BT preference, fair RF sharing
- WiFi RX buffer count: `WIFI_DYNAMIC_RX_BUFFER_NUM=6`, `WIFI_RX_BA_WIN=6` (matched to avoid `#error` in `wifi_init.c`)

### Interaction with Other Subsystems

| Subsystem | Concurrent with WiFi AP? | Notes |
|-----------|-------------------------|-------|
| **RMT (stepper)** | ✅ Yes | AP runs on CPU0, RMT ISR on CPU1. Confirmed stable by serial API test (6/6 passed) |
| **HTTP server** | ✅ Yes | Binds after homing, serves on 192.168.4.1:80 |
| **BLE** | ✅ Yes | Init after HTTP (GR-3), stops advertising when USB connected |
| **OneWire (temp)** | ✅ Yes | Bitbang on GPIO6, non-DMA, does not touch WiFi or RMT peripherals |
| **ADC (pH)** | ✅ Yes | Oneshot read, no DMA |

---

## Verification

### Automated (CI)

| Test | What it checks |
|------|---------------|
| `serial_api_test.py` | WiFi-independent, tests serial command/response |
| `http_api_test.py` | HTTP on 192.168.4.1 — requires client connected to `EcoTiter-*` AP |
| `ble_test.py` | BLE NUS — requires BT adapter on test machine |

### Manual

```bash
# Flash and monitor for 30s
scripts/idf.sh flash
timeout 30 python3 scripts/monitor.py

# Check log for IP within 30s of BOOT OK
rg "AP started|STA got IP|No saved WiFi" logs/serial_*.log

# Connect to AP and test HTTP
curl http://192.168.4.1/api/ping
curl http://192.168.4.1/wifi
mDNS: curl http://ecotiter.local/api/ping
```

### Acceptance Criteria

1. ✅ `rg "AP started.*192.168.4.1"` in serial log within 10 s of BOOT OK
2. ✅ `rg "STA got IP"` in serial log within 10 s of STA connect (if credentials saved)
3. ✅ `curl http://192.168.4.1/api/ping` returns `{"status":"ok"}`
4. ✅ `curl http://192.168.4.1/wifi` returns captive portal HTML (200 OK)
5. ✅ Motor homing concurrent with WiFi AP — no RTCWDT_RTC_RST, no TWDT panic

---

## Related Documents

| Document | Link |
|----------|------|
| Project refs (threads, stack budgets, GPIO) | [project.md](project.md) |
| Watchdog specification | [watchdog_spec.md](watchdog_spec.md) |
| LL-044: Homing + WiFi AP interrupt storm (obsolesced by dual-core) | [../lessons_learned/LL-044.yaml](../lessons_learned/LL-044.yaml) |
| GR-12: Dual-core mandatory | [../../AGENTS.md](../../AGENTS.md) |
| Coding style (RAII, error handling) | [coding_style.md](coding_style.md) |
