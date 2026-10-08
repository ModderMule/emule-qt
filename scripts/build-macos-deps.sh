#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# build-macos-deps.sh — static OpenSSL, zstd and lz4 for a macOS release build
#
# Homebrew builds its libraries for the OS and CPU of the machine it runs on.
# Bundling those made the v0.5.6 app arm64-only and macOS 26-only, although Qt
# itself supports x86_64 and macOS 13.  This builds the three libraries the app
# used to take from Homebrew, as static archives, for a chosen CPU and minimum
# macOS, so nothing of them ends up as a dylib in the bundle.
#
# Usage:
#   MACOSX_DEPLOYMENT_TARGET=13.0 ./scripts/build-macos-deps.sh <arch> <prefix>
#
#   arch     arm64 | x86_64   (cross-building works in both directions)
#   prefix   install dir; gets include/ and lib/
#
# Then configure with:
#   -DOPENSSL_ROOT_DIR=<prefix> -DOPENSSL_USE_STATIC_LIBS=ON
#   -DCMAKE_PREFIX_PATH="<qt>;<prefix>"
#   -DCMAKE_IGNORE_PREFIX_PATH="/opt/homebrew;/usr/local"
# ---------------------------------------------------------------------------

set -euo pipefail

OPENSSL_VERSION=3.5.9
OPENSSL_SHA256=603f5602e2eef00d77fbd429d34dcd5822bb301757a1bc9cdb24c670f1eb859a
ZSTD_VERSION=1.5.7
ZSTD_SHA256=eb33e51f49a15e023950cd7825ca74a4a2b43db8354825ac24fc1b7ee09e6fa3
LZ4_VERSION=1.10.0
LZ4_SHA256=537512904744b35e232912055ccf8ec66d768639ff3abe5788d90d792ec5f48b

if [ $# -ne 2 ]; then
    echo "Usage: MACOSX_DEPLOYMENT_TARGET=<ver> $0 <arm64|x86_64> <prefix>" >&2
    exit 1
fi

ARCH="$1"
case "$ARCH" in
    arm64|x86_64) ;;
    *) echo "Error: arch must be arm64 or x86_64, got '$ARCH'" >&2; exit 1 ;;
esac

if [ -z "${MACOSX_DEPLOYMENT_TARGET:-}" ]; then
    echo "Error: MACOSX_DEPLOYMENT_TARGET is not set" >&2
    exit 1
fi
export MACOSX_DEPLOYMENT_TARGET

mkdir -p "$2"
PREFIX="$(cd "$2" && pwd)"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
JOBS="$(sysctl -n hw.ncpu)"
ARCH_FLAGS="-arch $ARCH -mmacosx-version-min=$MACOSX_DEPLOYMENT_TARGET"

# fetch <url> <sha256> — download, verify, unpack into $WORK
fetch() {
    local url="$1" sha="$2" file
    file="$WORK/$(basename "$url")"
    echo "=== $(basename "$url") ==="
    curl -fsSL --retry 3 --retry-delay 5 -o "$file" "$url"
    echo "$sha  $file" | shasum -a 256 -c - >/dev/null || {
        echo "Error: checksum mismatch for $url" >&2
        exit 1
    }
    tar xzf "$file" -C "$WORK"
}

# -- OpenSSL -----------------------------------------------------------------

fetch "https://github.com/openssl/openssl/releases/download/openssl-$OPENSSL_VERSION/openssl-$OPENSSL_VERSION.tar.gz" "$OPENSSL_SHA256"
(
    cd "$WORK/openssl-$OPENSSL_VERSION"
    # --libdir=lib: some targets default to lib64, which FindOpenSSL would miss
    ./Configure "darwin64-$ARCH-cc" no-shared no-tests no-apps no-docs \
        --prefix="$PREFIX" --libdir=lib --openssldir="$PREFIX/ssl" \
        "-mmacosx-version-min=$MACOSX_DEPLOYMENT_TARGET" >/dev/null
    make -j"$JOBS" >/dev/null
    make install_sw >/dev/null
)

# -- zstd --------------------------------------------------------------------

fetch "https://github.com/facebook/zstd/releases/download/v$ZSTD_VERSION/zstd-$ZSTD_VERSION.tar.gz" "$ZSTD_SHA256"
make -C "$WORK/zstd-$ZSTD_VERSION/lib" -j"$JOBS" libzstd.a \
    CFLAGS="-O3 $ARCH_FLAGS" ASFLAGS="$ARCH_FLAGS" >/dev/null
cp "$WORK/zstd-$ZSTD_VERSION/lib/libzstd.a" "$PREFIX/lib/"
cp "$WORK/zstd-$ZSTD_VERSION/lib/"{zstd.h,zdict.h,zstd_errors.h} "$PREFIX/include/"

# -- lz4 ---------------------------------------------------------------------

fetch "https://github.com/lz4/lz4/releases/download/v$LZ4_VERSION/lz4-$LZ4_VERSION.tar.gz" "$LZ4_SHA256"
make -C "$WORK/lz4-$LZ4_VERSION/lib" -j"$JOBS" liblz4.a \
    CFLAGS="-O3 $ARCH_FLAGS" >/dev/null
cp "$WORK/lz4-$LZ4_VERSION/lib/liblz4.a" "$PREFIX/lib/"
cp "$WORK/lz4-$LZ4_VERSION/lib/"{lz4.h,lz4hc.h,lz4frame.h} "$PREFIX/include/"

# -- Check what was built ----------------------------------------------------
# A silently ignored flag would give the host's CPU or OS version, which is the
# bug this script exists to prevent.

FAILED=0
for lib in libcrypto.a libssl.a libzstd.a liblz4.a; do
    archs="$(lipo -archs "$PREFIX/lib/$lib")"
    minos="$(otool -l "$PREFIX/lib/$lib" | awk '$1 == "minos" { print $2 }' | sort -uV | tail -1)"
    echo "  $lib: [$archs] minos ${minos:-?}"
    if [ "$archs" != "$ARCH" ]; then
        echo "Error: $lib is [$archs], expected $ARCH" >&2
        FAILED=1
    fi
    if [ -z "$minos" ] || [ "$(printf '%s\n%s\n' "$minos" "$MACOSX_DEPLOYMENT_TARGET" | sort -V | tail -1)" != "$MACOSX_DEPLOYMENT_TARGET" ]; then
        echo "Error: $lib has minos ${minos:-?}, above $MACOSX_DEPLOYMENT_TARGET" >&2
        FAILED=1
    fi
done
[ "$FAILED" -eq 0 ] || exit 1

echo "Done: $PREFIX"
