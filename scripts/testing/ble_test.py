#!/usr/bin/env python3
"""
BLE API Format Validation Test.

Validates JSON command/response format over BLE NUS (Nordic UART Service),
collects broadcast notifications, and diagnoses timing.

Usage:
    python3 scripts/testing/ble_test.py                          # auto-detect serial + BLE
    python3 scripts/testing/ble_test.py --no-serial              # BLE only, no serial log
    python3 scripts/testing/ble_test.py -p /dev/ttyUSB0          # specify serial port

Exit code: 0 = PASS, 1 = FAIL.
"""

import argparse
import asyncio
import datetime
import json
import math
import sys
import time
import threading
from collections import deque
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
PROJECT_DIR = SCRIPT_DIR.parent
sys.path.insert(0, str(SCRIPT_DIR))
sys.path.insert(0, str(PROJECT_DIR))
sys.path.insert(0, str(PROJECT_DIR / "utils"))

from boot_detect import BootDetector, BOOT_OK_MARKER, wait_for_boot
from utils.log_utils import sanitize_line
from broadcast_validator import validate_broadcast_format, diagnose_broadcast_intervals

try:
    import serial
except ImportError:
    serial = None

try:
    from bleak import BleakBackend, BleakScanner, BleakClient
except ImportError:
    print("ERROR: bleak not installed. Run: pip install bleak")
    sys.exit(1)

from find_port import find_esp32_port

# BLE UUIDs
SERVICE_UUID = "6e400001-b5a3-f393-e0a9-e50e24dc0000"
CHAR_CMD_UUID = "6e400002-b5a3-f393-e0a9-e50e24dc0000"   # RX (write)
CHAR_RESP_UUID = "6e400003-b5a3-f393-e0a9-e50e24dc0000"  # TX (notify)

# Timing
SCAN_TIMEOUT = 15.0
SCAN_RETRIES = 3
CONNECT_TIMEOUT = 60.0
CMD_TIMEOUT_S = 5
BROADCAST_COLLECT_S = 30
BOOT_TIMEOUT_S = 15
SERIAL_BAUD = 115200

PASS = 0
FAIL = 0
log_file = None
serial_log_file = None
boot_detector = BootDetector()


def status(msg: str) -> None:
    global log_file
    print(msg, flush=True)
    if log_file:
        ts = datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3]
        log_file.write(f"[{ts}] {sanitize_line(msg)}\n")
        log_file.flush()


def log(msg: str) -> None:
    global log_file
    if log_file:
        ts = datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3]
        log_file.write(f"[{ts}] {sanitize_line(msg)}\n")
        log_file.flush()


def pass_msg(msg: str) -> None:
    global PASS
    PASS += 1
    status(f"  ==> PASS: {msg}")


def fail_msg(msg: str) -> None:
    global FAIL
    FAIL += 1
    status(f"  ==> FAIL: {msg}")


def serial_reader_thread(ser, buf: deque, stop_event: threading.Event,
                         slog_file=None) -> None:
    """Background serial reader — feeds raw lines into buf for optional boot detection."""
    partial = b""
    while not stop_event.is_set():
        try:
            if ser.in_waiting:
                data = ser.read(ser.in_waiting)
                partial += data
                while b"\n" in partial:
                    line, partial = partial.split(b"\n", 1)
                    line_str = line.decode("utf-8", errors="replace").strip("\r")
                    if line_str:
                        buf.append(line_str)
                        if slog_file:
                            ts = datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3]
                            slog_file.write(f"[{ts}] {sanitize_line(line_str)}\n")
                            slog_file.flush()
            else:
                time.sleep(0.005)
        except serial.SerialException:
            break
        except Exception:
            break


# ── BLE Test Class ──────────────────────────────────────────────────────

class BleApiTest:
    def __init__(self):
        self.client: BleakClient | None = None
        self.target_device = None
        self.resp_queue: asyncio.Queue = asyncio.Queue()
        self.broadcasts: list[tuple[dict, float]] = []
        self._collecting = False

    def on_notification(self, sender, data):
        raw = data.decode(errors="replace").strip()
        if self._collecting:
            # During collection phase, everything is a broadcast
            try:
                obj = json.loads(raw)
                if "id" not in obj and "cmd" not in obj:
                    self.broadcasts.append((obj, time.monotonic()))
            except json.JSONDecodeError:
                pass
        else:
            # During command phase, put in response queue
            self.resp_queue.put_nowait(raw)

    async def scan(self) -> bool:
        for attempt in range(1, SCAN_RETRIES + 1):
            status(f"BLE scan attempt {attempt}/{SCAN_RETRIES}...")
            devices = await BleakScanner.discover(
                timeout=SCAN_TIMEOUT, return_adv=True, use_cached=False
            )
            if not devices:
                continue
            for addr, (device, adv) in devices.items():
                name = device.name or ""
                svc_uuids = [str(u) for u in (adv.service_uuids or [])]
                if "EcoTiter" in name or SERVICE_UUID in svc_uuids:
                    self.target_device = device
                    status(f"Found EcoTiter: {name} ({addr})")
                    return True
        return False

    async def connect(self) -> bool:
        if not self.target_device:
            return False
        for attempt in range(1, 4):
            status(f"BLE connect attempt {attempt}/3...")
            try:
                self.client = BleakClient(
                    self.target_device.address, timeout=CONNECT_TIMEOUT,
                    disconnected_callback=lambda c: status("BLE disconnected")
                )
                await self.client.connect()
                # BlueZ doesn't expose MTU via standard API; workaround from
                # https://github.com/hbldh/bleak/blob/develop/examples/mtu_size.py
                if self.client.backend_id == BleakBackend.BLUEZ_DBUS:
                    await self.client._backend._acquire_mtu()
                status(f"BLE connected (MTU: {self.client.mtu_size})")
                # Subscribe to TX notifications
                resp_char = self.client.services.get_characteristic(CHAR_RESP_UUID)
                if resp_char:
                    await self.client.start_notify(resp_char, self.on_notification)
                return True
            except Exception as e:
                status(f"  Connection error: {e}")
                if self.client and self.client.is_connected:
                    try:
                        await self.client.disconnect()
                    except Exception:
                        pass
                self.client = None
                await asyncio.sleep(3)
        return False

    async def send_and_expect(
        self, cmd: str, cmd_id: int,
        expect_keys: list | None = None,
        expect_error: bool = False,
        timeout_s: float = CMD_TIMEOUT_S,
    ) -> dict | None:
        payload = json.dumps({"id": cmd_id, "cmd": cmd}) + "\n"
        log(f"  BLE >>> {payload.strip()}")
        try:
            await self.client.write_gatt_char(CHAR_CMD_UUID, payload.encode(), response=False)
        except Exception as e:
            fail_msg(f"{cmd}: write failed: {e}")
            return None

        deadline = time.time() + timeout_s
        while time.time() < deadline:
            try:
                raw = await asyncio.wait_for(self.resp_queue.get(), timeout=0.5)
            except asyncio.TimeoutError:
                continue

            log(f"  BLE <<< {raw.strip()}")
            if not raw.startswith("{"):
                continue
            try:
                resp = json.loads(raw)
            except json.JSONDecodeError:
                continue

            matched = resp.get("id") == cmd_id or resp.get("cmd") == cmd
            if not matched:
                continue

            st = resp.get("status", "")
            if expect_error:
                if st == "error" or "error" in resp.get("result", ""):
                    pass_msg(f"{cmd}: got expected error")
                    return resp
                fail_msg(f"{cmd}: expected error, got status='{st}'")
                return None
            elif st == "ok":
                data = resp.get("data", {})
                if expect_keys:
                    missing = [k for k in expect_keys if k not in data]
                    if missing:
                        fail_msg(f"{cmd}: missing keys {missing} in data")
                        return None
                pass_msg(f"{cmd}: status=ok")
                return resp
            elif resp.get("result") == "pong":
                pass_msg(f"{cmd}: result=pong")
                return resp
            else:
                fail_msg(f"{cmd}: unexpected status='{st}', result='{resp.get('result', '')}'")
                return None

        fail_msg(f"{cmd}: no response within {timeout_s}s")
        return None

    async def collect_broadcasts(self, duration_s: float) -> list[tuple[dict, float]]:
        self._collecting = True
        self.broadcasts.clear()
        status(f"Collecting BLE broadcasts for {duration_s}s...")
        await asyncio.sleep(duration_s)
        self._collecting = False
        status(f"Collected {len(self.broadcasts)} BLE broadcast messages")
        return self.broadcasts

    async def disconnect(self):
        if self.client and self.client.is_connected:
            try:
                await self.client.disconnect()
            except Exception:
                pass


# ── Main ────────────────────────────────────────────────────────────────

def make_serial_log_path(log_dir: str) -> Path:
    ts = datetime.datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
    return Path(log_dir) / f"ble_serial_{ts}.log"


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description="BLE API format validation test")
    p.add_argument("--no-serial", action="store_true", help="Skip serial boot detection")
    p.add_argument("-p", "--port", default=None, help="Serial port for boot detection")
    p.add_argument("--serial-log-dir", default=None,
                   help="Directory for serial log file (default: scripts/testing/logs/)")
    return p.parse_args()


def main() -> int:
    global PASS, FAIL, log_file, serial_log_file, boot_detector

    args = parse_args()

    log_dir = SCRIPT_DIR / "logs"
    log_dir.mkdir(parents=True, exist_ok=True)
    ts = datetime.datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
    log_path = log_dir / f"ble_api_test_{ts}.log"
    log_file = open(log_path, "w", encoding="utf-8")
    log_file.write(f"BLE API test log — {ts}\n")
    log_file.write("=" * 60 + "\n")
    log_file.flush()
    status(f"Log: {log_path}")

    serial_log_dir = args.serial_log_dir or str(log_dir)
    serial_log_path = make_serial_log_path(serial_log_dir)

    # ── Optional serial boot detection ──
    serial_buf: deque = deque()
    serial_stop = threading.Event()
    serial_thread = None

    if not args.no_serial:
        port = args.port or find_esp32_port()
        if port and serial:
            status(f"Serial boot detection on {port} @ {SERIAL_BAUD}")
            try:
                ser = serial.Serial(port=port, baudrate=SERIAL_BAUD, timeout=0.1)
                # DTR reset
                ser.dtr = False; ser.rts = False
                time.sleep(0.1)
                ser.dtr = True; ser.rts = True
                time.sleep(0.1)
                ser.dtr = False; ser.rts = False
                time.sleep(0.3)
                ser.reset_input_buffer()

                Path(serial_log_path).parent.mkdir(parents=True, exist_ok=True)
                serial_log_file = open(serial_log_path, "w", encoding="utf-8")
                serial_log_file.write(f"BLE serial log — {ts}\n")
                serial_log_file.write("=" * 60 + "\n")
                serial_log_file.flush()
                status(f"Serial log: {serial_log_path}")

                serial_thread = threading.Thread(
                    target=serial_reader_thread,
                    args=(ser, serial_buf, serial_stop, serial_log_file), daemon=True
                )
                serial_thread.start()

                boot_found = wait_for_boot(
                    serial_buf, boot_detector, timeout_s=BOOT_TIMEOUT_S,
                    log_fn=lambda line: log(f"  boot line: {line}")
                )
                if boot_found:
                    status(f"Boot detected (marker #{boot_detector.count})")
                else:
                    status("Boot marker not seen on serial (continuing with BLE)")
            except Exception as e:
                status(f"Serial init failed: {e}")
        else:
            status("No serial port found — BLE only mode")

    # ── BLE scan + connect ──
    async def run_ble():
        ble = BleApiTest()
        
        found = await ble.scan()
        if not found:
            fail_msg("EcoTiter not found via BLE scan")
            return
        
        connected = await ble.connect()
        if not connected:
            fail_msg("BLE connection failed")
            return
        
        # ── Command sequence ──
        status("\n=== BLE: serial.ping ===")
        await ble.send_and_expect("serial.ping", cmd_id=1, expect_keys=["status"])
        
        status("\n=== BLE: burette.getStatus ===")
        await ble.send_and_expect("burette.getStatus", cmd_id=2,
                                  expect_keys=["status", "volume_ml", "speed_ml_min"])
        
        status("\n=== BLE: burette.cal.get ===")
        await ble.send_and_expect("burette.cal.get", cmd_id=3,
                                  expect_keys=["steps_per_ml", "nominal_vol", "speed_coeff",
                                               "min_freq", "max_freq", "is_default"])
        
        status("\n=== BLE: valve.getState ===")
        await ble.send_and_expect("valve.getState", cmd_id=4, expect_keys=["position"])
        
        status("\n=== BLE: nonexistent command (expect error) ===")
        await ble.send_and_expect("nonexistent", cmd_id=5, expect_error=True)
        
        # ── Broadcast collection ──
        status(f"\n=== BLE broadcast collection ({BROADCAST_COLLECT_S}s) ===")
        broadcasts = await ble.collect_broadcasts(BROADCAST_COLLECT_S)
        
        status("\n=== Broadcast format validation ===")
        passed, total = validate_broadcast_format(broadcasts, log_fn=log)
        if total == 0:
            fail_msg("No broadcast messages received over BLE")
        elif passed == total:
            pass_msg(f"All {total} broadcasts conform to spec format")
        else:
            fail_msg(f"{passed}/{total} broadcasts conform to spec format")
        
        status("\n=== Broadcast interval diagnostics ===")
        diagnose_broadcast_intervals(broadcasts, log_fn=status)
        
        await ble.disconnect()

    asyncio.run(run_ble())

    # ── Reboot check ──
    if boot_detector.reboot_detected:
        fail_msg(f"ESP32-S3 reboot detected (BOOT OK: seen {boot_detector.count} times)")

    # ── Cleanup ──
    serial_stop.set()
    if serial_thread:
        serial_thread.join(timeout=3)
    if serial_log_file:
        serial_log_file.close()

    # ── Summary ──
    total = PASS + FAIL
    status("\n" + "=" * 50)
    status(f"RESULTS: {PASS}/{total} passed, {FAIL}/{total} failed")

    if FAIL == 0:
        status("ALL CHECKS PASSED")
    else:
        status("SOME CHECKS FAILED")

    if log_file:
        log_file.write("=" * 60 + "\n")
        log_file.write(f"Results: {PASS}/{total} passed, {FAIL}/{total} failed\n")
        log_file.close()

    return 0 if FAIL == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
