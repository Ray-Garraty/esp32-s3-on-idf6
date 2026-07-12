#!/usr/bin/env python3
"""
HTTP API Format Validation Test.

Validates HTTP endpoint responses against HTTP_API.md spec,
and collects WebSocket broadcast messages for format + interval validation.

Usage:
    python3 scripts/testing/http_api_test.py --ip 192.168.4.1   # specify IP
    python3 scripts/testing/http_api_test.py                     # auto-detect IP from serial

Exit code: 0 = PASS, 1 = FAIL.
"""

import argparse
import datetime
import json
import math
import re
import sys
import time
import threading
import urllib.error
import urllib.request
from collections import deque
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
PROJECT_DIR = SCRIPT_DIR.parent
sys.path.insert(0, str(SCRIPT_DIR))
sys.path.insert(0, str(PROJECT_DIR))

try:
    import serial as pyserial
except ImportError:
    pyserial = None

from find_port import find_esp32_port
from utils.monitor_classifier import DedupTracker

SERIAL_BAUD = 115200
HTTP_TIMEOUT_S = 10
WS_COLLECT_S = 20
WS_COUNT_TARGET = 5
BOOT_MARKER = "HTTP server ready"
BOOT2_MARKER = "Project name:"
BOOT_TIMEOUT_S = 20

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


# ── HTTP helpers ─────────────────────────────────────────────────────

def http_get(base_url: str, path: str) -> tuple[int, str]:
    url = f"http://{base_url}{path}"
    log(f"  >>> GET {url}")
    try:
        req = urllib.request.Request(url)
        with urllib.request.urlopen(req, timeout=HTTP_TIMEOUT_S) as resp:
            body = resp.read().decode("utf-8", errors="replace")
            log(f"  <<< HTTP {resp.status} {body[:500]}")
            return resp.status, body
    except urllib.error.HTTPError as e:
        body = e.read().decode("utf-8", errors="replace")
        log(f"  <<< HTTP {e.code} {body[:500]}")
        return e.code, body
    except urllib.error.URLError as e:
        log(f"  <<< ERROR: {e}")
        return 0, f"ERROR: {e}"
    except Exception as e:
        log(f"  <<< ERROR: {e}")
        return 0, f"ERROR: {e}"


def http_post(base_url: str, path: str, data: dict) -> tuple[int, str]:
    url = f"http://{base_url}{path}"
    payload = json.dumps(data).encode("utf-8")
    log(f"  >>> POST {url} {payload.decode()}")
    try:
        req = urllib.request.Request(
            url, data=payload,
            headers={"Content-Type": "application/json"},
            method="POST",
        )
        with urllib.request.urlopen(req, timeout=HTTP_TIMEOUT_S) as resp:
            body = resp.read().decode("utf-8", errors="replace")
            log(f"  <<< HTTP {resp.status} {body[:500]}")
            return resp.status, body
    except urllib.error.HTTPError as e:
        body = e.read().decode("utf-8", errors="replace")
        log(f"  <<< HTTP {e.code} {body[:500]}")
        return e.code, body
    except urllib.error.URLError as e:
        log(f"  <<< ERROR: {e}")
        return 0, f"ERROR: {e}"
    except Exception as e:
        log(f"  <<< ERROR: {e}")
        return 0, f"ERROR: {e}"


def try_parse_json(raw: str) -> dict | None:
    try:
        return json.loads(raw)
    except (json.JSONDecodeError, TypeError):
        return None


# ── IP discovery from serial ─────────────────────────────────────────

def find_ip_from_serial(port: str | None = None) -> str | None:
    """Read serial output until we see an IP address."""
    if pyserial is None:
        return None
    if not port:
        port = find_esp32_port()
    if not port:
        return None

    log(f"  Opening {port} @ {SERIAL_BAUD} for IP discovery")
    try:
        ser = pyserial.Serial(port=port, baudrate=SERIAL_BAUD, timeout=1)
    except Exception as e:
        log(f"  Cannot open serial: {e}")
        return None

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

    sta_pattern = re.compile(r"sta ip:\s*(\d+\.\d+\.\d+\.\d+)")
    any_ip = re.compile(r"(\d+\.\d+\.\d+\.\d+)")
    deadline = time.time() + BOOT_TIMEOUT_S
    dedup = DedupTracker()
    ts = lambda: datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3]
    fallback_ap: str | None = None
    while time.time() < deadline:
        try:
            line = ser.readline().decode("utf-8", errors="replace").strip()
            if line:
                for out in dedup.add(f"serial: {line}", ts()):
                    log(f"  {out}")
            if BOOT_MARKER in line or BOOT2_MARKER in line:
                for out in dedup.flush():
                    log(f"  {out}")
                status("Boot detected, waiting for IP...")

            m = sta_pattern.search(line)
            if m:
                ip = m.group(1)
                for out in dedup.flush():
                    log(f"  {out}")
                ser.close()
                status(f"Device STA IP: {ip}")
                return ip

            m = any_ip.search(line)
            if m:
                ip = m.group(1)
                if ip == "192.168.4.1":
                    if fallback_ap is None:
                        fallback_ap = ip
                elif ip != "0.0.0.0" and not ip.startswith("0."):
                    for out in dedup.flush():
                        log(f"  {out}")
                    ser.close()
                    status(f"Device IP: {ip}")
                    return ip
        except Exception:
            break

    for out in dedup.flush():
        log(f"  {out}")
    ser.close()
    if fallback_ap:
        status("")
        status("=" * 60)
        status("ESP32-S3 is in AP-only mode (no WiFi connection)")
        status("")
        status("HTTP API test requires the device to be on the same network.")
        status("To continue:")
        status("  1. Connect to the EcoTiter-AP access point")
        status("  2. Open http://192.168.4.1/ and configure WiFi")
        status("  3. After the device connects to WiFi, re-run:")
        status("     python3 scripts/testing/http_api_test.py")
        status("")
        status("Or specify the IP manually:")
        status("     python3 scripts/testing/http_api_test.py --ip 192.168.x.x")
        status("=" * 60)
        return None
    return None


# ── WebSocket broadcast collection ───────────────────────────────────

def collect_ws_broadcasts(base_url: str) -> list[tuple[dict, float]]:
    """Connect to /ws/stream, collect broadcast JSON messages."""
    ws_available = False
    try:
        import asyncio
        try:
            import websockets
            ws_available = True
        except ImportError:
            status("  websockets library not installed — skipping WS test")
            status("  Install: pip install websockets")
    except ImportError:
        pass

    if not ws_available:
        return []

    results: list[tuple[dict, float]] = []

    async def _collect():
        uri = f"ws://{base_url}/ws/stream"
        log(f"  >>> WS connect {uri}")
        try:
            async with websockets.connect(uri, timeout=10) as ws:
                # LL-044: ESP-IDF v6 does not call ws_handler on HTTP_GET
                # upgrade, so the session is not registered until the client
                # sends the first data frame. Must send a dummy message.
                await ws.send(json.dumps({"type": "sub"}))
                log("  >>> WS sent {type:'sub'} to register session")
                deadline = time.time() + WS_COLLECT_S
                while time.time() < deadline and len(results) < 30:
                    try:
                        raw = await asyncio.wait_for(
                            ws.recv(), timeout=min(2, deadline - time.time())
                        )
                        if isinstance(raw, bytes):
                            raw = raw.decode("utf-8", errors="replace")
                        log(f"  <<< WS: {raw[:300]}")
                        obj = try_parse_json(raw)
                        if obj:
                            results.append((obj, time.monotonic()))
                    except asyncio.TimeoutError:
                        continue
        except Exception as e:
            log(f"  <<< WS error: {e}")

    asyncio.run(_collect())
    status(f"Collected {len(results)} WebSocket messages in {WS_COLLECT_S}s")
    return results


# ── Broadcast format validation (HTTP API /api/status format) ────────

def validate_http_status(body: str) -> bool:
    obj = try_parse_json(body)
    if obj is None:
        fail_msg("GET /api/status: response is not valid JSON")
        return False

    checks = [
        ("ts", lambda o: isinstance(o.get("ts"), int)),
        ("meta.ip", lambda o: isinstance(o.get("meta", {}).get("ip"), str)),
        ("sensors.temperature.is_connected",
         lambda o: isinstance(o.get("sensors", {}).get("temperature", {}).get("is_connected"), bool)),
        ("sensors.temperature.celsius_val",
         lambda o: o.get("sensors", {}).get("temperature", {}).get("celsius_val") is not None),
        ("sensors.electrode.mv",
         lambda o: isinstance(o.get("sensors", {}).get("electrode", {}).get("mv"), (int, float))),
        ("valve.position",
         lambda o: o.get("valve", {}).get("position") in ("input", "output")),
        ("burette.status",
         lambda o: o.get("burette", {}).get("status") in ("idle", "working", "error")),
        ("burette.volume_ml",
         lambda o: isinstance(o.get("burette", {}).get("volume_ml"), (int, float))),
        ("burette.speed_ml_min",
         lambda o: isinstance(o.get("burette", {}).get("speed_ml_min"), (int, float))),
    ]

    all_ok = True
    for name, check in checks:
        if not check(obj):
            log(f"  field missing/type mismatch: {name}")
            all_ok = False

    if all_ok:
        pass_msg("GET /api/status: all required fields present and typed correctly")
    else:
        fail_msg("GET /api/status: some fields missing or type mismatch (see log)")
    return all_ok


# ── Broadcast interval diagnostics ───────────────────────────────────

SPEC_INTERVAL_MS = 300
FIRMWARE_INTERVAL_MS = 2000
OUTLIER_MS = 4000


def diagnose_broadcast_intervals(
    broadcasts: list[tuple[dict, float]], label: str
) -> None:
    if len(broadcasts) < 2:
        status(f"  {label}: too few messages (<2) for interval analysis")
        return

    ts_deltas: list[float] = []
    arrival_deltas: list[float] = []
    prev_ts: int | None = None
    prev_arrival: float | None = None

    for obj, arrival in broadcasts:
        ts = obj.get("ts")
        if not isinstance(ts, int) or ts < 0:
            continue
        if prev_ts is not None and ts > prev_ts:
            ts_deltas.append((ts - prev_ts) * 10)
        prev_ts = ts

        if prev_arrival is not None:
            d = (arrival - prev_arrival) * 1000
            if d > 0:
                arrival_deltas.append(d)
        prev_arrival = arrival

    _print_delta_stats(f"  {label} — ts delta", ts_deltas, "device-side blocking")
    _print_delta_stats(f"  {label} — status arrival delta",
                        arrival_deltas, "connection issue")


def _print_delta_stats(header: str, deltas: list[float], hint: str) -> None:
    if not deltas:
        status(f"{header}: no valid deltas")
        return

    mean = sum(deltas) / len(deltas)
    variance = sum((d - mean) ** 2 for d in deltas) / len(deltas)
    stddev = math.sqrt(variance)
    outliers = [d for d in deltas if d > OUTLIER_MS]

    status(f"{header}:")
    status(f"    Expected (spec): {SPEC_INTERVAL_MS} ms")
    status(f"    Expected (firmware): ~{FIRMWARE_INTERVAL_MS} ms (known deviation)")
    status(f"    Actual: min={min(deltas):.0f}ms  max={max(deltas):.0f}ms  "
            f"mean={mean:.0f}ms  stddev={stddev:.0f}ms")
    status(f"    Outliers (>{OUTLIER_MS}ms): {len(outliers)}/{len(deltas)}")
    if max(deltas) > FIRMWARE_INTERVAL_MS * 1.5:
        status(f"    -> WARN: large gap — possible {hint}")
    else:
        status(f"    -> OK")


# ── WS broadcast format validation ───────────────────────────────────

WS_TOP_KEYS = {"ts", "temp", "mv", "vlv", "brt"}
WS_VLV_VALID = {"in", "out", "unk"}
WS_BRT_STS_VALID = {"idle", "working", "error"}


def validate_ws_broadcasts(broadcasts: list[tuple[dict, float]]) -> None:
    if not broadcasts:
        status("  WS broadcasts: none collected, skipping validation")
        return

    valid = 0
    for obj, _ in broadcasts:
        issues = _check_ws_broadcast(obj)
        if issues:
            for i in issues:
                log(f"  WS format issue: {i}")
        else:
            valid += 1

    if valid == len(broadcasts):
        pass_msg(f"All {len(broadcasts)} WS broadcasts have valid format")
    else:
        fail_msg(f"{valid}/{len(broadcasts)} WS broadcasts have valid format")


def _check_ws_broadcast(obj: dict) -> list[str]:
    issues: list[str] = []
    if not isinstance(obj, dict):
        return ["not an object"]

    if "event" in obj:
        # Log event: {"event":"log","data":{"level":"...","msg":"..."}}
        if obj["event"] != "log":
            issues.append(f"unexpected event type: {obj['event']}")
            return issues
        d = obj.get("data", {})
        if not isinstance(d, dict):
            issues.append("log event data is not an object")
        else:
            if "level" not in d:
                issues.append("log event missing level")
            if "msg" not in d:
                issues.append("log event missing msg")
        return issues

    # Status broadcast: extended format with compact keys + optional extras
    missing = WS_TOP_KEYS - set(obj.keys())
    if missing:
        issues.append(f"missing keys: {missing}")
        return issues

    extra = set(obj.keys()) - WS_TOP_KEYS
    if extra:
        log(f"  WS broadcast extra keys: {sorted(extra)}")

    if not isinstance(obj["ts"], int) or obj["ts"] < 0:
        issues.append(f"ts: expected uint32")

    if obj["temp"] is not None and not isinstance(obj["temp"], (int, float)):
        issues.append(f"temp: expected float|null")

    if not isinstance(obj["mv"], (int, float)):
        issues.append(f"mv: expected number")

    if obj["vlv"] not in WS_VLV_VALID:
        issues.append(f"vlv: expected in/out/unk")

    brt = obj.get("brt", {})
    if not isinstance(brt, dict):
        issues.append("brt: expected object")
        return issues

    if not {"sts", "vl", "spd"}.issubset(brt.keys()):
        issues.append(f"brt missing keys: expected sts,vl,spd got {set(brt.keys())}")
    else:
        if brt["sts"] not in WS_BRT_STS_VALID:
            issues.append(f"brt.sts: expected idle/working/error")
        if brt["vl"] is not None and not isinstance(brt["vl"], (int, float)):
            issues.append(f"brt.vl: expected float|null")
        if not isinstance(brt["spd"], (int, float)):
            issues.append(f"brt.spd: expected number")

    return issues


# ── Test runner ──────────────────────────────────────────────────────

def run_http_test(base_url: str, name: str, method: str, path: str,
                  expect_code: int = 200, body: dict | None = None,
                  expect_fn=None) -> bool:
    status(f"\n=== {name} ===")

    if method == "GET":
        code, resp = http_get(base_url, path)
    else:
        code, resp = http_post(base_url, path, body or {})

    if code != expect_code:
        fail_msg(f"{name}: expected HTTP {expect_code}, got {code}")
        return False

    if expect_fn is not None and not expect_fn(resp):
        fail_msg(f"{name}: response validation failed")
        return False

    pass_msg(f"{name}: HTTP {code}")
    return True


# ── Main ─────────────────────────────────────────────────────────────

def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description="HTTP API format validation test")
    p.add_argument("--ip", default=None, help="Device IP address")
    p.add_argument("--port", default=None, help="Serial port for IP detection")
    return p.parse_args()


def main() -> int:
    global PASS, FAIL, log_file

    args = parse_args()
    base_url = args.ip

    if not base_url:
        status("Auto-detecting device IP from serial...")
        base_url = find_ip_from_serial(args.port)
        if not base_url:
            status("ERROR: Could not determine device IP. Use --ip 192.168.x.x")
            return 1

    log_dir = SCRIPT_DIR / "logs"
    log_dir.mkdir(parents=True, exist_ok=True)
    ts = datetime.datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
    log_path = log_dir / f"http_api_test_{ts}.log"
    log_file = open(log_path, "w", encoding="utf-8")
    log_file.write(f"HTTP API test log — {ts}\n")
    log_file.write(f"Target: http://{base_url}\n")
    log_file.write("=" * 60 + "\n")
    log_file.flush()
    status(f"Log: {log_path}")
    status(f"Target: http://{base_url}")

    # ── REST endpoint tests ───────────────────────────────────────

    # 1. GET /api/ping
    run_http_test(base_url, "GET /api/ping", "GET", "/api/ping",
                  expect_fn=lambda r: (
                      (j := try_parse_json(r)) is not None
                      and j.get("status") == "ok"
                  ))

    # 2. GET /api/status
    run_http_test(base_url, "GET /api/status", "GET", "/api/status",
                  expect_fn=validate_http_status)

    # 3. GET /api/valve
    run_http_test(base_url, "GET /api/valve", "GET", "/api/valve",
                  expect_fn=lambda r: (
                      (j := try_parse_json(r)) is not None
                      and j.get("valve") in ("input", "output")
                  ))

    # 4. POST /api/valve (set output)
    run_http_test(base_url, "POST /api/valve (output)", "POST", "/api/valve",
                  body={"position": "output"},
                  expect_fn=lambda r: (
                      (j := try_parse_json(r)) is not None
                      and j.get("valve") == "output"
                  ))

    # 5. POST /api/valve (restore input)
    run_http_test(base_url, "POST /api/valve (input)", "POST", "/api/valve",
                  body={"position": "input"},
                  expect_fn=lambda r: (
                      (j := try_parse_json(r)) is not None
                      and j.get("valve") == "input"
                  ))

    # 6. GET /api/logs
    run_http_test(base_url, "GET /api/logs", "GET", "/api/logs",
                  expect_fn=lambda r: (
                      (j := try_parse_json(r)) is not None
                      and "entries" in j
                      and "count" in j
                  ))

    # 7. GET /api/logs?limit=3
    run_http_test(base_url, "GET /api/logs?limit=3", "GET", "/api/logs?limit=3",
                  expect_fn=lambda r: (
                      (j := try_parse_json(r)) is not None
                      and isinstance(j.get("count"), int)
                      and j["count"] <= 3
                  ))

    # 8. GET /api/nvs/status
    run_http_test(base_url, "GET /api/nvs/status", "GET", "/api/nvs/status",
                  expect_fn=lambda r: (
                      (j := try_parse_json(r)) is not None
                      and "burette_cal" in j
                      and "adc_cal" in j
                      and isinstance(j["burette_cal"], bool)
                      and isinstance(j["adc_cal"], bool)
                  ))

    # 9. POST /api/command (serial.ping)
    run_http_test(base_url, "POST /api/command (ping)", "POST", "/api/command",
                  body={"id": 1, "cmd": "serial.ping"},
                  expect_fn=lambda r: (
                      (j := try_parse_json(r)) is not None
                      and (j.get("status") == "ok" or j.get("result") == "pong")
                  ))

    # 10. GET /wifi/status
    run_http_test(base_url, "GET /wifi/status", "GET", "/wifi/status",
                  expect_fn=lambda r: (
                      (j := try_parse_json(r)) is not None
                      and "ap" in j
                  ))

    # 11. GET / (dashboard)
    run_http_test(base_url, "GET /", "GET", "/",
                  expect_code=200,
                  expect_fn=lambda r: r is not None and "EcoTiter" in r)

    # 12. GET /js/init.js
    run_http_test(base_url, "GET /js/init.js", "GET", "/js/init.js",
                  expect_code=200,
                  expect_fn=lambda r: r is not None and "initApp" in r)

    # 13. GET /nonexistent (firmware serves page, no redirect)
    run_http_test(base_url, "GET /nonexistent", "GET", "/nonexistent",
                  expect_code=200)

    # ── WebSocket broadcast collection + validation ───────────────
    status(f"\n=== WebSocket broadcast collection ({WS_COLLECT_S}s) ===")
    ws_broadcasts = collect_ws_broadcasts(base_url)

    if ws_broadcasts:
        status("\n=== WS broadcast format validation ===")
        validate_ws_broadcasts(ws_broadcasts)

        status("\n=== WS broadcast interval diagnostics ===")
        diagnose_broadcast_intervals(ws_broadcasts, "WS broadcast")
    else:
        status("  WS test skipped (no broadcasts collected)")

    # ── Summary ───────────────────────────────────────────────────
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
