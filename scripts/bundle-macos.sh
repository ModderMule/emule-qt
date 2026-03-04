#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# bundle-macos.sh — bundle emulecored into the eMule Qt .app for macOS
#
# Copies the daemon binary into the app bundle so the GUI can auto-detect
# and launch it.  Optionally runs macdeployqt to pull in Qt frameworks.
#
# Usage:
#   ./scripts/bundle-macos.sh [build-dir]
#
#   build-dir   Path to the CMake build directory (default: ./build)
#
# Layout after bundling:
#   emuleqt.app/
#     Contents/
#       MacOS/
#         emuleqt          <- GUI executable
#         emulecored       <- daemon executable
#       Frameworks/        <- Qt frameworks (if macdeployqt runs)
#       Resources/
#         config/          <- default config files (nodes.dat, eMule.tmpl, …)
#           webserver/     <- web server assets (sprites, CSS, icons)
#         lang/            <- compiled translation files (.qm)
#       Info.plist
# ---------------------------------------------------------------------------

set -euo pipefail

BUILD_DIR="${1:-build}"

APP_BUNDLE="$BUILD_DIR/src/gui/emuleqt.app"
DAEMON_BIN="$BUILD_DIR/src/daemon/emulecored"
MACOS_DIR="$APP_BUNDLE/Contents/MacOS"

# -- Sanity checks -----------------------------------------------------------

if [ ! -d "$APP_BUNDLE" ]; then
    echo "Error: App bundle not found at $APP_BUNDLE"
    echo "Build the project first:  cmake --build $BUILD_DIR"
    exit 1
fi

if [ ! -f "$DAEMON_BIN" ]; then
    echo "Error: Daemon binary not found at $DAEMON_BIN"
    echo "Build the daemon first:  cmake --build $BUILD_DIR --target emulecored"
    exit 1
fi

if [ ! -f "$MACOS_DIR/emuleqt" ]; then
    echo "Error: GUI binary not found at $MACOS_DIR/emuleqt"
    exit 1
fi

# -- Copy daemon into bundle -------------------------------------------------

echo "Copying emulecored into app bundle..."
cp "$DAEMON_BIN" "$MACOS_DIR/emulecored"
chmod +x "$MACOS_DIR/emulecored"
echo "  -> $MACOS_DIR/emulecored"

# -- Copy default config data into bundle ------------------------------------

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CONFIG_SRC="$REPO_ROOT/data/config"
RESOURCES_DIR="$APP_BUNDLE/Contents/Resources"
CONFIG_DST="$RESOURCES_DIR/config"

if [ -d "$CONFIG_SRC" ]; then
    echo "Copying default config data into bundle..."
    rm -rf "$CONFIG_DST"
    cp -R "$CONFIG_SRC" "$CONFIG_DST"
    echo "  -> $CONFIG_DST  ($(find "$CONFIG_DST" -type f | wc -l | tr -d ' ') files)"
else
    echo "Warning: $CONFIG_SRC not found — skipping config data bundling."
fi

# -- Copy translation files into bundle --------------------------------------

LANG_DST="$RESOURCES_DIR/lang"
# Prefer build-generated .qm files; fall back to source lang/ directory
LANG_BUILD_DIR="$BUILD_DIR/src/gui"
LANG_SRC_DIR="$REPO_ROOT/lang"

mkdir -p "$LANG_DST"
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
    echo "Copied $QM_COUNT translation files into bundle."
    echo "  -> $LANG_DST"
else
    echo "Warning: No .qm translation files found — skipping lang bundling."
fi

# -- Run macdeployqt (optional) ----------------------------------------------

# Find macdeployqt — check common Qt install locations
MACDEPLOYQT=""
for candidate in \
    "$HOME/Qt/6.10.2/macos/bin/macdeployqt" \
    "$HOME/Qt/6.*/macos/bin/macdeployqt" \
    "$(command -v macdeployqt 2>/dev/null || true)"
do
    # Expand globs
    for expanded in $candidate; do
        if [ -x "$expanded" ]; then
            MACDEPLOYQT="$expanded"
            break 2
        fi
    done
done

if [ -n "$MACDEPLOYQT" ]; then
    echo "Running macdeployqt..."
    "$MACDEPLOYQT" "$APP_BUNDLE" -always-overwrite 2>&1 | tail -5 || true
    echo "  macdeployqt complete."
else
    echo "Warning: macdeployqt not found — skipping Qt framework bundling."
    echo "  The app will only work on machines with Qt installed."
    echo "  Set QTDIR or install Qt to enable framework bundling."
fi

# -- Verify ------------------------------------------------------------------

echo ""
echo "Bundle contents:"
ls -lh "$MACOS_DIR/"
echo ""
echo "Done. App bundle: $APP_BUNDLE"
