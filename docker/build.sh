#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Build eMuleQt Linux x86_64 bundle via Docker.
#
# Usage:
#   docker/build.sh              — build without bundled Qt
#   docker/build.sh --deploy-qt  — bundle Qt libraries (self-contained)
#
# Output: emuleqt-v*-linux-x86_64.tar.gz in the project root.
# ---------------------------------------------------------------------------
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

DEPLOY_QT=false
if [[ "${1:-}" == "--deploy-qt" ]]; then
    DEPLOY_QT=true
fi

docker build \
    --platform linux/amd64 \
    --build-arg DEPLOY_QT="$DEPLOY_QT" \
    --output="$PROJECT_ROOT" \
    -f "$SCRIPT_DIR/Dockerfile" \
    "$PROJECT_ROOT"