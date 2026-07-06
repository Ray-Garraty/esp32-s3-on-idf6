#!/bin/bash
set -e

# ESP toolchain paths (bypasses broken .cargo/bin symlinks)
ESP_BASE="$HOME/.rustup/toolchains/esp"
export PATH="$ESP_BASE/bin:$PATH"

PYTHON_PATH=""  # Set to pyenv path on Windows if needed
export PATH="$HOME/.local/bin:$PATH"

fast_mode=false
[ "$1" = "--fast" ] && fast_mode=true

echo "=== 1. Auto-format ==="
cargo fmt --all

echo "=== 2. Format check ==="
cargo fmt --all -- --check

echo "=== 3. Unsafe block audit ==="
python3 scripts/check_unsafe.py

echo "=== 4. Lint suppression audit ==="
python3 scripts/check_lint_suppressions.py

echo "=== 5. Host unit tests ==="
cargo test --lib

echo "=== 6. Clippy (host target, lib only — binary needs xtensa) ==="
cargo clippy --lib -- -D warnings

echo "=== 7. Dependency unsafe audit ==="
python3 scripts/fast_geiger.py

echo "=== 8. Semgrep blocking check ==="
semgrep --config .semgrep/ --error src/

echo "=== 9. Docs OKF validation ==="
python docs/validate_okf.py

if [ "$fast_mode" = false ]; then
    xtensa() {
        PATH="$PYTHON_PATH:$PATH" cargo "$@"
    }

    echo "=== 10. Clippy (xtensa target) ==="
    xtensa clippy --target xtensa-esp32-espidf -- -D warnings

    echo "=== 11. Check (xtensa) ==="
    xtensa check --target xtensa-esp32-espidf
fi

echo "=== All checks passed ==="
