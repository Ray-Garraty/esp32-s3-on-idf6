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

echo "=== 6. Check for undocumented unsafe blocks ==="
python3 scripts/check_unsafe.py
fi

echo "=== 7. Semgrep blocking check ==="
semgrep --config .semgrep/ --error src/

echo "=== 8. Docs OKF validation ==="
python docs/validate_okf.py

echo "=== All checks passed ==="
