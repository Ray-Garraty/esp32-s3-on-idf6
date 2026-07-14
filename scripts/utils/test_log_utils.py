#!/usr/bin/env python3
"""
Unit tests for log_utils sanitize_line.

Run:  python3 -m unittest scripts/utils/test_log_utils.py -v
"""

import unittest
import io
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from utils.log_utils import sanitize_line, write_sanitized


class TestSanitizeLine(unittest.TestCase):
    def test_keeps_printable_ascii(self):
        self.assertEqual(sanitize_line("hello world"), "hello world")

    def test_keeps_printable_unicode(self):
        self.assertEqual(sanitize_line("тест привет"), "тест привет")

    def test_keeps_newline_tab_cr(self):
        self.assertEqual(sanitize_line("a\nb\tc\rd"), "a\nb\tc\rd")

    def test_removes_null_bytes(self):
        self.assertEqual(sanitize_line("BOOT\x00 OK:"), "BOOT OK:")

    def test_removes_bell(self):
        self.assertEqual(sanitize_line("a\x07b"), "ab")

    def test_removes_escape(self):
        self.assertEqual(sanitize_line("a\x1bb"), "ab")

    def test_removes_various_control_chars(self):
        raw = "a\x00\x01\x02\x03b"
        self.assertEqual(sanitize_line(raw), "ab")

    def test_empty_string(self):
        self.assertEqual(sanitize_line(""), "")

    def test_only_control_chars(self):
        self.assertEqual(sanitize_line("\x00\x01\x02"), "")

    def test_realistic_rom_output(self):
        raw = "ESP-ROM\x00:esp32s3\x00-20210327\x00"
        expected = "ESP-ROM:esp32s3-20210327"
        self.assertEqual(sanitize_line(raw), expected)

    def test_realistic_log_line(self):
        raw = "I (476) cpu_start\x00: Single core mode\n"
        expected = "I (476) cpu_start: Single core mode\n"
        self.assertEqual(sanitize_line(raw), expected)


class TestWriteSanitized(unittest.TestCase):
    def setUp(self):
        self.buf = io.StringIO()

    def test_writes_sanitized_line(self):
        write_sanitized(self.buf, "hello\x00world")
        self.assertEqual(self.buf.getvalue(), "helloworld\n")

    def test_writes_printable_unchanged(self):
        write_sanitized(self.buf, "hello world")
        self.assertEqual(self.buf.getvalue(), "hello world\n")

    def test_preserves_tab(self):
        write_sanitized(self.buf, "a\tb")
        self.assertEqual(self.buf.getvalue(), "a\tb\n")


if __name__ == "__main__":
    unittest.main(verbosity=2)
