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
