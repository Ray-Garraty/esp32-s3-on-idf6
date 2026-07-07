#!/usr/bin/env python3
"""Serial monitor — 30s timeout, auto-detect port, log with timestamps, crash detection.

Usage:
    python scripts/monitor.py                     # auto-detect, 30s
    python scripts/monitor.py COM5                # explicit port
    python scripts/monitor.py --timeout 60        # longer monitor
    python scripts/monitor.py --no-reset          # skip DTR reset
    python scripts/monitor.py --no-log            # terminal only
    python scripts/monitor.py --log-dir /tmp/logs
"""
import serial, sys, time, os, argparse
from datetime import datetime
from pathlib import Path

BAUD = 115200

def timestamp():
    return datetime.now().strftime("%H:%M:%S.%f")[:-3]

def monitor_port(port, *, timeout=30, no_reset=False, log_dir=None, no_log=False):
    LOG_DIR = Path(log_dir) if log_dir else Path(__file__).resolve().parent.parent / "logs"

    try:
        ser = serial.Serial(
            port=port, baudrate=BAUD,
            bytesize=serial.EIGHTBITS, parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE, timeout=1,
            xonxoff=False, rtscts=False, dsrdtr=False,
        )
    except serial.SerialException as e:
        print(f"[{timestamp()}] ERROR: Cannot open {port}: {e}", flush=True)
        return 1

    log_path = None
    if not no_log:
        LOG_DIR.mkdir(parents=True, exist_ok=True)
        ts = datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
        log_path = LOG_DIR / f"serial_{ts}.log"
        counter = 1
        while log_path.exists():
            log_path = LOG_DIR / f"serial_{ts}_{counter}.log"
            counter += 1
        log_file = open(log_path, "w", encoding="utf-8")

    def writeline(msg):
        print(msg, flush=True)
        if not no_log:
            log_file.write(msg + "\n")
            log_file.flush()

    writeline(f"=== Connected to ESP32-S3 on {port} @ {BAUD} baud ===")

    if not no_reset:
        writeline("=== Resetting ESP32-S3 (DTR pulse) ===")
        ser.dtr = False; ser.rts = False; time.sleep(0.1)
        ser.dtr = True;  ser.rts = True;  time.sleep(0.1)
        ser.dtr = False; ser.rts = False; time.sleep(0.5)
        ser.reset_input_buffer()

    if log_path:
        writeline(f"=== Log: {log_path} ===")

    CRASH_STATE_IDLE = 0
    CRASH_STATE_COLLECTING = 1
    crash_state = CRASH_STATE_IDLE
    crash_buffer = []
    found_crash = False
    found_boot = False

    deadline = time.time() + timeout
    buf = ""
    try:
        while time.time() < deadline:
            if ser.in_waiting:
                data = ser.read(ser.in_waiting).decode("utf-8", errors="replace")
                buf += data
                while "\n" in buf:
                    line, buf = buf.split("\n", 1)
                    line = line.strip("\r")
                    if not line:
                        continue
                    ts = timestamp()

                    if "=== CRASH ===" in line:
                        crash_state = CRASH_STATE_COLLECTING
                        crash_buffer = [line]
                        found_crash = True
                        writeline(f"[{ts}] {line}")
                    elif crash_state == CRASH_STATE_COLLECTING:
                        crash_buffer.append(line)
                        writeline(f"[{ts}] {line}")
                        if "!!! EXCEPTION END !!!" in line or "Rebooting..." in line:
                            crash_state = CRASH_STATE_IDLE
                    else:
                        if "BOOT_OK_MARKER" in line:
                            found_boot = True
                        writeline(f"[{ts}] {line}")
            else:
                time.sleep(0.01)
    except serial.SerialException:
        writeline(f"[{timestamp()}] === Connection lost ===")
    except KeyboardInterrupt:
        writeline(f"[{timestamp()}] === Interrupted ===")

    ser.close()
    writeline(f"[{timestamp()}] === Port closed ===")

    if not no_log:
        log_file.close()
        if log_path and log_path.stat().st_size == 0:
            log_path.unlink()
            log_path = None

    if log_path:
        print(f"Log: {log_path}", flush=True)
    if found_crash:
        print("RESULT: CRASH DETECTED", flush=True)
        return 2
    elif found_boot:
        print("RESULT: BOOT OK", flush=True)
        return 0
    else:
        print("RESULT: No boot marker (possibly OK)", flush=True)
        return 0

def main():
    parser = argparse.ArgumentParser(description="ESP32-S3 serial monitor")
    parser.add_argument("port", nargs="?", default=None, help="COM port (auto-detect if omitted)")
    parser.add_argument("--timeout", type=int, default=30, help="Monitor duration in seconds")
    parser.add_argument("--no-reset", action="store_true", help="Skip DTR reset on connect")
    parser.add_argument("--no-log", action="store_true", help="Disable log file")
    parser.add_argument("--log-dir", default=None, help="Log directory")
    args = parser.parse_args()

    port = args.port
    if not port:
        from find_port import find_esp32_port
        port = find_esp32_port()
    if not port:
        print("ERROR: ESP32-S3 not found. Specify port: python scripts/monitor.py COM5", flush=True)
        return 1

    return monitor_port(port, timeout=args.timeout, no_reset=args.no_reset,
                        log_dir=args.log_dir, no_log=args.no_log)

if __name__ == "__main__":
    sys.exit(main())
