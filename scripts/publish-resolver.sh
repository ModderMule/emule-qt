#!/usr/bin/env bash
set -euo pipefail

# Publishes a hoster resolver refresh (resolver.zip, JDownloader plugins) between releases;
# publish-release.sh also runs it (with build; --no-build when it got --no-build) as its
# last step, since release bundles ship no zip.
# Clients with "Update hoster plugins automatically" on (ed3k: hoster: autoUpdate) fetch it
# within a day (Ed3kResolverUpdater, docs/hoster.md "Plugin updates").
#
# Steps: svn update the JD trees, scripts/build-hoster-resolver.sh, upload the zip as
# /pub/resolver-r<rev>.zip (never over /pub/resolver.zip: release tags carry that file's
# SHA-256), then rewrite only the "hosterResolver" entry of the live emuleqt-version.json.
#
# Clients take it only when its "api" (resolver/api.txt = ResolverMain.PROTOCOL) equals the
# protocol they speak, and its jdRevision is newer than what they have. So after a PROTOCOL
# bump, publish a refresh only once a release with that protocol is out: older clients skip
# it anyway, but the manifest holds one entry, and they lose updates until the next refresh.
#
# .env keys as for publish-release.sh: PUBLISH_SSH_USER/PASS/HOST, PUBLISH_REMOTE_PATH,
# PUBLISH_LOCAL_JSON, optional JD_DIR. Needs svn, jq, sshpass, unzip.
#
# Usage: scripts/publish-resolver.sh [--no-build] [--force] [--dry-run]
#   --no-build  use build/hoster-resolver/resolver.zip as it is
#   --force     publish even when the live entry has the same or a newer jdRevision
#   --dry-run   build and print the new entry; upload nothing

NO_BUILD=0 FORCE=0 DRY=0
for arg in "$@"; do
  case "$arg" in
    --no-build) NO_BUILD=1 ;;
    --force)    FORCE=1 ;;
    --dry-run)  DRY=1 ;;
    *) echo "usage: $0 [--no-build] [--force] [--dry-run]" >&2; exit 1 ;;
  esac
done

die() { echo "error: $*" >&2; exit 1; }

ROOT="$(git rev-parse --show-toplevel 2>/dev/null)" || die "not inside a git repository"
cd "$ROOT"

# --- .env: only PUBLISH_* and JD_DIR (same rules as publish-release.sh) ------
PUBLISH_SSH_USER="" PUBLISH_SSH_PASS="" PUBLISH_SSH_HOST="" PUBLISH_REMOTE_PATH="" PUBLISH_LOCAL_JSON=""
JD_DIR_ENV="${JD_DIR:-}"
ENV_FILE="$ROOT/.env"
[[ -f "$ENV_FILE" ]] || die "${ENV_FILE} not found"
while IFS= read -r line || [[ -n "$line" ]]; do
  line="${line#"${line%%[![:space:]]*}"}"
  [[ -z "$line" || "$line" == \#* || "$line" != *=* ]] && continue
  key="${line%%=*}"; val="${line#*=}"
  key="${key//[[:space:]]/}"
  [[ "$key" == PUBLISH_* || "$key" == JD_DIR ]] || continue
  [[ "$key" =~ ^[A-Za-z_][A-Za-z0-9_]*$ ]] || continue
  val="${val#"${val%%[![:space:]]*}"}"; val="${val%"${val##*[![:space:]]}"}"
  if [[ ${#val} -ge 2 && ( "$val" == \"*\" || "$val" == \'*\' ) ]]; then val="${val:1:${#val}-2}"; fi
  printf -v "$key" '%s' "$val"
done < "$ENV_FILE"
for key in PUBLISH_SSH_USER PUBLISH_SSH_PASS PUBLISH_SSH_HOST PUBLISH_REMOTE_PATH; do
  [[ -n "${!key}" ]] || die "missing/empty in ${ENV_FILE}: ${key}"
done
for tool in jq unzip; do command -v "$tool" >/dev/null || die "$tool not installed"; done
[[ "$DRY" -eq 1 ]] || command -v sshpass >/dev/null || die "sshpass not installed (brew install hudochenkov/sshpass/sshpass)"

# --- build ---------------------------------------------------------------------
JD_SRC="${JD_DIR_ENV:-${JD_DIR:-/Volumes/MySSD/Coding/Java}}"
ZIP="$ROOT/build/hoster-resolver/resolver.zip"
if [[ "$NO_BUILD" -eq 0 ]]; then
  command -v svn >/dev/null || die "svn not installed (brew install subversion)"
  [[ -d "$JD_SRC/JDownloader/src" ]] || die "JDownloader sources not found at ${JD_SRC} (set JD_DIR in .env)"
  for p in JDownloader AppWorkUtils JDBrowser MyJDownloaderClient; do
    [[ -d "$JD_SRC/$p/.svn" ]] || continue
    echo "svn update $JD_SRC/$p ..."
    svn update --non-interactive --accept postpone "$JD_SRC/$p" || die "svn update failed for $p"
    if svn status "$JD_SRC/$p" | grep -qE '^(C|.C|.{6}C)'; then die "svn update left conflicts in $p"; fi
  done
  scripts/build-hoster-resolver.sh "$JD_SRC" "$ROOT/build/hoster-resolver" || die "resolver build failed"
fi
[[ -f "$ZIP" ]] || die "${ZIP} missing (run scripts/build-hoster-resolver.sh)"

REV="$(unzip -p "$ZIP" resolver/revision.txt 2>/dev/null | tr -d '[:space:]' || true)"
API="$(unzip -p "$ZIP" resolver/api.txt 2>/dev/null | tr -d '[:space:]' || true)"
[[ "$REV" =~ ^[0-9]+$ ]] || die "resolver.zip has no numeric revision.txt"
[[ "$API" =~ ^[0-9]+$ ]] || die "resolver.zip has no api.txt (rebuild with the current build script)"
SHA="$(shasum -a 256 "$ZIP" | awk '{print $1}')"
SIZE="$(wc -c < "$ZIP" | tr -d ' ')"
NAME="resolver-r${REV}.zip"
ENTRY="$(jq -nc --arg u "https://emule-qt.org/pub/${NAME}" --arg s "$SHA" --argjson z "$SIZE" \
  --argjson r "$REV" --argjson a "$API" '{url: $u, sha256: $s, size: $z, jdRevision: $r, api: $a}')"
echo "new entry: ${ENTRY}"
[[ "$DRY" -eq 0 ]] || exit 0

# --- upload ----------------------------------------------------------------------
SSH_CMD='sshpass -e ssh -o StrictHostKeyChecking=accept-new -o PubkeyAuthentication=no -o PreferredAuthentications=password,keyboard-interactive'
REMOTE="${PUBLISH_SSH_USER}@${PUBLISH_SSH_HOST}:${PUBLISH_REMOTE_PATH%/}"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

echo "Pulling the live manifest ..."
SSHPASS="$PUBLISH_SSH_PASS" rsync -az -e "$SSH_CMD" "${REMOTE}/emuleqt-version.json" "$WORK/manifest.json" \
  || die "could not download the live manifest"
LIVE_REV="$(jq -r '.hosterResolver.jdRevision // 0' "$WORK/manifest.json")"
if [[ "$FORCE" -eq 0 && "$LIVE_REV" -ge "$REV" ]]; then
  echo "The live manifest already names r${LIVE_REV} (this build: r${REV}); nothing to do (--force to publish anyway)."
  exit 0
fi

cp "$ZIP" "$WORK/$NAME"
echo "Uploading ${NAME} (sha256 ${SHA}) ..."
SSHPASS="$PUBLISH_SSH_PASS" rsync -avz -e "$SSH_CMD" "$WORK/$NAME" "${REMOTE}/" || die "zip upload failed"
# served as uploaded: clients check size and SHA-256
curl -fsSL --retry 3 -o "$WORK/check.zip" "https://emule-qt.org/pub/${NAME}?t=$(date +%s)" || die "uploaded zip not reachable"
[[ "$(shasum -a 256 "$WORK/check.zip" | awk '{print $1}')" == "$SHA" ]] || die "served zip differs from the upload"

jq --argjson e "$ENTRY" '.hosterResolver = $e' "$WORK/manifest.json" > "$WORK/manifest.new"
mv "$WORK/manifest.new" "$WORK/manifest.json"
[[ -n "$PUBLISH_LOCAL_JSON" ]] && cp "$WORK/manifest.json" "$PUBLISH_LOCAL_JSON"
echo "Publishing the manifest ..."
SSHPASS="$PUBLISH_SSH_PASS" rsync -az -e "$SSH_CMD" "$WORK/manifest.json" "${REMOTE}/emuleqt-version.json" \
  || die "manifest upload failed (the zip is up; re-run with --no-build)"
echo "Published resolver r${REV} (api ${API}) -> https://emule-qt.org/pub/${NAME}"
