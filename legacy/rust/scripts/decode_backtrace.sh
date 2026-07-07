#!/bin/bash
# Decode ESP32-S3 backtrace using xtensa-esp32s3-elf-addr2line.
# Usage:
#   ./scripts/decode_backtrace.sh 0x40359c07 0x4038abcd ...
#   cat crash.txt | ./scripts/decode_backtrace.sh
#   ./scripts/decode_backtrace.sh < crash.txt

ELF="${ELF:-target/xtensa-esp32s3-espidf/debug/ecotiter}"

# Find addr2line in ESP-IDF tools
ADDR2LINE=""
for d in "$HOME/.espressif/tools/xtensa-esp32s3-elf/"*/xtensa-esp32s3-elf/bin \
         "$HOME/.espressif/tools/xtensa-esp-elf/"*/xtensa-esp-elf/bin; do
    if [ -f "$d/xtensa-esp32s3-elf-addr2line" ]; then
        ADDR2LINE="$d/xtensa-esp32s3-elf-addr2line"
        break
    fi
done

if [ -z "$ADDR2LINE" ]; then
    # Try PATH as fallback
    ADDR2LINE=$(command -v xtensa-esp32s3-elf-addr2line 2>/dev/null)
fi

if [ -z "$ADDR2LINE" ]; then
    echo "ERROR: xtensa-esp32s3-elf-addr2line not found." >&2
    echo "Source export-esp.sh first: . /home/vlabe/export-esp.sh" >&2
    exit 1
fi

if [ ! -f "$ELF" ]; then
    echo "ERROR: ELF not found at $ELF" >&2
    echo "Set ELF env var or build first: cargo +esp build --target xtensa-esp32s3-espidf" >&2
    exit 1
fi

# Collect addresses from args or stdin
if [ $# -gt 0 ]; then
    ADDRS="$@"
else
    ADDRS=$(cat)
fi

# Extract hex addresses, run addr2line
echo "$ADDRS" \
    | grep -oE '0x[0-9a-fA-F]{8}' \
    | tr '\n' ' ' \
    | xargs "$ADDR2LINE" -pfiaC -e "$ELF"

exit $?
