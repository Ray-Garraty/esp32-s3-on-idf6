#!/usr/bin/env python3
"""
Serial monitor for ESP32-S3 firmware. Auto-detects port, resets chip via DTR,
saves serial output to log file.

Default (quiet): only status messages to stdout, serial lines to log file only.
Use --verbose to echo serial output to terminal.

Usage:
    python scripts/monitor.py                        # quiet, 30s, auto-detect
    python scripts/monitor.py --verbose              # echo serial to terminal
    python scripts/monitor.py /dev/ttyACM0            # specify port
    python scripts/monitor.py --timeout 60           # longer
    python scripts/monitor.py --no-reset             # skip DTR reset
    python scripts/monitor.py --no-log               # terminal only, no file
    python scripts/monitor.py --log-dir /tmp/logs
    python scripts/monitor.py --broadcast-dir /tmp/broadcasts
    python scripts/monitor.py --no-broadcast-log     # no broadcast log
"""

import serial
import sys
import time
import argparse
from pathlib import Path
from datetime import datetime

SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR))
from find_port import find_esp32_port
from utils.monitor_classifier import SerialClassifier, ResultCode, DedupTracker
from utils.boot_detect import BootDetector
from utils.log_utils import is_broadcast_line, sanitize_line

BAUDRATE = 115200
PROJECT_DIR = SCRIPT_DIR.parent
DEFAULT_LOG_DIR = str(PROJECT_DIR / "logs")


def _update_capture_state(state: bool, line: str) -> bool:
    """Return new core dump capture state based on decoded serial line.

    Starts capture on:
      - ``=== CRASH ===`` (custom panic handler wrapper)
      - ``Print core dump to uart`` (ESP-IDF coredump UART trigger)
      - ``CORE DUMP START`` (start of base64 payload block)

    Stops capture on:
      - ``Rebooting...`` (ESP-IDF reboot message)
      - ``ESP-ROM:`` (ROM bootloader output after reset)

    NOTE: ``esp_core_dump_uart: Init core dump to UART`` (boot-time init)
    does NOT trigger capture — that was the root cause of log duplication.
    """
    if "=== CRASH ===" in line:
        return True
    if "Print core dump to uart" in line:
        return True
    if "CORE DUMP START" in line:
        return True
    if "Rebooting..." in line or "ESP-ROM:" in line:
        return False
    return state


def _extract_coredump(data: bytes) -> tuple[bytes | None, str | None]:
    """Extract core dump payload from raw captured UART data.

    Looks for ``CORE DUMP START`` / ``CORE DUMP END`` marker lines in the raw
    text, extracts the base64 block between them, decodes it, and returns the
    ELF binary (if valid) or the raw base64 text block.

    Returns:
        ``(payload, suffix)`` where *payload* is the bytes to save (or *None*
        if no core dump markers found) and *suffix* is ``.coredump`` for binary
        ELF, ``.coredump.base64`` for raw base64, or *None*.
    """
    text = data.decode("utf-8", errors="replace")
    lines = text.splitlines()

    start_lineno = end_lineno = None
    for i, line in enumerate(lines):
        if "CORE DUMP START" in line:
            start_lineno = i
        elif "CORE DUMP END" in line and start_lineno is not None:
            end_lineno = i
            break

    if start_lineno is None or end_lineno is None or end_lineno <= start_lineno + 1:
        return None, None

    base64_block = "\n".join(lines[start_lineno + 1:end_lineno]).strip()
    if not base64_block:
        return None, None

    # Try to decode as base64 -> binary ELF
    import base64
    try:
        decoded = base64.b64decode(base64_block)
        elf_magic = b'\x7fELF'
        elf_start = decoded.find(elf_magic)
        if elf_start >= 0:
            elf_end = len(decoded)
            for marker in [b"rst:", b"ESP-ROM:", b"Rebooting"]:
                idx = decoded.find(marker, elf_start)
                if 0 <= idx < elf_end:
                    elf_end = idx
            return decoded[elf_start:elf_end], ".coredump"
    except Exception:
        pass

    # ELF not found — save the raw base64 block between markers
    return base64_block.encode("utf-8"), ".coredump.base64"


def timestamp():
    return datetime.now().strftime("%H:%M:%S.%f")[:-3]


def make_log_filename(log_dir: str) -> Path:
    ts = datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
    return Path(log_dir) / f"serial_{ts}.log"


def make_broadcast_filename(log_dir: str) -> Path:
    ts = datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
    return Path(log_dir) / f"broadcast_{ts}.log"


def monitor_port(port, timeout=30, log_dir=DEFAULT_LOG_DIR, no_reset=False,
                 no_log=False, log_path=None, verbose=False,
                 broadcast_dir=None, no_broadcast_log=False):
    classifier = SerialClassifier(max_last_lines=5)
    boot_detector = BootDetector()
    log_file = None
    broadcast_log_file = None
    broadcast_log_path = None
    if not no_log:
        log_dir = Path(log_dir)
        log_dir.mkdir(parents=True, exist_ok=True)
        log_file = make_log_filename(str(log_dir))
        counter = 1
        while log_file.exists():
            stem = f"serial_{datetime.now().strftime('%Y-%m-%d_%H-%M-%S')}_{counter}"
            log_file = log_dir / f"{stem}.log"
            counter += 1
        log_path = log_file

    if not no_log and not no_broadcast_log:
        bc_dir = Path(broadcast_dir) if broadcast_dir else Path(log_dir)
        bc_dir.mkdir(parents=True, exist_ok=True)
        broadcast_log_file = make_broadcast_filename(str(bc_dir))
        counter = 1
        while broadcast_log_file.exists():
            stem = f"broadcast_{datetime.now().strftime('%Y-%m-%d_%H-%M-%S')}_{counter}"
            broadcast_log_file = bc_dir / f"{stem}.log"
            counter += 1
        broadcast_log_path = broadcast_log_file

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
        ser.dtr = False
        ser.rts = False

        def writeline(line: str, always_visible: bool = False, end: str = "\n"):
            line = sanitize_line(line)
            if always_visible or verbose:
                try:
                    print(line, end=end, flush=True)
                except UnicodeEncodeError:
                    safe = line.encode("utf-8", errors="replace").decode("utf-8", errors="replace")
                    print(safe, end=end, flush=True)
            if log_file is not None:
                try:
                    with open(log_file, "a", encoding="utf-8") as f:
                        f.write(line + "\n")
                except OSError:
                    pass

        def write_broadcast_line(line: str, ts: str):
            line = sanitize_line(line)
            prefixed = f"[{ts}] {line}"
            if verbose:
                try:
                    print(prefixed, flush=True)
                except UnicodeEncodeError:
                    safe = prefixed.encode("utf-8", errors="replace").decode("utf-8", errors="replace")
                    print(safe, flush=True)
            if broadcast_log_file is not None:
                try:
                    with open(broadcast_log_file, "a", encoding="utf-8") as f:
                        f.write(prefixed + "\n")
                except OSError:
                    pass

        writeline(f"=== Connected to ESP32-S3 on {port} @ {BAUDRATE} baud ===", always_visible=True)

        if not no_reset:
            writeline("=== Resetting ESP32-S3 (DTR pulse) ===", always_visible=True)
            ser.dtr = False
            ser.rts = False
            time.sleep(0.1)
            ser.dtr = True
            ser.rts = True
            time.sleep(0.1)
            ser.dtr = False
            ser.rts = False
            time.sleep(1.0)

        # Flush ROM bootloader binary garbage before reading.
        time.sleep(0.3)
        ser.reset_input_buffer()

        if log_file:
            writeline(f"=== Logging to {log_file} ===", always_visible=True)
        if broadcast_log_file:
            writeline(f"=== Broadcast log: {broadcast_log_file} ===", always_visible=True)

        deadline = time.time() + timeout
        buf = ""
        dedup = DedupTracker()
        all_raw_data = bytearray()
        coredump_buffer = bytearray()
        capturing_coredump = False

        def emit(line: str, ts: str):
            for out in dedup.add(line, ts):
                writeline(out)
        def flush():
            for out in dedup.flush():
                writeline(out)

        while time.time() < deadline:
            try:
                if ser.in_waiting:
                    raw_data = ser.read(ser.in_waiting)
                    all_raw_data.extend(raw_data)
                    if capturing_coredump:
                        coredump_buffer.extend(raw_data)
                    data = raw_data.decode("utf-8", errors="replace")
                    buf += data
                    while "\n" in buf:
                        line, buf = buf.split("\n", 1)
                        line = line.strip("\r")
                        if not line:
                            continue

                        # Core dump capture state machine
                        capturing_coredump = _update_capture_state(
                            capturing_coredump, line
                        )

                        # Filter out binary garbage from ROM bootloader preamble.
                        if line[0].isdigit():
                            continue
                        n_alpha = sum(c.isalpha() for c in line)
                        if n_alpha < len(line) * 0.3:
                            continue

                        classifier.add_line(line)
                        boot_detector.add_line(line)
                        if is_broadcast_line(line):
                            write_broadcast_line(line, timestamp())
                        else:
                            emit(line, timestamp())
                else:
                    time.sleep(0.01)
            except serial.SerialException:
                flush()
                writeline("=== Connection lost ===", always_visible=True)
                break
            except KeyboardInterrupt:
                flush()
                writeline("\n=== Exiting ===", always_visible=True)
                break

        flush()

        if boot_detector.reboot_detected:
            writeline(
                f"WARNING: ESP32-S3 reboot detected (BOOT OK: seen {boot_detector.count} times)",
                always_visible=True
            )

        ser.close()
        writeline("=== Port closed ===", always_visible=True)

        # Extract and save core dump payload (if any) from captured raw data.
        # Only saves between CORE DUMP START/END markers — never the entire log.
        if coredump_buffer and log_path:
            payload, suffix = _extract_coredump(coredump_buffer)
            if payload and suffix:
                dumps_dir = PROJECT_DIR / "dumps"
                dumps_dir.mkdir(parents=True, exist_ok=True)

                dump_path = dumps_dir / (log_path.stem + suffix)
                counter = 1
                while dump_path.exists():
                    dump_path = dumps_dir / f"{log_path.stem}_{counter}{suffix}"
                    counter += 1

                if suffix == ".coredump":
                    dump_path.write_bytes(payload)
                    writeline(f"=== Core dump saved to {dump_path} ({len(payload)} bytes) ===",
                              always_visible=True)
                else:
                    dump_path.write_text(payload.decode("utf-8", errors="replace"))
                    writeline(f"=== Raw core dump base64 saved to {dump_path} ({len(payload)} bytes) ===",
                              always_visible=True)

        if broadcast_log_file and broadcast_log_file.exists() and broadcast_log_file.stat().st_size == 0:
            broadcast_log_file.unlink()
        if log_file and log_file.exists() and log_file.stat().st_size == 0:
            log_file.unlink()

    except serial.SerialException as e:
        msg = f"[{timestamp()}] Error: Cannot open {port}: {e}"
        safe = msg.encode("utf-8", errors="replace").decode("utf-8", errors="ignore")
        sys.stdout.buffer.write((safe + "\n").encode("utf-8", errors="replace"))
        sys.stdout.buffer.flush()
        return 1

    if boot_detector.reboot_detected:
        msg = f"WARNING: ESP32-S3 reboot detected (BOOT OK: seen {boot_detector.count} times)"
        if log_path:
            msg += f" — run: crash_analyzer.py < {log_path}"
        print(msg, flush=True)
        return 2
    else:
        if log_path and log_path.exists() and log_path.stat().st_size > 0:
            print(f"Log: {log_path}", flush=True)
        if broadcast_log_path and broadcast_log_path.exists() and broadcast_log_path.stat().st_size > 0:
            print(f"Broadcast log: {broadcast_log_path}", flush=True)
        print(classifier.result_message(), flush=True)
        return classifier.result()


def main():
    parser = argparse.ArgumentParser(description="ESP32-S3 serial monitor")
    parser.add_argument("port", nargs="?", default=None, help="Serial port (auto-detect if omitted)")
    parser.add_argument("--timeout", type=int, default=30, help="Monitor duration in seconds")
    parser.add_argument("--no-reset", action="store_true", help="Skip DTR reset on connect")
    parser.add_argument("--log-dir", default=DEFAULT_LOG_DIR, help="Directory for log files (default: project_root/logs/)")
    parser.add_argument("--no-log", action="store_true", help="Disable log file saving")
    parser.add_argument("--verbose", action="store_true", help="Echo serial output to terminal (default: quiet, log only)")
    parser.add_argument("--broadcast-dir", default=None, help="Directory for broadcast log (default: same as --log-dir)")
    parser.add_argument("--no-broadcast-log", action="store_true", help="Disable broadcast log file saving")
    args = parser.parse_args()

    port = args.port or find_esp32_port()
    if not port:
        print("ERROR: ESP32-S3 not found. Specify port: python scripts/monitor.py /dev/ttyACM0", flush=True)
        return 1

    return monitor_port(port=port, timeout=args.timeout, log_dir=args.log_dir,
                        no_reset=args.no_reset, no_log=args.no_log,
                        verbose=args.verbose,
                        broadcast_dir=args.broadcast_dir,
                        no_broadcast_log=args.no_broadcast_log)


if __name__ == "__main__":
    sys.exit(main())
