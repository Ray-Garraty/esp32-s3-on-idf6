#!/usr/bin/env bash
# Recommended timeout: 600s (full mode includes build + tidy + serial test)
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
export PATH="$HOME/.local/bin:$PATH"

fast_mode=false
[ "${1:-}" = "--fast" ] && fast_mode=true

# --- Staged files list — used by steps 1-2 ---
STAGED=$(git diff --cached --name-only --diff-filter=ACMR 2>/dev/null || true)

# ============================================================
# FINAL VERDICT — single line, unambiguous, machine-parseable.
# Only printed in full mode (not --fast) because fast mode skips
# the heavy hardware-dependent steps and a pass there is not
# a comprehensive green light.
# If the script crashes without printing this line (timeout,
# SIGKILL, bash -e panic), the run is invalid — re-run with
# a larger timeout.
# ============================================================
trap 'ec=$?; if [ "$fast_mode" = false ]; then echo ""; if [ $ec -eq 0 ]; then echo "=== PRE_COMMIT_VERDICT: PASS ==="; else echo "=== PRE_COMMIT_VERDICT: FAIL ==="; fi; fi; exit $ec' EXIT

# ============================================================

# ============================================================
echo "=== 1. Staged files: merge conflict check ==="
conflicts=$(echo "$STAGED" | grep -E '\.(cpp|h|hpp)$' | xargs -r grep -ln '<<<<<<< \|=======\|>>>>>>> ' 2>/dev/null || true)
if [[ -n "$conflicts" ]]; then
  echo "  ❌ FAIL: Merge conflict markers in:"
  echo "$conflicts" | sed 's/^/    /'
  echo ""
  echo "  Fix and re-stage before committing."
  exit 1
fi
echo "  ✅ No merge conflict markers"

# ============================================================
echo "=== 2. Staged files: trailing whitespace ==="
STAGED_ALL=$(echo "$STAGED" | grep -v '^$' || true)
ws_files=$(echo "$STAGED_ALL" | xargs -r grep -l '[[:space:]]$' 2>/dev/null || true)
if [[ -n "$ws_files" ]]; then
  echo "  ❌ FAIL: Trailing whitespace in:"
  echo "$ws_files" | sed 's/^/    /'
  echo ""
  echo "  Fix with:"
  echo "    echo \"$ws_files\" | xargs sed -i 's/[[:space:]]*$//'"
  echo "    git add $(echo "$ws_files" | tr '\n' ' ')"
  exit 1
fi
echo "  ✅ No trailing whitespace"

# ============================================================
echo "=== 3. Format check ==="
if ! "$SCRIPT_DIR/idf.sh" format-check --staged; then
    echo "  To fix: $SCRIPT_DIR/idf.sh format --staged && git add <files>"
    exit 1
fi

# ============================================================
echo "=== 4. Semgrep (main loop blocking) ==="
SEMGREP=""
for candidate in \
  "$HOME/.local/bin/semgrep" \
  "$HOME/.local/pipx/venvs/semgrep/bin/semgrep" \
  /usr/bin/semgrep \
  /usr/local/bin/semgrep; do
  cand_exp=$(eval echo "$candidate")
  if [[ -x "$cand_exp" ]]; then
    SEMGREP="$cand_exp"
    break
  fi
done
if [[ -z "$SEMGREP" ]]; then
  # Check if module is available (works even with broken entry-point symlinks)
  if python3 -m semgrep --version >/dev/null 2>&1; then
    SEMGREP="python3 -m semgrep"
  else
    echo "  ⚠️  semgrep not found — attempting auto-install..."
    pip3 install --break-system-packages semgrep 2>&1 | tail -1
    # Re-check
    if command -v semgrep &>/dev/null; then
      SEMGREP="semgrep"
    elif python3 -m semgrep --version >/dev/null 2>&1; then
      SEMGREP="python3 -m semgrep"
    else
      echo "  ❌ FAIL: semgrep not installed and auto-install failed"
      echo "  Manual: pip install --break-system-packages semgrep"
      exit 1
    fi
  fi
fi
echo "  🔍 semgrep: $SEMGREP"
$SEMGREP --config "$PROJECT_DIR/.semgrep/main_loop_blocking.yaml" --error "$PROJECT_DIR/main/main.cpp"
echo "  ✅ No main loop blocking violations"

# ============================================================
echo "=== 5. Architecture dependency check ==="
UNCOMMITTED_CPP=$(
  {
    git diff --name-only HEAD
    git ls-files --others --exclude-standard
  } 2>/dev/null \
  | grep -E '\.(cpp|hpp|h)$' \
  | sort -u \
  | head -200 \
  || true
)
if [[ -z "$UNCOMMITTED_CPP" ]]; then
    echo "  ⏭️  No uncommitted C++ files — skipping"
else
    if ! echo "$UNCOMMITTED_CPP" | python3 "$PROJECT_DIR/scripts/check_arch.py" --quiet --files; then
        echo "  ❌ FAIL: Architecture dependency violation detected"
        echo "  Run: python3 scripts/check_arch.py"
        exit 1
    fi
    echo "  ✅ No architecture dependency violations in uncommitted files"
fi

# ============================================================
echo "=== 6. Host unit tests ==="
"$SCRIPT_DIR/idf.sh" test
echo "  ✅ All unit tests pass"

# ============================================================
echo "=== 7. Docs OKF validation ==="
python "$PROJECT_DIR/docs/validate_okf.py"
echo "  ✅ All docs valid"

# ============================================================
echo "=== 8. sdkconfig constraint ==="
SDKCONFIG="$PROJECT_DIR/sdkconfig.defaults"
if [[ ! -f "$SDKCONFIG" ]]; then
  echo "  ❌ FAIL: $SDKCONFIG not found"
  exit 1
fi
rx_ba_win=$(grep -E '^CONFIG_ESP_WIFI_RX_BA_WIN=' "$SDKCONFIG" | cut -d= -f2)
dyn_rx_buf=$(grep -E '^CONFIG_ESP_WIFI_DYNAMIC_RX_BUFFER_NUM=' "$SDKCONFIG" | cut -d= -f2)
if [[ -z "$rx_ba_win" || -z "$dyn_rx_buf" ]]; then
  echo "  ❌ FAIL: Cannot read CONFIG_ESP_WIFI_RX_BA_WIN or CONFIG_ESP_WIFI_DYNAMIC_RX_BUFFER_NUM"
  echo "  Check that both are defined in $SDKCONFIG"
  exit 1
fi
if [[ "$rx_ba_win" -gt "$dyn_rx_buf" ]]; then
  echo "  ❌ FAIL: CONFIG_ESP_WIFI_RX_BA_WIN ($rx_ba_win) > CONFIG_ESP_WIFI_DYNAMIC_RX_BUFFER_NUM ($dyn_rx_buf)"
  echo "  RX_BA_WIN must be ≤ DYNAMIC_RX_BUFFER_NUM to prevent wifi_init compile error."
  exit 1
fi
echo "  ✅ CONFIG_ESP_WIFI_RX_BA_WIN ($rx_ba_win) ≤ CONFIG_ESP_WIFI_DYNAMIC_RX_BUFFER_NUM ($dyn_rx_buf)"

# ============================================================
# Full mode
if [ "$fast_mode" = false ]; then
  echo "=== 9. Smoke test (build + flash + 70s monitor) ==="
  "$SCRIPT_DIR/idf.sh" smoke
  echo "  ✅ Smoke test passed"

  echo "=== 10. Stack watermark check ==="
  if ls "$PROJECT_DIR"/logs/serial_*.log >/dev/null 2>&1; then
      python3 "$SCRIPT_DIR/check_watermarks.py"
  else
      echo "  ⏭️  No serial log found — skipping watermark check"
  fi

  echo "=== 11. clang-tidy ==="
  if [[ ! -f "$PROJECT_DIR/build/compile_commands.json" ]]; then
    echo "  ❌ FAIL: build/compile_commands.json not found"
    echo "  Run build first or check build output."
    exit 1
  fi
  "$SCRIPT_DIR/idf.sh" tidy
  echo "  ✅ clang-tidy clean"

  echo "=== 12. Serial API hardware test ==="
  if [[ -x "$PROJECT_DIR/scripts/testing/serial_api_test.py" ]]; then
    timeout 60 python3 "$PROJECT_DIR/scripts/testing/serial_api_test.py"
  fi
fi

# ============================================================
exit 0
