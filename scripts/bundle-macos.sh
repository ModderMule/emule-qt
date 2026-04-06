#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# bundle-macos.sh — bundle emulecored into the eMule Qt .app for macOS
#
# Copies the daemon binary into the app bundle so the GUI can auto-detect
# and launch it.  Optionally runs macdeployqt to pull in Qt frameworks.
#
# Usage:
#   ./scripts/bundle-macos.sh [--dmg] [build-dir]
#
#   --dmg       Create a .dmg disk image after bundling
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

CREATE_DMG=false
BUILD_DIR="build"

for arg in "$@"; do
    case "$arg" in
        --dmg) CREATE_DMG=true ;;
        *)     BUILD_DIR="$arg" ;;
    esac
done

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
VERSION=$(grep -m1 '^ *VERSION [0-9]' "$REPO_ROOT/CMakeLists.txt" | grep -oE '[0-9]+\.[0-9]+\.[0-9]+')
CONFIG_SRC="$REPO_ROOT/data/config"
RESOURCES_DIR="$APP_BUNDLE/Contents/Resources"
CONFIG_DST="$RESOURCES_DIR/config"

if [ -d "$CONFIG_SRC" ]; then
    echo "Copying default config data into bundle..."
    mkdir -p "$RESOURCES_DIR"
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

rm -rf "$LANG_DST"
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

# -- Bundle OpenSSL dylib ----------------------------------------------------

FRAMEWORKS_DIR="$APP_BUNDLE/Contents/Frameworks"
mkdir -p "$FRAMEWORKS_DIR"

# Find the absolute OpenSSL path linked by the binaries
OPENSSL_DYLIB=$(otool -L "$MACOS_DIR/emulecored" | grep -o '/.*libcrypto[^[:space:]]*' | head -1)

if [ -n "$OPENSSL_DYLIB" ] && [ -f "$OPENSSL_DYLIB" ]; then
    DYLIB_NAME=$(basename "$OPENSSL_DYLIB")
    echo ""
    echo "=== Bundling OpenSSL ==="
    echo "  Source: $OPENSSL_DYLIB"

    cp "$OPENSSL_DYLIB" "$FRAMEWORKS_DIR/$DYLIB_NAME"

    # Rewrite load path in both binaries
    for bin in "$MACOS_DIR/emuleqt" "$MACOS_DIR/emulecored"; do
        install_name_tool -change "$OPENSSL_DYLIB" "@executable_path/../Frameworks/$DYLIB_NAME" "$bin"
    done

    # Fix the dylib's own install name
    install_name_tool -id "@executable_path/../Frameworks/$DYLIB_NAME" "$FRAMEWORKS_DIR/$DYLIB_NAME"

    # Ad-hoc re-sign (required on Apple Silicon)
    codesign --force --sign - "$FRAMEWORKS_DIR/$DYLIB_NAME"
    codesign --force --sign - "$MACOS_DIR/emuleqt"
    codesign --force --sign - "$MACOS_DIR/emulecored"

    echo "  Bundled and relinked $DYLIB_NAME"
else
    echo "Warning: OpenSSL dylib not found — skipping OpenSSL bundling."
    echo "  The app will only work on machines with OpenSSL installed."
fi

# Bundle any other non-system absolute-path dylibs (e.g. zstd from Homebrew)
for bin in "$MACOS_DIR/emuleqt" "$MACOS_DIR/emulecored"; do
    # Find absolute paths that are NOT /usr/lib (system) or @rpath (already handled)
    for dylib in $(otool -L "$bin" | grep -o '/opt/[^[:space:]]*\|/usr/local/[^[:space:]]*' | sort -u); do
        DYLIB_NAME=$(basename "$dylib")
        if [ -f "$dylib" ] && [ ! -f "$FRAMEWORKS_DIR/$DYLIB_NAME" ]; then
            echo "  Bundling $DYLIB_NAME <- $dylib"
            cp "$dylib" "$FRAMEWORKS_DIR/$DYLIB_NAME"
            install_name_tool -id "@executable_path/../Frameworks/$DYLIB_NAME" "$FRAMEWORKS_DIR/$DYLIB_NAME"
            codesign --force --sign - "$FRAMEWORKS_DIR/$DYLIB_NAME"
        fi
        # Rewrite the reference in the binary
        install_name_tool -change "$dylib" "@executable_path/../Frameworks/$DYLIB_NAME" "$bin"
    done
    codesign --force --sign - "$bin"
done

# -- Verify ------------------------------------------------------------------

echo ""
echo "Bundle contents:"
ls -lh "$MACOS_DIR/"
echo ""
echo "Done. App bundle: $APP_BUNDLE"

# -- Create DMG (optional, --dmg flag) -----------------------------------------

if [ "$CREATE_DMG" = true ]; then
    DMG_OUTPUT="$BUILD_DIR/eMuleQt-v${VERSION}-macOS.dmg"
    APP_NAME="$(basename "$APP_BUNDLE")"

    if command -v create-dmg &>/dev/null; then
        echo ""
        echo "Creating DMG with create-dmg..."
        rm -f "$DMG_OUTPUT"
        create-dmg \
            --volname "eMule Qt" \
            --window-pos 200 120 \
            --window-size 600 400 \
            --icon-size 100 \
            --icon "$APP_NAME" 150 190 \
            --app-drop-link 450 190 \
            "$DMG_OUTPUT" \
            "$APP_BUNDLE"
    else
        echo ""
        echo "Creating DMG with hdiutil..."
        rm -f "$DMG_OUTPUT"
        STAGING_DIR=$(mktemp -d)
        cp -R "$APP_BUNDLE" "$STAGING_DIR/"
        sync
        sleep 2
        hdiutil create -volname "eMule Qt" \
            -srcfolder "$STAGING_DIR/$(basename "$APP_BUNDLE")" \
            -ov -format UDZO \
            "$DMG_OUTPUT"
        rm -rf "$STAGING_DIR"
    fi

    echo "  -> $DMG_OUTPUT ($(du -h "$DMG_OUTPUT" | cut -f1))"
fi
