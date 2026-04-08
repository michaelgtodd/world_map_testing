#!/bin/bash
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

export ROCKY_FILE_PATH="/home/parallels/local/share/rocky"
export DISPLAY="${DISPLAY:-:0}"
export QT_QPA_PLATFORM=xcb

exec "$SCRIPT_DIR/build/rocky_qt_docking" "$@"
