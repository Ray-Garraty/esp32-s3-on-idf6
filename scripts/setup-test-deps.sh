#!/usr/bin/env bash
set -euo pipefail

# Pre-download test dependencies (Catch2, nlohmann_json) to local cache.
# Called automatically by scripts/idf.sh test — no manual step needed.
#
# Usage:  ./scripts/setup-test-deps.sh          # verbose (human)
#         ./scripts/setup-test-deps.sh --quiet   # silent if cache hit
#
# Env:    ESP_TEST_DEPS_CACHE_DIR  — override cache dir (default: ~/.cache/esp-test-deps)

CACHE_DIR="${ESP_TEST_DEPS_CACHE_DIR:-$HOME/.cache/esp-test-deps}"
mkdir -p "$CACHE_DIR"

QUIET=false
[ "${1:-}" = "--quiet" ] && QUIET=true

log() { $QUIET || echo "$@"; }

# Check if all tarballs exist
check_cached() {
    [ -f "$CACHE_DIR/nlohmann_json-3.11.3.tar.gz" ] && \
    [ -f "$CACHE_DIR/Catch2-3.15.1.tar.gz" ]
}

if check_cached; then
    log "✔ Test deps cache up to date at $CACHE_DIR"
    exit 0
fi

log "📦 Populating test dependency cache in: $CACHE_DIR"

# Detect download tool
if command -v wget &>/dev/null; then
    DL="wget -q --show-progress"
elif command -v curl &>/dev/null; then
    DL="curl -sSL -o"
else
    echo "❌ Neither wget nor curl found — install one of them" >&2
    exit 1
fi

# --- nlohmann_json v3.11.3 ---
NLOHMANN_OUT="$CACHE_DIR/nlohmann_json-3.11.3.tar.gz"
if [ ! -f "$NLOHMANN_OUT" ]; then
    log "  ⬇ Downloading nlohmann_json v3.11.3..."
    if command -v wget &>/dev/null; then
        wget -q --show-progress -O "$NLOHMANN_OUT" \
            "https://github.com/nlohmann/json/archive/refs/tags/v3.11.3.tar.gz"
    else
        curl -sSL -o "$NLOHMANN_OUT" \
            "https://github.com/nlohmann/json/archive/refs/tags/v3.11.3.tar.gz"
    fi
    log "  ✔ Done ($(du -h "$NLOHMANN_OUT" | cut -f1))"
fi

# --- Catch2 v3.15.1 ---
CATCH2_OUT="$CACHE_DIR/Catch2-3.15.1.tar.gz"
if [ ! -f "$CATCH2_OUT" ]; then
    log "  ⬇ Downloading Catch2 v3.15.1..."
    if command -v wget &>/dev/null; then
        wget -q --show-progress -O "$CATCH2_OUT" \
            "https://github.com/catchorg/Catch2/archive/refs/tags/v3.15.1.tar.gz"
    else
        curl -sSL -o "$CATCH2_OUT" \
            "https://github.com/catchorg/Catch2/archive/refs/tags/v3.15.1.tar.gz"
    fi
    log "  ✔ Done ($(du -h "$CATCH2_OUT" | cut -f1))"
fi

log ""
log "✅ Cache ready at $CACHE_DIR"
