#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# build.sh — build eMule Qt
#
# Usage:
#   ./scripts/build.sh [clean] [bundle] [--target NAME]... [build-dir]
#
#   clean        Remove build directory before building
#   bundle       Run platform-specific bundling after build (e.g. macdeployqt)
#   deploy-qt    Bundle Qt libraries (Linux only, requires bundle)
#   --target N   Build only target N (repeatable); default builds all, tests too
#   build-dir    Path to the CMake build directory (default: ./build)
# ---------------------------------------------------------------------------

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

CLEAN=false
BUNDLE=false
DEPLOY_QT=false
BUILD_DIR=""
TARGETS=()

while [ $# -gt 0 ]; do
    arg="$1"
    shift
    if [ "$arg" = "clean" ]; then
        CLEAN=true
    elif [ "$arg" = "bundle" ]; then
        BUNDLE=true
    elif [ "$arg" = "deploy-qt" ]; then
        DEPLOY_QT=true
    elif [ "$arg" = "--target" ]; then
        [ $# -gt 0 ] || { echo "--target needs a name" >&2; exit 1; }
        TARGETS+=("$1")
        shift
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
cmake -S "$PROJECT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release -DEMULE_BITTORRENT=ON -DEMULE_HOSTER_JD=ON

TARGET_ARGS=()
if [ ${#TARGETS[@]} -gt 0 ]; then
    TARGET_ARGS=(--target "${TARGETS[@]}")
    echo "Building targets: ${TARGETS[*]}"
else
    echo "Building..."
fi
cmake --build "$BUILD_DIR" --parallel "$(nproc 2>/dev/null || sysctl -n hw.ncpu)" ${TARGET_ARGS[@]+"${TARGET_ARGS[@]}"}

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
        Linux)
            echo "Running Linux bundle script..."
            LINUX_BUNDLE_ARGS=("$BUILD_DIR")
            if [ "$DEPLOY_QT" = true ]; then
                LINUX_BUNDLE_ARGS=("deploy-qt" "$BUILD_DIR")
            fi
            "$SCRIPT_DIR/bundle-linux.sh" "${LINUX_BUNDLE_ARGS[@]}"
            ;;
        MINGW*|MSYS*|CYGWIN*)
            echo "Running Windows bundle script..."
            powershell -NoProfile -ExecutionPolicy Bypass \
                -File "$SCRIPT_DIR/bundle-win.ps1" -BuildDir "$BUILD_DIR"
            ;;
    esac
fi