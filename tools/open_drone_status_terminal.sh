#!/bin/bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
WATCH_SCRIPT="$ROOT_DIR/tools/watch_drone_status_local.sh"

if [ ! -x "$WATCH_SCRIPT" ]; then
    chmod +x "$WATCH_SCRIPT"
fi

exec x-terminal-emulator -e bash -lc "cd '$ROOT_DIR' && '$WATCH_SCRIPT'"
