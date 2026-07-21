#!/usr/bin/env python3
"""Architecture dependency checker for ESP-IDF components.

Two-pass static analysis:
  Pass 1 — include-dependency layering enforcement (domain→application→infrastructure→interface)
  Pass 2 — SRP metrics (LOC, fan-out, public methods)

Zero external dependencies — Python stdlib only (tomllib, pathlib, re, sys, json, argparse).
"""

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Any

import tomllib


# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------

STDLIB_SYSTEM_HEADERS: frozenset[str] = frozenset({
    # C++ standard library
    "algorithm", "array", "atomic", "bit", "bitset", "cassert", "cctype",
    "cerrno", "cfenv", "cfloat", "chrono", "cinttypes", "ciso646", "climits",
    "clocale", "cmath", "codecvt", "compare", "complex", "concepts",
    "coroutine", "csetjmp", "csignal", "cstdarg", "cstddef", "cstdint",
    "cstdio", "cstdlib", "cstring", "ctime", "ctype.h", "cuchar", "cwchar",
    "cwctype", "deque", "exception", "expected", "filesystem", "forward_list",
    "fstream", "functional", "future", "initializer_list", "iomanip", "ios",
    "iosfwd", "iostream", "istream", "iterator", "limits", "list", "locale",
    "map", "memory", "memory_resource", "mutex", "new", "numbers", "numeric",
    "optional", "ostream", "queue", "random", "ranges", "ratio", "regex",
    "scoped_allocator", "set", "shared_mutex", "source_location", "span",
    "sstream", "stack", "stdexcept", "stop_token", "streambuf", "string",
    "string_view", "strstream", "syncstream", "system_error", "thread",
    "tuple", "type_traits", "typeindex", "typeinfo", "unordered_map",
    "unordered_set", "utility", "valarray", "variant", "vector", "version",
    # C standard library (angle-bracket forms)
    "assert.h", "ctype.h", "errno.h", "fenv.h", "float.h", "inttypes.h",
    "iso646.h", "limits.h", "locale.h", "math.h", "setjmp.h", "signal.h",
    "stdalign.h", "stdarg.h", "stdatomic.h", "stdbit.h", "stdbool.h",
    "stdckdint.h", "stddef.h", "stdint.h", "stdio.h", "stdlib.h", "string.h",
    "tgmath.h", "threads.h", "time.h", "uchar.h", "wchar.h", "wctype.h",
    # POSIX / common on embedded
    "unistd.h", "fcntl.h", "sys/stat.h", "sys/types.h", "errno.h",
    "assert.h", "stdarg.h", "stdio.h", "stdlib.h", "string.h",
    "time.h", "math.h", "float.h", "ctype.h", "limits.h", "inttypes.h",
    "sys/select.h", "sys/socket.h", "sys/time.h", "sys/un.h",
    "sys/ioctl.h", "pthread.h", "signal.h", "dirent.h",
    # Xtensa / toolchain architecture headers
    "xtensa_context.h", "xtensa/config/core-isa.h", "xtensa/xtruntime.h",
})

# Directories to skip during file discovery
SKIP_DIRS: frozenset[str] = frozenset({
    "managed_components", "build", "build-tests", "legacy", "__pycache__",
})

SKIP_EXTENSIONS: frozenset[str] = frozenset({".pyc", ".pyo", ".o", ".obj", ".a", ".lib", ".so", ".dll"})


# ---------------------------------------------------------------------------
# Data classes (typed dicts for readability)
# ---------------------------------------------------------------------------

class Config:
    """Loaded arch_config.toml data."""

    def __init__(self, data: dict[str, Any]) -> None:
        self.components: dict[str, ComponentDef] = {
            name: ComponentDef(name=name, path=Path(c["path"]), allowed=set(c["allowed"]))
            for name, c in data["components"].items()
        }
        self.virtual_prefix_map: list[tuple[str, str]] = [
            (entry["prefix"], entry["tag"])
            for entry in data["virtual_prefix"]
        ]
        self.generated_headers: set[str] = set(data["generated_headers"])
        srp = data["srp_thresholds"]
        self.max_loc: int = srp["max_loc"]
        self.max_fan_out: int = srp["max_fan_out"]
        self.max_public_methods: int = srp["max_public_methods"]


class ComponentDef:
    """Definition of a project component."""

    def __init__(self, name: str, path: Path, allowed: set[str]) -> None:
        self.name = name
        self.path = path
        self.allowed = allowed


class IncludeDirective:
    """Parsed #include directive."""

    def __init__(self, source_file: str, line_no: int, include_path: str,
                 include_type: str, conditional: bool) -> None:
        self.source_file = source_file
        self.line_no = line_no
        self.include_path = include_path
        self.include_type = include_type  # "quoted" or "angled"
        self.conditional = conditional

    @property
    def conditionality(self) -> str:
        return "conditional" if self.conditional else "unconditional"


class Violation:
    """A layering violation or baselined edge."""

    def __init__(self, include_dir: IncludeDirective, source_component: str,
                 target_component: str, file_type: str) -> None:
        self.include = include_dir
        self.source_component = source_component
        self.target_component = target_component
        self.file_type = file_type
        self.is_new = True  # becomes False if baselined
        self.baseline_info: dict[str, Any] | None = None

    @property
    def severity(self) -> str:
        if self.target_component == "UNRESOLVED":
            return "HIGH"
        if self.baseline_info:
            sev = self.baseline_info.get("severity", "low")
        else:
            sev = "low"
        # PUBLIC_HEADER violations with unknown/baseline severity → HIGH
        if self.file_type == "PUBLIC_HEADER":
            if self.is_new:
                return "HIGH"
            return sev.upper() if sev else "LOW"
        return sev.upper() if sev else "LOW"

    @property
    def tier(self) -> str | None:
        return self.baseline_info.get("tier") if self.baseline_info else None

    @property
    def sunset(self) -> str | None:
        return self.baseline_info.get("sunset") if self.baseline_info else None

    @property
    def action(self) -> str:
        if self.target_component == "UNRESOLVED":
            return ("unknown include target — add to arch_config.toml "
                    "VIRTUAL_PREFIX_MAP or fix include path")
        if self.file_type == "PUBLIC_HEADER":
            return (f"move this include behind a sink interface in infrastructure/ — "
                    f"{self.source_component} public headers must not pull {self.target_component}")
        if self.is_new:
            return (f"new cross-layer dependency — {self.source_component} "
                    f"should not depend on {self.target_component}")
        return "no action required — accepted architectural debt"


class FileCache:
    """Cache for preprocessed file data to avoid re-reading and
    re-preprocessing the same file multiple times across analysis passes."""

    def __init__(self) -> None:
        self._cache: dict[Path, tuple[list[str], list[int]]] = {}

    def get(self, file_path: Path) -> tuple[list[str], list[int]]:
        """Return preprocessed data for *file_path*, computing it once."""
        if file_path not in self._cache:
            self._cache[file_path] = preprocess_source(file_path)
        return self._cache[file_path]


# ---------------------------------------------------------------------------
# File processing
# ---------------------------------------------------------------------------

def discover_source_files(components: dict[str, ComponentDef]) -> dict[str, list[Path]]:
    """Walk each component's path and collect .cpp/.hpp/.h files.

    Returns a dict: component_name -> list of Paths.
    """
    result: dict[str, list[Path]] = {}
    for name, comp in components.items():
        files: list[Path] = []
        if not comp.path.is_dir():
            print(f"⚠  Component path not found: {comp.path}", file=sys.stderr)
            result[name] = files
            continue
        for p in comp.path.rglob("*"):
            if p.suffix not in (".cpp", ".hpp", ".h"):
                continue
            # Skip ignored directories
            if any(part in SKIP_DIRS for part in p.relative_to(comp.path).parts):
                continue
            files.append(p)
        result[name] = sorted(files)
    return result


def strip_line_comments(line: str) -> str:
    """Remove // line comments (but not inside string literals)."""
    result = []
    i = 0
    in_string = False
    string_char = None
    while i < len(line):
        ch = line[i]
        if in_string:
            result.append(ch)
            if ch == '\\':
                i += 1
                if i < len(line):
                    result.append(line[i])
            elif ch == string_char:
                in_string = False
                string_char = None
            i += 1
            continue
        if ch in ('"', "'"):
            in_string = True
            string_char = ch
            result.append(ch)
            i += 1
            continue
        # Check for //
        if ch == '/' and i + 1 < len(line) and line[i + 1] == '/':
            break  # rest is comment
        result.append(ch)
        i += 1
    return ''.join(result)


def strip_block_comments(text: str) -> str:
    """Remove /* ... */ block comments (non-nested)."""
    result = []
    i = 0
    in_string = False
    string_char = None
    while i < len(text):
        ch = text[i]
        if in_string:
            result.append(ch)
            if ch == '\\':
                i += 1
                if i < len(text):
                    result.append(text[i])
            elif ch == string_char:
                in_string = False
                string_char = None
            i += 1
            continue
        if ch in ('"', "'"):
            in_string = True
            string_char = ch
            result.append(ch)
            i += 1
            continue
        # Check for /*
        if ch == '/' and i + 1 < len(text) and text[i + 1] == '*':
            i += 2
            # Scan for closing */
            while i + 1 < len(text):
                if text[i] == '*' and text[i + 1] == '/':
                    i += 2
                    break
                i += 1
            continue
        result.append(ch)
        i += 1
    return ''.join(result)


def _strip_line_stateful(line: str, in_block: bool) -> tuple[str, bool]:
    """Process a single line with block-comment state tracking.

    Handles /* and */ within a single line while tracking the block state.
    Returns (cleaned_line, new_in_block).
    """
    i = 0
    in_string = False
    string_char = None
    result = []

    while i < len(line):
        ch = line[i]

        if in_string:
            result.append(ch)
            if ch == '\\':
                i += 1
                if i < len(line):
                    result.append(line[i])
            elif ch == string_char:
                in_string = False
                string_char = None
            i += 1
            continue

        if ch in ('"', "'"):
            in_string = True
            string_char = ch
            result.append(ch)
            i += 1
            continue

        if in_block:
            # Look for closing */
            if ch == '*' and i + 1 < len(line) and line[i + 1] == '/':
                in_block = False
                i += 2
                continue
            i += 1
            continue

        # Check for /*
        if ch == '/' and i + 1 < len(line) and line[i + 1] == '*':
            # Check if there's a */ on the same line
            rest = line[i + 2:]
            close_pos = rest.find('*/')
            if close_pos != -1:
                # Complete block comment on same line — skip it
                i += 2 + close_pos + 2
                continue
            else:
                in_block = True
                i += 2
                continue

        # Check for //
        if ch == '/' and i + 1 < len(line) and line[i + 1] == '/':
            break  # rest is comment

        result.append(ch)
        i += 1

    return ''.join(result), in_block


def preprocess_source(file_path: Path) -> tuple[list[str], list[int]]:
    """Read and preprocess a source file.

    Returns (processed_lines, line_number_map) where:
    - processed_lines: list of preprocessed lines (comments stripped)
    - line_number_map: maps processed line index to original line number (1-based)

    Processes line-by-line with stateful block-comment tracking so that
    line numbers are preserved for accurate diagnostic reporting.
    """
    try:
        raw_lines = file_path.read_text(encoding="utf-8", errors="replace").split('\n')
    except Exception as exc:
        print(f"⚠  Cannot read {file_path}: {exc}", file=sys.stderr)
        return [], []

    processed: list[str] = []
    line_map: list[int] = []
    in_block = False

    for idx, line in enumerate(raw_lines, start=1):
        cleaned, in_block = _strip_line_stateful(line, in_block)
        processed.append(cleaned)
        line_map.append(idx)

    return processed, line_map


def parse_includes(lines: list[str], line_map: list[int],
                   file_path: Path) -> list[IncludeDirective]:
    """Extract #include directives from preprocessed lines.

    Also tracks #if/#ifdef/#ifndef/#elif/#else/#endif nesting.
    """
    includes: list[IncludeDirective] = []
    conditional_depth = 0
    include_re = re.compile(r'#\s*include\s+([<"])([^>"]+)[>"]')

    for line_idx, line in enumerate(lines):
        stripped = line.strip()

        # Track conditional compilation
        if re.match(r'#\s*if(?:def|ndef)?\b', stripped):
            conditional_depth += 1
        elif re.match(r'#\s*endif\b', stripped):
            conditional_depth = max(0, conditional_depth - 1)
        # #elif and #else don't change depth

        # Parse includes
        m = include_re.search(stripped)
        if m:
            inc_type = "angled" if m.group(1) == '<' else "quoted"
            inc_path = m.group(2)
            is_conditional = conditional_depth > 0
            orig_line = line_map[line_idx]
            includes.append(IncludeDirective(
                source_file=str(file_path),
                line_no=orig_line,
                include_path=inc_path,
                include_type=inc_type,
                conditional=is_conditional,
            ))

    return includes


# ---------------------------------------------------------------------------
# Component resolution
# ---------------------------------------------------------------------------

def resolve_component(include_path: str,
                      virtual_prefix_map: list[tuple[str, str]],
                      generated_headers: set[str],
                      ) -> str | None:
    """Resolve an include path to a component tag.

    Returns the tag string, or None if this is a generated/stdlib header.
    Returns "UNRESOLVED" if no prefix matches.
    """
    if include_path in generated_headers:
        return None  # Skip generated headers

    # System headers: <...> that don't match any prefix
    for prefix, tag in virtual_prefix_map:
        if include_path.startswith(prefix):
            return tag

    # No prefix matched
    return "UNRESOLVED"


def is_stdlib_system(include_path: str, include_type: str) -> bool:
    """Check if an include is a standard library / system header.

    Only applied to angle-bracket includes.
    """
    if include_type != "angled":
        return False
    # Extract the leaf filename: "cstdint" or "sys/stat.h" -> "cstdint" / "stat.h"
    path_part = include_path.replace('\\', '/')
    base = path_part.split('/')[0]  # first component
    # Also check the full path
    if include_path in STDLIB_SYSTEM_HEADERS:
        return True
    if base in STDLIB_SYSTEM_HEADERS:
        return True
    return False


def is_local_include(source_file: Path, include_path: str,
                     components: dict[str, ComponentDef] | None = None) -> bool:
    """Check if an include is a local file (same directory or subdirectory
    relative to the source file). Local includes are implementation details
    that don't cross component boundaries.

    If `components` is provided, also verifies the resolved path belongs
    to the same component.
    """
    source_dir = source_file.parent
    candidate = (source_dir / include_path).resolve()
    if candidate.exists() and candidate.is_file():
        if components is not None:
            source_owner = find_component_for_file(source_file, components)
            target_owner = find_component_for_file(candidate, components)
            if source_owner is not None and target_owner is not None:
                return source_owner == target_owner
        return True
    # Also check common include path conventions (just the filename in
    # the same directory)
    if '/' not in include_path:
        candidate = (source_dir / include_path).resolve()
        if candidate.exists() and candidate.is_file():
            if components is not None:
                source_owner = find_component_for_file(source_file, components)
                target_owner = find_component_for_file(candidate, components)
                if source_owner is not None and target_owner is not None:
                    return source_owner == target_owner
            return True
    return False


def classify_file(file_path: Path, component_path: Path) -> str:
    """Classify as PUBLIC_HEADER or PRIVATE_SOURCE."""
    rel = file_path.relative_to(component_path)
    parts = rel.parts
    if file_path.suffix in (".hpp", ".h") and "include" in parts:
        return "PUBLIC_HEADER"
    return "PRIVATE_SOURCE"


def find_component_for_file(file_path: Path,
                            components: dict[str, ComponentDef]) -> str | None:
    """Find which project component a file belongs to.

    Returns component name or None if not found.
    """
    file_path = file_path.resolve()
    for name, comp in components.items():
        comp_path = comp.path.resolve()
        try:
            file_path.relative_to(comp_path)
            return name
        except ValueError:
            continue
    return None


# ---------------------------------------------------------------------------
# Pass 1: Layer enforcement
# ---------------------------------------------------------------------------

def run_pass1(config: Config, baseline: dict[str, Any],
              for_baseline_generation: bool = False,
              file_filter: list[Path] | None = None,
              cache: FileCache | None = None) -> list[Violation]:
    """Run include-dependency analysis.

    If file_filter is provided, only scan those files (--files mode).
    Returns list of Violation objects.
    """
    components = config.components
    vpm = config.virtual_prefix_map

    # Discover files
    comp_files = discover_source_files(components)

    # Flatten into (comp_name, path) list for optional filtering
    scan_items: list[tuple[str, Path, ComponentDef]] = []
    for comp_name, files in comp_files.items():
        comp_def = components.get(comp_name)
        if not comp_def:
            continue
        for fp in files:
            if file_filter is None or fp in file_filter:
                scan_items.append((comp_name, fp, comp_def))

    violations: list[Violation] = []

    for source_comp, fp, comp_def in scan_items:
        lines, line_map = cache.get(fp) if cache else preprocess_source(fp)
        if not lines:
            continue
        includes = parse_includes(lines, line_map, fp)

        for inc in includes:
            # Skip stdlib system headers
            if is_stdlib_system(inc.include_path, inc.include_type):
                continue

            # Resolve target
            target = resolve_component(inc.include_path, vpm,
                                       config.generated_headers)
            if target is None:
                continue  # generated header

            # Check for local includes — files that resolve relative to
            # the source file's own directory (implementation details).
            if is_local_include(fp, inc.include_path, components):
                continue

            file_type = classify_file(fp, comp_def.path)

            # Check allowed list
            # If source allowed has ["*"], everything passes
            if "*" in comp_def.allowed:
                continue

            if target in comp_def.allowed:
                continue

            # This is a violation
            viol = Violation(
                include_dir=inc,
                source_component=source_comp,
                target_component=target if target else "UNRESOLVED",
                file_type=file_type,
            )

            # UNRESOLVED is always NEW, never baselined
            if target == "UNRESOLVED":
                viol.is_new = True
            else:
                # Check baseline
                bl = baseline_match(fp, inc.include_path, baseline)
                if bl is not None:
                    viol.is_new = False
                    viol.baseline_info = bl

            violations.append(viol)

    return violations


def baseline_match(file_path: Path, include_path: str,
                   baseline: dict[str, Any]) -> dict[str, Any] | None:
    """Check if a (file, include) pair exists in the baseline.

    Returns the baseline entry dict or None.
    """
    try:
        rel = str(file_path.relative_to(Path.cwd()))
    except ValueError:
        rel = str(file_path)
    if rel in baseline:
        entry = baseline[rel]
        bl_includes: list[str] = entry.get("includes", [])
        if include_path in bl_includes:
            return entry
    return None


# ---------------------------------------------------------------------------
# Cycle detection
# ---------------------------------------------------------------------------

def scan_all_edges(components: dict[str, ComponentDef],
                   virtual_prefix_map: list[tuple[str, str]],
                   generated_headers: set[str],
                   project_comps: set[str],
                   cache: FileCache | None = None) -> dict[str, set[str]]:
    """Full scan of all files to build the component dependency graph.

    Only project components are tracked as sources. All targets (including
    virtual tags like freertos, esp_system) are recorded. Callers filter
    as needed.
    """
    comp_files = discover_source_files(components)

    adj: dict[str, set[str]] = {c: set() for c in project_comps}

    for comp_name, files in comp_files.items():
        if comp_name not in project_comps:
            continue
        for fp in files:
            lines, line_map = cache.get(fp) if cache else preprocess_source(fp)
            if not lines:
                continue
            includes = parse_includes(lines, line_map, fp)

            for inc in includes:
                if is_stdlib_system(inc.include_path, inc.include_type):
                    continue
                target = resolve_component(inc.include_path, virtual_prefix_map,
                                           generated_headers)
                if target is None:
                    continue
                # Skip UNRESOLVED — not a real component
                if target == "UNRESOLVED":
                    continue
                # Record all targets (project and virtual)
                adj.setdefault(comp_name, set()).add(target)

    return adj


# ---------------------------------------------------------------------------
# Pass 2: SRP metrics
# ---------------------------------------------------------------------------

class SrpResult:
    """SRP metric result for a file."""

    def __init__(self, file_path: str, metric: str, value: int,
                 threshold: int) -> None:
        self.file_path = file_path
        self.metric = metric
        self.value = value
        self.threshold = threshold


def run_pass2(config: Config,
              cache: FileCache | None = None) -> list[SrpResult]:
    """Run SRP metrics analysis.

    Returns list of SrpResult for files exceeding thresholds.
    """
    comp_files = discover_source_files(config.components)
    results: list[SrpResult] = []

    for comp_name, files in comp_files.items():
        comp_def = config.components.get(comp_name)
        if not comp_def:
            continue

        for fp in files:
            lines, line_map = cache.get(fp) if cache else preprocess_source(fp)
            if not lines:
                continue

            # LOC: count non-blank, non-comment lines
            loc = count_loc(lines, fp)

            # Fan-out: unique resolved targets from includes
            includes = parse_includes(lines, line_map, fp)
            fan_out = count_fan_out(includes, config)

            # Public methods (headers only)
            public_methods = 0
            if fp.suffix in (".hpp", ".h"):
                public_methods = count_public_methods(lines)

            # Compare thresholds
            if loc > config.max_loc:
                results.append(SrpResult(
                    file_path=str(fp), metric="loc",
                    value=loc, threshold=config.max_loc,
                ))
            if fan_out > config.max_fan_out:
                results.append(SrpResult(
                    file_path=str(fp), metric="fan_out",
                    value=fan_out, threshold=config.max_fan_out,
                ))
            if public_methods > config.max_public_methods:
                results.append(SrpResult(
                    file_path=str(fp), metric="public_methods",
                    value=public_methods,
                    threshold=config.max_public_methods,
                ))

    return results


def count_loc(lines: list[str], file_path: Path) -> int:
    """Count non-blank, non-comment-only lines (total physical LOC)."""
    count = 0
    in_block_comment = False
    for line in lines:
        stripped = line.strip()

        # Skip empty lines
        if not stripped:
            continue

        # Track block comments across lines
        if "/*" in stripped:
            in_block_comment = True
        if in_block_comment:
            if "*/" in stripped:
                in_block_comment = False
            # Check if there's code before /* or after */
            before = stripped.split("/*")[0].strip()
            after = ""
            if "*/" in stripped:
                after = stripped.split("*/")[-1].strip()
            if before or after:
                count += 1
            continue

        # Skip comment-only lines (already stripped // and /* in preprocessing,
        # but there might be remnants)
        if stripped.startswith("//") or stripped.startswith("/*") or stripped.startswith("*"):
            continue

        count += 1

    return count


def count_fan_out(includes: list[IncludeDirective],
                  config: Config) -> int:
    """Count unique resolved targets, excluding stdlib and generated headers."""
    targets: set[str] = set()
    for inc in includes:
        if is_stdlib_system(inc.include_path, inc.include_type):
            continue
        target = resolve_component(inc.include_path, config.virtual_prefix_map,
                                   config.generated_headers)
        if target is None:
            continue
        targets.add(target)
    return len(targets)


def count_public_methods(lines: list[str]) -> int:
    """Heuristic: count method declarations in public: sections.

    Lines containing '(' but not matching if/for/while/switch/return patterns.
    C++ structs have public access by default.
    """
    in_public = False
    count = 0

    for line in lines:
        stripped = line.strip()

        # struct → public by default
        if re.match(r'\s*struct\s+\w+', stripped):
            in_public = True
            continue

        # class → private by default
        if re.match(r'\s*class\s+\w+', stripped):
            in_public = False
            continue

        # Track public:/private:/protected: sections
        if stripped.startswith("public:"):
            in_public = True
            continue
        if stripped.startswith("private:") or stripped.startswith("protected:"):
            in_public = False
            continue

        if not in_public:
            continue

        # Skip empty, comments, preprocessor
        if not stripped or stripped.startswith("//") or stripped.startswith("#"):
            continue

        # Count lines with '(' for method declarations
        if '(' not in stripped:
            continue

        # Exclude control flow statements
        if re.match(r'\s*(if|for|while|switch|return)\s*\(', stripped):
            continue

        count += 1

    return count


# ---------------------------------------------------------------------------
# Output formatters
# ---------------------------------------------------------------------------

def format_violation_kv(v: Violation) -> str:
    """Format a violation as key=value pairs."""
    lines: list[str] = []
    if v.is_new:
        lines.append("status=VIOLATION")
    else:
        lines.append("status=BASELINED")

    lines.append(f"source_file={v.include.source_file}")
    lines.append(f"source_line={v.include.line_no}")
    lines.append(f"include={v.include.include_path}")
    lines.append(f"source_component={v.source_component}")
    lines.append(f"target_component={v.target_component}")
    lines.append(f"conditionality={v.include.conditionality}")
    lines.append(f"file_type={v.file_type}")
    lines.append(f"severity={v.severity}")
    if v.tier:
        lines.append(f"tier={v.tier}")
    if v.sunset:
        lines.append(f"sunset={v.sunset}")
    lines.append(f"action={v.action}")
    return '\n'.join(lines)


def format_cycle_kv(cycle: list[str]) -> str:
    """Format a cycle as key=value pairs."""
    path_str = " → ".join(cycle)
    return (
        f"status=CYCLE\n"
        f"path={path_str}\n"
        f"action=break one edge with an abstraction layer — "
        f"cycles cause non-deterministic rebuilds"
    )


def format_srp_kv(srp: SrpResult) -> str:
    """Format an SRP result as key=value pairs."""
    metric_labels = {
        "loc": "total lines of code",
        "fan_out": "distinct include targets",
        "public_methods": "public methods",
    }
    label = metric_labels.get(srp.metric, srp.metric)
    return (
        f"status=SRP\n"
        f"source_file={srp.file_path}\n"
        f"metric={srp.metric}\n"
        f"value={srp.value}\n"
        f"threshold={srp.threshold}\n"
        f"action=split god-class — {srp.value} {label} exceeds limit of {srp.threshold}"
    )


def generate_baseline_json(violations: list[Violation]) -> dict[str, Any]:
    """Generate baseline.json content from current violations.

    PUBLIC_HEADER violations get severity=high, tier=A, sunset=2026-10-01.
    PRIVATE_SOURCE violations get severity=low, tier=B, sunset=never.
    """
    baseline: dict[str, Any] = {}

    # Group violations by source file
    by_file: dict[str, dict[str, Any]] = {}
    for v in violations:
        if v.target_component == "UNRESOLVED":
            continue  # UNRESOLVED never baselines
        sf = v.include.source_file
        if sf not in by_file:
            if v.file_type == "PUBLIC_HEADER":
                by_file[sf] = {
                    "includes": [],
                    "severity": "high",
                    "tier": "A",
                    "sunset": "2026-10-01",
                }
            else:
                by_file[sf] = {
                    "includes": [],
                    "severity": "low",
                    "tier": "B",
                    "sunset": "never",
                }
        by_file[sf]["includes"].append(v.include.include_path)

    # Sort by file path for deterministic output
    for sf in sorted(by_file.keys()):
        entry = by_file[sf]
        entry["includes"] = sorted(set(entry["includes"]))
        baseline[sf] = entry

    return baseline


def format_json_output(violations: list[Violation],
                       cycles: list[list[str]],
                       srp_results: list[SrpResult],
                       strict: bool = False) -> str:
    """Format output as JSON."""
    vlist: list[dict[str, Any]] = []
    new_count = 0
    baselined_count = 0
    unresolved_count = 0

    for v in violations:
        entry: dict[str, Any] = {
            "status": "VIOLATION" if v.is_new else "BASELINED",
            "source_file": v.include.source_file,
            "source_line": v.include.line_no,
            "include": v.include.include_path,
            "source_component": v.source_component,
            "target_component": v.target_component,
            "conditionality": v.include.conditionality,
            "file_type": v.file_type,
            "severity": v.severity,
        }
        if v.tier:
            entry["tier"] = v.tier
        if v.sunset:
            entry["sunset"] = v.sunset
        vlist.append(entry)

        if v.target_component == "UNRESOLVED":
            unresolved_count += 1
        elif v.is_new:
            new_count += 1
        else:
            baselined_count += 1

    output: dict[str, Any] = {
        "violations": {
            "total": len(violations),
            "new": new_count,
            "baselined": baselined_count,
            "unresolved": unresolved_count,
            "list": vlist,
        },
        "cycles": len(cycles),
        "srp": {
            "over_loc": [s.file_path for s in srp_results if s.metric == "loc"],
            "over_fan_out": [s.file_path for s in srp_results if s.metric == "fan_out"],
            "over_public_methods": [s.file_path for s in srp_results if s.metric == "public_methods"],
        },
    }

    # Determine exit code (same logic as main)
    if strict:
        output["exit_code"] = 1 if (len(violations) > 0 or len(cycles) > 0) else 0
    else:
        has_new = any(v.is_new for v in violations)
        has_unresolved = any(v.target_component == "UNRESOLVED" for v in violations)
        output["exit_code"] = 1 if (has_new or has_unresolved) else 0

    return json.dumps(output, indent=2)


# ---------------------------------------------------------------------------
# Graph output (DOT)
# ---------------------------------------------------------------------------

def format_dot(components: dict[str, ComponentDef],
               edges: dict[str, set[str]],
               include_all: bool,
               project_comps: set[str]) -> str:
    """Format a DOT digraph of component dependencies."""
    lines: list[str] = ['digraph arch {', '    rankdir=LR;', '    node [shape=box, style=rounded];', '']

    if include_all:
        # All nodes including virtual tags
        all_nodes: set[str] = set()
        for src, tgts in edges.items():
            all_nodes.add(src)
            all_nodes.update(tgts)
        for node in sorted(all_nodes):
            lines.append(f'    "{node}";')
    else:
        # Only project components
        for node in sorted(project_comps):
            lines.append(f'    "{node}";')

    lines.append('')

    for src in sorted(edges.keys()):
        if src not in project_comps and not include_all:
            continue
        for tgt in sorted(edges[src]):
            if src == tgt:
                continue  # skip self-loops in DOT output
            if include_all:
                lines.append(f'    "{src}" -> "{tgt}";')
            elif src in project_comps and tgt in project_comps:
                lines.append(f'    "{src}" -> "{tgt}";')

    lines.append('}')
    return '\n'.join(lines)


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main() -> int:
    parser = argparse.ArgumentParser(
        description="Architecture dependency checker for ESP-IDF components",
    )
    parser.add_argument("--strict", action="store_true",
                        help="Fail on both NEW and BASELINED violations")
    parser.add_argument("--format", choices=["text", "json"], default="text",
                        help="Output format (default: text)")
    parser.add_argument("--graph", action="store_true",
                        help="Output DOT digraph of project components")
    parser.add_argument("--all", action="store_true", dest="graph_all",
                        help="With --graph, include virtual tags")
    parser.add_argument("--generate-baseline", action="store_true",
                        help="Dump current violations as baseline.json to stdout")
    parser.add_argument("--quiet", action="store_true",
                        help="Suppress BASELINED, CYCLE, and SRP output — only show NEW/UNRESOLVED violations")
    parser.add_argument("--files", nargs="*", default=None,
                        help="Only scan specified files (space-separated). Disables cycle detection and SRP.")

    args = parser.parse_args()

    # Load config
    config_path = Path(__file__).parent / "arch_config.toml"
    try:
        with open(config_path, "rb") as f:
            data = tomllib.load(f)
    except FileNotFoundError:
        print(f"ERROR: {config_path} not found", file=sys.stderr)
        return 1
    except Exception as exc:
        print(f"ERROR: Cannot load config: {exc}", file=sys.stderr)
        return 1

    config = Config(data)
    project_comps: set[str] = set(config.components.keys())
    file_cache = FileCache()

    # Load baseline
    baseline_path = Path(__file__).parent.parent / "baseline.json"
    baseline: dict[str, Any] = {}
    if baseline_path.exists():
        try:
            baseline = json.loads(baseline_path.read_text(encoding="utf-8"))
        except Exception as exc:
            print(f"⚠  Cannot load baseline: {exc}", file=sys.stderr)

    # --graph mode (no analysis, just output DOT)
    if args.graph:
        edges = scan_all_edges(
            config.components,
            config.virtual_prefix_map,
            config.generated_headers,
            project_comps,
            cache=file_cache,
        )
        print(format_dot(config.components, edges, args.graph_all, project_comps))
        return 0

    # --generate-baseline mode
    if args.generate_baseline:
        # Run pass 1 without baseline matching
        violations = run_pass1(config, {}, for_baseline_generation=True,
                               cache=file_cache)
        bl_json = generate_baseline_json(violations)
        print(json.dumps(bl_json, indent=2))
        return 0

    # Resolve --files filter if provided
    # If --files is used without arguments, read file list from stdin
    file_filter: list[Path] | None = None
    if args.files is not None:
        if len(args.files) == 0:
            args.files = [l.strip() for l in sys.stdin if l.strip()]
        file_filter = [Path(f) for f in args.files]

    # Pass 1: Layer enforcement
    violations = run_pass1(config, baseline, file_filter=file_filter,
                           cache=file_cache)

    new_violations = [v for v in violations if v.is_new]
    unresolved_violations = [v for v in violations
                             if v.target_component == "UNRESOLVED"]
    baselined_violations = [v for v in violations if not v.is_new]

    # Cycle detection (skipped in --files mode — needs full project graph)
    cycles: list[list[str]] = []
    if file_filter is None:
        edges = scan_all_edges(
            config.components,
            config.virtual_prefix_map,
            config.generated_headers,
            project_comps,
            cache=file_cache,
        )
        cycles = detect_cycles_from_edges(edges, project_comps)

    # Pass 2: SRP metrics (skipped in --files mode — needs full project context)
    srp_results: list[SrpResult] = []
    if file_filter is None:
        srp_results = run_pass2(config, cache=file_cache)

    # --- Output ---

    if args.format == "json":
        print(format_json_output(violations, cycles, srp_results,
                                 strict=args.strict))
    else:
        # Text output with key=value pairs
        output_parts: list[str] = []

        if args.quiet:
            # Quiet: only NEW / UNRESOLVED violations
            for v in violations:
                if v.is_new or v.target_component == "UNRESOLVED":
                    output_parts.append(format_violation_kv(v))
        else:
            for v in violations:
                output_parts.append(format_violation_kv(v))

            for cycle in cycles:
                output_parts.append(format_cycle_kv(cycle))

            for sr in srp_results:
                output_parts.append(format_srp_kv(sr))

        # Separator
        print('\n\n'.join(output_parts))

    # --- Exit code ---
    has_new = len(new_violations) > 0 or len(unresolved_violations) > 0
    has_cycles = len(cycles) > 0

    if args.strict:
        # Strict: fail on any violation (including baselined)
        if len(violations) > 0 or has_cycles:
            return 1
        return 0

    # Default mode: cycles are informational (block only in --strict)
    if has_new:
        return 1

    return 0


def detect_cycles_from_edges(adj: dict[str, set[str]],
                             project_comps: set[str]) -> list[list[str]]:
    """Detect cycles from a complete edge dict.

    Self-loops (component→itself) are excluded — internal includes within
    the same component are not architectural cycles.
    """
    cycles: list[list[str]] = []

    # Only include project components, exclude self-loops
    pruned: dict[str, set[str]] = {}
    for node in project_comps:
        if node in adj:
            pruned[node] = {t for t in adj[node]
                            if t in project_comps and t != node}
        else:
            pruned[node] = set()

    visited: set[str] = set()
    rec_stack: list[str] = []

    def dfs(node: str, path: list[str]) -> None:
        visited.add(node)
        rec_stack.append(node)
        for neigh in sorted(pruned.get(node, set())):
            if neigh not in visited:
                dfs(neigh, path + [neigh])
            elif neigh in rec_stack:
                # Found a cycle
                idx = path.index(neigh)
                cycle_path = path[idx:] + [neigh]
                cycles.append(cycle_path)
        rec_stack.pop()

    for comp in sorted(project_comps):
        if comp not in visited:
            dfs(comp, [comp])

    return cycles


if __name__ == "__main__":
    sys.exit(main())
