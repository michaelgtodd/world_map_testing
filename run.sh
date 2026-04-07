#!/bin/bash
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

export LD_LIBRARY_PATH="/home/parallels/local/lib:/home/parallels/rocky/build/vcpkg_installed/arm64-linux/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export ROCKY_FILE_PATH="/home/parallels/local/share/rocky"
export DISPLAY="${DISPLAY:-:0}"

# vsgQt creates an XCB Vulkan surface, so Qt must use the xcb platform
export QT_QPA_PLATFORM=xcb

exec "$SCRIPT_DIR/build/rocky_qt_docking" "$@"
