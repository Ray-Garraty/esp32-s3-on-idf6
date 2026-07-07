#!/usr/bin/env bash
set -euo pipefail

PORT="${1:-/dev/ttyACM0}"
TIMEOUT="${2:-30}"

echo "Monitoring $PORT for $TIMEOUT seconds..."
timeout "$TIMEOUT" idf.py -p "$PORT" monitor || true
echo "Monitor finished."
