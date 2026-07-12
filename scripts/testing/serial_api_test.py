#!/usr/bin/env python3
"""
Serial API Format Validation Test.

Validates serial command/response format against SERIAL_API.md,
collects broadcast messages, and diagnoses timing issues via
two methods (timestamp delta and arrival delta).

Usage:
    python3 scripts/testing/serial_api_test.py                    # auto-detect port
    python3 scripts/testing/serial_api_test.py -p /dev/ttyUSB0   # specify port

Exit code: 0 = PASS, 1 = FAIL.
"""

import argparse
import datetime
import json
import math
import sys
import threading
import time
from collections import deque
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
PROJECT_DIR = SCRIPT_DIR.parent
sys.path.insert(0, str(SCRIPT_DIR))
sys.path.insert(0, str(PROJECT_DIR))

try:
    import serial
except ImportError:
    print("ERROR: pyserial not installed. Run: pip install pyserial")
    sys.exit(1)

from find_port import find_esp32_port
from utils.monitor_classifier import DedupTracker

BAUDRATE = 115200
BOOT_TIMEOUT_S = 15
CMD_TIMEOUT_S = 5
BROADCAST_COLLECT_S = 30
BOOT_MARKER = "HTTP server ready"
BOOT2_MARKER = "Project name:"

PASS = 0
FAIL = 0
log_file = None


def status(msg: str) -> None:
    global log_file
    print(msg, flush=True)
    if log_file:
        ts = datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3]
        log_file.write(f"[{ts}] {msg}\n")
        log_file.flush()


def log(msg: str) -> None:
    global log_file
    if log_file:
        ts = datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3]
        log_file.write(f"[{ts}] {msg}\n")
        log_file.flush()


def pass_msg(msg: str) -> None:
    global PASS
    PASS += 1
    status(f"  ==> PASS: {msg}")


def fail_msg(msg: str) -> None:
    global FAIL
    FAIL += 1
    status(f"  ==> FAIL: {msg}")


def reader_thread(ser, buf: deque, stop_event: threading.Event) -> None:
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
            else:
                time.sleep(0.005)
        except serial.SerialException:
            break
        except Exception:
            break


def wait_for_boot(ser, buf: deque) -> bool:
    status("Waiting for ESP32 boot...")
    deadline = time.time() + BOOT_TIMEOUT_S
    lines_seen = 0
    dedup = DedupTracker()
    ts = lambda: datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3]
    while time.time() < deadline:
        while buf:
            line = buf.popleft()
            lines_seen += 1
            for out in dedup.add(line.strip(), ts()):
                log(f"  boot line: {out}")
            if BOOT_MARKER in line or BOOT2_MARKER in line:
                for out in dedup.flush():
                    log(f"  boot line: {out}")
                status(f"Boot detected after {lines_seen} lines")
                return True
        time.sleep(0.1)

    for out in dedup.flush():
        log(f"  boot line: {out}")
    fail_msg(f"Boot marker not seen within {BOOT_TIMEOUT_S}s")
    return False


def drain_buf(buf: deque) -> None:
    while buf:
        buf.popleft()


def send_and_expect(
    ser, buf: deque, cmd: str, cmd_id: int,
    expect_keys: list | None = None,
    expect_error: bool = False,
    timeout_s: float = CMD_TIMEOUT_S,
) -> dict | None:
    payload = json.dumps({"id": cmd_id, "cmd": cmd}) + "\n"
    log(f"  >>> {payload.strip()}")
    ser.write(payload.encode())

    deadline = time.time() + timeout_s
    while time.time() < deadline:
        while buf:
            line = buf.popleft()
            log(f"  <<< {line}")
            if line.startswith("{"):
                try:
                    resp = json.loads(line)
                except json.JSONDecodeError:
                    continue

                # Match by id (spec format) or cmd field (current firmware)
                matched = resp.get("id") == cmd_id or resp.get("cmd") == cmd
                if not matched:
                    continue

                # Prefer spec format, fallback to current firmware format
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
                    fail_msg(f"{cmd}: unexpected status='{st}', result='{resp.get('result','')}'")
                    return None

    fail_msg(f"{cmd}: no response within {timeout_s}s")
    return None


def collect_broadcasts(buf: deque, duration_s: float) -> list[tuple[dict, float]]:
    results: list[tuple[dict, float]] = []
    deadline = time.time() + duration_s
    while time.time() < deadline:
        while buf:
            line = buf.popleft()
            log(f"  broadcast raw: {line}")
            if line.startswith("{"):
                try:
                    obj = json.loads(line)
                except json.JSONDecodeError:
                    continue
                if "id" not in obj and "cmd" not in obj:
                    results.append((obj, time.monotonic()))
        time.sleep(0.01)

    status(f"Collected {len(results)} broadcast messages in {duration_s}s")
    return results


# ── Broadcast format validation ──────────────────────────────────────

SPEC_TOP_KEYS = {"ts", "temp", "mv", "vlv", "brt"}
BRT_SUB_KEYS = {"sts", "vl", "spd"}
VLV_VALID = {"in", "out", "unk"}
BRT_STS_VALID = {"idle", "working", "error"}


def validate_broadcast_format(broadcasts: list[tuple[dict, float]]) -> None:
    if not broadcasts:
        fail_msg("No broadcast messages received for format validation")
        return

    spec_ok = 0
    spec_all = 0
    for obj, _arrival in broadcasts:
        spec_issues = _check_spec(obj)
        extra_keys = set(obj.keys()) - SPEC_TOP_KEYS
        if extra_keys:
            log(f"  broadcast extra keys: {sorted(extra_keys)}")
        if not spec_issues:
            spec_ok += 1
        else:
            for e in spec_issues:
                log(f"  broadcast spec issue: {e}")
        spec_all += 1

    if spec_ok == spec_all:
        pass_msg(f"All {spec_all} broadcasts conform to spec format")
    else:
        fail_msg(f"{spec_ok}/{spec_all} broadcasts conform to spec format")


def _check_spec(obj: dict) -> list[str]:
    issues: list[str] = []

    missing_top = SPEC_TOP_KEYS - set(obj.keys())
    if missing_top:
        issues.append(f"missing spec top-level keys: {missing_top}")
        return issues

    if not isinstance(obj["ts"], int) or obj["ts"] < 0:
        issues.append(f"ts: expected uint32, got {obj['ts']!r}")

    if obj["temp"] is not None and not isinstance(obj["temp"], (int, float)):
        issues.append(f"temp: expected float|null, got {type(obj['temp']).__name__}")

    if not isinstance(obj["mv"], (int, float)):
        issues.append(f"mv: expected number, got {type(obj['mv']).__name__}")

    if obj["vlv"] not in VLV_VALID:
        issues.append(f"vlv: expected in/out/unk, got {obj['vlv']!r}")

    brt = obj["brt"]
    if not isinstance(brt, dict):
        issues.append("brt: expected object")
        return issues

    missing_brt = BRT_SUB_KEYS - set(brt.keys())
    if missing_brt:
        issues.append(f"brt missing keys: {missing_brt}")
    else:
        if brt["sts"] not in BRT_STS_VALID:
            issues.append(f"brt.sts: expected idle/working/error, got {brt['sts']!r}")
        if brt["vl"] is not None and not isinstance(brt["vl"], (int, float)):
            issues.append(f"brt.vl: expected float|null, got {type(brt['vl']).__name__}")
        if not isinstance(brt["spd"], (int, float)):
            issues.append(f"brt.spd: expected number, got {type(brt['spd']).__name__}")

    return issues


# ── Broadcast interval diagnostics ───────────────────────────────────

SPEC_INTERVAL_MS = 300
FIRMWARE_INTERVAL_MS = 2000
OUTLIER_THRESHOLD_MS = 4000


def diagnose_broadcast_intervals(broadcasts: list[tuple[dict, float]]) -> None:
    if len(broadcasts) < 2:
        status("  Broadcast interval: too few messages (<2) for analysis")
        return

    ts_deltas_ms: list[float] = []
    arrival_deltas_ms: list[float] = []

    for i in range(1, len(broadcasts)):
        ts_prev = broadcasts[i - 1][0]["ts"]
        ts_curr = broadcasts[i][0]["ts"]
        delta_ts = (ts_curr - ts_prev) * 10  # ts is in 10ms ticks
        if delta_ts > 0:
            ts_deltas_ms.append(delta_ts)

        arrival_prev = broadcasts[i - 1][1]
        arrival_curr = broadcasts[i][1]
        delta_arrival = (arrival_curr - arrival_prev) * 1000
        if delta_arrival > 0:
            arrival_deltas_ms.append(delta_arrival)

    status(f"\n=== Broadcast interval diagnostics ({len(broadcasts)} frames) ===")

    _report_deltas("Method A (ts * 10ms)", ts_deltas_ms, "device-side blocking")
    _report_deltas("Method B (arrival)", arrival_deltas_ms, "connection/read issue")


def _report_deltas(label: str, deltas: list[float], issue_hint: str) -> None:
    if not deltas:
        status(f"  {label}: no valid deltas")
        return

    mean = sum(deltas) / len(deltas)
    variance = sum((d - mean) ** 2 for d in deltas) / len(deltas)
    stddev = math.sqrt(variance)
    outliers = [d for d in deltas if d > OUTLIER_THRESHOLD_MS]
    max_d = max(deltas)
    min_d = min(deltas)

    status(f"  {label}:")
    status(f"    Expected (spec): {SPEC_INTERVAL_MS} ms")
    status(f"    Expected (firmware): ~{FIRMWARE_INTERVAL_MS} ms (known deviation)")
    status(f"    Actual: min={min_d:.0f}ms  max={max_d:.0f}ms  "
            f"mean={mean:.0f}ms  stddev={stddev:.0f}ms")
    status(f"    Outliers (>{OUTLIER_THRESHOLD_MS}ms): {len(outliers)}/{len(deltas)}")

    if max_d > FIRMWARE_INTERVAL_MS * 1.5:
        status(f"    -> WARN: large gap detected — possible {issue_hint}")
    else:
        status(f"    -> OK: no anomalies detected")


# ── Main ─────────────────────────────────────────────────────────────

def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description="Serial API format validation test")
    p.add_argument("-p", "--port", default=None, help="Serial port")
    return p.parse_args()


def main() -> int:
    global PASS, FAIL, log_file

    args = parse_args()
    port = args.port or find_esp32_port()
    if not port:
        status("ERROR: ESP32 not found. Specify port with -p /dev/ttyUSB0")
        return 1

    log_dir = SCRIPT_DIR / "logs"
    log_dir.mkdir(parents=True, exist_ok=True)
    ts = datetime.datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
    log_path = log_dir / f"serial_api_test_{ts}.log"
    log_file = open(log_path, "w", encoding="utf-8")
    log_file.write(f"Serial API test log — {ts}\n")
    log_file.write(f"Port: {port} @ {BAUDRATE}\n")
    log_file.write("=" * 60 + "\n")
    log_file.flush()
    status(f"Log: {log_path}")

    status(f"Connecting to {port} @ {BAUDRATE} baud")
    try:
        ser = serial.Serial(
            port=port,
            baudrate=BAUDRATE,
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE,
            timeout=0.1,
            xonxoff=False,
            rtscts=False,
            dsrdtr=False,
        )
    except serial.SerialException as e:
        status(f"ERROR: Cannot open {port}: {e}")
        if log_file:
            log_file.close()
        return 1

    status("Resetting ESP32 (DTR pulse)")
    try:
        ser.dtr = False
        ser.rts = False
        time.sleep(0.1)
        ser.dtr = True
        ser.rts = True
        time.sleep(0.1)
        ser.dtr = False
        ser.rts = False
        time.sleep(0.3)
        ser.reset_input_buffer()
    except Exception:
        pass

    buf: deque = deque()
    stop_event = threading.Event()
    reader = threading.Thread(target=reader_thread, args=(ser, buf, stop_event), daemon=True)
    reader.start()

    # ── Step 1: Wait for boot ─────────────────────────────────────
    if not wait_for_boot(ser, buf):
        stop_event.set()
        ser.close()
        if log_file:
            log_file.close()
        return 1

    time.sleep(1)
    drain_buf(buf)

    # ── Step 2: serial.ping ───────────────────────────────────────
    status("\n=== serial.ping ===")
    send_and_expect(ser, buf, "serial.ping", cmd_id=1,
                    expect_keys=["status"])

    time.sleep(0.5)
    drain_buf(buf)

    # ── Step 3: burette.getStatus ─────────────────────────────────
    status("\n=== burette.getStatus ===")
    send_and_expect(ser, buf, "burette.getStatus", cmd_id=2,
                    expect_keys=["status", "volume_ml", "speed_ml_min"])

    time.sleep(0.5)
    drain_buf(buf)

    # ── Step 4: burette.cal.get ───────────────────────────────────
    status("\n=== burette.cal.get ===")
    send_and_expect(ser, buf, "burette.cal.get", cmd_id=3,
                    expect_keys=["steps_per_ml", "nominal_vol", "speed_coeff",
                                 "min_freq", "max_freq", "is_default"])

    time.sleep(0.5)
    drain_buf(buf)

    # ── Step 5: valve.getState ────────────────────────────────────
    status("\n=== valve.getState ===")
    send_and_expect(ser, buf, "valve.getState", cmd_id=4,
                    expect_keys=["position"])

    time.sleep(0.5)
    drain_buf(buf)

    # ── Step 6: nonexistent command (error case) ──────────────────
    status("\n=== nonexistent command (expect error) ===")
    send_and_expect(ser, buf, "nonexistent", cmd_id=5,
                    expect_error=True)

    time.sleep(0.5)
    drain_buf(buf)

    # ── Step 7: Collect + validate broadcast messages ─────────────
    status(f"\n=== Broadcast collection ({BROADCAST_COLLECT_S}s) ===")
    time.sleep(1)
    drain_buf(buf)
    broadcasts = collect_broadcasts(buf, BROADCAST_COLLECT_S)

    status("\n=== Broadcast format validation ===")
    validate_broadcast_format(broadcasts)

    status("\n=== Broadcast interval diagnostics ===")
    diagnose_broadcast_intervals(broadcasts)

    # ── Summary ───────────────────────────────────────────────────
    stop_event.set()
    ser.close()

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
