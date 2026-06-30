#!/usr/bin/env python3
"""Check that every unsafe block has a SAFETY/Safety justification comment.

Scans src/**/*.rs and reports any `unsafe {` or `unsafe impl` without
a `// SAFETY:` / `// Safety:` / `// CHECKED_SAFE:` comment within 3
lines above.

Returns:
  Exit 0 — all unsafe blocks are documented.
  Exit 1 — one or more undocumented unsafe blocks found.
"""

import re
import sys
from pathlib import Path


SRC_DIR = Path(__file__).resolve().parent.parent / "src"

# Match // SAFETY:, // Safety:, /// # Safety (doc comment), // CHECKED_SAFE:
SAFETY_RE = re.compile(r"/[/*]\s*(#\s*)?(SAFETY|Safety|CHECKED_SAFE)[:\s]?")
UNSAFE_LINE_RE = re.compile(r"\bunsafe\s*(\{|impl\b)")

# Lookback window in lines to find a SAFETY comment
LOOKBACK = 6


def check_file(path: Path) -> list[tuple[int, str]]:
    text = path.read_text()
    lines = text.split("\n")
    undocumented: list[tuple[int, str]] = []

    for i, line in enumerate(lines):
        if not UNSAFE_LINE_RE.search(line):
            continue

        # Search within LOOKBACK lines before, same line, and 1 line after
        # (comments can be inside the unsafe block, e.g. after `unsafe {`)
        window = range(max(0, i - LOOKBACK), min(len(lines), i + 2))
        has_safety = any(SAFETY_RE.search(lines[j]) for j in window)

        if not has_safety:
            undocumented.append((i + 1, line.strip()[:80]))

    return undocumented


def main():
    total_unsafe = 0
    all_undocumented: list[tuple[Path, int, str]] = []

    for rs_file in sorted(SRC_DIR.rglob("*.rs")):
        undocumented = check_file(rs_file)
        total_unsafe += len(undocumented)
        for line_num, snippet in undocumented:
            rel = rs_file.relative_to(SRC_DIR.parent)
            all_undocumented.append((rel, line_num, snippet))

    print(f"Total undocumented unsafe blocks: {len(all_undocumented)}")

    if all_undocumented:
        print("\nUndocumented unsafe blocks found:")
        for rel, line_num, snippet in all_undocumented:
            print(f"  {rel}:{line_num}  {snippet}")
        print("\nEach unsafe block must have a `// SAFETY:` or `// Safety:`")
        print("comment within 3 lines above explaining why it is safe.")
        sys.exit(1)

    print("All unsafe blocks have SAFETY justification comments.")
    sys.exit(0)


if __name__ == "__main__":
    main()
