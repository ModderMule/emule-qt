#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# bundle-linux.sh — package eMule Qt for Linux x86_64
#
# Creates a tarball with binaries, config data, and translation files.
# Optionally bundles Qt libraries via linuxdeploy for a self-contained
# distribution.
#
# Usage:
#   ./scripts/bundle-linux.sh [deploy-qt] [build-dir]
#
#   deploy-qt   Bundle Qt libraries and plugins via linuxdeploy
#   build-dir   Path to the CMake build directory (default: ./build)
#
# Output:
#   emuleqt-vX.Y.Z-linux-x86_64.tar.gz containing:
#     eMuleQt/
#       emuleqt          GUI executable
#       emulecored       daemon executable
#       lib/             Qt libraries (only with deploy-qt)
#       plugins/         Qt plugins  (only with deploy-qt)
#       config/          default config data (nodes.dat, eMule.tmpl, …)
#         webserver/     web server assets
#       lang/            compiled translation files (.qm)
# ---------------------------------------------------------------------------

set -euo pipefail

DEPLOY_QT=false
BUILD_DIR=""

for arg in "$@"; do
    if [ "$arg" = "deploy-qt" ]; then
        DEPLOY_QT=true
    elif [ -z "$BUILD_DIR" ]; then
        BUILD_DIR="$arg"
    fi
done

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$(cd "${BUILD_DIR:-build}" 2>/dev/null && pwd || echo "$REPO_ROOT/build")"
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

# -- Deploy Qt libraries via linuxdeploy (optional) --------------------------

if [ "$DEPLOY_QT" = true ]; then
    echo ""
    echo "=== Deploying Qt libraries via linuxdeploy ==="

    TOOLS_DIR="$BUILD_DIR/tools"
    mkdir -p "$TOOLS_DIR"

    LINUXDEPLOY="$TOOLS_DIR/linuxdeploy-x86_64.AppImage"
    LINUXDEPLOY_QT="$TOOLS_DIR/linuxdeploy-plugin-qt-x86_64.AppImage"

    # Download linuxdeploy if not cached
    if [ ! -x "$LINUXDEPLOY" ]; then
        echo "  Downloading linuxdeploy..."
        curl -fsSL -o "$LINUXDEPLOY" \
            "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage"
        chmod +x "$LINUXDEPLOY"
    fi

    if [ ! -x "$LINUXDEPLOY_QT" ]; then
        echo "  Downloading linuxdeploy-plugin-qt..."
        curl -fsSL -o "$LINUXDEPLOY_QT" \
            "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage"
        chmod +x "$LINUXDEPLOY_QT"
    fi

    # AppImages need FUSE; in Docker/CI extract them instead.
    # Under QEMU emulation the AppImage runtime itself may fail (exit 127),
    # so we fall back to unsquashfs which extracts the embedded squashfs
    # by finding the offset past the ELF header.
    extract_appimage() {
        local appimage="$1" dest="$2"
        # Try native --appimage-extract first
        if "$appimage" --appimage-extract > /dev/null 2>&1; then
            mv squashfs-root "$dest"
            return 0
        fi
        # Fallback: find squashfs magic (hsqs) offset, extract with dd, then unsquashfs.
        # Needed under QEMU emulation where the AppImage runtime can't execute.
        if command -v unsquashfs > /dev/null 2>&1; then
            local offset
            offset=$(python3 -c "
import sys, struct
data = open(sys.argv[1], 'rb').read()
# Find all 'hsqs' magic occurrences and pick the one that looks like
# a valid squashfs superblock (inode count > 0, block size is power of 2)
pos = 0
best = -1
while True:
    idx = data.find(b'hsqs', pos)
    if idx < 0:
        break
    # Check superblock: bytes 4-7 = inode count, bytes 12-15 = block size
    if idx + 96 <= len(data):
        inodes = struct.unpack_from('<I', data, idx + 4)[0]
        blksz = struct.unpack_from('<I', data, idx + 12)[0]
        if inodes > 0 and blksz > 0 and (blksz & (blksz - 1)) == 0:
            best = idx
            break  # first valid one is usually correct
    pos = idx + 1
print(best if best >= 0 else '')
" "$appimage")
            if [ -n "$offset" ] && [ "$offset" -ge 0 ] 2>/dev/null; then
                echo "    squashfs offset: $offset"
                local tmpimg="/tmp/squashfs_$$.img"
                tail -c +"$((offset + 1))" "$appimage" > "$tmpimg"
                unsquashfs -d "$dest" -f "$tmpimg"
                rm -f "$tmpimg"
                return 0
            fi
            echo "  Error: could not find squashfs magic in $appimage" >&2
        fi
        echo "Error: cannot extract $appimage (no unsquashfs or offset)" >&2
        return 1
    }

    if [ ! -d /dev/fuse ] && [ ! -e /dev/fuse ]; then
        echo "  FUSE not available — extracting AppImages..."
        cd "$TOOLS_DIR"
        [ -d "squashfs-root-linuxdeploy" ] || extract_appimage "$LINUXDEPLOY" squashfs-root-linuxdeploy
        [ -d "squashfs-root-qt-plugin" ]   || extract_appimage "$LINUXDEPLOY_QT" squashfs-root-qt-plugin
        LINUXDEPLOY="$TOOLS_DIR/squashfs-root-linuxdeploy/AppRun"
        # Create a wrapper so linuxdeploy can discover the Qt plugin by name on PATH
        mkdir -p "$TOOLS_DIR/bin"
        cat > "$TOOLS_DIR/bin/linuxdeploy-plugin-qt" <<'WRAPPER'
#!/usr/bin/env bash
exec "$(dirname "$0")/../squashfs-root-qt-plugin/AppRun" "$@"
WRAPPER
        chmod +x "$TOOLS_DIR/bin/linuxdeploy-plugin-qt"
        export PATH="$TOOLS_DIR/bin:$PATH"
        cd "$REPO_ROOT"
    fi

    # Set up an AppDir structure for linuxdeploy
    APPDIR="$BUILD_DIR/stage/AppDir"
    mkdir -p "$APPDIR/usr/bin"
    mkdir -p "$APPDIR/usr/share/applications"
    mkdir -p "$APPDIR/usr/share/icons/hicolor/256x256/apps"

    cp "$STAGE_DIR/emuleqt" "$APPDIR/usr/bin/emuleqt"
    chmod +x "$APPDIR/usr/bin/emuleqt"

    # Minimal .desktop file required by linuxdeploy
    cat > "$APPDIR/usr/share/applications/emuleqt.desktop" <<DESKTOP
[Desktop Entry]
Type=Application
Name=eMuleQt
Exec=emuleqt
Icon=emuleqt
Categories=Network;P2P;
DESKTOP

    # Placeholder icon (linuxdeploy requires one)
    if [ -f "$REPO_ROOT/data/icons/emuleqt.png" ]; then
        cp "$REPO_ROOT/data/icons/emuleqt.png" \
            "$APPDIR/usr/share/icons/hicolor/256x256/apps/emuleqt.png"
    else
        # Create a 1x1 pixel PNG as fallback
        printf '\x89PNG\r\n\x1a\n\x00\x00\x00\rIHDR\x00\x00\x00\x01\x00\x00\x00\x01\x08\x02\x00\x00\x00\x90wS\xde\x00\x00\x00\x0cIDATx\x9cc\xf8\x0f\x00\x00\x01\x01\x00\x05\x18\xd8N\x00\x00\x00\x00IEND\xaeB`\x82' \
            > "$APPDIR/usr/share/icons/hicolor/256x256/apps/emuleqt.png"
    fi

    # Run linuxdeploy with Qt plugin (deploy only, no AppImage output)
    export QMAKE="${QMAKE:-$(command -v qmake 2>/dev/null || echo "")}"
    # Deploy Qt libs into AppDir without producing an AppImage
    "$LINUXDEPLOY" --appdir "$APPDIR" --plugin qt

    # Copy deployed libraries and plugins into our staging directory
    if [ -d "$APPDIR/usr/lib" ]; then
        cp -a "$APPDIR/usr/lib" "$STAGE_DIR/lib"
        echo "  Bundled $(find "$STAGE_DIR/lib" -name '*.so*' | wc -l | tr -d ' ') shared libraries"
    fi
    if [ -d "$APPDIR/usr/plugins" ]; then
        cp -a "$APPDIR/usr/plugins" "$STAGE_DIR/plugins"
        echo "  Bundled $(find "$STAGE_DIR/plugins" -name '*.so' | wc -l | tr -d ' ') Qt plugins"
    fi

    # Create qt.conf so emuleqt finds the bundled libs
    cat > "$STAGE_DIR/qt.conf" <<QTCONF
[Paths]
Prefix = .
Libraries = lib
Plugins = plugins
QTCONF

    # Create launcher script that sets LD_LIBRARY_PATH
    mv "$STAGE_DIR/emuleqt" "$STAGE_DIR/emuleqt.bin"
    cat > "$STAGE_DIR/emuleqt" <<'LAUNCHER'
#!/usr/bin/env bash
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
export LD_LIBRARY_PATH="$SCRIPT_DIR/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec "$SCRIPT_DIR/emuleqt.bin" "$@"
LAUNCHER
    chmod +x "$STAGE_DIR/emuleqt"

    # Same for daemon
    mv "$STAGE_DIR/emulecored" "$STAGE_DIR/emulecored.bin"
    cat > "$STAGE_DIR/emulecored" <<'LAUNCHER'
#!/usr/bin/env bash
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
export LD_LIBRARY_PATH="$SCRIPT_DIR/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec "$SCRIPT_DIR/emulecored.bin" "$@"
LAUNCHER
    chmod +x "$STAGE_DIR/emulecored"

    # Clean up AppDir
    rm -rf "$APPDIR"

    echo "  Qt deployment complete"
fi

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
ls -lhR "$STAGE_DIR/"