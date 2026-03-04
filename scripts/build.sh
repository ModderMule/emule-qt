#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# build.sh — build eMule Qt
#
# Usage:
#   ./scripts/build.sh [clean] [bundle] [build-dir]
#
#   clean       Remove build directory before building
#   bundle      Run platform-specific bundling after build (e.g. macdeployqt)
#   build-dir   Path to the CMake build directory (default: ./build)
# ---------------------------------------------------------------------------

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

CLEAN=false
BUNDLE=false
BUILD_DIR=""

for arg in "$@"; do
    if [ "$arg" = "clean" ]; then
        CLEAN=true
    elif [ "$arg" = "bundle" ]; then
        BUNDLE=true
    elif [ -z "$BUILD_DIR" ]; then
        BUILD_DIR="$arg"
    fi
done

BUILD_DIR="${BUILD_DIR:-$PROJECT_DIR/build}"

# -- Clean (only if requested) -----------------------------------------------

if [ "$CLEAN" = true ] && [ -d "$BUILD_DIR" ]; then
    echo "Cleaning build directory: $BUILD_DIR"
    rm -rf "$BUILD_DIR"
fi

# -- Configure & Build -------------------------------------------------------

echo "Configuring..."
cmake -S "$PROJECT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release

echo "Building..."
cmake --build "$BUILD_DIR" --parallel "$(nproc 2>/dev/null || sysctl -n hw.ncpu)"

echo "Build complete."

# -- Ad-hoc code signing (macOS) --------------------------------------------
# Preserves firewall allow-rules across rebuilds.

if [ "$(uname -s)" = "Darwin" ]; then
    bin="$BUILD_DIR/src/daemon/emulecored"
    if [ -f "$bin" ]; then
        codesign --force -s - "$bin" 2>/dev/null && echo "Signed: $bin"
    fi
fi

# -- Platform-specific bundling (only if requested) --------------------------

if [ "$BUNDLE" = true ]; then
    case "$(uname -s)" in
        Darwin)
            echo "Running macOS bundle script..."
            "$SCRIPT_DIR/bundle-macos.sh" "$BUILD_DIR"
            ;;
        MINGW*|MSYS*|CYGWIN*)
            echo "Running Windows bundle script..."
            cmd //c "$SCRIPT_DIR/bundle-win.bat" "$BUILD_DIR"
            ;;
    esac
fi