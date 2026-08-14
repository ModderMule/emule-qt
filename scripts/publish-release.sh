#!/usr/bin/env bash
set -euo pipefail

# Interactive release helper for eMuleQt.
#
# Reads the current version from CMakeLists.txt (the VERSION field), lets you
# either tag that version as-is or pick a new one. When a new version is chosen
# it delegates the multi-file version bump to scripts/bump-version.sh (which
# keeps CMakeLists.txt, config_win.h and vcpkg.json in lockstep -- everything
# else derives its version from those), then creates a release commit +
# annotated git tag and pushes both.
#
# Finally it updates the update manifest that shipped clients poll
# (src/gui/app/VersionChecker.cpp fetches https://emule-qt.org/pub/emuleqt-version.json)
# -- setting "latest", "date" and pointing "releaseNotes" at the GitHub release
# page for the tag just created -- and rsyncs it to the webserver.
#
# Tags use the vMAJOR.MINOR.PATCH form to match existing tags (v0.1.6, ...).
#
# Requires these keys in the project-root .env (gitignored):
#   PUBLISH_SSH_USER     ssh user on the webserver
#   PUBLISH_SSH_PASS     ssh password (fed to sshpass via the environment)
#   PUBLISH_SSH_HOST     webserver hostname
#   PUBLISH_REMOTE_PATH  remote directory that serves /pub/
#   PUBLISH_LOCAL_JSON   local path of emuleqt-version.json
# and the sshpass + jq tools (brew install hudochenkov/sshpass/sshpass, brew install jq).
#
# Pushing the tag triggers .github/workflows/release.yml, which verifies the tag
# matches CMakeLists.txt, builds Linux/macOS/Windows (by calling the per-OS
# workflows) and publishes the GitHub Release -- with the three archives and a
# SHA256SUMS.txt attached -- that the releaseNotes URL below points at.
# Nothing needs to be started by hand.
#
# Usage: scripts/publish-release.sh [--publish-only]
#
#   --publish-only   Skip the whole git side (no bump, no commit, no tag, no push)
#                    and just rewrite + upload the manifest for the version already
#                    in CMakeLists.txt. Its tag must already exist, otherwise the
#                    releaseNotes link would 404. Useful to re-run a failed upload
#                    or to refresh a manifest that has drifted behind the tags.

# --- arguments --------------------------------------------------------------
PUBLISH_ONLY=0
if [[ $# -gt 0 ]]; then
  if [[ $# -eq 1 && "$1" == "--publish-only" ]]; then
    PUBLISH_ONLY=1
  else
    echo "usage: $0 [--publish-only]" >&2
    exit 1
  fi
fi

# --- locate repo root -------------------------------------------------------
if ! ROOT="$(git rev-parse --show-toplevel 2>/dev/null)"; then
  echo "error: not inside a git repository" >&2
  exit 1
fi
cd "$ROOT"

# --- guard: clean working tree ---------------------------------------------
# Only matters when we are about to commit/tag; --publish-only never touches git.
if [[ "$PUBLISH_ONLY" -eq 0 && -n "$(git status --porcelain)" ]]; then
  echo "error: working tree has uncommitted changes; commit or stash them first" >&2
  git status --short >&2
  exit 1
fi

BRANCH="$(git rev-parse --abbrev-ref HEAD)"
REMOTE="origin"

# --- load publish settings from .env ----------------------------------------
# Validated up front, before anything is committed/tagged/pushed, so a
# misconfigured .env can never leave a half-finished release behind.
PUBLISH_SSH_USER=""
PUBLISH_SSH_PASS=""
PUBLISH_SSH_HOST=""
PUBLISH_REMOTE_PATH=""
PUBLISH_LOCAL_JSON=""

ENV_FILE="$ROOT/.env"
if [[ ! -f "$ENV_FILE" ]]; then
  echo "error: ${ENV_FILE} not found (needed for the publish step)" >&2
  exit 1
fi

# Same KEY=VALUE semantics as loadEnvFile() in tests/TestHelpers.h: skip blanks
# and '#' comments, split on the first '=', trim surrounding whitespace. Only
# PUBLISH_* keys are imported -- .env also holds SMTP credentials that have no
# business in this script's environment.
while IFS= read -r line || [[ -n "$line" ]]; do
  line="${line#"${line%%[![:space:]]*}"}"
  [[ -z "$line" || "$line" == \#* ]] && continue
  [[ "$line" != *=* ]] && continue
  key="${line%%=*}"
  val="${line#*=}"
  key="${key//[[:space:]]/}"
  [[ "$key" == PUBLISH_* ]] || continue
  [[ "$key" =~ ^[A-Za-z_][A-Za-z0-9_]*$ ]] || continue
  val="${val#"${val%%[![:space:]]*}"}"
  val="${val%"${val##*[![:space:]]}"}"
  printf -v "$key" '%s' "$val"
done < "$ENV_FILE"

MISSING=()
for key in PUBLISH_SSH_USER PUBLISH_SSH_PASS PUBLISH_SSH_HOST \
           PUBLISH_REMOTE_PATH PUBLISH_LOCAL_JSON; do
  [[ -n "${!key}" ]] || MISSING+=("$key")
done
if [[ "${#MISSING[@]}" -gt 0 ]]; then
  echo "error: missing/empty in ${ENV_FILE}: ${MISSING[*]}" >&2
  exit 1
fi

if [[ ! -f "$PUBLISH_LOCAL_JSON" ]]; then
  echo "error: PUBLISH_LOCAL_JSON does not exist: ${PUBLISH_LOCAL_JSON}" >&2
  exit 1
fi

if ! command -v jq >/dev/null 2>&1; then
  echo "error: jq not installed (needed to rewrite the version manifest)" >&2
  echo "  brew install jq" >&2
  exit 1
fi

if ! command -v sshpass >/dev/null 2>&1; then
  echo "error: sshpass not installed (needed for password-based rsync)" >&2
  echo "  brew install hudochenkov/sshpass/sshpass" >&2
  exit 1
fi

# --- derive the GitHub owner/repo slug from the remote ----------------------
# The remote URL may embed credentials (https://user:token@github.com/...).
# Everything up to and including '@' is dropped so a token can never end up in
# the published manifest.
slug="$(git remote get-url "$REMOTE")"
slug="${slug%.git}"
slug="${slug##*@}"
slug="${slug#https://}"
slug="${slug#http://}"
slug="${slug#github.com[:/]}"
if [[ ! "$slug" =~ ^[^/:]+/[^/:]+$ ]]; then
  echo "error: could not derive owner/repo from ${REMOTE} remote" >&2
  exit 1
fi
SLUG="$slug"

# --- current version + suggested next patch bump ---------------------------
CURRENT="$(grep -m1 '^ *VERSION [0-9]' CMakeLists.txt | grep -oE '[0-9]+\.[0-9]+\.[0-9]+')"
if [[ -z "$CURRENT" ]]; then
  echo "error: could not read VERSION from CMakeLists.txt" >&2
  exit 1
fi

# suggest patch+1
suggest="$CURRENT"
if [[ "$CURRENT" =~ ^([0-9]+)\.([0-9]+)\.([0-9]+)$ ]]; then
  suggest="${BASH_REMATCH[1]}.${BASH_REMATCH[2]}.$(( BASH_REMATCH[3] + 1 ))"
fi

echo "Current version: ${CURRENT}"
echo "Branch:          ${BRANCH} (remote: ${REMOTE} -> ${SLUG})"
echo

# --- pick the version -------------------------------------------------------
if [[ "$PUBLISH_ONLY" -eq 1 ]]; then
  NEW="$CURRENT"
  TAG="v${NEW}"
  # The manifest points at the release page for this tag, so it has to exist.
  if ! git rev-parse -q --verify "refs/tags/${TAG}" >/dev/null; then
    echo "error: --publish-only needs tag ${TAG} to exist already; run without it to create one" >&2
    exit 1
  fi
  echo "Publish-only: republishing the manifest for ${TAG} (no git changes)."
  echo
else
  echo "Press Enter to release the current version (${CURRENT}) as tag v${CURRENT},"
  echo "or type a new version to bump to (suggested next: ${suggest})."
  echo

  read -r -p "Version to release [${CURRENT}]: " NEW
  NEW="${NEW:-$CURRENT}"
  # normalize away a leading 'v'
  NEW="${NEW#v}"

  if [[ ! "$NEW" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo "error: '${NEW}' is not a valid MAJOR.MINOR.PATCH version" >&2
    exit 1
  fi

  TAG="v${NEW}"
  if git rev-parse -q --verify "refs/tags/${TAG}" >/dev/null; then
    echo "error: tag ${TAG} already exists" >&2
    exit 1
  fi
fi

RELEASE_URL="https://github.com/${SLUG}/releases/tag/${TAG}"
RELEASE_DATE="$(date -u +%Y-%m-%dT%H:%M:%SZ)"

# --- bump version files if a new version was chosen -------------------------
BUMPED=0
if [[ "$PUBLISH_ONLY" -eq 0 && "$NEW" != "$CURRENT" ]]; then
  echo
  echo "Bumping version files ${CURRENT} -> ${NEW} via scripts/bump-version.sh ..."
  scripts/bump-version.sh "$NEW"
  BUMPED=1
fi

# --- confirm ----------------------------------------------------------------
echo
echo "About to:"
if [[ "$PUBLISH_ONLY" -eq 1 ]]; then
  echo "  git      : (nothing -- --publish-only, reusing existing tag ${TAG})"
else
  if [[ "$BUMPED" -eq 1 ]]; then
    echo "  commit   : release: ${TAG}   (version files -> ${NEW})"
  else
    echo "  commit   : (none -- tagging existing HEAD at version ${CURRENT})"
  fi
  echo "  tag      : ${TAG}  (annotated)"
  echo "  push     : ${REMOTE} ${BRANCH}  and  ${REMOTE} ${TAG}"
fi
echo "  manifest : ${PUBLISH_LOCAL_JSON}"
echo "             latest=${NEW}  date=${RELEASE_DATE}"
echo "             releaseNotes=${RELEASE_URL}"
echo "  publish  : rsync -> ${PUBLISH_SSH_USER}@${PUBLISH_SSH_HOST}:${PUBLISH_REMOTE_PATH}"
echo
read -r -p "Proceed? [y/N]: " CONFIRM
if [[ ! "$CONFIRM" =~ ^[Yy]$ ]]; then
  echo "aborted"
  if [[ "$BUMPED" -eq 1 ]]; then
    echo "reverting version files"
    git checkout -- .
  fi
  exit 1
fi

# --- commit / tag / push ----------------------------------------------------
if [[ "$PUBLISH_ONLY" -eq 0 ]]; then
  if [[ "$BUMPED" -eq 1 ]]; then
    git add -A
    git commit -m "release: ${TAG}"
  fi
  git tag -a "${TAG}" -m "Release ${TAG}"
  git push "${REMOTE}" "${BRANCH}"
  git push "${REMOTE}" "${TAG}"

  echo
  echo "Released ${TAG}."
fi

# --- update the version manifest --------------------------------------------
# Only latest/date/releaseNotes are touched; the whole "downloads" object is
# passed through untouched.
#
# ToDo: once the GitHub workflows publish real artifacts, also update
# .downloads[*].url / .sha256 / .size from the release assets. Until then those
# entries keep their placeholder not-ready:// URLs, empty hashes and zero sizes.
echo "Updating ${PUBLISH_LOCAL_JSON} ..."
MANIFEST_TMP="$(mktemp)"
trap 'rm -f "$MANIFEST_TMP"' EXIT

if ! jq --arg v "$NEW" --arg d "$RELEASE_DATE" --arg r "$RELEASE_URL" \
        '.latest = $v | .date = $d | .releaseNotes = $r' \
        "$PUBLISH_LOCAL_JSON" > "$MANIFEST_TMP"; then
  echo "error: failed to rewrite ${PUBLISH_LOCAL_JSON}" >&2
  echo "  tag ${TAG} IS already pushed -- update and upload the manifest by hand." >&2
  exit 1
fi
cat "$MANIFEST_TMP" > "$PUBLISH_LOCAL_JSON"

# --- publish the manifest ---------------------------------------------------
echo "Publishing to ${PUBLISH_SSH_USER}@${PUBLISH_SSH_HOST}:${PUBLISH_REMOTE_PATH} ..."
if ! SSHPASS="$PUBLISH_SSH_PASS" rsync -avz \
       -e 'sshpass -e ssh -o StrictHostKeyChecking=accept-new' \
       "$PUBLISH_LOCAL_JSON" \
       "${PUBLISH_SSH_USER}@${PUBLISH_SSH_HOST}:${PUBLISH_REMOTE_PATH}"; then
  echo >&2
  echo "warning: rsync failed -- tag ${TAG} IS pushed and the local manifest IS updated." >&2
  echo "  Re-run just the upload once the server is reachable:" >&2
  echo "    SSHPASS=\"\$PUBLISH_SSH_PASS\" rsync -avz -e 'sshpass -e ssh' \\" >&2
  echo "      '${PUBLISH_LOCAL_JSON}' '${PUBLISH_SSH_USER}@${PUBLISH_SSH_HOST}:${PUBLISH_REMOTE_PATH}'" >&2
  exit 1
fi

echo
echo "Published manifest: latest=${NEW}  ->  ${RELEASE_URL}"
if [[ "$PUBLISH_ONLY" -eq 0 ]]; then
  echo
  echo "The tag push started the release build for all three platforms:"
  echo "  https://github.com/${SLUG}/actions/workflows/release.yml"
  echo "It publishes the release (with assets) at ${RELEASE_URL} when it finishes."
fi
