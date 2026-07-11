---
type: CrashReport
title: Boot loop — invalid segment length 0xFFFFFFFF
description: Bootloader reads erased flash (0xFF) at 0x10000; app not bootable. Likely stale sdkconfig or flash write verification issue.
tags: [boot, crash, flash, sdkconfig]
version: "1.0"
task_id: "manual"
timestamp: 2026-07-11
crash_signature: "BOOT_CRASH — esp_image: invalid segment length 0xffffffff at 262ms"
---

# Crash Report: Boot Loop — `invalid segment length 0xffffffff`

## Verdict

- **Status:** root_cause_not_found (requires hardware verification)
- **Root Cause:** Likely flash write verification issue or sdkconfig staleness — the bootloader at 0x0 finds the partition table at 0x8000 and factory partition at 0x10000, but reads 0xFF bytes (unwritten/erased flash) from the app image at 0x10000.
- **Confidence:** medium (offline analysis only — needs flash read-back for confirmation)
- **Crash Type:** Bootloader-level app validation failure (before `app_main()`)

## Evidence Chain

### Step 1: Triage

**Crash log from device:**
```
E (262) esp_image: invalid segment length 0xffffffff
E (262) boot: Factory app partition is not bootable
E (262) boot: No bootable app partitions in the partition table
```

**Timing:** 262ms after reset — bootloader has already:
1. Loaded from flash at 0x0 (ROM → 2nd-stage bootloader) ✓
2. Read partition table at 0x8000 ✓
3. Found factory partition at 0x10000 ✓
4. FAILED to parse app image at 0x10000 ✗

**Build artifacts (all verified):**

| Artifact | Status |
|----------|--------|
| `build/ecotiter.bin` | ✅ Valid ESP32-S3 image (magic 0xE9, 6 seg, DIO/80MHz/16MB) |
| `build/bootloader/bootloader.bin` | ✅ Valid ESP32-S3 image (magic 0xE9, 3 seg, DIO/80MHz/16MB) |
| `build/partition_table/partition-table.bin` | ✅ 3 partitions (NVS @ 0x9000, PHY @ 0xF000, Factory @ 0x10000) |
| `build/flasher_args.json` | ✅ Flash: DIO, 80MHz, 16MB; Files at 0x0, 0x8000, 0x10000 |

**Partition layout verification:**
```
Name            Offset      Size         End
nvs             0x009000    0x6000       0x00F000
phy_init        0x00F000    0x1000       0x010000
factory (app)   0x010000    0x177000     0x187000
```
- App (1,315,664 bytes) fits in factory partition (1,536,000 bytes) ✅
- Bootloader (18,464 bytes) fits before partition table at 0x8000 ✅
- No overlapping regions ✅
- All fits within 16MB flash ✅ (even 4MB flash would work)

**Key observation:** Error `0xFFFFFFFF` means the bootloader reads `0xFF` bytes (erased/unwritten flash) from address 0x10000 where the app should be.

### Step 2: S1–S5 Protocol (Adapted for Bootloader Crash)

| Step | Result | Action |
|------|--------|--------|
| S1 (stack watermark) | N/A — app never starts, bootloader-level crash | Not applicable |
| S2 (heap integrity) | N/A — app never starts | Not applicable |
| S3 (smoke test) | Not attempted — bootloader can't find app image | Try after S4/S5 fixes |
| S4 (delta analysis) | sdkconfig.defaults last changed in `03d388f` (mDNS only). No flash config changes. Bootloader and app both have same flash config (DIO/80MHz/16MB). | See below |
| S5 (red flags) | `CONFIG_ESPTOOLPY_FLASH_MODE_AUTO_DETECT=y` AND `CONFIG_ESPTOOLPY_FLASHMODE_DIO=y` both set. Possible sdkconfig staleness. | See below |

**S4 Delta Analysis:**
```
$ git log --oneline -3 -- sdkconfig.defaults
03d388f fix(network,docs,testing): enable mDNS hostname resolution
fc7f2b8 Attempted to fix captive portal not showing up
015e951 fix(network,drivers,docs): add DHCP DNS options and relocate PSRAM-bus GPIOs
```

`03d388f` only uncommented `CONFIG_MDNS_MAX_SERVICES=1` — no flash config changes.

**S5 Red Flags:**
- 🔴 Both `CONFIG_ESPTOOLPY_FLASH_MODE_AUTO_DETECT=y` AND `CONFIG_ESPTOOLPY_FLASHMODE_DIO=y` are active in `sdkconfig`. This suggests a stale/corrupted sdkconfig where a Kconfig `choice` has two options selected simultaneously.
- 🔴 `scripts/idf.sh build` (`do_build()`) removes `build/` but does NOT remove `sdkconfig`. If sdkconfig.defaults changed without a corresponding sdkconfig regeneration, stale config could be used.
- ✅ No recent flash config changes in git history.
- ✅ Bootloader sdkconfig matches app sdkconfig on flash settings.

### Step 3: Systematic Elimination

**Technique applied: Build artifact verification**

All build artifacts were verified as valid and consistent:
- Both binary headers show DIO/80MHz/16MB ✅
- Partition table correct for `partitions_singleapp_large.csv` ✅
- `flasher_args.json` specifies `--flash-mode dio --flash-freq 80m --flash-size 16MB` ✅

**Technique applied: sdkconfig cross-check**

The `sdkconfig` file (not `.defaults`) contains the resolved config used by the build:
- `CONFIG_ESPTOOLPY_FLASHMODE_DIO=y` ✅
- `CONFIG_ESPTOOLPY_FLASHFREQ_80M=y` ✅
- `CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y` ✅
- `CONFIG_PARTITION_TABLE_SINGLE_APP_LARGE=y` ✅
- `CONFIG_PARTITION_TABLE_OFFSET=0x8000` ✅

**Technique not applied: Flash read-back verification**
Cannot verify what was actually written to the flash without connecting to the device.

### Step 4: Root Cause Hypotheses

#### Hypothesis A (Most Likely): Flash write not persisted despite verification

**Evidence:**
- The bootloader reads `0xFF` (erased flash) at 0x10000
- `esptool.py verify_flash` confirmed the data — but this might verify the wrong region or the chip might lose data after power cycle

**To verify:**
```bash
# Read back flash at 0x10000 and compare with source binary
python3 /tmp/opencode/verify_flash_write.py [port]
```

**Probability:** Medium

#### Hypothesis B: Stale sdkconfig with conflicting AUTO_DETECT + DIO mode

**Evidence:**
- Both `CONFIG_ESPTOOLPY_FLASH_MODE_AUTO_DETECT=y` and `CONFIG_ESPTOOLPY_FLASHMODE_DIO=y` are set
- `do_build()` never removes `sdkconfig`, so it persists across builds
- This could cause `idf.py flash` to use auto-detected flash mode that doesn't match what the bootloader expects

**To verify:**
```bash
scripts/idf.sh reconfigure   # removes sdkconfig + idf.py reconfigure
scripts/idf.sh build          # fresh build from clean sdkconfig
scripts/idf.sh flash [port]   # reflash
```

**Probability:** Medium

#### Hypothesis C: Incompatible flash chip or timing

**Evidence:**
- ESP32-S3 rev v0.2 with Octal PSRAM at 80MHz
- Some 16MB flash chips need specific dummy cycle configuration
- The CONFIG_SPI_FLASH_HPM_AUTO setting might not work with all flash chips

**To verify:**
- Read flash ID: `python3 -m esptool --port <PORT> flash_id`
- Check if flash chip supports 80MHz DIO mode confirmed by datasheet

**Probability:** Low-Medium

#### Hypothesis D: Bootloader incorrectly configured for this hardware revision

**Evidence:**
- Bootloader was compiled with `CONFIG_BOOTLOADER_FLASH_XMC_SUPPORT=y` (XMC flash support)
- But the actual flash might be from a different vendor (MXIC, Winbond, etc.)
- All vendor drivers are enabled (no issue here)

**To verify:**
- Check actual flash vendor via `flash_id`
- Compare bootloader config options against vendor support

**Probability:** Low

## Fix

### Immediate Steps (User to Execute)

**Step 1 — Force-regenerate sdkconfig:**
```bash
# This removes stale sdkconfig and regenerates from defaults
scripts/idf.sh reconfigure
scripts/idf.sh build
```

**Step 2 — Verify flash write with read-back:**
```bash
# Run the verification script to read back flash contents
python3 /tmp/opencode/verify_flash_write.py /dev/ttyUSB0
```
If the read-back shows the app header (0xE9) at 0x10000 → the original flash worked but something corrupted it between verify and boot. Try replacing the flash cable or using a different USB port.

**Step 3 — If read-back shows 0xFF:**
Try flashing with explicit esptool commands:
```bash
python3 -m esptool --chip esp32s3 --port /dev/ttyUSB0 \
  --before default_reset --after hard_reset \
  write_flash --flash_mode dio --flash_freq 80m --flash_size 16MB \
  0x0 build/bootloader/bootloader.bin \
  0x8000 build/partition_table/partition-table.bin \
  0x10000 build/ecotiter.bin
```

**Step 4 — If still failing, try with flash size detection:**
```bash
# Let esptool auto-detect flash size
python3 -m esptool --chip esp32s3 --port /dev/ttyUSB0 \
  write_flash --flash_mode dio --flash_freq 80m \
  0x0 build/bootloader/bootloader.bin \
  0x8000 build/partition_table/partition-table.bin \
  0x10000 build/ecotiter.bin
```

### Long-term Fix Recommendations

1. **Fix `do_build()` to also remove `sdkconfig`:** Modify `scripts/idf.sh` to remove `sdkconfig` alongside `build/` in the `do_build()` function, ensuring truly clean builds.

2. **Add ESPTOOLPY_FLASHMODE_DIO to `sdkconfig.defaults`:** Explicitly set `CONFIG_ESPTOOLPY_FLASHMODE_DIO=y` instead of relying on auto-detect, preventing ambiguity.

3. **Add flash write verification to build pipeline:** After `idf.py flash`, run `esptool.py verify_flash` to confirm the data was written correctly.

## Investigation Artifacts

| Item | Status |
|------|--------|
| `main/main_smoke.cpp` | Not created (bootloader crash — smoke test not applicable) |
| `[INVESTIGATION]` markers | None added (no code changes made) |
| Flash verification script | `/tmp/opencode/verify_flash_write.py` |
| Header analysis script | `/tmp/opencode/check_esp_header.py` |
| Partition layout script | `/tmp/opencode/check_partition_layout.py` |
| Lessons learned | Not applicable (root cause unconfirmed) |

## Remaining Issues

- Cannot verify actual flash contents without hardware access
- Root cause may be hardware-specific (flash chip timing, power supply, signal integrity)
- If the `reconfigure` + rebuild + reflash cycle fixes it, the issue is stale sdkconfig
- If not, flash read-back is mandatory to determine next steps
