---
type: ESP32 Reference
title: ESP-IDF v6 Migration Reference
description: Comprehensive migration guide from ESP-IDF v5.5 to v6.0 — system, WiFi, protocols, storage, toolchain, build system, networking
tags: [esp-idf, migration, v6, reference, esp32]
timestamp: 2026-07-06
---

# ESP-IDF v6 Migration Reference

## Overview

ESP-IDF v6.0 introduces several breaking changes from v5.5. This document consolidates all migration-relevant information from the official Espressif migration guides for the ecotiter-fw project.

**Key structural changes:**
- Default LibC: Newlib → **PicolibC** (smaller, less stack/heap, breaking stdio semantics)
- Toolchain: GCC **14.2.0 → 15.1.0** (new warnings, stricter checks)
- Build system: orphan sections → **linker error** (was warning); warnings → **errors** by default
- Managed components: JSON, MQTT, Modbus, Ethernet PHY drivers moved to IDF Component Registry

---

## System

### LibC: Newlib → PicolibC

Since ESP-IDF v6.0, the default LibC is **PicolibC** (a Newlib fork with rewritten stdio).

| Metric | Newlib | Picolibc | Difference |
|--------|--------|----------|------------|
| Binary size | 280 KB | 225 KB | −19.8% |
| Stack usage | 1,748 B | 802 B | −54.1% |
| Heap usage | 1,652 B | 376 B | −77.2% |
| Performance | 278M cycles | 280M cycles | −0.59% |

**CRITICAL — Breaking change:** `stdin`, `stdout`, `stderr` are now **global and shared** across all tasks (POSIX standard). Newlib's per-task redefinition is gone.

- `CONFIG_LIBC_PICOLIBC_NEWLIB_COMPATIBILITY` (default=y) provides thread-local copies of stdin/stdout/stderr + `getreent()`.
- Disable it if not linking against Newlib-built libraries to save memory.
- Manipulating `struct reent` internals with PicolibC → **task stack corruption**.
- Switch back to Newlib via menuconfig: `CONFIG_LIBC` → `LIBC_NEWLIB`.

**Project impact:** Our logging in `logger.rs` uses `esp_timer_get_time()` — verify no `struct reent` manipulation.

### Power Management

**`esp_sleep_get_wakeup_cause()` — DEPRECATED.**

Use `esp_sleep_get_wakeup_causes()` which returns a **bitmap** of all wakeup sources:

```c
// Old (single source)
esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
if (cause == ESP_SLEEP_WAKEUP_EXT1) { ... }

// New (bitmap, all sources)
uint32_t causes = esp_sleep_get_wakeup_causes();
if (causes & BIT(ESP_SLEEP_WAKEUP_EXT1)) { ... }
if (causes & BIT(ESP_SLEEP_WAKEUP_TIMER)) { ... }
```

**GPIO wakeup APIs renamed:**

| Removed | Replacement |
|---------|-------------|
| `esp_deep_sleep_enable_gpio_wakeup()` | `esp_sleep_enable_gpio_wakeup_on_hp_periph_powerdown()` |
| `gpio_deep_sleep_wakeup_enable()` | `gpio_wakeup_enable_on_hp_periph_powerdown_sleep()` |
| `gpio_deep_sleep_wakeup_disable()` | `gpio_wakeup_disable_on_hp_periph_powerdown_sleep()` |
| `esp_deepsleep_gpio_wake_up_mode_t` | `esp_sleep_gpio_wake_up_mode_t` |
| `GPIO_IS_DEEP_SLEEP_WAKEUP_VALID_GPIO()` | `GPIO_IS_HP_PERIPH_PD_WAKEUP_VALID_IO()` |

New APIs work for **both** Deep Sleep and Light Sleep (when `PM_POWER_DOWN_PERIPHERAL_IN_LIGHT_SLEEP` is enabled).

### FreeRTOS

**Removed functions:**

| Removed | Replacement |
|---------|-------------|
| `xTaskGetAffinity()` | `xTaskGetCoreID()` |
| `xTaskGetIdleTaskHandleForCPU()` | `xTaskGetIdleTaskHandleForCore()` |
| `xTaskGetCurrentTaskHandleForCPU()` | `xTaskGetCurrentTaskHandleForCore()` |
| `xQueueGenericReceive()` | `xQueueReceive()` / `xQueuePeek()` / `xQueueSemaphoreTake()` |
| `vTaskDelayUntil()` | `xTaskDelayUntil()` |
| `ulTaskNotifyTake()` | macro `ulTaskNotifyTake` |
| `xTaskNotifyWait()` | macro `xTaskNotifyWait` |

**Deprecated:** `pxTaskGetStackStart()` → `xTaskGetStackStart()` (type safety).

**Memory placement — CRITICAL:** Most FreeRTOS functions moved from IRAM to **flash** by default. `CONFIG_FREERTOS_PLACE_FUNCTIONS_INTO_FLASH` removed. To restore IRAM placement, enable `CONFIG_FREERTOS_IN_IRAM`.

**Project impact:** Our AGENTS.md stack budget and thread model assume IRAM placement. If performance-critical paths degrade, enable `CONFIG_FREERTOS_IN_IRAM`. Task snapshot API now in `freertos/freertos_debug.h`.

### Ring Buffer

`esp_ringbuf` functions also moved from IRAM to flash by default. `CONFIG_RINGBUF_PLACE_FUNCTIONS_INTO_FLASH` removed. Enable `CONFIG_RINGBUF_IN_IRAM` to restore.

### Log

| Removed | Replacement |
|---------|-------------|
| `esp_log_buffer_hex()` | `ESP_LOG_BUFFER_HEX()` (macro) |
| `esp_log_buffer_char()` | `ESP_LOG_BUFFER_CHAR()` (macro) |
| `esp_log_internal.h` | `esp_log_buffer.h` |

### Heap

`MALLOC_CAP_EXEC` is now **conditional**. If `CONFIG_ESP_SYSTEM_MEMPROT` is enabled, `MALLOC_CAP_EXEC` is not defined → compile-time error.

### App Trace

- Config moved: `Component config > Application Level Tracing` → `Component config > ESP Trace Configuration`
- Must explicitly enable: `Component config > ESP Trace Configuration > Trace transport` → `ESP-IDF apptrace`
- Component: `app_trace` → `esp_trace` (update `CMakeLists.txt`)
- `ESP_APPTRACE_DEST_TRAX` → `ESP_APPTRACE_DEST_JTAG`
- UART destination: `CONFIG_APPTRACE_DEST_UART0/1` → `CONFIG_APPTRACE_DEST_UART` + `CONFIG_APPTRACE_DEST_UART_NUM`
- SystemView moved to separate managed component `espressif/esp_sysview`

### OTA

| Removed | Replacement |
|---------|-------------|
| `esp_ota_get_app_description()` | `esp_app_get_description()` |
| `esp_ota_get_app_elf_sha256()` | `esp_app_get_elf_sha256()` |

Functions moved to `esp_app_format` component. Include `esp_app_desc.h` instead of `esp_ota_ops.h`.

Partial download now requires `CONFIG_ESP_HTTPS_OTA_ENABLE_PARTIAL_DOWNLOAD`.

### Core Dump

- Binary data format dropped. **ELF is default.**
- CRC32 checksum dropped. **SHA256 is default.**
- `esp_core_dump_partition_and_size_get()` returns `ESP_ERR_NOT_FOUND` for blank partitions (was `ESP_ERR_INVALID_SIZE`).

### Other System Changes

| Item | Change |
|------|--------|
| Xtensa headers | `specreg.h` → `xt_specreg.h` (use `XT_REG_` prefix) |
| Bootloader | `-O0` removed; use `-Og` (`CONFIG_BOOTLOADER_COMPILER_OPTIMIZATION_DEBUG`) |
| RTC clock | `RTC_CLK_SRC_INT_RC32K` removed (unstable at extreme temps) |
| Headers | `soc_memory_types.h` → `esp_memory_utils.h`; `intr_types.h` → `esp_intr_types.h` |
| `esp_fault.h` | Moved from `esp_hw_support` to `esp_common` (update REQUIRES) |
| ROM headers | `STATUS` → `ETS_STATUS` in `ets_sys.h` |
| `ESP-Event` | `esp_event.h` no longer implicitly includes FreeRTOS headers — add `freertos/queue.h` and `freertos/semphr.h` explicitly |
| `EXT_RAM_ATTR` | Removed (deprecated since v5.0). Use `EXT_RAM_BSS_ATTR` for `.bss` on PSRAM |
| System Console | `esp_vfs_cdcacm.h` moved to `esp_usb_cdc_romconsole` component (add REQUIRES) |
| ULP | LP-Core wakes main CPU on exception during deep sleep by default (`CONFIG_ULP_TRAP_WAKEUP`) |
| Assert | `CONFIG_COMPILER_ASSERT_NDEBUG_EVALUATE` default = n (C standard behavior) |

---

## Wi-Fi

### Removed Functions

| Removed | Replacement |
|---------|-------------|
| `esp_wifi_set_ant_gpio` / `get` | `esp_phy_set_ant_gpio` / `esp_phy_get_ant_gpio` |
| `esp_wifi_set_ant` / `get` | `esp_phy_set_ant` / `esp_phy_get_ant` |
| `esp_wifi_config_espnow_rate` | `esp_now_set_peer_rate_config` |
| `esp_rrm_send_neighbor_rep_request` | `esp_rrm_send_neighbor_report_request` |

### Removed Types / Macros / Enum Values

| Removed | Replacement |
|---------|-------------|
| `WIFI_AUTH_WPA3_EXT_PSK` / `WIFI_AUTH_WPA3_EXT_PSK_MIXED_MODE` | `WIFI_AUTH_WPA3_PSK` |
| `ESP_IF_WIFI_STA` / `ESP_IF_WIFI_AP` | `WIFI_IF_STA` / `WIFI_IF_AP` |
| `WIFI_BW_HT20` / `WIFI_BW_HT40` | `WIFI_BW20` / `WIFI_BW40` |
| `WIFI_REASON_ASSOC_EXPIRE` | `WIFI_REASON_AUTH_EXPIRE` |
| `WIFI_REASON_NOT_AUTHED` | `WIFI_REASON_CLASS2_FRAME_FROM_NONAUTH_STA` |
| `WIFI_REASON_NOT_ASSOCED` | `WIFI_REASON_CLASS3_FRAME_FROM_NONASSOC_STA` |
| `esp_interface.h` header | Enum now in `esp_wifi_types_generic.h` |
| DPP `esp_supp_dpp_event_cb_t` / `esp_supp_dpp_event_t` | Use Wi-Fi events directly (`WIFI_EVENT_DPP_*`) |

### Modified Functions

| Function | Change |
|----------|--------|
| `esp_supp_dpp_init` | No longer accepts callback: `esp_supp_dpp_init(void)` |
| `esp_wifi_wps_start` | No `timeout_ms` arg: `esp_wifi_wps_start(void)` |
| `esp_wifi_init` | Second call returns `ESP_ERR_INVALID_STATE` (was `ESP_OK`) |

### NAN API Overhaul

- `esp_wifi_nan_start/stop` → `esp_wifi_nan_sync_start/stop`
- `WIFI_NAN_CONFIG_DEFAULT()` → `WIFI_NAN_SYNC_CONFIG_DEFAULT()`
- `wifi_nan_config_t` → `wifi_nan_sync_config_t`
- Events: `WIFI_EVENT_NAN_STARTED/STOPPED` → `WIFI_EVENT_NAN_SYNC_STARTED/STOPPED`
- `svc_info` field → `ssi` / `ssi_len` in NAN structures
- `wifi_nan_wfa_ssi_t.proto` now `uint8_t` (was `wifi_nan_svc_proto_t`)

### Off-Channel Operations

- `wifi_action_tx_req_t` now includes `bssid` field (init to zeros for broadcast behavior)
- `wifi_roc_req_t` now includes `allow_broadcast` flag (default `false`)

**Project impact:** Our WiFi code likely uses `ESP_IF_WIFI_STA` — must change to `WIFI_IF_STA`. Check for any deprecated auth mode usage.

---

## Protocols

### JSON — Built-in Component Removed

The built-in `json` component is **removed**. Use `espressif/cjson` from IDF Component Manager.

**Migration:**
1. Remove `json` from `REQUIRES` / `PRIV_REQUIRES` in `CMakeLists.txt`
2. Add to `idf_component.yml`:
   ```yaml
   dependencies:
     espressif/cjson: "^1.7.19"
   ```
3. No code changes — API is identical (`cJSON.h`)

### ESP-TLS — wolfSSL Support Removed

wolfSSL TLS stack support removed. Options:
- **A:** Switch to mbedTLS (default, no action needed)
- **B:** Register custom stack via `esp_tls_stack_ops_t` + `esp_tls_register_stack()`

Removed Kconfig: `CONFIG_ESP_TLS_USING_WOLFSSL`, `CONFIG_ESP_DEBUG_WOLFSSL`, `CONFIG_ESP_TLS_OCSP_CHECKALL`.

**Removed deprecated API:** `esp_tls_conn_http_new()` → use `esp_tls_conn_http_new_sync()` / `esp_tls_conn_http_new_async()`.

### ESP-Modbus — v1 Removed

ESP-Modbus v1 examples removed. Use **v2** from external repo:
- [esp-modbus on GitHub](https://github.com/espressif/esp-modbus)
- Docs: [v2.x.x stable](https://docs.espressif.com/projects/esp-modbus/en/stable)

### ESP-MQTT — Managed Component

ESP-MQTT removed from IDF. Use `espressif/mqtt` managed component:
```
idf.py add-dependency espressif/mqtt
```
Headers (`mqtt_client.h`) and APIs unchanged.

**Project impact:** Our project's MQTT dependency must be updated to use the managed component. Add `espressif/mqtt` to `idf_component.yml`.

---

## Storage

### VFS

| Removed | Replacement |
|---------|-------------|
| `esp_vfs_fat_sdmmc_unmount` | `esp_vfs_fat_sdcard_unmount` |
| `esp_vfs_dev_uart_*` (in VFS) | `uart_vfs_dev_*` (in UART driver) |
| `esp_vfs_dev_usb_serial_jtag_*` (in VFS) | `usb_serial_jtag_vfs_*` (in USB-Serial-JTAG driver) |

**`esp_vfs_fat_register`** prototype changed to match `esp_vfs_fat_register_cfg`. The latter is now deprecated.

**TERMIOS disabled by default.** Enable `CONFIG_VFS_SUPPORT_TERMIOS` if using `tcsetattr`/`tcgetattr`.

**Context-less VFS function pointers deprecated.** Use context-aware `*_p` callbacks with `ESP_VFS_FLAG_CONTEXT_PTR`.

**Legacy VFS APIs** (`esp_vfs_register` with `esp_vfs_t`) deprecated. Use `esp_vfs_fs_ops_t`-based APIs.

### `esp_vfs_console` → `esp_stdio`

The `esp_vfs_console` component renamed to `esp_stdio`. No public API impact — `esp_stdio` is added to all components by default.

### FATFS

- Dynamic buffers (`CONFIG_FATFS_USE_DYN_BUFFERS`) **default enabled** — saves RAM when multiple volumes mounted.
- Long filename heap buffers (`CONFIG_FATFS_LFN_HEAP=y`) **default enabled**.

**Project impact:** If we mount SD cards via FATFS, verify the new defaults are acceptable. If heap is constrained, disable LFN heap.

---

## Toolchain

### GCC 14.2.0 → 15.1.0

Required: port code to GCC 15.1.0. Reference: [Porting to GCC 15](https://gcc.gnu.org/gcc-15/porting_to.html).

### New Warnings (likely to fire)

| Warning | Typical trigger |
|---------|-----------------|
| `-Wunterminated-string-initialization` | `char arr[3] = "foo"` — use `NONSTRING_ATTR` |
| `-Wheader-guard` | `#ifndef` / `#define` mismatch in include guard |
| `-Wself-move` (C++) | `t = std::move(t)` — no-op, remove line |
| `-Wtemplate-body` (C++) | Errors in template body diagnosed only on instantiation |
| `-Wdangling-reference` (C++) | `const int& r = std::max(n-1, n+1)` |
| `-Wdefaulted-function-deleted` (C++) | `C(const C&&) = default` — implicitly deleted |

Suppress all new warnings: `CONFIG_COMPILER_DISABLE_GCC15_WARNINGS`.

### Header Changes

| Issue | Fix |
|-------|-----|
| `<sys/dirent.h>` no longer includes function prototypes | Use `<dirent.h>` instead |
| `<sys/signal.h>` removed in PicolibC | Use `<signal.h>` (standard C) |

**Project impact:** Any Rust `build.rs` or C dependencies using `<sys/dirent.h>` or `<sys/signal.h>` must be updated.

---

## Build System

### Orphan Sections → Linker Error

Orphan sections now cause linker **errors** (was warnings). Resolve by:
1. Remove unused code/data
2. Place section via [linker fragment file](../../api-guides/linker-script-generation.html#ldgen-linker-fragment-files)
3. **Temporary:** set `CONFIG_COMPILER_ORPHAN_SECTIONS` to `warning` or `place` (not recommended)

### Global Constructor Order Changed

Non-priority constructors now process in **ascending** order (was descending). Alignment with standard toolchain behavior.

If your code depends on constructor ordering:
- Insert at **tail** instead of head in linked list registrations
- Use `__attribute__((constructor(PRIO)))` with explicit priority

### Default Compiler Warnings → Errors

`CONFIG_COMPILER_DISABLE_DEFAULT_ERRORS` changed to `n`. All default warnings are errors.

### Kconfig Files

ESP-IDF v6 uses **esp-idf-kconfig v3**. See [migration guide](https://docs.espressif.com/projects/esp-idf-kconfig/en/latest/developer-guide/migration-guide.html).

---

## Networking

### Ethernet

**RMII Clock Configuration:** Removed from Kconfig. Set in code via EMAC config:

```c
eth_esp32_emac_config_t emac_config = ETH_ESP32_EMAC_DEFAULT_CONFIG();
emac_config.clock_config.rmii.clock_mode = EMAC_CLK_OUT;      // or EMAC_CLK_EXT_IN
emac_config.clock_config.rmii.clock_gpio = 0;                  // GPIO0 for ESP32
```

**PHY / SPI drivers moved to external repo:** `esp-eth-drivers` on GitHub. All `esp_eth_phy_new_*()` and `esp_eth_mac_new_*()` for IP101, LAN87xx, RTL8201, DP83848, KSZ80xx, DM9051, KSZ8851SNL, W5500 are **removed from IDF**. Use `idf.py add-dependency` to pull from [ESP Component Registry](https://components.espressif.com/).

**PTP ioctl commands removed.** Use new PTP API instead. Removed:
- `ETH_MAC_ESP_CMD_PTP_ENABLE`, `ETH_MAC_ESP_CMD_S_PTP_TIME`, `ETH_MAC_ESP_CMD_G_PTP_TIME`, etc.

**`esp_eth_phy_802_3_reset_hw()`** now accepts 1 parameter (no `reset_assert_us`).

### ESP-NETIF

**`esp_netif_next()` — REMOVED.** Use one of:

```c
// Unsafe iteration (controlled context only):
esp_netif_t *it = NULL;
while ((it = esp_netif_next_unsafe(it)) != NULL) { ... }

// Safe iteration inside TCP/IP context:
esp_err_t iterate_netifs(void *ctx) {
    esp_netif_t *it = NULL;
    while ((it = esp_netif_next_unsafe(it)) != NULL) { ... }
    return ESP_OK;
}
ESP_ERROR_CHECK(esp_netif_tcpip_exec(iterate_netifs, NULL));

// Find with predicate:
esp_netif_t *target = esp_netif_find_if(match_by_key, (void *)"WIFI_STA_DEF");
```

**Project impact:** Our `net_owner` thread or any code iterating netifs must switch to `esp_netif_next_unsafe()` or `esp_netif_find_if()`.

### DHCP Server DNS Option

`LWIP_DHCPS_ADD_DNS` macro removed. DNS advertisement now requires explicit configuration:

```c
// Enable DNS option in DHCP offers
esp_netif_dhcps_option(ap_netif, ESP_NETIF_OP_SET, ESP_NETIF_DOMAIN_NAME_SERVER, ...);

// Set custom DNS
esp_netif_set_dns_info(ap_netif, ESP_NETIF_DNS_MAIN, &dns_info);

// To suppress DNS entirely, set DNS to 0.0.0.0
```

**Project impact:** Our AP-mode DNS responder must explicitly configure DHCP DNS option.

### LWIP

- Thread name: `"tiT"` → `"tcpip"`
- SNTP: `<sntp.h>` → `<esp_sntp.h>`
- **Ping API removed:** `esp_ping.h`, `ping.h`, `ping_init()`, `ping_deinit()`, `esp_ping_set_target()`, `esp_ping_get_target()`, `esp_ping_result()` — all removed. Use `ping/ping_sock.h`:

```c
#include "ping/ping_sock.h"
esp_ping_config_t config = ESP_PING_DEFAULT_CONFIG();
esp_ping_callbacks_t cbs = {
    .on_ping_success = on_ping_success,
    .on_ping_timeout = on_ping_timeout,
    .on_ping_end = on_ping_end,
};
esp_ping_handle_t ping;
esp_ping_new_session(&config, &cbs, &ping);
esp_ping_start(ping);
```

**Project impact:** If we have any ping-based connectivity checks, rewrite using `ping/ping_sock.h`.

---

## Forbidden Patterns (v4/v5 API that no longer compiles in v6)

| ❌ v4/v5 API | ✅ v6 API | Section |
|---|---|---|
| `esp_log_buffer_hex()` | `ESP_LOG_BUFFER_HEX()` | System/Log |
| `xTaskGetAffinity()` | `xTaskGetCoreID()` | System/FreeRTOS |
| `vTaskDelayUntil()` | `xTaskDelayUntil()` | System/FreeRTOS |
| `esp_deep_sleep_enable_gpio_wakeup()` | `esp_sleep_enable_gpio_wakeup_on_hp_periph_powerdown()` | System/Power |
| `ESP_IF_WIFI_STA` | `WIFI_IF_STA` | Wi-Fi |
| `WIFI_BW_HT20` | `WIFI_BW20` | Wi-Fi |
| `WIFI_AUTH_WPA3_EXT_PSK` | `WIFI_AUTH_WPA3_PSK` | Wi-Fi |
| `esp_netif_next()` | `esp_netif_next_unsafe()` / `esp_netif_find_if()` | Networking |
| `<sntp.h>` | `<esp_sntp.h>` | Networking |
| `esp_ping.h` / `ping_init()` | `ping/ping_sock.h` / `esp_ping_new_session()` | Networking |
| `<sys/dirent.h>` | `<dirent.h>` | Toolchain |
| `<sys/signal.h>` | `<signal.h>` | Toolchain |
| `json` in REQUIRES | `espressif/cjson` in `idf_component.yml` | Protocols |
| `app_trace` in REQUIRES | `esp_trace` in REQUIRES | System/App Trace |

---

## Citations

[1] [ESP-IDF v6.0 Migration — System](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/migration-guides/release-6.x/6.0/system.html)

[2] [ESP-IDF v6.0 Migration — Wi-Fi](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/migration-guides/release-6.x/6.0/wifi.html)

[3] [ESP-IDF v6.0 Migration — Protocols](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/migration-guides/release-6.x/6.0/protocols.html)

[4] [ESP-IDF v6.0 Migration — Storage](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/migration-guides/release-6.x/6.0/storage.html)

[5] [ESP-IDF v6.0 Migration — Toolchain](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/migration-guides/release-6.x/6.0/toolchain.html)

[6] [ESP-IDF v6.0 Migration — Build System](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/migration-guides/release-6.x/6.0/build-system.html)

[7] [ESP-IDF v6.0 Migration — Networking](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/migration-guides/release-6.x/6.0/networking.html)

[8] [ESP-IDF v6.0 Migration Index](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/migration-guides/release-6.x/6.0/index.html)
