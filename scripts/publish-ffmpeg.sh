#!/usr/bin/env bash
set -euo pipefail

# Repackages ffmpeg + ffprobe for every platform and publishes them for the hoster resolver
# (Ed3kFfmpegTool, docs/ffmpeg-tools.local.md). JD's HLS/DASH plugins need both; clients
# without them fetch this platform's zip from the "ffmpeg" entry of emuleqt-version.json.
#
# Each zip holds ffmpeg[.exe] and ffprobe[.exe] at its root, nothing else:
#   /pub/ffmpeg-<version>-<platform>.zip   (never overwritten: a new version, a new name)
# Upstream (static builds, all of one FFmpeg release):
#   windows-x64/-arm64, linux-x64/-arm64: github.com/BtbN/FFmpeg-Builds (GPL, static)
#   mac-arm64/-x64:                       ffmpeg.martin-riedl.de (signed, static)
#
# .env keys as for publish-resolver.sh: PUBLISH_SSH_USER/PASS/HOST, PUBLISH_REMOTE_PATH,
# PUBLISH_LOCAL_JSON. Needs curl, jq, tar, python3; sshpass + rsync to upload.
#
# Usage: scripts/publish-ffmpeg.sh [--dry-run] [--out DIR] [--only PLATFORM]...
#   --dry-run       build the zips into DIR and print the manifest entry; upload nothing
#   --out DIR       work folder (default build/ffmpeg)
#   --only P        just this platform (repeatable); a dry run only (the entry needs all)

# --- upstream: bump together ---------------------------------------------------
VERSION="9.0.2"
BTBN="https://github.com/BtbN/FFmpeg-Builds/releases/download/autobuild-2026-11-17-12-40"
BTBN_NAME="ffmpeg-n9.0.2-57-gd838f9f9ae"
RIEDL="https://ffmpeg.martin-riedl.de/download/macos"
declare -A SRC=(
  [windows-x64]="$BTBN/${BTBN_NAME}-win64-gpl-9.0.zip"
  [windows-arm64]="$BTBN/${BTBN_NAME}-winarm64-gpl-9.0.zip"
  [linux-x64]="$BTBN/${BTBN_NAME}-linux64-gpl-9.0.tar.xz"
  [linux-arm64]="$BTBN/${BTBN_NAME}-linuxarm64-gpl-9.0.tar.xz"
  [mac-arm64]="$RIEDL/arm64/1789931890_9.0.2"
  [mac-x64]="$RIEDL/amd64/1789931006_9.0.2"
)
PLATFORMS=(windows-x64 windows-arm64 mac-arm64 mac-x64 linux-x64 linux-arm64)
PUB="https://emule-qt.org/pub"

DRY=0 OUT="" ONLY=()
while [[ $# -gt 0 ]]; do
  case "$1" in
    --dry-run) DRY=1 ;;
    --out)     OUT="$2"; shift ;;
    --only)    ONLY+=("$2"); shift ;;
    *) echo "usage: $0 [--dry-run] [--out DIR] [--only PLATFORM]..." >&2; exit 1 ;;
  esac
  shift
done

die() { echo "error: $*" >&2; exit 1; }
ROOT="$(git rev-parse --show-toplevel 2>/dev/null)" || die "not inside a git repository"
cd "$ROOT"
OUT="${OUT:-$ROOT/build/ffmpeg}"
PY="$(command -v python3 || command -v python)" || die "python3 not installed"
for tool in curl jq tar; do command -v "$tool" >/dev/null || die "$tool not installed"; done
if [[ ${#ONLY[@]} -gt 0 ]]; then
  PLATFORMS=("${ONLY[@]}")
  [[ "$DRY" -eq 1 ]] || die "--only needs --dry-run (the manifest entry names every platform)"
fi

mkdir -p "$OUT"
ENTRIES="{}"
for p in "${PLATFORMS[@]}"; do
  src="${SRC[$p]:-}"
  [[ -n "$src" ]] || die "unknown platform $p"
  work="$OUT/$p"
  rm -rf "$work"; mkdir -p "$work/dl" "$work/x"
  echo "== $p"
  case "$p" in
    mac-*)
      for b in ffmpeg ffprobe; do
        curl -fL --retry 3 -o "$work/dl/$b.zip" "$src/$b.zip" || die "$p: $b download failed"
        "$PY" -I -c 'import sys,zipfile; zipfile.ZipFile(sys.argv[1]).extractall(sys.argv[2])' "$work/dl/$b.zip" "$work/x"
      done ;;
    *)
      file="$work/dl/$(basename "$src")"
      curl -fL --retry 3 -o "$file" "$src" || die "$p: download failed"
      if [[ "$file" == *.zip ]]; then
        "$PY" -I -c 'import sys,zipfile; zipfile.ZipFile(sys.argv[1]).extractall(sys.argv[2])' "$file" "$work/x"
      else
        tar -xf "$file" -C "$work/x"
      fi ;;
  esac
  ext=""; [[ "$p" == windows-* ]] && ext=".exe"
  ff="$(find "$work/x" -type f -name "ffmpeg$ext" | head -1)"
  fp="$(find "$work/x" -type f -name "ffprobe$ext" | head -1)"
  [[ -n "$ff" && -n "$fp" ]] || die "$p: ffmpeg$ext/ffprobe$ext not in the upstream archive"
  name="ffmpeg-${VERSION}-${p}.zip"
  zip="$OUT/$name"
  # flat, exec bits kept (Ed3kFfmpegTool sets them anyway)
  "$PY" -I - "$zip" "$ff" "$fp" <<'EOF'
import os, sys, zipfile
out, *files = sys.argv[1:]
with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    for f in files:
        i = zipfile.ZipInfo.from_file(f, os.path.basename(f))
        i.external_attr = (0o100755 << 16)
        i.compress_type = zipfile.ZIP_DEFLATED
        with open(f, "rb") as fh:
            z.writestr(i, fh.read(), compresslevel=9)
EOF
  sha="$("$PY" -I -c 'import hashlib,sys; print(hashlib.sha256(open(sys.argv[1],"rb").read()).hexdigest())' "$zip")"
  size="$(wc -c < "$zip" | tr -d ' ')"
  ENTRIES="$(jq -c --arg k "$p" --arg u "$PUB/$name" --arg s "$sha" --argjson z "$size" \
    '. + {($k): {url: $u, sha256: $s, size: $z}}' <<<"$ENTRIES")"
  rm -rf "$work"
  echo "   $name  $size bytes  sha256 $sha"
done

ENTRY="$(jq -c --arg v "$VERSION" '{version: $v, downloads: .}' <<<"$ENTRIES")"
echo "new \"ffmpeg\" entry:"
jq . <<<"$ENTRY"
[[ "$DRY" -eq 0 ]] || exit 0

# --- upload (same .env rules as publish-resolver.sh) ------------------------------
PUBLISH_SSH_USER="" PUBLISH_SSH_PASS="" PUBLISH_SSH_HOST="" PUBLISH_REMOTE_PATH="" PUBLISH_LOCAL_JSON=""
ENV_FILE="$ROOT/.env"
[[ -f "$ENV_FILE" ]] || die "${ENV_FILE} not found"
while IFS= read -r line || [[ -n "$line" ]]; do
  line="${line#"${line%%[![:space:]]*}"}"
  [[ -z "$line" || "$line" == \#* || "$line" != *=* ]] && continue
  key="${line%%=*}"; val="${line#*=}"
  key="${key//[[:space:]]/}"
  [[ "$key" == PUBLISH_* ]] || continue
  [[ "$key" =~ ^[A-Za-z_][A-Za-z0-9_]*$ ]] || continue
  val="${val#"${val%%[![:space:]]*}"}"; val="${val%"${val##*[![:space:]]}"}"
  if [[ ${#val} -ge 2 && ( "$val" == \"*\" || "$val" == \'*\' ) ]]; then val="${val:1:${#val}-2}"; fi
  printf -v "$key" '%s' "$val"
done < "$ENV_FILE"
for key in PUBLISH_SSH_USER PUBLISH_SSH_PASS PUBLISH_SSH_HOST PUBLISH_REMOTE_PATH; do
  [[ -n "${!key}" ]] || die "missing/empty in ${ENV_FILE}: ${key}"
done
command -v sshpass >/dev/null || die "sshpass not installed"
REMOTE_HOST="${PUBLISH_SSH_USER}@${PUBLISH_SSH_HOST}"
REMOTE="${REMOTE_HOST}:${PUBLISH_REMOTE_PATH%/}"
WORK="$(mktemp -d)"
# one SSH login for the whole run: every rsync rides this master (the host blocks repeated logins);
# socket in /tmp, the macOS $TMPDIR path is too long for a unix socket
SOCK_DIR="$(mktemp -d /tmp/ffpub.XXXXXX)"
SOCK="$SOCK_DIR/s"
trap 'ssh -o ControlPath="$SOCK" -O exit "$REMOTE_HOST" 2>/dev/null; rm -rf "$WORK" "$SOCK_DIR"' EXIT
SSHPASS="$PUBLISH_SSH_PASS" sshpass -e ssh -o StrictHostKeyChecking=accept-new -o PubkeyAuthentication=no \
  -o PreferredAuthentications=password,keyboard-interactive \
  -o ControlMaster=yes -o ControlPath="$SOCK" -o ControlPersist=600 -fN "$REMOTE_HOST" \
  || die "SSH login failed"
SSH_CMD="ssh -o ControlMaster=no -o ControlPath=$SOCK -o BatchMode=yes"

FILES=()
for p in "${PLATFORMS[@]}"; do FILES+=("$OUT/ffmpeg-${VERSION}-${p}.zip"); done
echo "Uploading ${#FILES[@]} zips ..."
rsync -az --ignore-existing -e "$SSH_CMD" "${FILES[@]}" "${REMOTE}/" || die "zip upload failed"
for p in "${PLATFORMS[@]}"; do
  name="ffmpeg-${VERSION}-${p}.zip"
  # served as uploaded: clients check size and SHA-256
  want="$(jq -r --arg k "$p" '.downloads[$k].sha256' <<<"$ENTRY")"
  curl -fsSL --retry 3 -o "$WORK/check.zip" "$PUB/${name}?t=$(date +%s)" || die "$name not reachable"
  got="$("$PY" -I -c 'import hashlib,sys; print(hashlib.sha256(open(sys.argv[1],"rb").read()).hexdigest())' "$WORK/check.zip")"
  [[ "$got" == "$want" ]] || die "served $name differs from the build (an older upload of that name?)"
  echo "   $name  served hash OK"
done

echo "Pulling the live manifest ..."
rsync -az -e "$SSH_CMD" "${REMOTE}/emuleqt-version.json" "$WORK/manifest.json" \
  || die "could not download the live manifest"
jq --argjson e "$ENTRY" '.ffmpeg = $e' "$WORK/manifest.json" > "$WORK/manifest.new"
mv "$WORK/manifest.new" "$WORK/manifest.json"
[[ -n "$PUBLISH_LOCAL_JSON" ]] && cp "$WORK/manifest.json" "$PUBLISH_LOCAL_JSON"
echo "Publishing the manifest ..."
rsync -az -e "$SSH_CMD" "$WORK/manifest.json" "${REMOTE}/emuleqt-version.json" \
  || die "manifest upload failed (the zips are up; re-run)"
echo "Published ffmpeg ${VERSION} for ${#PLATFORMS[@]} platforms"
