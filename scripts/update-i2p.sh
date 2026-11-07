#!/usr/bin/env bash
set -euo pipefail

# Pins the I2P router (emuleqt-i2pd) the core downloads on demand to a release of the
# fork: reads the release's SHA256SUMS and rewrites src/core/net/I2pdRelease.h (tag +
# one SHA-256 per platform). Not shipped with the app (I2pdInstaller).
#
# Usage:
#   scripts/update-i2p.sh [TAG]      TAG = emuleqt-<i2pd version>-<n>; default: the
#                                    latest emuleqt-* release
#   I2PD_REPO=owner/repo             default ed3k-protocol/i2pd
#   GITHUB_TOKEN=...                 optional, against the API rate limit

REPO="${I2PD_REPO:-ed3k-protocol/i2pd}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/src/core/net/I2pdRelease.h"
ASSETS=(i2pd-linux-x86_64.tar.gz i2pd-linux-aarch64.tar.gz i2pd-macos-universal.tar.gz i2pd-windows-x64.zip)
# the constant per asset in I2pdRelease.h
NAMES=(kLinuxX64 kLinuxArm64 kMacUniversal kWindowsX64)

auth=()
[[ -n "${GITHUB_TOKEN:-}" ]] && auth=(-H "Authorization: Bearer $GITHUB_TOKEN")

tag="${1:-}"
if [[ -z "$tag" ]]; then
    # newest emuleqt-* release, not upstream's own tags
    tag="$(curl -fsSL "${auth[@]}" "https://api.github.com/repos/$REPO/releases?per_page=30" \
        | python3 -c 'import json,sys
for r in json.load(sys.stdin):
    if r["tag_name"].startswith("emuleqt-") and not r["draft"] and not r["prerelease"]:
        print(r["tag_name"]); break')"
    [[ -n "$tag" ]] || { echo "no emuleqt-* release in $REPO" >&2; exit 1; }
fi
[[ "$tag" == emuleqt-* ]] || { echo "not an emuleqt-* tag: $tag" >&2; exit 1; }

sums="$(curl -fsSL "${auth[@]}" "https://github.com/$REPO/releases/download/$tag/SHA256SUMS")" \
    || { echo "no SHA256SUMS in release $tag" >&2; exit 1; }

lines=()
for i in "${!ASSETS[@]}"; do
    a="${ASSETS[$i]}"
    # sha256sum -b format: "<hash> *<name>"
    h="$(awk -v n="$a" '{ f=$2; sub(/^\*/, "", f); if (f == n) print $1 }' <<<"$sums")"
    [[ "$h" =~ ^[0-9a-fA-F]{64}$ ]] || { echo "$a missing in SHA256SUMS of $tag" >&2; exit 1; }
    lines+=("inline constexpr char ${NAMES[$i]}[] = \"$(tr 'A-F' 'a-f' <<<"$h")\";")
done

cat > "$OUT" <<EOF
#pragma once

/// @file I2pdRelease.h
/// @brief Pinned release of the I2P router (emuleqt-i2pd) the core downloads on demand.
///
/// Written by scripts/update-i2p.sh from the release's SHA256SUMS — don't edit by hand.
/// A release of github.com/ed3k-protocol/i2pd, built by its emuleqt-release.yml on a tag
/// emuleqt-<version>-<n>.

namespace eMule::i2pd_release {

inline constexpr char kRepo[] = "$REPO";
inline constexpr char kTag[] = "$tag";

$(printf '%s\n' "${lines[@]}")

} // namespace eMule::i2pd_release
EOF

echo "src/core/net/I2pdRelease.h -> $tag"
printf '  %s\n' "${lines[@]}"
echo "users get it on the next start of the own router (old install kept if the download fails)"
