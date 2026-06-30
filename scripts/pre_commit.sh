#!/bin/bash
set -e

PYTHON_PATH=""  # Set to pyenv path on Windows if needed
export PATH="$HOME/.local/bin:$PATH"

fast_mode=false
[ "$1" = "--fast" ] && fast_mode=true

echo "=== 1. Format check ==="
cargo fmt --all -- --check

echo "=== 2. Host unit tests ==="
cargo test --lib

echo "=== 3. Clippy (host target, lib only — binary needs xtensa) ==="
cargo clippy --lib -- -D warnings

if [ "$fast_mode" = false ]; then
    xtensa() {
        PATH="$PYTHON_PATH:$PATH" cargo "$@"
    }

    echo "=== 4. Clippy (xtensa target) ==="
    xtensa clippy --target xtensa-esp32-espidf -- -D warnings

    echo "=== 5. Build (xtensa) ==="
    xtensa build --target xtensa-esp32-espidf

    echo "=== 6. Check for unsafe blocks ==="
    UNSAFE_COUNT=$(grep -r "unsafe {" src/ 2>/dev/null | wc -l)
    echo "Found $UNSAFE_COUNT unsafe blocks"
    if [ "$UNSAFE_COUNT" -gt 10 ]; then
        echo "WARNING: Too many unsafe blocks! Review if all are necessary."
    fi
fi

echo "=== 7. Semgrep blocking check ==="
semgrep --config .semgrep/ --error src/

echo "=== 8. Docs OKF validation ==="
python docs/validate_okf.py

echo "=== All checks passed ==="
