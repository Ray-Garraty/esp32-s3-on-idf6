#!/usr/bin/env python3
"""
Regression tests for core dump capture logic in monitor.py.

Tests extracted pure functions:
  - _update_capture_state()
  - _extract_coredump()

Run:  python3 -m unittest scripts/utils/test_coredump_capture.py -v
"""

import unittest
import base64
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from monitor import _update_capture_state, _extract_coredump

# Minimal ELF binary header (12 bytes, ELFCLASS32 + valid ident)
ELF_MAGIC = b'\x7fELF'
TEST_ELF = ELF_MAGIC + b'\x01\x01\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00'
TEST_BASE64 = base64.b64encode(TEST_ELF).decode('ascii')


class TestUpdateCaptureState(unittest.TestCase):
    """_update_capture_state() trigger logic — the core of the fix."""

    # ── Should NOT trigger ──────────────────────────────────────

    def test_boot_init_does_not_trigger(self):
        """False-positive regression: boot-time init must NOT start capture."""
        self.assertFalse(
            _update_capture_state(
                False, "I (873) esp_core_dump_uart: Init core dump to UART"
            )
        )

    def test_normal_log_lines_preserve_state(self):
        """Normal ESP-IDF log output should leave state unchanged."""
        for state in [False, True]:
            for line in [
                "I (30) main: Build: 2026-07-13",
                "W (123) stack_monitor: LOW STACK",
                '{"ts":100,"temp":23.4}',
                "BOOT OK: ecotiter v0.1.0",
                "DBG: step 1 - nvs_flash_init",
            ]:
                self.assertEqual(
                    _update_capture_state(state, line), state,
                    f"State should be preserved for line: {line!r}"
                )

    def test_empty_line_preserves_state(self):
        self.assertTrue(_update_capture_state(True, ""))
        self.assertFalse(_update_capture_state(False, ""))

    # ── Should START capture ────────────────────────────────────

    def test_crash_marker_triggers(self):
        """=== CRASH === from the custom panic handler."""
        self.assertTrue(
            _update_capture_state(False, "=== CRASH ===")
        )

    def test_print_core_dump_triggers(self):
        """Print core dump to uart from ESP-IDF coredump module."""
        self.assertTrue(
            _update_capture_state(False, "I (13391) esp_core_dump_uart: Print core dump to uart...")
        )

    def test_core_dump_start_marker_triggers(self):
        """CORE DUMP START marks the beginning of base64 payload."""
        self.assertTrue(
            _update_capture_state(False, "================= CORE DUMP START =================")
        )

    # ── Should STOP capture ─────────────────────────────────────

    def test_reboot_stops_capture(self):
        self.assertFalse(
            _update_capture_state(True, "Rebooting...")
        )

    def test_esp_rom_stops_capture(self):
        """ESP-ROM: bootloader banner after reset."""
        self.assertFalse(
            _update_capture_state(True, "ESP-ROM:esp32s3-20210327")
        )

    # ── State transitions ───────────────────────────────────────

    def test_trigger_then_stop_then_trigger_again(self):
        """Full lifecycle: crash → reboot → crash."""
        state = False
        state = _update_capture_state(state, "=== CRASH ===")
        self.assertTrue(state)
        state = _update_capture_state(state, "Rebooting...")
        self.assertFalse(state)
        state = _update_capture_state(state, "=== CRASH ===")
        self.assertTrue(state)

    def test_crash_override_reboot(self):
        """CRASH marker takes priority over reboot in the same line."""
        # If both appear in the same line, CRASH wins
        self.assertTrue(
            _update_capture_state(False, "=== CRASH === Rebooting...")
        )

    def test_print_coredump_overrides_reboot(self):
        self.assertTrue(
            _update_capture_state(False, "Print core dump to uart Rebooting...")
        )

    def test_core_dump_start_after_crash(self):
        """Already capturing: CORE DUMP START keeps capture active."""
        self.assertTrue(
            _update_capture_state(True, "================= CORE DUMP START =================")
        )


class TestExtractCoredump(unittest.TestCase):
    """_extract_coredump() — payload isolation from raw UART buffer."""

    def test_no_markers_returns_none(self):
        """No CORE DUMP START/END → no payload."""
        payload, suffix = _extract_coredump(b"I (30) main: normal boot\n")
        self.assertIsNone(payload)
        self.assertIsNone(suffix)

    def test_only_start_no_end_returns_none(self):
        """Only START marker without END → incomplete, no payload."""
        data = b"=== CRASH ===\n================= CORE DUMP START =================\nAAAA\n"
        payload, suffix = _extract_coredump(data)
        self.assertIsNone(payload)
        self.assertIsNone(suffix)

    def test_only_end_no_start_returns_none(self):
        data = b"AAAA\n================= CORE DUMP END =================\n"
        payload, suffix = _extract_coredump(data)
        self.assertIsNone(payload)
        self.assertIsNone(suffix)

    def test_empty_block_between_markers_returns_none(self):
        data = (
            b"================= CORE DUMP START =================\n"
            b"================= CORE DUMP END =================\n"
        )
        payload, suffix = _extract_coredump(data)
        self.assertIsNone(payload)
        self.assertIsNone(suffix)

    def test_whitespace_only_block_returns_none(self):
        data = (
            b"================= CORE DUMP START =================\n"
            b"   \n"
            b"================= CORE DUMP END =================\n"
        )
        payload, suffix = _extract_coredump(data)
        self.assertIsNone(payload)
        self.assertIsNone(suffix)

    def test_base64_block_returns_raw(self):
        """Valid base64 without ELF magic → .coredump.base64."""
        base64_payload = base64.b64encode(b"hello world").decode('ascii')
        data = (
            b"=== CRASH ===\n"
            b"================= CORE DUMP START =================\n"
            + base64_payload.encode('ascii') + b"\n"
            b"================= CORE DUMP END =================\n"
            b"Rebooting...\n"
        )
        payload, suffix = _extract_coredump(data)
        self.assertIsNotNone(payload)
        self.assertEqual(suffix, ".coredump.base64")
        # payload should be the base64 text
        decoded = payload.decode('utf-8')
        self.assertEqual(decoded.strip(), base64_payload)

    def test_elf_magic_detected(self):
        """Valid ELF in decoded base64 → .coredump binary."""
        data = (
            b"=== CRASH ===\n"
            b"exccause=0 pc=0x40000000\n"
            b"================= CORE DUMP START =================\n"
            + TEST_BASE64.encode('ascii') + b"\n"
            b"================= CORE DUMP END =================\n"
            b"Rebooting...\n"
        )
        payload, suffix = _extract_coredump(data)
        self.assertIsNotNone(payload)
        self.assertEqual(suffix, ".coredump")
        self.assertEqual(payload, TEST_ELF)

    def test_elf_with_trailing_data_trimmed(self):
        """Truncate at rst:/ESP-ROM:/Rebooting markers in decoded data."""
        extended = TEST_ELF + b"rst:0x9 (RTCWDT_SYS_RESET)\ntrailing garbage"
        base64_payload = base64.b64encode(extended).decode('ascii')
        data = (
            b"================= CORE DUMP START =================\n"
            + base64_payload.encode('ascii') + b"\n"
            b"================= CORE DUMP END =================\n"
        )
        payload, suffix = _extract_coredump(data)
        self.assertEqual(suffix, ".coredump")
        self.assertEqual(payload, TEST_ELF,
                         "Should trim ELF at first rst: marker")

    def test_realistic_full_sequence(self):
        """End-to-end: crash → dump markers → reboot."""
        base64_payload = base64.b64encode(TEST_ELF + b"rst:0x9").decode('ascii')
        data = (
            b"I (100) main: normal boot\n"
            b"I (873) esp_core_dump_uart: Init core dump to UART\n"
            b'I (200) main: {"ts":100,"temp":23.4}\n'
            b"=== CRASH ===\n"
            b"exccause=0 pc=0x40000000 excvaddr=0x00000000\n"
            b"================= CORE DUMP START =================\n"
            + base64_payload.encode('ascii') + b"\n"
            b"================= CORE DUMP END =================\n"
            b"Rebooting...\n"
            b"ESP-ROM:esp32s3-20210327\n"
        )
        payload, suffix = _extract_coredump(data)
        self.assertEqual(suffix, ".coredump")
        self.assertEqual(payload, TEST_ELF,
                         "Realistic crash sequence should yield valid ELF")


class TestIntegrationSerialClassifier(unittest.TestCase):
    """Verify the existing SerialClassifier still works correctly.

    This ensures our edits to monitor.py didn't break classifier import.
    """

    def test_classifier_imports(self):
        from utils.monitor_classifier import SerialClassifier, ResultCode
        c = SerialClassifier()
        c.add_line("=== CRASH ===")
        self.assertEqual(c.result(), ResultCode.CRASH)

    def test_classifier_still_detects_boot(self):
        from utils.monitor_classifier import SerialClassifier, ResultCode
        c = SerialClassifier()
        c.add_line("BOOT OK:")
        self.assertEqual(c.result(), ResultCode.BOOT_OK)


if __name__ == "__main__":
    unittest.main(verbosity=2)
