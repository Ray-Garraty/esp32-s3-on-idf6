#!/usr/bin/env python3
"""
BLE + USB Serial dual monitor for Autosampler firmware.

Connects to ESP32 over USB serial (to see firmware logs in real time)
AND over BLE simultaneously. Useful for debugging BLE connection issues
by correlating ESP32-side logs with BLE-side events.

Usage:
    python scripts/ble_serial_test.py                         # auto-detect COM + full test
    python scripts/ble_serial_test.py --port COM5             # specify COM port
    python scripts/ble_serial_test.py --scan-only             # scan BLE only, no connect
    python scripts/ble_serial_test.py --interactive           # type BLE commands manually
    python scripts/ble_serial_test.py --cmd "CMD:SYS:VERSION\n"  # single command, then exit
    python scripts/ble_serial_test.py --no-reset              # skip DTR reset on serial
"""

import asyncio
import re
import sys
import os
import time
import threading
import argparse
from datetime import datetime, timezone, timedelta
from pathlib import Path
from bleak import BleakScanner, BleakClient

sys.path.insert(0, str(Path(__file__).parent))
from find_port import find_esp32_port

try:
    import serial
except ImportError:
    print("ERROR: pyserial not installed. Run: pip install pyserial", flush=True)
    sys.exit(1)

# EcoTiter NUS (Nordic UART Service) UUIDs
SERVICE_UUID = "6e400001-b5a3-f393-e0a9-e50e24dc0000"
CHAR_CMD_UUID = "6e400002-b5a3-f393-e0a9-e50e24dc0000"   # RX (write)
CHAR_RESP_UUID = "6e400003-b5a3-f393-e0a9-e50e24dc0000"  # TX (notify)
CHAR_STATUS_UUID = "6e400003-b5a3-f393-e0a9-e50e24dc0000" # same as TX for EcoTiter

SCAN_TIMEOUT = 15.0
CONNECT_TIMEOUT = 60.0
SCAN_RETRIES = 3
RETRY_DELAY_S = 3
RESPONSE_TIMEOUT_S = 15
DONE_TIMEOUT_S = 15
TOTAL_TIMEOUT = 240
SERIAL_BAUD = 115200

RESP_RE = re.compile(r'^(ACK|ERR|DONE):(\w+)(?::(.*))?')
MSK = timezone(timedelta(hours=3))

# ── coloured prefixes ──────────────────────────────────────────────
SERIAL_TAG = "\033[36m[SERIAL]\033[0m"   # cyan
BLE_TAG    = "\033[33m[BLE]   \033[0m"   # yellow
BLE_SEND   = "\033[33m[BLE ->]\033[0m"   # yellow send
BLE_RECV   = "\033[33m[BLE <-]\033[0m"   # yellow recv
ERR_TAG    = "\033[31m[ERROR] \033[0m"   # red
OK_TAG     = "\033[32m[OK]    \033[0m"   # green
SCAN_TAG   = "\033[35m[SCAN]  \033[0m"   # magenta


def timestamp():
    return datetime.now(MSK).strftime("[%H:%M:%S]")


def log(tag, msg):
    print(f"{timestamp()} {tag} {msg}", flush=True)


# ── serial reader thread ────────────────────────────────────────────
class SerialReader(threading.Thread):
    """Reads serial output from ESP32 in a background thread."""

    def __init__(self, port: str, baud: int = SERIAL_BAUD, do_reset: bool = True):
        super().__init__(daemon=True)
        self.port = port
        self.baud = baud
        self.do_reset = do_reset
        self.ser = None
        self._done = threading.Event()

    def stop(self):
        self._done.set()

    def run(self):
        try:
            self.ser = serial.Serial(
                port=self.port,
                baudrate=self.baud,
                bytesize=serial.EIGHTBITS,
                parity=serial.PARITY_NONE,
                stopbits=serial.STOPBITS_ONE,
                timeout=0.5,
            )
        except serial.SerialException as e:
            log(ERR_TAG, f"Cannot open {self.port}: {e}")
            return

        log(SERIAL_TAG, f"Connected to {self.port} @ {self.baud} baud")

        if self.do_reset:
            log(SERIAL_TAG, "Resetting ESP32 (DTR pulse)...")
            self.ser.dtr = False
            self.ser.rts = False
            time.sleep(0.1)
            self.ser.dtr = True
            self.ser.rts = True
            time.sleep(0.1)
            self.ser.dtr = False
            self.ser.rts = False
            time.sleep(0.5)
            self.ser.reset_input_buffer()
            log(SERIAL_TAG, "ESP32 reset complete — awaiting boot logs")

        buf = ""
        while not self._done.is_set():
            try:
                if self.ser.in_waiting:
                    data = self.ser.read(self.ser.in_waiting).decode("utf-8", errors="replace")
                    buf += data
                    while "\n" in buf:
                        line, buf = buf.split("\n", 1)
                        line = line.strip("\r")
                        if line:
                            log(SERIAL_TAG, line)
                else:
                    time.sleep(0.01)
            except serial.SerialException:
                log(SERIAL_TAG, "Connection lost")
                break
            except Exception as e:
                log(ERR_TAG, f"Serial read error: {e}")
                break

        if self.ser and self.ser.is_open:
            self.ser.close()


# ── BLE test logic ──────────────────────────────────────────────────
class BleSerialTest:
    def __init__(self, serial_reader: SerialReader, interactive: bool = False):
        self.serial_reader = serial_reader
        self.interactive = interactive
        self.target_device = None
        self.target_info = None
        self.client = None
        self.resp_queue = asyncio.Queue()
        self.status_queue = asyncio.Queue()
        self.failed_steps = []

    def _parse_response(self, raw):
        m = RESP_RE.match(raw.strip())
        if not m:
            return None
        return {
            'type': m.group(1),
            'command': m.group(2),
            'detail': m.group(3) or ''
        }

    def on_notification(self, sender, data):
        raw = data.decode(errors="replace").strip()
        self.resp_queue.put_nowait(raw)

    def on_status_notification(self, sender, data):
        raw = data.decode(errors="replace").strip()
        self.status_queue.put_nowait(raw)

    async def _wait_for(self, expected_type, expected_cmd, timeout):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if self.client and not self.client.is_connected:
                return False, "Connection lost"
            try:
                raw = await asyncio.wait_for(self.resp_queue.get(), timeout=0.5)
            except asyncio.TimeoutError:
                continue

            parsed = self._parse_response(raw)
            if parsed is None:
                log(BLE_RECV, raw)
                continue

            log(BLE_RECV, f"{parsed['type']}:{parsed['command']}"
                f"{' (' + parsed['detail'] + ')' if parsed['detail'] else ''}")

            if parsed['type'] == expected_type and parsed['command'] == expected_cmd:
                return True, raw
            if parsed['type'] == 'ERR' and expected_type == 'ERR':
                return True, raw

        return False, f"Timeout ({timeout}s) waiting for {expected_type}:{expected_cmd}"

    async def send_cmd(self, cmd_str: str, label: str = ""):
        """Send one BLE command and wait for ACK/ERR response."""
        label = label or cmd_str.strip()[:40]
        log(BLE_SEND, cmd_str.strip())
        try:
            await self.client.write_gatt_char(
                CHAR_CMD_UUID, cmd_str.encode(), response=False
            )
        except Exception as e:
            log(ERR_TAG, f"Write failed: {e}")
            return None

        ok, msg = await self._wait_for(None, None, RESPONSE_TIMEOUT_S)
        if not ok:
            # try ACK first
            ok, msg = await self._wait_for('ACK', label.split()[0] if label else '', 2)
            if not ok:
                log(ERR_TAG, f"No response for: {cmd_str.strip()}")
        return msg

    async def scan(self):
        for attempt in range(1, SCAN_RETRIES + 1):
            log(SCAN_TAG, f"Scan attempt {attempt}/{SCAN_RETRIES} (timeout={SCAN_TIMEOUT}s)...")
            devices = await BleakScanner.discover(
                timeout=SCAN_TIMEOUT, return_adv=True, use_cached=False
            )

            if not devices:
                log(SCAN_TAG, "No devices found")
                if attempt < SCAN_RETRIES:
                    await asyncio.sleep(RETRY_DELAY_S)
                continue

            log(SCAN_TAG, f"Found {len(devices)} device(s):")
            for addr, (device, adv) in devices.items():
                name = device.name or "(unknown)"
                rssi = adv.rssi if adv.rssi is not None else "N/A"
                svc_uuids = [str(u) for u in adv.service_uuids] if adv.service_uuids else []
                is_target = (
                    "EcoTiter" in name
                    or SERVICE_UUID in svc_uuids
                )
                marker = "  \033[1m<--- ECOTITER\033[0m" if is_target else ""
                log(SCAN_TAG, f"  {name:<30} {addr:<36} RSSI={str(rssi):>4}{marker}")

                if is_target and not self.target_device:
                    self.target_device = device
                    self.target_info = (name, addr, rssi)

            if self.target_device:
                return True

            if attempt < SCAN_RETRIES:
                await asyncio.sleep(RETRY_DELAY_S)

        return False

    async def run_test_sequence(self):
        """EcoTiter BLE test sequence — sends JSON CommandEnvelope and checks logs."""
        steps = [
            ("ping",       '{"cmd":"ping"}\n',             3),
            ("status",     '{"cmd":"status"}\n',           3),
        ]

        log(OK_TAG, "=== EcoTiter BLE test sequence ===")

        for i, (name, cmd, wait_s) in enumerate(steps, 1):
            log(BLE_SEND, f"[Step {i}/{len(steps)}] {name}: {cmd.strip()}")

            try:
                await self.client.write_gatt_char(
                    CHAR_CMD_UUID, cmd.encode(), response=False
                )
                log(OK_TAG, f"  Wrote {len(cmd)} bytes to RX char")
            except Exception as e:
                self.failed_steps.append((i, name, f"Write failed: {e}"))
                continue

            # Wait a bit for any notification or just to observe serial logs
            await asyncio.sleep(wait_s)

        log("", "")
        total = len(steps)
        passed = total - len(self.failed_steps)
        if passed == total:
            log(OK_TAG, f"\033[1m=== ECOTITER BLE TEST: PASSED ({passed}/{total}) ===\033[0m")
        else:
            log(ERR_TAG, f"\033[1m=== ECOTITER BLE TEST: FAILED ({passed}/{total}) ===\033[0m")
            for num, name, reason in self.failed_steps:
                log(ERR_TAG, f"  Step {num} ({name}): {reason}")

    async def run_interactive(self):
        """Read JSON commands from stdin and send them via BLE."""
        log(OK_TAG, "=== Interactive mode (EcoTiter) ===")
        log(OK_TAG, "Type a JSON command (e.g. {\"cmd\":\"ping\"}) and press Enter.")
        log(OK_TAG, "Type 'quit' or Ctrl+C to exit.")
        log("", "")

        loop = asyncio.get_event_loop()
        while True:
            try:
                line = await loop.run_in_executor(None, sys.stdin.readline)
                if not line:
                    break
                line = line.strip()
                if not line:
                    continue
                if line.lower() in ("quit", "exit", "q"):
                    break

                if not line.endswith("\n"):
                    line += "\n"

                log(BLE_SEND, line.strip())
                try:
                    await self.client.write_gatt_char(
                        CHAR_CMD_UUID, line.encode(), response=False
                    )
                    log(OK_TAG, f"Sent {len(line)} bytes")
                except Exception as e:
                    log(ERR_TAG, f"Write failed: {e}")
                    continue

                await asyncio.sleep(2)

            except KeyboardInterrupt:
                log("", "")
                log(OK_TAG, "Interactive mode ended")
                break

    async def connect_and_test(self):
        """Connect to the target BLE device and run tests."""
        dev = self.target_device
        name, addr, rssi = self.target_info

        for attempt in range(1, 4):
            log(BLE_TAG, f"Connect attempt {attempt}/3 to {name} ({addr})...")

            if attempt > 1:
                self.target_device = None
                found = await self.scan()
                if not found:
                    log(ERR_TAG, "Autosampler not found, aborting")
                    return
                dev = self.target_device
                name, addr, _ = self.target_info

            try:
                self.client = BleakClient(
                    dev.address, timeout=CONNECT_TIMEOUT,
                    disconnected_callback=lambda c: log(BLE_TAG, "*** Disconnected ***")
                )
                log(BLE_TAG, "Connecting...")
                await self.client.connect()
                log(OK_TAG, f"Connected! MTU: {self.client.mtu_size}")

                resp_char = self.client.services.get_characteristic(CHAR_RESP_UUID)
                if resp_char:
                    log(OK_TAG, f"Found TX char ({CHAR_RESP_UUID}), subscribing for notifications...")
                    await self.client.start_notify(resp_char, self.on_notification)
                else:
                    log(BLE_TAG, "TX notify char not found — will listen for notifications on service discovery")
                    # List available characteristics for debugging
                    for svc in self.client.services:
                        for ch in svc.characteristics:
                            log(BLE_TAG, f"  [{svc.uuid}] {ch.uuid} -> {', '.join(ch.properties)}")

                if self.interactive:
                    await self.run_interactive()
                else:
                    await self.run_test_sequence()

                return

            except asyncio.TimeoutError:
                log(ERR_TAG, f"Connection timeout — attempt {attempt}/3")
            except Exception as e:
                log(ERR_TAG, f"Connection error: {type(e).__name__}: {e}")

            if self.client and self.client.is_connected:
                try:
                    await self.client.disconnect()
                except Exception:
                    pass
                self.client = None

            if attempt < 3:
                await asyncio.sleep(5)

        log(ERR_TAG, "All 3 connection attempts failed")

    async def run(self):
        """Main entry point: scan, connect, test."""
        log(OK_TAG, "=== EcoTiter BLE + Serial Test ===")

        if self.target_device:
            await self.connect_and_test()
        else:
            log(ERR_TAG, "No target device found")


# ── single command mode ─────────────────────────────────────────────
async def send_single_command(cmd_str: str):
    """Scan, connect, send one JSON command, disconnect."""
    log(OK_TAG, f"=== Single command mode: {cmd_str.strip()} ===")

    ble = BleSerialTest(serial_reader=None, interactive=False)
    found = await ble.scan()
    if not found:
        log(ERR_TAG, "EcoTiter not found")
        return

    dev = ble.target_device
    name, addr, _ = ble.target_info
    log(BLE_TAG, f"Connecting to {name} ({addr})...")

    try:
        ble.client = BleakClient(dev.address, timeout=CONNECT_TIMEOUT)
        await ble.client.connect()
        log(OK_TAG, f"Connected! MTU: {ble.client.mtu_size}")

        log(BLE_SEND, cmd_str.strip())
        await ble.client.write_gatt_char(
            CHAR_CMD_UUID, cmd_str.encode(), response=False
        )
        log(OK_TAG, f"Sent {len(cmd_str)} bytes")

        await asyncio.sleep(5)

    except Exception as e:
        log(ERR_TAG, f"Error: {e}")
    finally:
        if ble.client and ble.client.is_connected:
            await ble.client.disconnect()
        log(OK_TAG, "Done")


# ── CLI entry point ─────────────────────────────────────────────────
def main():
    parser = argparse.ArgumentParser(
        description="BLE + USB Serial dual monitor for Autosampler firmware"
    )
    parser.add_argument("--port", default=None,
                        help="Serial port (auto-detect if omitted)")
    parser.add_argument("--baud", type=int, default=SERIAL_BAUD,
                        help=f"Serial baud rate (default: {SERIAL_BAUD})")
    parser.add_argument("--no-reset", action="store_true",
                        help="Skip DTR reset on serial connect")
    parser.add_argument("--scan-only", action="store_true",
                        help="Scan BLE only, do not connect")
    parser.add_argument("--interactive", action="store_true",
                        help="Interactive command entry mode")
    parser.add_argument("--cmd", default=None,
                        help="Send one BLE command and exit")
    args = parser.parse_args()

    # ── start serial reader (unless scan-only or single-cmd) ──
    serial_reader = None
    if not args.scan_only:
        port = args.port or find_esp32_port()
        if port:
            serial_reader = SerialReader(port, args.baud, not args.no_reset)
            serial_reader.start()
            # Give serial time to connect and show boot
            time.sleep(2)
        else:
            log(ERR_TAG, "ESP32 serial port not found "
                "(specify with --port COMx or check connection)")
            if not args.cmd and not args.scan_only:
                log(ERR_TAG, "Continuing with BLE only (no serial output)")

    # ── run BLE part ───────────────────────────────────────────
    try:
        if args.cmd:
            asyncio.run(send_single_command(args.cmd))
        else:
            ble = BleSerialTest(serial_reader=None, interactive=args.interactive)

            async def run_ble():
                found = await ble.scan()
                if args.scan_only:
                    if found:
                        n, a, r = ble.target_info
                        log(OK_TAG, f"Target: {n} ({a}) RSSI={r}")
                    else:
                        log(ERR_TAG, "Autosampler not found")
                    return
                if found:
                    ble.target_device = ble.target_device
                    ble.target_info = ble.target_info
                    await ble.run()
                else:
                    log(ERR_TAG, "Autosampler not found after scan")

            asyncio.run(run_ble())
    except KeyboardInterrupt:
        log("", "")
        log(OK_TAG, "Interrupted by user")
    finally:
        if serial_reader:
            serial_reader.stop()
            serial_reader.join(timeout=3)
        log(OK_TAG, "Exiting")


if __name__ == "__main__":
    main()
