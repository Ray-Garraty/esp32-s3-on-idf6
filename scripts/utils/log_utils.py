"""
Unified sanitized logging for integration tests and serial monitor.

Eliminates binary garbage (null bytes, control characters) from log files.
All serial-logging code MUST use sanitize_line() before writing to disk.
"""


def sanitize_line(line: str) -> str:
    """Remove non-printable characters except common whitespace.

    Strips all control characters (null bytes, bell, escape, etc.)
    while preserving printable characters plus \\n, \\r, \\t.
    """
    return ''.join(c for c in line if c.isprintable() or c in '\n\r\t')


def write_sanitized(file, msg: str) -> None:
    """Sanitize *msg* and write it (with newline) to *file*, then flush."""
    file.write(sanitize_line(msg) + '\n')
    file.flush()
