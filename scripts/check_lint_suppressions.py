#!/usr/bin/env python3
"""Check that every #[allow()] / #[expect()] has a justification comment.

Scans src/**/*.rs and for each suppression attribute checks whether a
comment (// or ///) exists within the 3 lines immediately preceding
it.  Reports all violations and exits 1 if any are found.

Exit 0 — all suppressions are documented.
Exit 1 — one or more bare suppressions exist.
"""

import re
import sys
from pathlib import Path

SRC_DIR = Path(__file__).resolve().parent.parent / "src"

# Matches #[allow(...)] and #[expect(...)] including multi-line forms
SUPPRESSION_RE = re.compile(r"#!?\s*\[(allow|expect)\(")

# Lines that count as justification comments (line or doc comments)
COMMENT_RE = re.compile(r"^\s*(//|///|/\*)")

LOOKBACK = 10  # matches check_unsafe.py; handles grouped comment blocks for crate-level allows


def check_file(path: Path) -> list[tuple[int, str]]:
    """Return list of (line_number, snippet) for undocumented suppressions."""
    lines = path.read_text().split("\n")
    undocumented: list[tuple[int, str]] = []

    for i, line in enumerate(lines):
        stripped = line.strip()
        if not SUPPRESSION_RE.search(stripped):
            continue

        # Check preceding LOOKBACK lines for a comment
        window = range(max(0, i - LOOKBACK), i)
        has_comment = any(COMMENT_RE.match(lines[j]) for j in window)

        # Check if the attribute itself contains reason = "..." (including multi-line attrs)
        # Collect lines until the closing paren
        attr_text = stripped
        j = i + 1
        while not stripped.rstrip().endswith(")") and j < len(lines):
            next_line = lines[j].strip()
            attr_text += " " + next_line
            stripped = next_line
            j += 1

        if 'reason =' in attr_text:
            has_comment = True

        if not has_comment:
            undocumented.append((i + 1, lines[i].strip()[:100]))

    return undocumented


def main() -> int:
    undocumented_total = 0
    all_undocumented: list[tuple[Path, int, str]] = []

    for rs_file in sorted(SRC_DIR.rglob("*.rs")):
        rel = rs_file.relative_to(SRC_DIR.parent)
        undocumented = check_file(rs_file)
        undocumented_total += len(undocumented)
        for line_num, snippet in undocumented:
            all_undocumented.append((rel, line_num, snippet))

    if undocumented_total == 0:
        print("OK: all suppressions have a justification comment.")
        return 0

    print(f"Undocumented suppressions found ({undocumented_total}):")
    for rel, line_num, snippet in all_undocumented:
        print(f"  {rel}:{line_num}  {snippet}")

    print(
        "\nEach #[allow()] / #[expect()] must have an English // or /// comment"
        f" within {LOOKBACK} lines above explaining why."
    )
    print("Prefer #[expect] over #[allow] when the lint always fires.")
    print("Run: `rg -n '#\\[allow\\(' src/` to list all offenders.")
    return 1


if __name__ == "__main__":
    sys.exit(main())
