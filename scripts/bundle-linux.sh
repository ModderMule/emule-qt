#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# bundle-linux.sh — package eMule Qt for Linux x86_64
#
# Creates a self-contained tarball with binaries, config data, and
# translation files.  Qt libraries are NOT bundled — users install them
# via their distribution's package manager.
#
# Usage:
#   ./scripts/bundle-linux.sh [build-dir]
#
#   build-dir   Path to the CMake build directory (default: ./build)
#
# Output:
#   eMuleQt-linux-x86_64.tar.gz containing:
#     eMuleQt/
#       emuleqt          GUI executable
#       emulecored       daemon executable
#       config/          default config data (nodes.dat, eMule.tmpl, …)
#         webserver/     web server assets
#       lang/            compiled translation files (.qm)
# ---------------------------------------------------------------------------

# try https://flatpak.org/ ?

set -euo pipefail

BUILD_DIR="${1:-build}"

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VERSION=$(grep -m1 '^ *VERSION [0-9]' "$REPO_ROOT/CMakeLists.txt" | grep -oE '[0-9]+\.[0-9]+\.[0-9]+')

GUI_BIN="$BUILD_DIR/src/gui/emuleqt"
DAEMON_BIN="$BUILD_DIR/src/daemon/emulecored"

# -- Sanity checks -----------------------------------------------------------

if [ ! -f "$GUI_BIN" ]; then
    echo "Error: GUI binary not found at $GUI_BIN"
    echo "Build the project first:  cmake --build $BUILD_DIR"
    exit 1
fi

if [ ! -f "$DAEMON_BIN" ]; then
    echo "Error: Daemon binary not found at $DAEMON_BIN"
    echo "Build the daemon first:  cmake --build $BUILD_DIR --target emulecored"
    exit 1
fi

# -- Assemble staging directory ----------------------------------------------

STAGE_DIR="$BUILD_DIR/stage/eMuleQt"

rm -rf "$BUILD_DIR/stage"
mkdir -p "$STAGE_DIR"

echo "=== Staging binaries ==="
cp "$GUI_BIN" "$STAGE_DIR/emuleqt"
cp "$DAEMON_BIN" "$STAGE_DIR/emulecored"
chmod +x "$STAGE_DIR/emuleqt" "$STAGE_DIR/emulecored"
echo "  emuleqt"
echo "  emulecored"

# -- Copy config data --------------------------------------------------------

CONFIG_SRC="$REPO_ROOT/data/config"
CONFIG_DST="$STAGE_DIR/config"

if [ -d "$CONFIG_SRC" ]; then
    echo ""
    echo "=== Copying config data ==="
    cp -R "$CONFIG_SRC" "$CONFIG_DST"
    echo "  config/ copied ($(find "$CONFIG_DST" -type f | wc -l | tr -d ' ') files)"
else
    echo "Warning: $CONFIG_SRC not found — skipping config data."
fi

# -- Copy translation files --------------------------------------------------

LANG_DST="$STAGE_DIR/lang"
mkdir -p "$LANG_DST"

LANG_BUILD_DIR="$BUILD_DIR/src/gui"
LANG_SRC_DIR="$REPO_ROOT/lang"

QM_COUNT=0
for qm in "$LANG_BUILD_DIR"/emuleqt_*.qm "$LANG_SRC_DIR"/emuleqt_*.qm; do
    [ -f "$qm" ] || continue
    base="$(basename "$qm")"
    # Skip if already copied (build dir takes precedence)
    [ -f "$LANG_DST/$base" ] && continue
    cp "$qm" "$LANG_DST/$base"
    QM_COUNT=$((QM_COUNT + 1))
done

if [ "$QM_COUNT" -gt 0 ]; then
    echo ""
    echo "=== Copying translation files ==="
    echo "  $QM_COUNT .qm files copied"
else
    echo "Warning: No .qm translation files found — skipping lang bundling."
fi

# -- Create tarball ----------------------------------------------------------

TAR_NAME="emuleqt-v${VERSION}-linux-x86_64.tar.gz"
TAR_PATH="$REPO_ROOT/$TAR_NAME"

echo ""
echo "=== Creating $TAR_NAME ==="
tar czf "$TAR_PATH" -C "$BUILD_DIR/stage" eMuleQt

echo ""
echo "=== Done ==="
echo "Package: $TAR_PATH"
echo ""
echo "Staging contents:"
ls -lh "$STAGE_DIR/"
