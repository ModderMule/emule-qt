#!/usr/bin/env bash
set -euo pipefail

# Rewrites the update manifest (emuleqt-version.json, served at
# https://emule-qt.org/pub/ and polled by src/gui/app/VersionChecker.cpp) for one
# release. Run by the `manifest` job of .github/workflows/release.yml once the
# GitHub Release has its assets, and by `scripts/publish-release.sh --publish-only`
# as the manual fallback.
#
# Sets latest/date/releaseNotes, the per-platform "downloads" (GitHub asset URL,
# sha256, size), "specialdlp" (versions bundled in the tag), "hosterResolver"
# (resolver.zip + JDownloader SVN revision) and "geoip" (GeoLite2 build date).
# The GUI only ever links to the website front page; the download entries are
# for scripts and a future in-app updater.
#
# Usage:
#   scripts/update-manifest.sh --manifest FILE --version X.Y.Z --slug OWNER/REPO
#                              (--assets DIR | --from-github)
#                              [--geoip GeoLite2-Country.mmdb] [--resolver resolver.zip]
#                              [--repo-root DIR]
#
#   --assets DIR     DIR holds the release files and SHA256SUMS.txt (CI)
#   --from-github    read SHA256SUMS.txt + sizes from the public GitHub release
#   --geoip FILE     already verified against the tag; omitted = "geoip" kept as is
#   --resolver FILE  already verified against the tag; omitted = "hosterResolver" kept
#   --repo-root DIR  checkout of the tag, for the specialdlp versions (default: cwd)
#
# An older version than the manifest's "latest" (a re-run of an old tag) leaves
# the file untouched and exits 0.
#
# CI secrets for the `manifest` job (Settings -> Secrets and variables -> Actions),
# the same values as in .env:
#   PUBLISH_SSH_USER, PUBLISH_SSH_PASS, PUBLISH_SSH_HOST, PUBLISH_REMOTE_PATH
#   PUBLISH_SSH_KNOWN_HOSTS   output of: ssh-keyscan <PUBLISH_SSH_HOST>
# Without them the job warns and skips; run --publish-only locally instead.

MANIFEST="" VERSION="" SLUG="" ASSETS="" FROM_GITHUB=0 GEOIP="" RESOLVER="" REPO_ROOT="."
while [[ $# -gt 0 ]]; do
  case "$1" in
    --manifest)    MANIFEST="$2"; shift 2 ;;
    --version)     VERSION="${2#v}"; shift 2 ;;
    --slug)        SLUG="$2"; shift 2 ;;
    --assets)      ASSETS="$2"; shift 2 ;;
    --from-github) FROM_GITHUB=1; shift ;;
    --geoip)       GEOIP="$2"; shift 2 ;;
    --resolver)    RESOLVER="$2"; shift 2 ;;
    --repo-root)   REPO_ROOT="$2"; shift 2 ;;
    *) echo "error: unknown argument $1" >&2; exit 1 ;;
  esac
done

die() { echo "error: $*" >&2; exit 1; }

[[ -f "$MANIFEST" ]] || die "--manifest file not found: ${MANIFEST}"
[[ "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || die "--version must be X.Y.Z, got '${VERSION}'"
[[ "$SLUG" =~ ^[^/:]+/[^/:]+$ ]] || die "--slug must be owner/repo, got '${SLUG}'"
[[ -n "$ASSETS" || "$FROM_GITHUB" -eq 1 ]] || die "need --assets DIR or --from-github"
[[ -z "$ASSETS" || "$FROM_GITHUB" -eq 0 ]] || die "--assets and --from-github exclude each other"
command -v jq >/dev/null || die "jq not installed"

TAG="v${VERSION}"

sha256_of() {
  if command -v sha256sum >/dev/null; then sha256sum "$1" | awk '{print $1}'
  else shasum -a 256 "$1" | awk '{print $1}'; fi
}
size_of() { wc -c < "$1" | tr -d ' '; }

# --- regression guard -------------------------------------------------------
current="$(jq -r '.latest // ""' "$MANIFEST")"
if [[ -n "$current" && "$current" != "$VERSION" ]]; then
  newest="$(printf '%s\n%s\n' "$current" "$VERSION" | sort -V | tail -n1)"
  if [[ "$newest" == "$current" ]]; then
    echo "Manifest already announces ${current}, newer than ${VERSION} -- left unchanged."
    exit 0
  fi
fi

# --- specialdlp versions bundled in the tag -----------------------------------
# For the future library auto-update (docs/specialdlp-auto-update.local.md)
DLP_DIR="${REPO_ROOT}/external/specialdlp"
DLP_LIB_VERSION="$(sed -n 's/^project(specialdlp VERSION \([0-9.]*\).*/\1/p' "$DLP_DIR/CMakeLists.txt" 2>/dev/null || true)"
DLP_RULES_VERSION="$(sed -n 's/^#define DLPVERSION[[:space:]]*\([0-9]*\).*/\1/p' "$DLP_DIR/specialdlp/specialdlp/antiLeech.h" 2>/dev/null | head -1 || true)"
DLP_ABI="$(sed -n 's/^#define SDLP_ABI_VERSION[[:space:]]*\([0-9]*\).*/\1/p' "$DLP_DIR/include/specialdlp.h" 2>/dev/null || true)"
if [[ -z "$DLP_LIB_VERSION" || -z "$DLP_RULES_VERSION" || -z "$DLP_ABI" ]]; then
  die "could not read the specialdlp versions from ${DLP_DIR}"
fi

# --- release assets -----------------------------------------------------------
# manifest key -> file name; keep in sync with the asset list in release.yml
KEYS=(windows-x64 windows-x64-installer mac-arm64 mac-x64 linux-x64)
FILES=(
  "eMuleQt-${TAG}-win64.zip"
  "eMuleQt-${TAG}-win64-setup.exe"
  "eMuleQt-${TAG}-macOS-arm64.dmg"
  "eMuleQt-${TAG}-macOS-x86_64.dmg"
  "eMuleQt-${TAG}-linux-x86_64.tar.gz"
)

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

if [[ "$FROM_GITHUB" -eq 1 ]]; then
  api="https://api.github.com/repos/${SLUG}/releases/tags/${TAG}"
  curl -fsSL --retry 3 --retry-delay 5 -H "Accept: application/vnd.github+json" \
       -o "$WORK/release.json" "$api" \
    || die "no GitHub release ${TAG} yet (${api}) -- wait for release.yml to finish"
  sums_url="$(jq -r '.assets[] | select(.name == "SHA256SUMS.txt") | .browser_download_url' "$WORK/release.json")"
  [[ -n "$sums_url" ]] || die "release ${TAG} has no SHA256SUMS.txt asset"
  curl -fsSL --retry 3 --retry-delay 5 -o "$WORK/SHA256SUMS.txt" "$sums_url"
  SUMS="$WORK/SHA256SUMS.txt"
else
  SUMS="${ASSETS}/SHA256SUMS.txt"
  [[ -f "$SUMS" ]] || die "${SUMS} not found"
fi

downloads='{}'
for i in "${!KEYS[@]}"; do
  key="${KEYS[$i]}" file="${FILES[$i]}"
  sha="$(awk -v f="$file" '$2 == f || $2 == "*"f {print $1}' "$SUMS")"
  [[ "$sha" =~ ^[0-9a-f]{64}$ ]] || die "${file} missing from SHA256SUMS.txt"
  if [[ "$FROM_GITHUB" -eq 1 ]]; then
    size="$(jq -r --arg n "$file" '.assets[] | select(.name == $n) | .size' "$WORK/release.json")"
  else
    [[ -f "${ASSETS}/${file}" ]] || die "${ASSETS}/${file} not found"
    size="$(size_of "${ASSETS}/${file}")"
  fi
  [[ "$size" =~ ^[1-9][0-9]*$ ]] || die "no size for ${file}"
  url="https://github.com/${SLUG}/releases/download/${TAG}/${file}"
  downloads="$(jq -c --arg k "$key" --arg u "$url" --arg s "$sha" --argjson z "$size" \
                 '. + {($k): {url: $u, sha256: $s, size: $z}}' <<< "$downloads")"
done

# --- hoster resolver ----------------------------------------------------------
resolver='null'
if [[ -n "$RESOLVER" ]]; then
  [[ -f "$RESOLVER" ]] || die "--resolver file not found: ${RESOLVER}"
  rev="$(unzip -p "$RESOLVER" resolver/revision.txt 2>/dev/null | tr -d '[:space:]' || true)"
  resolver="$(jq -nc --arg s "$(sha256_of "$RESOLVER")" --argjson z "$(size_of "$RESOLVER")" --arg r "$rev" \
    '{url: "https://emule-qt.org/pub/resolver.zip", sha256: $s, size: $z}
     + (if ($r | test("^[0-9]+$")) then {jdRevision: ($r | tonumber)} else {} end)')"
  [[ "$rev" =~ ^[0-9]+$ ]] || echo "warning: resolver.zip has no numeric revision.txt -- jdRevision omitted" >&2
fi

# --- GeoLite2 -----------------------------------------------------------------
geoip='null'
if [[ -n "$GEOIP" ]]; then
  [[ -f "$GEOIP" ]] || die "--geoip file not found: ${GEOIP}"
  # build_epoch from the mmdb metadata: a uint64 (extended type 9) after the key
  epoch="$(python3 -I - "$GEOIP" <<'PY' 2>/dev/null || true
import sys
data = open(sys.argv[1], 'rb').read()
meta = data[data.rfind(b'\xab\xcd\xefMaxMind.com') + 14:]
j = meta.index(b'build_epoch') + len(b'build_epoch')
ctrl = meta[j]
if ctrl >> 5 != 0 or meta[j + 1] != 2:
    sys.exit(1)
n = ctrl & 0x1f
print(int.from_bytes(meta[j + 2:j + 2 + n], 'big'))
PY
)"
  src="build_epoch"
  if [[ ! "$epoch" =~ ^[0-9]+$ ]]; then
    echo "warning: no build_epoch in ${GEOIP} -- using the file time" >&2
    epoch="$(date -r "$GEOIP" +%s)"
    src="mtime"
  fi
  built="$(date -u -d "@${epoch}" +%Y-%m-%dT%H:%M:%SZ 2>/dev/null || date -u -r "$epoch" +%Y-%m-%dT%H:%M:%SZ)"
  geoip="$(jq -nc --arg d "$built" --arg s "$(sha256_of "$GEOIP")" --argjson z "$(size_of "$GEOIP")" --arg src "$src" \
    '{database: "GeoLite2-Country", buildDate: $d, sha256: $s, size: $z}
     + (if $src == "mtime" then {buildDateSource: "mtime"} else {} end)')"
fi

# --- write --------------------------------------------------------------------
jq --arg v "$VERSION" \
   --arg d "$(date -u +%Y-%m-%dT%H:%M:%SZ)" \
   --arg r "https://github.com/${SLUG}/releases/tag/${TAG}" \
   --argjson dl "$downloads" \
   --arg dlv "$DLP_LIB_VERSION" --argjson drv "$DLP_RULES_VERSION" --argjson dabi "$DLP_ABI" \
   --argjson res "$resolver" --argjson geo "$geoip" \
   '.latest = $v | .date = $d | .releaseNotes = $r
    | .downloads = $dl
    | .specialdlp = ((.specialdlp // {}) + {libVersion: $dlv, rulesVersion: $drv, abi: $dabi})
    | .specialdlp.downloads = (.specialdlp.downloads // {})
    | if $res != null then .hosterResolver = $res else . end
    | if $geo != null then .geoip = $geo else . end' \
   "$MANIFEST" > "$WORK/manifest.json"

jq -e --arg v "$VERSION" '.latest == $v' "$WORK/manifest.json" >/dev/null || die "rewritten manifest failed validation"
cat "$WORK/manifest.json" > "$MANIFEST"

echo "Manifest updated: latest=${VERSION}"
jq -r '.downloads | to_entries[] | "  \(.key): \(.value.size) bytes"' "$MANIFEST"
jq -r '"  specialdlp \(.specialdlp.libVersion) (DLP v\(.specialdlp.rulesVersion), ABI \(.specialdlp.abi))"' "$MANIFEST"
jq -r 'if .hosterResolver then "  resolver r\(.hosterResolver.jdRevision // "?")" else empty end' "$MANIFEST"
jq -r 'if .geoip then "  geoip \(.geoip.buildDate)" else empty end' "$MANIFEST"
