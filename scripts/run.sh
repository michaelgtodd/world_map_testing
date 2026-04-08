#!/bin/bash
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

export ROCKY_FILE_PATH="/home/parallels/local/share/rocky"
export DISPLAY="${DISPLAY:-:0}"
export QT_QPA_PLATFORM=xcb

exec "$PROJECT_DIR/build/rocky_qt_docking" "$@"
