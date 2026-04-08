#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

cd "$PROJECT_DIR"

# Configure if needed
if [ ! -f build/build.ninja ]; then
    echo "=== Configuring ==="
    cmake --preset default
fi

# Build
echo "=== Building ==="
cmake --build --preset default

echo ""
echo "=== Build complete ==="
echo "Run:  scripts/run.sh"
echo "Test: scripts/test.sh"
