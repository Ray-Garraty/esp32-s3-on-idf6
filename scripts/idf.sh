#!/usr/bin/env bash
set -euo pipefail

# ecotiter C++23 firmware toolchain — single entry point for all build ops

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

IDF_PATH="${IDF_PATH:-$HOME/.espressif/v6.0.1/esp-idf}"
if [ -f "$IDF_PATH/export.sh" ]; then
    export IDF_PATH
    source "$IDF_PATH/export.sh" > /dev/null 2>&1
fi

# File-based mutex: only one idf.sh instance at a time (like Cargo)
LOCKDIR="$PROJECT_DIR/.idf_lock"
LOCK_TIMEOUT=300
acquire_lock() {
    local waited=0
    while ! mkdir "$LOCKDIR" 2>/dev/null; do
        if [ "$waited" -eq 0 ]; then
            echo "⏳ Waiting for another idf.sh instance (PID $(cat "$LOCKDIR/pid" 2>/dev/null || echo '?'))..."
        fi
        sleep 1
        waited=$((waited + 1))
        if [ "$waited" -ge "$LOCK_TIMEOUT" ]; then
            echo "❌ Timed out waiting for lock (${LOCK_TIMEOUT}s)"
            echo "   To force: rm -rf $LOCKDIR"
            exit 1
        fi
    done
    echo "$$" > "$LOCKDIR/pid"
    trap 'rm -rf "$LOCKDIR"' EXIT SIGTERM SIGINT
}
acquire_lock

# Auto-detect ESP32-S3 port by VID/PID, fallback to /dev/ttyUSB0
resolve_port() {
    local port="${1:-}"
    if [ -n "$port" ]; then
        echo "$port"
        return 0
    fi
    local detected
    detected=$(python3 "$SCRIPT_DIR/find_port.py" 2>/dev/null || true)
    if [ -n "$detected" ]; then
        echo "$detected"
        return 0
    fi
    echo "/dev/ttyUSB0"
}

# Clean build with metadata injection
do_build() {
    rm -rf "$PROJECT_DIR/build"
    BUILD_DATE=$(date '+%Y-%m-%d %H:%M:%S')
    GIT_HASH=$(git rev-parse --short HEAD 2>/dev/null || echo "unknown")
    export BUILD_DATE GIT_HASH
    echo "Building firmware: $BUILD_DATE (git: $GIT_HASH)"
    idf.py build
    mkdir -p "$PROJECT_DIR/build"
    {
        echo "BUILD_DATE=\"$BUILD_DATE\""
        echo "GIT_HASH=$GIT_HASH"
    } > "$PROJECT_DIR/build/.build_meta"
}

CMD="${1:-build}"

case "$CMD" in
    build)
        do_build
        ;;

    flash|monitor|uart)
        PORT=$(resolve_port "${2:-}")
        echo "Port: $PORT"
        case "$CMD" in
            flash)
                BINARY="$PROJECT_DIR/build/ecotiter.bin"
                META="$PROJECT_DIR/build/.build_meta"
                if [ -f "$META" ]; then
                    source "$META"
                    echo "Firmware built: ${BUILD_DATE:-?} (git: ${GIT_HASH:-?})"
                else
                    echo "⚠️  No build metadata found"
                fi
                if [ ! -f "$BINARY" ]; then
                    echo "❌ Firmware binary not found. Run './scripts/idf.sh build' first."
                    exit 1
                fi
                STALE=$(find "$PROJECT_DIR" \
                    -path "$PROJECT_DIR/build" -prune -o \
                    -path "$PROJECT_DIR/build-tests" -prune -o \
                    -path "$PROJECT_DIR/.git" -prune -o \
                    -path "$PROJECT_DIR/logs" -prune -o \
                    -path "$PROJECT_DIR/legacy" -prune -o \
                    \( -name '*.cpp' -o -name '*.c' -o -name '*.h' -o -name '*.hpp' -o -name 'CMakeLists.txt' -o -name 'sdkconfig.defaults' \) \
                    -newer "$BINARY" -print -quit 2>/dev/null)
                if [ -n "$STALE" ]; then
                    echo "⚠️  Warning: Source file '$STALE' is newer than firmware binary."
                    echo "   Run './scripts/idf.sh build' for a fresh build."
                fi
                idf.py -p "$PORT" flash
                ;;
            monitor)
                timeout 30 python3 "$SCRIPT_DIR/monitor.py" "$PORT" || true
                ;;
            uart)
                timeout 30 python3 scripts/uart_test.py -p "$PORT"
                ;;
        esac
        ;;

    smoke)
        do_build
        PORT=$(python3 "$SCRIPT_DIR/find_port.py") || {
            echo "❌ No ESP32 port found"
            exit 1
        }
        echo "Port: $PORT"
        idf.py -p "$PORT" flash
        sleep 0.3
        python3 "$SCRIPT_DIR/monitor.py" "$PORT" --timeout 30 --log-dir "$PROJECT_DIR/logs" --no-reset
        exit $?
        ;;

    test)
        test_cmd="${2:-run}"
        shift 2 2>/dev/null || true
        if [ ! -f "$PROJECT_DIR/build-tests/CMakeCache.txt" ]; then
            cmake -B "$PROJECT_DIR/build-tests" -S "$PROJECT_DIR/tests"
        fi
        cmake --build "$PROJECT_DIR/build-tests" 2>&1 | tail -1
        case "$test_cmd" in
            --build) ;;
            --list) "$PROJECT_DIR/build-tests/unit_tests" --list-tests ;;
            run)    "$PROJECT_DIR/build-tests/unit_tests" ;;
            --)     "$PROJECT_DIR/build-tests/unit_tests" "$@" ;;
            *)
                echo "Usage: $0 test {run|--build|--list|-- <filter>}"
                exit 1
                ;;
        esac
        ;;

    tidy)
        ./scripts/lint.sh "${@:2}"
        ;;

    reconfigure)
        rm -f "$PROJECT_DIR/sdkconfig"
        idf.py reconfigure
        ;;

    clean)
        rm -rf build build-tests
        ;;

    help|--help|-h)
        echo "ecotiter C++23 firmware toolchain — single entry point"
        echo ""
        echo "PREREQUISITES"
        echo "  ESP-IDF v6.0.1 at \$IDF_PATH (default: ~/.espressif/v6.0.1/esp-idf)"
        echo "  C++23 toolchain (ESP-IDF clang-based)"
        echo "  cmake >= 3.16, Python 3.10+"
        echo ""
        echo "COMMANDS"
        echo "  build                 Clean build — removes build/, injects timestamp + git hash"
        echo "  flash [port]          Flash firmware (auto-detect port, stale source check)"
        echo "  monitor [port]        Serial monitor, 30s timeout"
        echo "  smoke                 Clean build + flash + 30s monitor (full pipeline test)"
        echo "  test                  Run host unit tests (Catch2)"
        echo "  test --build          Configure + build tests only, don't run"
        echo "  test --list           List test case names"
        echo "  test -- <filter>      Run specific tests (Catch2 wildcard)"
        echo "  tidy                  clang-tidy static analysis"
        echo "  uart [port]           UART integration test"
        echo "  reconfigure           Remove sdkconfig + idf.py reconfigure"
        echo "  clean                 Remove build/ and build-tests/"
        echo ""
        echo "EXAMPLES"
        echo "  ./scripts/idf.sh build                 # clean build"
        echo "  ./scripts/idf.sh flash                 # flash (auto-detect port)"
        echo "  ./scripts/idf.sh flash /dev/ttyUSB0    # flash (specific port)"
        echo "  ./scripts/idf.sh smoke                 # full pipeline test"
        echo "  ./scripts/idf.sh test -- \"dose*\"       # run dose-related tests"
        echo "  ./scripts/idf.sh clean                 # remove artifacts"
        echo ""
        echo "SDK CONFIG POLICY"
        echo "  Edit only sdkconfig.defaults — never sdkconfig (auto-generated)"
        echo "  Never run idf.py menuconfig (not reproducible)"
        echo "  After changing defaults: ./scripts/idf.sh reconfigure"
        echo ""
        echo "NOTES"
        echo "  - Port auto-detection: VID/PID matching (CP2102, CH340, FTDI, ESP32-S3)"
        echo "  - Fallback port if auto-detect fails: /dev/ttyUSB0"
        echo "  - Each build embeds BUILD_DATE + GIT_HASH; shown in boot log and by flash"
        echo "  - flash warns if any source file is newer than the binary"
        echo ""
        echo "DOCS"
        echo "  docs/refs/coding_style.md   — C++23 conventions, RAII, error handling"
        echo "  docs/refs/project.md        — HW pinout, threads, network stack"
        echo "  AGENTS.md                    — golden rules (GR-1..GR-11), crash patterns"
        ;;

    *)
        echo "Usage: $0 {build|flash|monitor|smoke|test|tidy|uart|reconfigure|clean|help}"
        exit 1
        ;;
esac
