#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# make-appimage.sh — wrap the Linux release tarball into an AppImage (#194)
#
# Repacks the tarball from bundle-linux.sh as is: AppRun runs the
# tarball's emuleqt (or, with --emulecored, emulecored) launcher script.
# No rebuild, so release.yml runs it beside the macOS/Windows builds.
#
# Usage:
#   ./scripts/make-appimage.sh <eMuleQt-vX.Y.Z-linux-<arch>.tar.gz> [out-dir]
#
# Output: <out-dir>/eMuleQt-vX.Y.Z-linux-<arch>.AppImage (default out-dir: the
# tarball's dir). arch = x86_64 (released) or aarch64; run on a host of that arch.
#
# Tools are pinned and SHA-256 checked. The runtime is passed explicitly, else
# appimagetool fetches the "continuous" one at build time. To bump: new tag +
# digest from the release page (gh api .../releases/tags/<tag> lists digests).
# ---------------------------------------------------------------------------

set -euo pipefail

APPIMAGETOOL_TAG="1.9.1"
RUNTIME_TAG="20251108"

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ICON_SRC="$REPO_ROOT/resources/icons/app/emuleqt-256.png"

TARBALL="${1:?usage: $0 <eMuleQt-vX.Y.Z-linux-<arch>.tar.gz> [out-dir]}"
[ -f "$TARBALL" ] || { echo "Error: $TARBALL not found" >&2; exit 1; }
TARBALL="$(cd "$(dirname "$TARBALL")" && pwd)/$(basename "$TARBALL")"
OUT_DIR="${2:-$(dirname "$TARBALL")}"
mkdir -p "$OUT_DIR"
OUT_DIR="$(cd "$OUT_DIR" && pwd)"

NAME="$(basename "$TARBALL" .tar.gz)"
case "$NAME" in
    eMuleQt-v*-linux-x86_64)
        ARCH="x86_64"
        APPIMAGETOOL_SHA256="ed4ce84f0d9caff66f50bcca6ff6f35aae54ce8135408b3fa33abfc3cb384eb0"
        RUNTIME_SHA256="2fca8b443c92510f1483a883f60061ad09b46b978b2631c807cd873a47ec260d" ;;
    eMuleQt-v*-linux-aarch64)
        ARCH="aarch64"
        APPIMAGETOOL_SHA256="f0837e7448a0c1e4e650a93bb3e85802546e60654ef287576f46c71c126a9158"
        RUNTIME_SHA256="00cbdfcf917cc6c0ff6d3347d59e0ca1f7f45a6df1a428a0d6d8a78664d87444" ;;
    *) echo "Error: expected eMuleQt-vX.Y.Z-linux-<x86_64|aarch64>.tar.gz, got $(basename "$TARBALL")" >&2; exit 1 ;;
esac
OUT="$OUT_DIR/$NAME.AppImage"
[ -f "$ICON_SRC" ] || { echo "Error: icon missing: $ICON_SRC" >&2; exit 1; }

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# -- Pinned tools --------------------------------------------------------------

fetch() {
    local url="$1" sha="$2" dest="$3"
    curl -fsSL --retry 8 --retry-delay 15 --retry-all-errors -o "$dest" "$url"
    echo "$sha  $dest" | sha256sum -c --quiet - \
        || { echo "Error: SHA-256 mismatch for $url" >&2; exit 1; }
}

echo "=== Fetching appimagetool $APPIMAGETOOL_TAG + runtime $RUNTIME_TAG ==="
APPIMAGETOOL="$WORK/appimagetool-$ARCH.AppImage"
RUNTIME="$WORK/runtime-$ARCH"
fetch "https://github.com/AppImage/appimagetool/releases/download/$APPIMAGETOOL_TAG/appimagetool-$ARCH.AppImage" \
    "$APPIMAGETOOL_SHA256" "$APPIMAGETOOL"
fetch "https://github.com/AppImage/type2-runtime/releases/download/$RUNTIME_TAG/runtime-$ARCH" \
    "$RUNTIME_SHA256" "$RUNTIME"
chmod +x "$APPIMAGETOOL"

# AppImage magic "AI\x02" at ELF offset 8 trips binfmt under QEMU/Rosetta
# ("Exec format error"); the runtime ignores the bytes, so zero them on copies
unmagic() { printf '\0\0\0' | dd of="$1" bs=1 seek=8 count=3 conv=notrunc status=none; }
unmagic "$APPIMAGETOOL"

# -- AppDir ----------------------------------------------------------------------

echo "=== Building AppDir from $(basename "$TARBALL") ==="
tar xzf "$TARBALL" -C "$WORK"
APPDIR="$WORK/eMuleQt"
[ -x "$APPDIR/emuleqt" ] || { echo "Error: tarball has no eMuleQt/emuleqt launcher" >&2; exit 1; }

# --emulecored (AppConfig::kAppImageCoreArg): the GUI starts the core as its own
# instance of the image, so the core keeps a mount of its own after the GUI
# quits. Extract-and-run: the GUI starts emulecored from a hard-linked copy
cat > "$APPDIR/AppRun" <<'APPRUN'
#!/bin/sh -
HERE="$(dirname "$(readlink -f -- "$0")")"
if [ "${1-}" = "--emulecored" ]; then
    shift
    exec "$HERE/emulecored" "$@"
fi
exec "$HERE/emuleqt" "$@"
APPRUN
chmod +x "$APPDIR/AppRun"
[ -x "$APPDIR/emulecored" ] || { echo "Error: tarball has no eMuleQt/emulecored launcher" >&2; exit 1; }
cp "$ICON_SRC" "$APPDIR/emuleqt.png"
ln -s emuleqt.png "$APPDIR/.DirIcon"
# same entry FileAssociation writes; the app registers link types itself (wizard)
cat > "$APPDIR/emuleqt.desktop" <<'DESKTOP'
[Desktop Entry]
Type=Application
Name=eMule Qt
Comment=Peer-to-peer and Usenet client
Exec=emuleqt %U
Icon=emuleqt
StartupWMClass=emuleqt
Terminal=false
Categories=Network;FileTransfer;P2P;
DESKTOP

# -- Pack ------------------------------------------------------------------------

echo "=== Packing $(basename "$OUT") ==="
rm -f "$OUT"
# extract-and-run: CI runners and containers have no FUSE for the tool itself
APPIMAGE_EXTRACT_AND_RUN=1 ARCH="$ARCH" "$APPIMAGETOOL" --no-appstream \
    --runtime-file "$RUNTIME" "$APPDIR" "$OUT"
chmod +x "$OUT"

# smoke check on a copy: the image holds the launcher, GUI binary and .desktop
cp "$OUT" "$WORK/check.AppImage"
unmagic "$WORK/check.AppImage"
listing="$(cd "$WORK" && ./check.AppImage --appimage-extract '[Ae]*' >/dev/null && ls -A squashfs-root)"
for f in AppRun emuleqt emuleqt.bin emulecored emuleqt.desktop; do
    grep -qx "$f" <<<"$listing" || { echo "Error: $f missing from the AppImage" >&2; exit 1; }
done

echo "Done: $OUT ($(du -h "$OUT" | cut -f1))"
