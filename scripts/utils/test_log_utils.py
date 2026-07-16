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
from utils.log_utils import is_broadcast_line, sanitize_line, write_sanitized


class TestIsBroadcastLine(unittest.TestCase):
    """Unit tests for is_broadcast_line."""

    def test_valid_broadcast(self):
        self.assertTrue(is_broadcast_line(
            '{"ts":1234,"temp":23.5,"mv":1500.0,"vlv":"in",'
            '"brt":{"sts":"idle","vl":null,"spd":0.00}}'
        ))

    def test_valid_broadcast_working_state(self):
        self.assertTrue(is_broadcast_line(
            '{"ts":5678,"temp":30.0,"mv":1500.0,"vlv":"out",'
            '"brt":{"sts":"working","vl":5.00,"spd":10.00}}'
        ))

    def test_valid_broadcast_null_temp(self):
        self.assertTrue(is_broadcast_line(
            '{"ts":999,"temp":null,"mv":1500.0,"vlv":"in",'
            '"brt":{"sts":"idle","vl":null,"spd":0.00}}'
        ))

    def test_valid_broadcast_max_tick(self):
        self.assertTrue(is_broadcast_line(
            '{"ts":4294967295,"temp":25.0,"mv":1500.0,"vlv":"in",'
            '"brt":{"sts":"idle","vl":null,"spd":0.00}}'
        ))

    def test_esp_idf_log_line(self):
        self.assertFalse(is_broadcast_line(
            "I (476) cpu_start: Single core mode"
        ))

    def test_boot_ok_line(self):
        self.assertFalse(is_broadcast_line(
            "BOOT OK: ecotiter v1.0 [2026-07-16] (git: abc1234)"
        ))

    def test_json_command(self):
        self.assertFalse(is_broadcast_line(
            '{"cmd":"setPosition","id":1,"params":{"position":"out"}}'
        ))

    def test_json_response(self):
        self.assertFalse(is_broadcast_line(
            '{"id":1,"status":"ok","result":{"position":"out"}}'
        ))

    def test_empty_string(self):
        self.assertFalse(is_broadcast_line(""))

    def test_binary_garbage(self):
        self.assertFalse(is_broadcast_line("\x00\x01\x02\x03"))


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
