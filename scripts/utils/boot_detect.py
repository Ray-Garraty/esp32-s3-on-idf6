"""
Boot marker detection utilities for integration tests.
Pure logic, no I/O. Thread-safe counting of boot markers.
"""

import threading
import time
from collections import deque

BOOT_OK_MARKER = "BOOT OK:"


class BootDetector:
    """Thread-safe counter of BOOT_OK_MARKER occurrences.
    
    Feed every serial line via add_line(). If marker appears more than 
    once, the ESP32 crashed and rebooted mid-test.
    """
    
    def __init__(self):
        self._count = 0
        self._lock = threading.Lock()
    
    @property
    def count(self) -> int:
        with self._lock:
            return self._count
    
    @property
    def reboot_detected(self) -> bool:
        return self.count > 1
    
    def add_line(self, line: str) -> None:
        if BOOT_OK_MARKER in line:
            with self._lock:
                self._count += 1


def wait_for_boot(
    buf: deque,
    detector: BootDetector,
    timeout_s: float = 15,
    log_fn = None,
) -> bool:
    """Read from deque until BOOT_OK_MARKER appears or timeout.
    
    Feeds every popped line into detector. Calls log_fn(line) if provided.
    Returns True if boot marker found within timeout.
    """
    deadline = time.time() + timeout_s
    lines_seen = 0
    while time.time() < deadline:
        while buf:
            line = buf.popleft()
            lines_seen += 1
            detector.add_line(line)
            if log_fn:
                log_fn(line)
            if BOOT_OK_MARKER in line:
                return True
        time.sleep(0.1)
    return False
