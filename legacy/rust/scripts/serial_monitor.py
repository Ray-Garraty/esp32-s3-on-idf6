#!/usr/bin/env python3
"""
Serial monitor for Autosampler firmware.
Auto-detects ESP32 port, resets the chip via DTR, prints serial output with timestamps.
Optionally saves output to timestamped log files.

Usage:
    python3 scripts/serial_monitor.py                     # auto-detect port, save logs
    python3 scripts/serial_monitor.py /dev/ttyUSB0        # specify port manually
    python3 scripts/serial_monitor.py /dev/ttyUSB0 --no-reset
    python3 scripts/serial_monitor.py --no-log            # terminal only, no file
    python3 scripts/serial_monitor.py --log-dir /tmp/logs # custom log dir
"""

import serial
import sys
import os
import time
import argparse
import subprocess
import threading
from pathlib import Path
from datetime import datetime

SCRIPT_DIR = Path(__file__).resolve().parent
PROJECT_DIR = SCRIPT_DIR.parent
sys.path.insert(0, str(SCRIPT_DIR))
from find_port import find_esp32_port

BAUDRATE = 115200
DEFAULT_LOG_DIR = str(Path(__file__).resolve().parent.parent / "logs")


def timestamp():
    return datetime.now().strftime("%H:%M:%S.%f")[:-3]


def make_log_filename(log_dir: str) -> Path:
    ts = datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
    return Path(log_dir) / f"serial_{ts}.log"


def analyze_crash(crash_text: str, project_dir: Path, writeline):
    """Run crash_analyzer.py on captured crash text and print results."""
    import tempfile
    with tempfile.NamedTemporaryFile(mode="w", suffix=".txt", delete=False) as f:
        f.write(crash_text)
        tmp_path = f.name

    try:
        # Run crash_analyzer
        analyzer = SCRIPT_DIR / "crash_analyzer.py"
        elf = project_dir / "target" / "xtensa-esp32s3-espidf" / "debug" / "ecotiter"

        result = subprocess.run(
            [sys.executable, str(analyzer), "--dump", tmp_path, "--elf", str(elf)],
            capture_output=True, text=True, timeout=30,
        )

        if result.returncode == 0 and result.stdout.strip():
            writeline("\n=== CRASH ANALYSIS ===")
            for line in result.stdout.strip().split("\n"):
                writeline(f"  {line}")
            writeline("=== END ANALYSIS ===\n")
        elif result.stderr:
            writeline(f"\n=== CRASH ANALYSIS ERROR ===\n  {result.stderr.strip()}\n")
    except Exception as e:
        writeline(f"\n=== CRASH ANALYSIS FAILED: {e} ===\n")
    finally:
        try:
            os.unlink(tmp_path)
        except OSError:
            pass


def main():
    parser = argparse.ArgumentParser(description="Autosampler serial monitor")
    parser.add_argument("port", nargs="?", default=None, help="COM port (auto-detect if omitted)")
    parser.add_argument("--no-reset", action="store_true", help="Skip DTR reset on connect")
    parser.add_argument("--log-dir", default=DEFAULT_LOG_DIR, help="Directory for log files (default: project_root/logs/)")
    parser.add_argument("--no-log", action="store_true", help="Disable log file saving")
    args = parser.parse_args()

    port = args.port or find_esp32_port()
    if not port:
        print("ERROR: ESP32-S3 not found. Specify port manually: python3 serial_monitor.py /dev/ttyACM0", flush=True)
        return 1

    log_file = None
    # Crash detection state
    CRASH_STATE_IDLE = 0
    CRASH_STATE_COLLECTING = 1
    crash_state = CRASH_STATE_IDLE
    crash_buffer: list[str] = []
    if not args.no_log:
        log_dir = Path(args.log_dir)
        log_dir.mkdir(parents=True, exist_ok=True)
        log_file = make_log_filename(args.log_dir)
        # ensure unique filename by appending counter if needed
        counter = 1
        while log_file.exists():
            stem = f"serial_{datetime.now().strftime('%Y-%m-%d_%H-%M-%S')}_{counter}"
            log_file = Path(args.log_dir) / f"{stem}.log"
            counter += 1

    try:
        ser = serial.Serial(
            port=port,
            baudrate=BAUDRATE,
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE,
            timeout=1,
            xonxoff=False,
            rtscts=False,
            dsrdtr=False,
        )

        def writeline(line: str, end: str = "\n"):
            print(line, end=end, flush=True)
            if log_file is not None:
                try:
                    with open(log_file, "a") as f:
                        f.write(line + "\n")
                except OSError:
                    pass

        writeline(f"=== Connected to ESP32-S3 on {port} @ {BAUDRATE} baud ===")

        if not args.no_reset:
            writeline("=== Resetting ESP32-S3 (DTR pulse) ===")
            ser.dtr = False
            ser.rts = False
            time.sleep(0.1)
            ser.dtr = True
            ser.rts = True
            time.sleep(0.1)
            ser.dtr = False
            ser.rts = False
            time.sleep(0.5)
            ser.reset_input_buffer()

        if log_file:
            writeline(f"=== Logging to {log_file} ===")
        writeline("=== Monitoring (Ctrl+C to exit) ===\n")

        buf = ""
        while True:
            try:
                if ser.in_waiting:
                    data = ser.read(ser.in_waiting).decode("utf-8", errors="replace")
                    buf += data
                    while "\n" in buf:
                        line, buf = buf.split("\n", 1)
                        line = line.strip("\r")
                        if line:
                            ts = timestamp()

                            # Crash detection
                            if "=== CRASH ===" in line:
                                crash_state = CRASH_STATE_COLLECTING
                                crash_buffer = [line]
                                writeline(f"[{ts}] {line}")
                            elif crash_state == CRASH_STATE_COLLECTING:
                                crash_buffer.append(line)
                                writeline(f"[{ts}] {line}")
                                # Check for end markers
                                if "Rebooting..." in line or "!!! EXCEPTION END !!!" in line:
                                    crash_state = CRASH_STATE_IDLE
                                    # Launch analysis in a thread to not block serial
                                    crash_text = "\n".join(crash_buffer)
                                    threading.Thread(
                                        target=analyze_crash,
                                        args=(crash_text, PROJECT_DIR, writeline),
                                        daemon=True,
                                    ).start()
                            else:
                                writeline(f"[{ts}] {line}")
                else:
                    time.sleep(0.01)
            except serial.SerialException:
                writeline("=== Connection lost ===")
                break
            except KeyboardInterrupt:
                writeline("\n=== Exiting ===")
                break

        ser.close()
        writeline("Port closed.")

        if log_file and log_file.exists() and log_file.stat().st_size == 0:
            log_file.unlink()

    except serial.SerialException as e:
        writeline(f"Error: {e}")
        return 1

    return 0


if __name__ == "__main__":
    main()
