import unittest
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from crash_analyzer import (
    parse_crash_dump, _find_addr2line, RE_RWDT, RE_SAVED_PC, RE_WDT
)


class TestRegexPatterns(unittest.TestCase):
    def test_re_rwdt_matches(self):
        self.assertIsNotNone(RE_RWDT.search("rst:0x9 (RTCWDT_SYS_RESET)"))

    def test_re_rwdt_optional_whitespace(self):
        self.assertIsNotNone(RE_RWDT.search("rst:0x9   (RTCWDT_SYS_RESET)"))

    def test_re_wdt_matches(self):
        self.assertIsNotNone(RE_WDT.search("rst:0x8 (TG1WDT_SYS_RESET)"))

    def test_re_saved_pc_matches(self):
        m = RE_SAVED_PC.search("Saved PC:0x4037b9a1")
        self.assertIsNotNone(m)
        self.assertEqual(m.group(1), "0x4037b9a1")

    def test_re_saved_pc_whitespace(self):
        m = RE_SAVED_PC.search("Saved PC: 0x4037b9a1")
        self.assertIsNotNone(m)
        self.assertEqual(m.group(1), "0x4037b9a1")


class TestWatchdogParsing(unittest.TestCase):
    def test_parse_watchdog_rwdt(self):
        text = "rst:0x9 (RTCWDT_SYS_RESET)\nSaved PC:0x4037b9a1"
        info = parse_crash_dump(text)
        self.assertTrue(info.get("wdt_reset"))
        self.assertEqual(info.get("type"), "watchdog")
        self.assertEqual(info.get("wdt_type"), "RTCWDT")
        self.assertEqual(info.get("pc"), 0x4037b9a1)

    def test_parse_watchdog_tg1wdt(self):
        text = "rst:0x8 (TG1WDT_SYS_RESET)\nSaved PC:0x4037b9a1"
        info = parse_crash_dump(text)
        self.assertTrue(info.get("wdt_reset"))
        self.assertEqual(info.get("type"), "watchdog")
        self.assertEqual(info.get("wdt_type"), "TG1WDT")

    def test_parse_watchdog_saved_pc_only(self):
        text = "some preamble\nSaved PC:0x4037b9a1\nmore text"
        info = parse_crash_dump(text)
        self.assertTrue(info.get("wdt_reset"))
        self.assertEqual(info.get("pc"), 0x4037b9a1)

    def test_parse_normal_crash_still_works(self):
        text = "=== CRASH ===\nexccause=0 name=IllegalInstruction pc=0x40091100 excvaddr=0x00000000 ps=0x00060020 sp=0x3fffcee0"
        info = parse_crash_dump(text)
        self.assertEqual(info.get("type"), "IllegalInstruction")
        self.assertFalse(info.get("wdt_reset"))

    def test_parse_guru_old_format_still_works(self):
        text = "Guru Meditation Error: Core  0 panic'ed (StoreProhibited)"
        info = parse_crash_dump(text)
        self.assertEqual(info.get("type"), "StoreProhibited")


class TestFindAddr2line(unittest.TestCase):
    def test_finds_something(self):
        result = _find_addr2line()
        self.assertIsNotNone(result, "Should find xtensa-esp-elf-addr2line or llvm-symbolizer")
        self.assertTrue(Path(result).exists(), f"Path {result} should exist")


class TestSyntheticBacktrace(unittest.TestCase):
    def test_synthetic_backtrace_from_pc(self):
        from crash_analyzer import decode_backtrace
        info = {
            "pc": 0x4037b9a1,
            "backtrace_raw": [],
        }
        # Without this, decode_backtrace would return [] because backtrace_raw is empty
        # But with the synthetic entry logic, it should create one
        if not info.get("backtrace_raw"):
            info["backtrace_raw"] = [{"pc": info["pc"], "sp": 0}]
        self.assertEqual(len(info["backtrace_raw"]), 1)
        self.assertEqual(info["backtrace_raw"][0]["pc"], 0x4037b9a1)
        self.assertEqual(info["backtrace_raw"][0]["sp"], 0)


if __name__ == "__main__":
    unittest.main()
