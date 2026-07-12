#!/usr/bin/env python3
"""Unit tests for boot_detect module."""

import threading
import time
import unittest
from collections import deque
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from boot_detect import BootDetector, BOOT_OK_MARKER, wait_for_boot


class TestBootDetector(unittest.TestCase):
    def test_starts_at_zero(self):
        d = BootDetector()
        self.assertEqual(d.count, 0)
        self.assertFalse(d.reboot_detected)

    def test_single_boot_detected(self):
        d = BootDetector()
        d.add_line(f"{BOOT_OK_MARKER} ecotiter v0.1.0 [2026-07-12] (git: abc1234)")
        self.assertEqual(d.count, 1)
        self.assertFalse(d.reboot_detected)

    def test_reboot_detected(self):
        d = BootDetector()
        d.add_line(BOOT_OK_MARKER)
        d.add_line("I (30) main: running")
        d.add_line(BOOT_OK_MARKER)
        self.assertEqual(d.count, 2)
        self.assertTrue(d.reboot_detected)

    def test_ignores_other_lines(self):
        d = BootDetector()
        d.add_line("I (30) main: Build: 2026-07-11")
        d.add_line("DBG: step 1 - nvs_flash_init")
        d.add_line('{"ts":100,"temp":23.4}')
        self.assertEqual(d.count, 0)
        self.assertFalse(d.reboot_detected)

    def test_marker_prefix_not_confused(self):
        d = BootDetector()
        d.add_line("BOOT OKAY")   # similar but not exact
        d.add_line("BOOT_OK:")    # underscore, not space
        d.add_line("boot ok:")    # lowercase
        self.assertEqual(d.count, 0)

    def test_marker_substring_safe(self):
        # "BOOT OK:" appears as substring
        d = BootDetector()
        d.add_line(f"prefix {BOOT_OK_MARKER} suffix")
        self.assertEqual(d.count, 1)

    def test_thread_safety(self):
        d = BootDetector()
        errors = []
        def worker():
            for _ in range(1000):
                d.add_line(BOOT_OK_MARKER)
                d.add_line("noise line")
        threads = [threading.Thread(target=worker) for _ in range(4)]
        for t in threads:
            t.start()
        for t in threads:
            t.join()
        self.assertEqual(d.count, 4000)
        self.assertTrue(d.reboot_detected)

    def test_wait_for_boot_found(self):
        buf = deque()
        d = BootDetector()
        buf.append("ESP-ROM:esp32s3")
        buf.append(f"{BOOT_OK_MARKER} test v1.0 [now] (git: xyz)")
        result = wait_for_boot(buf, d, timeout_s=5)
        self.assertTrue(result)
        self.assertEqual(d.count, 1)

    def test_wait_for_boot_timeout(self):
        buf = deque()
        d = BootDetector()
        buf.append("ESP-ROM:esp32s3")
        buf.append("I (30) main: running")
        result = wait_for_boot(buf, d, timeout_s=0.5)
        self.assertFalse(result)
        self.assertEqual(d.count, 0)

    def test_wait_for_boot_with_logger(self):
        buf = deque()
        d = BootDetector()
        logged = []
        buf.append(f"{BOOT_OK_MARKER} v1.0")
        result = wait_for_boot(buf, d, timeout_s=5, log_fn=lambda l: logged.append(l))
        self.assertTrue(result)
        self.assertIn(f"{BOOT_OK_MARKER} v1.0", logged)


if __name__ == "__main__":
    unittest.main(verbosity=2)
