#!/bin/bash
# Analyze the most recent crash from serial log files.
# Usage:
#   ./scripts/analyze_last_crash.sh              # analyze latest log
#   ./scripts/analyze_last_crash.sh --elf <path>  # specify ELF path
#   ./scripts/analyze_last_crash.sh --no-decode   # skip addr2line

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$SCRIPT_DIR/.."
LOG_DIR="$PROJECT_DIR/logs"
ELF="${ELF:-$PROJECT_DIR/target/xtensa-esp32s3-espidf/debug/ecotiter}"
NO_DECODE=""

# Parse args
while [[ $# -gt 0 ]]; do
    case "$1" in
        --elf) ELF="$2"; shift 2 ;;
        --no-decode) NO_DECODE="--no-decode"; shift ;;
        *) echo "Usage: $0 [--elf <path>] [--no-decode]"; exit 1 ;;
    esac
done

# Find latest log file
LATEST=$(ls -t "$LOG_DIR"/serial_*.log 2>/dev/null | head -1)
if [ -z "$LATEST" ]; then
    echo "ERROR: No serial log files found in $LOG_DIR/" >&2
    exit 1
fi
echo "=== Analyzing: $LATEST ==="

# Run crash_analyzer with --log flag (handles extraction internally)
python3 "$SCRIPT_DIR/crash_analyzer.py" --log "$LATEST" --elf "$ELF" $NO_DECODE
