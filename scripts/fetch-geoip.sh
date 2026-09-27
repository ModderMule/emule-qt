#!/usr/bin/env bash
set -euo pipefail

# Downloads the latest MaxMind GeoLite2-Country database for release bundles.
#
# The bundlers copy data/config wholesale, so the default destination makes the
# .mmdb ship in config/ beside nodes.dat and eMule.tmpl. The daemon's
# GeoIpUpdater adopts a bundled copy when its build is newer than the live one
# (docs/IP2Country.md). data/config/GeoLite2-Country.mmdb is gitignored.
#
# Credentials, never in the repo:
#   MAXMIND_ACCOUNT_ID, MAXMIND_LICENSE_KEY  from the environment (CI secrets),
#   falling back to the project-root .env (gitignored).
# The key goes in HTTP Basic auth only -- never in a URL or on stdout. curl does
# not forward it on MaxMind's redirect to the storage host (no --location-trusted);
# that host rejects an Authorization header anyway.
#
# Usage: scripts/fetch-geoip.sh [dest-dir]     (default: data/config)

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEST="${1:-$ROOT/data/config}"
NAME="GeoLite2-Country.mmdb"
URL="https://download.maxmind.com/geoip/databases/GeoLite2-Country/download"

# --- credentials -----------------------------------------------------------
# Same KEY=VALUE rules as publish-release.sh; only MAXMIND_* keys are imported.
ENV_FILE="$ROOT/.env"
if [[ ( -z "${MAXMIND_ACCOUNT_ID:-}" || -z "${MAXMIND_LICENSE_KEY:-}" ) && -f "$ENV_FILE" ]]; then
  while IFS= read -r line || [[ -n "$line" ]]; do
    line="${line#"${line%%[![:space:]]*}"}"
    [[ -z "$line" || "$line" == \#* || "$line" != *=* ]] && continue
    key="${line%%=*}"
    val="${line#*=}"
    key="${key//[[:space:]]/}"
    [[ "$key" == MAXMIND_* && "$key" =~ ^[A-Za-z_][A-Za-z0-9_]*$ ]] || continue
    [[ -n "${!key:-}" ]] && continue   # the environment wins
    val="${val#"${val%%[![:space:]]*}"}"
    val="${val%"${val##*[![:space:]]}"}"
    if [[ ${#val} -ge 2 && ( "$val" == \"*\" || "$val" == \'*\' ) ]]; then
      val="${val:1:${#val}-2}"
    fi
    printf -v "$key" '%s' "$val"
  done < "$ENV_FILE"
fi

if [[ -z "${MAXMIND_ACCOUNT_ID:-}" || -z "${MAXMIND_LICENSE_KEY:-}" ]]; then
  echo "error: MAXMIND_ACCOUNT_ID / MAXMIND_LICENSE_KEY not set (environment or ${ENV_FILE})" >&2
  exit 1
fi

# --- download + verify -----------------------------------------------------
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# Credentials via a curl config file on stdin, so they never show in `ps`
fetch() {
  printf 'user = "%s:%s"\n' "$MAXMIND_ACCOUNT_ID" "$MAXMIND_LICENSE_KEY" |
    curl --config - -fsSL --retry 3 --retry-delay 5 -o "$2" "$URL?suffix=$1"
}

echo "Downloading GeoLite2-Country..."
if ! fetch tar.gz "$WORK/db.tar.gz" || ! fetch tar.gz.sha256 "$WORK/db.sha256"; then
  echo "error: download failed (wrong account ID / license key, or the daily limit is used up)" >&2
  exit 1
fi

expected="$(awk '{print $1; exit}' "$WORK/db.sha256")"
if command -v sha256sum >/dev/null 2>&1; then
  actual="$(sha256sum "$WORK/db.tar.gz" | awk '{print $1}')"
else
  actual="$(shasum -a 256 "$WORK/db.tar.gz" | awk '{print $1}')"
fi
if [[ -z "$expected" || "$expected" != "$actual" ]]; then
  echo "error: SHA-256 mismatch (expected '${expected}', got '${actual}')" >&2
  exit 1
fi

tar -xzf "$WORK/db.tar.gz" -C "$WORK"
mmdb="$(find "$WORK" -type f -name "$NAME" | head -n1)"
if [[ -z "$mmdb" ]]; then
  echo "error: ${NAME} not found in the archive" >&2
  exit 1
fi

size="$(wc -c < "$mmdb" | tr -d ' ')"
if [[ "$size" -lt 1000000 ]] || ! LC_ALL=C grep -aq 'MaxMind.com' "$mmdb"; then
  echo "error: ${NAME} looks broken (${size} bytes)" >&2
  exit 1
fi

# The tarball's directory carries the build date: GeoLite2-Country_YYYYMMDD
build="$(basename "$(dirname "$mmdb")")"
build="${build##*_}"

mkdir -p "$DEST"
cp "$mmdb" "$DEST/$NAME.tmp"
mv -f "$DEST/$NAME.tmp" "$DEST/$NAME"
echo "OK: ${DEST}/${NAME} (build ${build}, ${size} bytes)"
