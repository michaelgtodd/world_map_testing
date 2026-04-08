#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

echo "=== Building Docker test image ==="
echo "Project: $PROJECT_DIR"

docker build \
    -f "$SCRIPT_DIR/Dockerfile" \
    -t rocky-flight-planner-test \
    "$PROJECT_DIR"

echo ""
echo "=== Build and tests passed ==="
