#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# build-linux-deps.sh — OpenSSL 3.5 and ngtcp2 for the Linux release tarball
#
# The tarball is built on Ubuntu 22.04 (glibc 2.35 floor), whose OpenSSL is
# 3.0 -- no QUIC TLS API, which ngtcp2_crypto_ossl needs (3.5+).  This builds
# OpenSSL 3.5 as shared libs, so Qt's TLS plugin and our code load one libssl
# (a static 3.5 copy next to a system 3.0 libssl would mix versions in one
# process), and ngtcp2 as static archives on top of it.
#
# Usage:
#   ./scripts/build-linux-deps.sh <prefix>
#
#   prefix   install dir; gets include/ and lib/
#
# Then configure with:
#   -DOPENSSL_ROOT_DIR=<prefix> -DOPENSSL_USE_STATIC_LIBS=OFF
#   -DCMAKE_PREFIX_PATH="<qt>;<prefix>"
# and bundle with LD_LIBRARY_PATH=<prefix>/lib, so ldd/linuxdeploy pick these
# (no RUNPATH in the libs; the tarball launchers set LD_LIBRARY_PATH).
#
# Needs: a C compiler ($CC), make, perl, cmake >= 3.20, ninja, curl.
# ---------------------------------------------------------------------------

set -euo pipefail

# Same OpenSSL as scripts/build-macos-deps.sh
OPENSSL_VERSION=3.5.9
OPENSSL_SHA256=603f5602e2eef00d77fbd429d34dcd5822bb301757a1bc9cdb24c670f1eb859a
NGTCP2_VERSION=1.25.0
NGTCP2_SHA256=1c0843076528a87b65e9a9d455100941f4cb65d44f96c5da6ae56df146043955

if [ $# -ne 1 ]; then
    echo "Usage: $0 <prefix>" >&2
    exit 1
fi

mkdir -p "$1"
PREFIX="$(cd "$1" && pwd)"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
JOBS="$(nproc)"
case "$(uname -m)" in
    x86_64)  OSSL_TARGET=linux-x86_64 ;;
    aarch64) OSSL_TARGET=linux-aarch64 ;;
    *) echo "Error: unsupported arch $(uname -m)" >&2; exit 1 ;;
esac

# fetch <url> <sha256> — download, verify, unpack into $WORK
fetch() {
    local url="$1" sha="$2" file
    file="$WORK/$(basename "$url")"
    echo "=== $(basename "$url") ==="
    curl -fsSL --retry 3 --retry-delay 5 -o "$file" "$url"
    echo "$sha  $file" | sha256sum -c - >/dev/null || {
        echo "Error: checksum mismatch for $url" >&2
        exit 1
    }
    tar xzf "$file" -C "$WORK"
}

# -- OpenSSL (shared) --------------------------------------------------------

fetch "https://github.com/openssl/openssl/releases/download/openssl-$OPENSSL_VERSION/openssl-$OPENSSL_VERSION.tar.gz" "$OPENSSL_SHA256"
(
    cd "$WORK/openssl-$OPENSSL_VERSION"
    # --libdir=lib: linux-x86_64 defaults to lib64, which FindOpenSSL would miss.
    # --openssldir: Debian/Ubuntu's, so default CA paths work on the user's box
    # (only compiled in; install_sw writes nothing there).
    ./Configure "$OSSL_TARGET" shared no-tests no-apps no-docs \
        --prefix="$PREFIX" --libdir=lib --openssldir=/usr/lib/ssl >/dev/null
    make -j"$JOBS" >/dev/null
    make install_sw >/dev/null
    # shared only: FindOpenSSL must not pick a static copy
    rm -f "$PREFIX/lib/libssl.a" "$PREFIX/lib/libcrypto.a"
)

# -- ngtcp2 (static, OpenSSL backend) ----------------------------------------

fetch "https://github.com/ngtcp2/ngtcp2/releases/download/v$NGTCP2_VERSION/ngtcp2-$NGTCP2_VERSION.tar.gz" "$NGTCP2_SHA256"
cmake -S "$WORK/ngtcp2-$NGTCP2_VERSION" -B "$WORK/ngtcp2-build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$PREFIX" \
    -DCMAKE_INSTALL_LIBDIR=lib \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    -DENABLE_LIB_ONLY=ON \
    -DENABLE_STATIC_LIB=ON \
    -DENABLE_SHARED_LIB=OFF \
    -DENABLE_OPENSSL=ON \
    -DENABLE_GNUTLS=OFF \
    -DBUILD_TESTING=OFF \
    -DOPENSSL_ROOT_DIR="$PREFIX" \
    -DOPENSSL_USE_STATIC_LIBS=OFF >/dev/null
cmake --build "$WORK/ngtcp2-build" --parallel "$JOBS" >/dev/null
cmake --install "$WORK/ngtcp2-build" >/dev/null

# -- Check what was built ----------------------------------------------------

FAILED=0
for lib in libssl.so.3 libcrypto.so.3 libngtcp2.a libngtcp2_crypto_ossl.a; do
    if [ ! -f "$PREFIX/lib/$lib" ]; then
        echo "Error: $PREFIX/lib/$lib missing" >&2
        FAILED=1
    fi
done
ver="$(grep -a -o -m1 "OpenSSL $OPENSSL_VERSION" "$PREFIX/lib/libcrypto.so.3" | head -1 || true)"
echo "  libcrypto.so.3: ${ver:-unknown version}"
[ -n "$ver" ] || { echo "Error: libcrypto.so.3 is not OpenSSL $OPENSSL_VERSION" >&2; FAILED=1; }
[ "$FAILED" -eq 0 ] || exit 1

echo "Done: $PREFIX"
