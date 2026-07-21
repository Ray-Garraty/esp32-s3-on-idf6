#!/usr/bin/env python3
"""Unit tests for check_arch module — architecture dependency checker.

Tests each pure function in isolation, then end-to-end via subprocess.
Zero external dependencies — stdlib only (unittest, json, subprocess, pathlib).
"""

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from check_arch import (
    ComponentDef,
    Config,
    FileCache,
    IncludeDirective,
    SrpResult,
    Violation,
    _strip_line_stateful,
    baseline_match,
    classify_file,
    count_fan_out,
    count_loc,
    count_public_methods,
    detect_cycles_from_edges,
    find_component_for_file,
    format_json_output,
    format_violation_kv,
    generate_baseline_json,
    is_local_include,
    parse_includes,
    preprocess_source,
    resolve_component,
    run_pass1,
    strip_block_comments,
    strip_line_comments,
)

# ---------------------------------------------------------------------------
# Test helpers
# ---------------------------------------------------------------------------

TEST_VPM: list[tuple[str, str]] = [
    ("domain/", "domain"),
    ("infrastructure/", "infrastructure"),
    ("freertos/", "freertos"),
    ("esp_wifi", "esp_wifi"),
    ("esp_", "esp_system"),
]

TEST_GENERATED: set[str] = {"sdkconfig.h"}


def make_include_directive(
    source_file: str = "/tmp/test.cpp",
    line_no: int = 1,
    include_path: str = "foo.h",
    include_type: str = "quoted",
    conditional: bool = False,
) -> IncludeDirective:
    """Factory helper for creating IncludeDirective instances."""
    return IncludeDirective(
        source_file=source_file,
        line_no=line_no,
        include_path=include_path,
        include_type=include_type,
        conditional=conditional,
    )


def make_violation(
    is_new: bool = True,
    source: str = "test",
    target: str = "other",
    file_type: str = "PRIVATE_SOURCE",
    include_path: str = "domain/types.hpp",
    include_type: str = "quoted",
    conditional: bool = False,
) -> Violation:
    """Factory helper for creating Violation instances."""
    inc = make_include_directive(
        source_file="/tmp/test.cpp",
        line_no=10,
        include_path=include_path,
        include_type=include_type,
        conditional=conditional,
    )
    v = Violation(
        include_dir=inc,
        source_component=source,
        target_component=target,
        file_type=file_type,
    )
    v.is_new = is_new
    if not is_new:
        v.baseline_info = {"severity": "low", "tier": "B", "sunset": "never"}
    return v


def make_minimal_config(
    vpm: list[tuple[str, str]] | None = None,
    generated_headers: set[str] | None = None,
) -> Config:
    """Build a Config object with minimal / test data."""
    if vpm is None:
        vpm = TEST_VPM
    if generated_headers is None:
        generated_headers = set()
    data = {
        "components": {
            "test": {"path": "components/test", "allowed": ["*"]},
        },
        "virtual_prefix": [
            {"prefix": p, "tag": t} for p, t in vpm
        ],
        "generated_headers": sorted(generated_headers),
        "srp_thresholds": {
            "max_loc": 800,
            "max_fan_out": 12,
            "max_public_methods": 25,
        },
    }
    return Config(data)


REPO_ROOT = Path(__file__).resolve().parent.parent.parent


# ===================================================================
# 1.  strip_line_comments
# ===================================================================

class TestStripLineComments(unittest.TestCase):
    """Tests for strip_line_comments() — removes // comments safely."""

    def test_removes_line_comment(self) -> None:
        """Line comment after code is stripped."""
        result = strip_line_comments('int x; // comment')
        self.assertEqual(result, 'int x; ')

    def test_string_literal_with_slashes_preserved(self) -> None:
        """A // inside a string literal is NOT treated as a comment."""
        result = strip_line_comments('char* s = "//not a comment";')
        self.assertEqual(result, 'char* s = "//not a comment";')

    def test_entire_line_is_comment(self) -> None:
        """Line that is only a // comment becomes empty."""
        result = strip_line_comments('// #include "foo.h"')
        self.assertEqual(result, '')


# ===================================================================
# 2.  strip_block_comments
# ===================================================================

class TestStripBlockComments(unittest.TestCase):
    """Tests for strip_block_comments() — removes /* */ comments safely."""

    def test_removes_block_comment_inline(self) -> None:
        """Block comment in the middle of code is removed."""
        result = strip_block_comments('int x; /* comment */ int y;')
        self.assertEqual(result, 'int x;  int y;')

    def test_string_literal_with_block_preserved(self) -> None:
        """A /* inside a string literal is NOT treated as a comment."""
        result = strip_block_comments('char* s = "/* not a comment */";')
        self.assertEqual(result, 'char* s = "/* not a comment */";')

    def test_multiline_block_comment(self) -> None:
        """Multi-line block comment is removed entirely (newline consumed)."""
        result = strip_block_comments('/* line1\n line2 */ int x;')
        self.assertEqual(result, ' int x;')


# ===================================================================
# 3.  parse_includes
# ===================================================================

class TestParseIncludes(unittest.TestCase):
    """Tests for parse_includes() — extracts #include directives."""

    def test_simple_quoted_include(self) -> None:
        """A single quoted include is parsed correctly."""
        lines = ['#include "foo.h"']
        line_map = [1]
        includes = parse_includes(lines, line_map, Path("/tmp/test.cpp"))
        self.assertEqual(len(includes), 1)
        self.assertEqual(includes[0].include_path, "foo.h")
        self.assertEqual(includes[0].include_type, "quoted")
        self.assertEqual(includes[0].line_no, 1)

    def test_simple_angled_include(self) -> None:
        """A single angle-bracket include is parsed correctly."""
        lines = ['#include <cstdint>']
        line_map = [1]
        includes = parse_includes(lines, line_map, Path("/tmp/test.cpp"))
        self.assertEqual(len(includes), 1)
        self.assertEqual(includes[0].include_path, "cstdint")
        self.assertEqual(includes[0].include_type, "angled")

    def test_multiple_includes(self) -> None:
        """Multiple includes on separate lines are all collected."""
        lines = ['#include "a.h"', '#include "b.h"']
        line_map = [1, 2]
        includes = parse_includes(lines, line_map, Path("/tmp/test.cpp"))
        self.assertEqual(len(includes), 2)
        self.assertEqual(includes[0].include_path, "a.h")
        self.assertEqual(includes[1].include_path, "b.h")

    def test_conditional_include(self) -> None:
        """Include inside #ifdef is marked as conditional."""
        lines = [
            '#ifdef X',
            '#include "foo.h"',
            '#endif',
        ]
        line_map = [1, 2, 3]
        includes = parse_includes(lines, line_map, Path("/tmp/test.cpp"))
        self.assertEqual(len(includes), 1)
        self.assertTrue(includes[0].conditional)

    def test_no_includes(self) -> None:
        """File with no includes returns an empty list."""
        lines = ['int x = 0;']
        line_map = [1]
        includes = parse_includes(lines, line_map, Path("/tmp/test.cpp"))
        self.assertEqual(len(includes), 0)


# ===================================================================
# 4.  resolve_component
# ===================================================================

class TestResolveComponent(unittest.TestCase):
    """Tests for resolve_component() — maps include paths to component tags."""

    def test_maps_domain_path(self) -> None:
        """Include starting with domain/ resolves to domain."""
        result = resolve_component("domain/types.hpp", TEST_VPM, set())
        self.assertEqual(result, "domain")

    def test_maps_freertos_path(self) -> None:
        """Include starting with freertos/ resolves to freertos."""
        result = resolve_component("freertos/FreeRTOS.h", TEST_VPM, set())
        self.assertEqual(result, "freertos")

    def test_specific_before_generic(self) -> None:
        """Specific prefix 'esp_wifi' matches before generic 'esp_'."""
        result = resolve_component("esp_wifi.h", TEST_VPM, set())
        self.assertEqual(result, "esp_wifi")

    def test_generic_fallback(self) -> None:
        """'esp_timer.h' matches generic 'esp_' → esp_system."""
        result = resolve_component("esp_timer.h", TEST_VPM, set())
        self.assertEqual(result, "esp_system")

    def test_generated_header_skipped(self) -> None:
        """Generated headers (e.g. sdkconfig.h) return None."""
        result = resolve_component("sdkconfig.h", TEST_VPM, TEST_GENERATED)
        self.assertIsNone(result)

    def test_unknown_path_unresolved(self) -> None:
        """Unknown prefix returns UNRESOLVED."""
        result = resolve_component("unknown/foo.h", TEST_VPM, set())
        self.assertEqual(result, "UNRESOLVED")


# ===================================================================
# 5.  classify_file
# ===================================================================

class TestClassifyFile(unittest.TestCase):
    """Tests for classify_file() — PUBLIC_HEADER vs PRIVATE_SOURCE."""

    def test_public_header(self) -> None:
        """File under include/ dir with .hpp suffix is PUBLIC_HEADER."""
        result = classify_file(
            Path("components/domain/include/domain/types.hpp"),
            Path("components/domain"),
        )
        self.assertEqual(result, "PUBLIC_HEADER")

    def test_private_source(self) -> None:
        """File under src/ with .cpp suffix is PRIVATE_SOURCE."""
        result = classify_file(
            Path("components/domain/src/log_buffer.cpp"),
            Path("components/domain"),
        )
        self.assertEqual(result, "PRIVATE_SOURCE")

    def test_internal_header(self) -> None:
        """File under src/ with .hpp suffix is PRIVATE_SOURCE."""
        result = classify_file(
            Path("components/domain/src/internal.hpp"),
            Path("components/domain"),
        )
        self.assertEqual(result, "PRIVATE_SOURCE")


# ===================================================================
# 6.  count_loc
# ===================================================================

class TestCountLoc(unittest.TestCase):
    """Tests for count_loc() — counts physical lines of code."""

    def test_counts_non_comment_lines(self) -> None:
        """Only non-comment, non-blank lines are counted."""
        lines = ["int x;", "// comment", "int y;"]
        result = count_loc(lines, Path("/tmp/test.cpp"))
        self.assertEqual(result, 2)

    def test_blank_and_comment_only_lines(self) -> None:
        """Blank lines and comment-only lines are excluded."""
        lines = ["", "  ", "// comment"]
        result = count_loc(lines, Path("/tmp/test.cpp"))
        self.assertEqual(result, 0)

    def test_block_comment_with_code_same_line(self) -> None:
        """Line with block comment and code on same line is counted."""
        lines = ["/* block */ int x;"]
        result = count_loc(lines, Path("/tmp/test.cpp"))
        self.assertEqual(result, 1)


# ===================================================================
# 7.  count_fan_out
# ===================================================================

class TestCountFanOut(unittest.TestCase):
    """Tests for count_fan_out() — counts unique resolved targets."""

    def test_two_different_targets(self) -> None:
        """Two includes targeting different components → fan-out 2."""
        config = make_minimal_config()
        includes = [
            make_include_directive(include_path="domain/types.hpp"),
            make_include_directive(include_path="freertos/FreeRTOS.h"),
        ]
        result = count_fan_out(includes, config)
        self.assertEqual(result, 2)

    def test_two_includes_same_target(self) -> None:
        """Two includes targeting the same component → fan-out 1."""
        config = make_minimal_config()
        includes = [
            make_include_directive(include_path="domain/types.hpp"),
            make_include_directive(include_path="domain/foo.hpp"),
        ]
        result = count_fan_out(includes, config)
        self.assertEqual(result, 1)

    def test_with_stdlib_excluded(self) -> None:
        """Stdlib angled includes are excluded from fan-out."""
        config = make_minimal_config()
        includes = [
            make_include_directive(include_path="domain/types.hpp"),
            make_include_directive(include_path="freertos/FreeRTOS.h"),
            make_include_directive(
                include_path="cstdint",
                include_type="angled",
            ),
        ]
        result = count_fan_out(includes, config)
        self.assertEqual(result, 2)


# ===================================================================
# 8.  count_public_methods
# ===================================================================

class TestCountPublicMethods(unittest.TestCase):
    """Tests for count_public_methods() — counts methods in public sections."""

    def test_one_public_method(self) -> None:
        """One method declaration inside public: section is counted."""
        lines = ["public:", "void foo();"]
        result = count_public_methods(lines)
        self.assertEqual(result, 1)

    def test_method_in_private_not_counted(self) -> None:
        """Method inside private: section is not counted."""
        lines = ["private:", "void foo();"]
        result = count_public_methods(lines)
        self.assertEqual(result, 0)

    def test_if_statement_not_counted(self) -> None:
        """Control flow statement with ( in public section is not counted."""
        lines = ["public:", "if (x) {}"]
        result = count_public_methods(lines)
        self.assertEqual(result, 0)

    def test_two_public_methods(self) -> None:
        """Two method declarations in public section → count 2."""
        lines = ["public:", "void foo();", "void bar();"]
        result = count_public_methods(lines)
        self.assertEqual(result, 2)


# ===================================================================
# 9.  detect_cycles_from_edges
# ===================================================================

class TestDetectCycles(unittest.TestCase):
    """Tests for detect_cycles_from_edges() — finds cycles in dependency graph."""

    def test_no_cycle_in_dag(self) -> None:
        """A simple DAG produces no cycles."""
        adj = {"A": {"B"}, "B": {"C"}}
        project_comps = {"A", "B", "C"}
        cycles = detect_cycles_from_edges(adj, project_comps)
        self.assertEqual(len(cycles), 0)

    def test_simple_cycle_detected(self) -> None:
        """A↔B cycle is detected."""
        adj = {"A": {"B"}, "B": {"A"}}
        project_comps = {"A", "B"}
        cycles = detect_cycles_from_edges(adj, project_comps)
        self.assertEqual(len(cycles), 1)
        self.assertIn("A", cycles[0])
        self.assertIn("B", cycles[0])
        self.assertEqual(cycles[0][0], cycles[0][-1])

    def test_self_loop_excluded(self) -> None:
        """Self-loop (A→A) is excluded from cycle detection."""
        adj = {"A": {"A"}}
        project_comps = {"A"}
        cycles = detect_cycles_from_edges(adj, project_comps)
        self.assertEqual(len(cycles), 0)


# ===================================================================
# 10.  baseline_match
# ===================================================================

class TestBaselineMatch(unittest.TestCase):
    """Tests for baseline_match() — checks include against baseline."""

    def test_file_and_include_in_baseline(self) -> None:
        """Known file + include returns the baseline entry."""
        baseline = {
            "/tmp/test.cpp": {
                "includes": ["foo.h"],
                "severity": "low",
                "tier": "B",
                "sunset": "never",
            },
        }
        result = baseline_match(Path("/tmp/test.cpp"), "foo.h", baseline)
        self.assertIsNotNone(result)
        self.assertEqual(result["severity"], "low")

    def test_file_not_in_baseline(self) -> None:
        """Unknown file returns None."""
        baseline: dict = {}
        result = baseline_match(Path("/tmp/test.cpp"), "foo.h", baseline)
        self.assertIsNone(result)


# ===================================================================
# 11.  format_violation_kv
# ===================================================================

class TestFormatViolationKv(unittest.TestCase):
    """Tests for format_violation_kv() — key=value output formatting."""

    def test_new_violation_first_line(self) -> None:
        """New violation output starts with status=VIOLATION."""
        v = make_violation(is_new=True)
        output = format_violation_kv(v)
        first_line = output.split('\n')[0]
        self.assertEqual(first_line, "status=VIOLATION")

    def test_baselined_violation_first_line(self) -> None:
        """Baselined violation output starts with status=BASELINED."""
        v = make_violation(is_new=False)
        output = format_violation_kv(v)
        first_line = output.split('\n')[0]
        self.assertEqual(first_line, "status=BASELINED")


# ===================================================================
# 12.  _strip_line_stateful
# ===================================================================

class TestStripLineStateful(unittest.TestCase):
    """Tests for _strip_line_stateful() — line-by-line block comment removal."""

    def test_no_comment_passthrough(self) -> None:
        """Line without comments passes through unchanged."""
        cleaned, in_block = _strip_line_stateful("int x;", False)
        self.assertEqual(cleaned, "int x;")
        self.assertFalse(in_block)

    def test_single_line_block_comment(self) -> None:
        """Single-line /* */ comment is removed."""
        cleaned, in_block = _strip_line_stateful("int x; /* comment */ int y;", False)
        self.assertEqual(cleaned, "int x;  int y;")
        self.assertFalse(in_block)

    def test_block_comment_starts_continues(self) -> None:
        """Block comment that starts but doesn't end sets in_block=True."""
        cleaned, in_block = _strip_line_stateful("int x; /* start", False)
        self.assertTrue(in_block)

    def test_block_comment_ends(self) -> None:
        """Block comment that started on previous line is closed."""
        cleaned, in_block = _strip_line_stateful(" end */ int y;", True)
        self.assertEqual(cleaned, " int y;")
        self.assertFalse(in_block)


# ===================================================================
# 13.  preprocess_source line numbering (P0-3)
# ===================================================================

class TestPreprocessSourceLineNumbers(unittest.TestCase):
    """Tests for preprocess_source — preserves line numbers with block comments."""

    def test_block_comment_preserves_line_numbers(self) -> None:
        """Multi-line block comment does not collapse line numbers."""
        content = "/* line 1\n   line 2 */\n#include \"nonexistent/foo.hpp\""
        with tempfile.NamedTemporaryFile(mode='w', suffix='.cpp', delete=False) as f:
            f.write(content)
            tmp = Path(f.name)
        try:
            lines, line_map = preprocess_source(tmp)
            # 3 original lines (no trailing newline = still 3)
            self.assertEqual(len(lines), 3)
            self.assertEqual(line_map, [1, 2, 3])
            # The include should be on line 3 (index 2)
            self.assertIn('nonexistent/foo.hpp', lines[2])
        finally:
            tmp.unlink(missing_ok=True)


# ===================================================================
# 14.  baseline_match with absolute paths (P0-4)
# ===================================================================

class TestBaselineMatchAbsolutePath(unittest.TestCase):
    """Tests for baseline_match — absolute path normalization."""

    def test_absolute_path_matches_baseline(self) -> None:
        """Absolute path to a file resolves to the relative baseline key."""
        baseline = {
            "components/domain/include/domain/log_buffer.hpp": {
                "includes": ["freertos/FreeRTOS.h"],
                "severity": "high",
                "tier": "A",
                "sunset": "2026-10-01",
            },
        }
        abs_path = Path.cwd() / "components/domain/include/domain/log_buffer.hpp"
        result = baseline_match(abs_path, "freertos/FreeRTOS.h", baseline)
        self.assertIsNotNone(result)
        self.assertEqual(result["severity"], "high")

    def test_absolute_path_not_in_baseline(self) -> None:
        """Absolute path to a file not in baseline returns None."""
        baseline: dict = {}
        abs_path = Path.cwd() / "components/domain/include/domain/log_buffer.hpp"
        result = baseline_match(abs_path, "freertos/FreeRTOS.h", baseline)
        self.assertIsNone(result)


# ===================================================================
# 15.  generate_baseline_json Tier A/B (P0-5)
# ===================================================================

class TestGenerateBaselineJson(unittest.TestCase):
    """Tests for generate_baseline_json — severity/tier/sunset per file type."""

    def test_public_header_gets_tier_a(self) -> None:
        """PUBLIC_HEADER violation gets severity=high, tier=A, sunset=2026-10-01."""
        inc = make_include_directive()
        v = Violation(
            include_dir=inc,
            source_component="app",
            target_component="infrastructure",
            file_type="PUBLIC_HEADER",
        )
        bl = generate_baseline_json([v])
        key = inc.source_file
        self.assertEqual(bl[key]["severity"], "high")
        self.assertEqual(bl[key]["tier"], "A")
        self.assertEqual(bl[key]["sunset"], "2026-10-01")

    def test_private_source_gets_tier_b(self) -> None:
        """PRIVATE_SOURCE violation gets severity=low, tier=B, sunset=never."""
        inc = make_include_directive()
        v = Violation(
            include_dir=inc,
            source_component="app",
            target_component="infrastructure",
            file_type="PRIVATE_SOURCE",
        )
        bl = generate_baseline_json([v])
        key = inc.source_file
        self.assertEqual(bl[key]["severity"], "low")
        self.assertEqual(bl[key]["tier"], "B")
        self.assertEqual(bl[key]["sunset"], "never")

    def test_unresolved_skipped(self) -> None:
        """UNRESOLVED violations are excluded from baseline."""
        inc = make_include_directive(include_path="unknown/foo.h")
        v = Violation(
            include_dir=inc,
            source_component="app",
            target_component="UNRESOLVED",
            file_type="PRIVATE_SOURCE",
        )
        bl = generate_baseline_json([v])
        self.assertEqual(bl, {})


# ===================================================================
# 16.  format_json_output --strict (P0-6)
# ===================================================================

class TestFormatJsonOutputStrict(unittest.TestCase):
    """Tests for format_json_output — respects --strict flag."""

    def test_strict_with_only_baselined_returns_1(self) -> None:
        """With strict=True, even baselined violations cause exit_code=1."""
        v = make_violation(is_new=False, target="domain")
        output = format_json_output([v], [], [], strict=True)
        data = json.loads(output)
        self.assertEqual(data["exit_code"], 1)

    def test_non_strict_with_only_baselined_returns_0(self) -> None:
        """With strict=False, only baselined violations → exit_code=0."""
        v = make_violation(is_new=False, target="domain")
        output = format_json_output([v], [], [], strict=False)
        data = json.loads(output)
        self.assertEqual(data["exit_code"], 0)


# ===================================================================
# 17.  Violation.severity for PUBLIC_HEADER (P0-7)
# ===================================================================

class TestViolationSeverity(unittest.TestCase):
    """Tests for Violation.severity — correct severity for PUBLIC_HEADER."""

    def test_new_public_header_is_high(self) -> None:
        """New PUBLIC_HEADER violation has severity HIGH."""
        v = make_violation(is_new=True, file_type="PUBLIC_HEADER", target="domain")
        self.assertEqual(v.severity, "HIGH")

    def test_baselined_public_header_high_severity(self) -> None:
        """Baselined PUBLIC_HEADER with severity=high returns HIGH."""
        v = make_violation(is_new=False, file_type="PUBLIC_HEADER", target="domain")
        v.baseline_info = {"severity": "high", "tier": "A", "sunset": "2026-10-01"}
        self.assertEqual(v.severity, "HIGH")

    def test_new_private_source_is_low(self) -> None:
        """New PRIVATE_SOURCE violation has severity LOW."""
        v = make_violation(is_new=True, file_type="PRIVATE_SOURCE", target="domain")
        self.assertEqual(v.severity, "LOW")


# ===================================================================
# 18.  is_local_include cross-component (P1-10)
# ===================================================================

class TestIsLocalInclude(unittest.TestCase):
    """Tests for is_local_include — cross-component detection."""

    def setUp(self) -> None:
        self.tmpdir = Path(tempfile.mkdtemp())
        self.comp_a = self.tmpdir / "comp_a"
        self.comp_b = self.tmpdir / "comp_b"
        (self.comp_a / "src").mkdir(parents=True)
        (self.comp_a / "include" / "comp_a").mkdir(parents=True)
        (self.comp_b / "include" / "comp_b").mkdir(parents=True)

        # Create files
        (self.comp_a / "include" / "comp_a" / "foo.hpp").write_text("")
        (self.comp_b / "include" / "comp_b" / "bar.hpp").write_text("")
        (self.comp_a / "src" / "foo.cpp").write_text("")

        self.components: dict[str, ComponentDef] = {
            "comp_a": ComponentDef("comp_a", self.comp_a, {"comp_a"}),
            "comp_b": ComponentDef("comp_b", self.comp_b, {"comp_b"}),
        }

    def tearDown(self) -> None:
        import shutil
        shutil.rmtree(self.tmpdir, ignore_errors=True)

    def test_same_component_include_is_local(self) -> None:
        """Same-component include returns True."""
        source = self.comp_a / "src" / "foo.cpp"
        result = is_local_include(
            source, "../include/comp_a/foo.hpp", self.components,
        )
        self.assertTrue(result)

    def test_cross_component_include_is_not_local(self) -> None:
        """Cross-component include via .. returns False."""
        source = self.comp_a / "src" / "foo.cpp"
        result = is_local_include(
            source, "../../comp_b/include/comp_b/bar.hpp", self.components,
        )
        self.assertFalse(result)

    def test_no_components_fallback(self) -> None:
        """Without components dict, existing file is still treated as local."""
        source = self.comp_a / "src" / "foo.cpp"
        result = is_local_include(source, "../include/comp_a/foo.hpp")
        self.assertTrue(result)


# ===================================================================
# 19.  count_public_methods with struct (P1-11)
# ===================================================================

class TestCountPublicMethodsStruct(unittest.TestCase):
    """Tests for count_public_methods — struct has public by default."""

    def test_struct_methods_are_public(self) -> None:
        """Methods inside struct (no public: label) are counted."""
        lines = ["struct Foo {", "void bar();", "};"]
        result = count_public_methods(lines)
        self.assertEqual(result, 1)

    def test_class_methods_not_public_without_label(self) -> None:
        """Methods inside class (no public: label) are not counted."""
        lines = ["class Foo {", "void bar();", "};"]
        result = count_public_methods(lines)
        self.assertEqual(result, 0)

    def test_class_with_public_label_counts(self) -> None:
        """Methods after public: in class are counted."""
        lines = ["class Foo {", "public:", "void bar();", "void baz();", "};"]
        result = count_public_methods(lines)
        self.assertEqual(result, 2)


# ===================================================================
# 20.  FileCache (P1-12)
# ===================================================================

class TestFileCache(unittest.TestCase):
    """Tests for FileCache — single processing per file."""

    def test_cache_returns_same_data(self) -> None:
        """FileCache.get returns same data on repeated calls."""
        content = "#include \"foo.h\"\nint x;\n"
        with tempfile.NamedTemporaryFile(mode='w', suffix='.cpp', delete=False) as f:
            f.write(content)
            tmp = Path(f.name)
        try:
            cache = FileCache()
            result1 = cache.get(tmp)
            result2 = cache.get(tmp)
            self.assertEqual(result1, result2)
            # Both calls should return the same tuple
            self.assertIs(result1, result2)  # same cached object
        finally:
            tmp.unlink(missing_ok=True)

    def test_multiple_files_cached_independently(self) -> None:
        """Different files are cached independently."""
        with tempfile.NamedTemporaryFile(mode='w', suffix='.cpp', delete=False) as f1:
            f1.write("int a;\n")
            p1 = Path(f1.name)
        with tempfile.NamedTemporaryFile(mode='w', suffix='.cpp', delete=False) as f2:
            f2.write("int b;\n")
            p2 = Path(f2.name)
        try:
            cache = FileCache()
            data1 = cache.get(p1)
            data2 = cache.get(p2)
            self.assertNotEqual(data1, data2)
            # Calling get again returns cached (same object)
            self.assertIs(cache.get(p1), data1)
            self.assertIs(cache.get(p2), data2)
        finally:
            p1.unlink(missing_ok=True)
            p2.unlink(missing_ok=True)


# ===================================================================
# 21.  Integration: P1-8 interface arch config
# ===================================================================

class TestInterfaceArchConfig(unittest.TestCase):
    """Integration tests for P1-8: interface must not allow infrastructure."""

    def test_interface_with_infrastructure_include_is_violation(self) -> None:
        """Interface file including infrastructure/config.hpp → VIOLATION."""
        # Find an existing interface source file
        interface_src = Path("components/interface/src")
        if not interface_src.is_dir():
            self.skipTest("No interface/src directory")
        # Use --files mode to scan a specific file
        result = subprocess.run(
            [sys.executable, "scripts/check_arch.py", "--files",
             "components/interface/src/serial.cpp"],
            capture_output=True,
            text=True,
            cwd=str(REPO_ROOT),
        )
        # Should have violations (but exit 0 because they're baselined
        # or we only care about content)
        output = result.stdout + result.stderr
        self.assertIn("infrastructure/config.hpp", output)


# ===================================================================
# 22.  Integration: P1-9 application arch config
# ===================================================================

class TestApplicationArchConfig(unittest.TestCase):
    """Integration tests for P1-9: application must not allow freertos/esp_system."""

    def test_application_with_freertos_include_is_violation(self) -> None:
        """Application file including freertos/FreeRTOS.h → VIOLATION."""
        result = subprocess.run(
            [sys.executable, "scripts/check_arch.py", "--files",
             "components/application/src/state_machine.cpp"],
            capture_output=True,
            text=True,
            cwd=str(REPO_ROOT),
        )
        output = result.stdout + result.stderr
        self.assertIn("freertos/FreeRTOS.h", output)


# ===================================================================
# 23.  End-to-end: main() exit codes
# ===================================================================

class TestMainIntegration(unittest.TestCase):
    """Integration tests running the real script via subprocess.

    These tests run the actual check_arch.py against the real codebase
    config and source files. They validate that the CLI entry point
    behaves correctly.
    """

    def test_default_run_returns_zero(self) -> None:
        """Default run (no flags) returns exit code 0."""
        result = subprocess.run(
            [sys.executable, "scripts/check_arch.py"],
            capture_output=True,
            text=True,
            cwd=str(REPO_ROOT),
        )
        self.assertEqual(result.returncode, 0)

    def test_strict_mode_returns_one(self) -> None:
        """Strict mode returns exit code 1 when baselined violations exist."""
        result = subprocess.run(
            [sys.executable, "scripts/check_arch.py", "--strict"],
            capture_output=True,
            text=True,
            cwd=str(REPO_ROOT),
        )
        self.assertEqual(result.returncode, 1)

    def test_json_format_valid_output(self) -> None:
        """JSON output is valid JSON with exit_code 0."""
        result = subprocess.run(
            [sys.executable, "scripts/check_arch.py", "--format=json"],
            capture_output=True,
            text=True,
            cwd=str(REPO_ROOT),
        )
        self.assertEqual(result.returncode, 0)
        data = json.loads(result.stdout)
        self.assertIn("exit_code", data)
        self.assertEqual(data["exit_code"], 0)
        self.assertIn("violations", data)
        self.assertIn("cycles", data)
        self.assertIn("srp", data)


if __name__ == "__main__":
    unittest.main(verbosity=2)
