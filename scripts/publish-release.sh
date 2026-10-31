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
# The update manifest that shipped clients poll (src/gui/app/VersionChecker.cpp
# fetches https://emule-qt.org/pub/emuleqt-version.json) is NOT written here: the
# `manifest` job of release.yml runs scripts/update-manifest.sh once the GitHub
# Release has its assets, so a version is only announced when it can be
# downloaded. It needs the PUBLISH_SSH_* repo secrets (see update-manifest.sh).
#
# Before tagging it also downloads the GeoLite2-Country database via
# scripts/fetch-geoip.sh (MAXMIND_ACCOUNT_ID / MAXMIND_LICENSE_KEY in .env) and
# rsyncs it next to the manifest; release.yml's geoip job fetches it from
# https://emule-qt.org/pub/ and checks it against the SHA-256 the tag message
# carries. CI has no MaxMind credentials.
#
# The hoster resolver (resolver.zip) goes the same way: built here by
# scripts/build-hoster-resolver.sh from the JDownloader sources (JD_DIR in .env or
# the environment, default /Volumes/MySSD/Coding/Java), rsynced to /pub/ and its
# SHA-256 put in the tag.
# CI has no JD sources.
#
# The ed3k core (libs/packmule, the private ModderMule/packmule) is pinned for CI
# in scripts/packmule.ref: its HEAD must be pushed, and is written there and
# committed with the release. CI checks out that commit with PACKMULE_TOKEN.
#
# Tags use the vMAJOR.MINOR.PATCH form to match existing tags (v0.1.6, ...).
#
# Requires these keys in the project-root .env (gitignored):
#   PUBLISH_SSH_USER     ssh user on the webserver
#   PUBLISH_SSH_PASS     ssh password (fed to sshpass via the environment)
#   PUBLISH_SSH_HOST     webserver hostname
#   PUBLISH_REMOTE_PATH  remote directory that serves /pub/
#   PUBLISH_LOCAL_JSON   local copy of emuleqt-version.json (--publish-only)
# and optionally
#   JD_DIR               dir holding the JDownloader, AppWorkUtils, JDBrowser and
#                        MyJDownloaderClient SVN trees (an environment JD_DIR wins)
# and the sshpass + jq tools (brew install hudochenkov/sshpass/sshpass, brew install jq).
#
# Pushing the tag triggers .github/workflows/release.yml, which restarts itself
# on main (a build there shares its caches with the next release), verifies the
# tag matches CMakeLists.txt, builds Linux/macOS/Windows (by calling the per-OS
# workflows) and publishes the GitHub Release -- with the platform archives and a
# SHA256SUMS.txt attached -- and then publishes the update manifest.
# Nothing needs to be started by hand.
#
# Usage: scripts/publish-release.sh [--publish-only]
#
#   --publish-only   Skip the whole git side (no bump, no commit, no tag, no push)
#                    and do what CI's manifest job does, for the version already in
#                    CMakeLists.txt: pull the live manifest, fill it from the GitHub
#                    release of that tag (must be published already) plus the
#                    GeoLite2/resolver files on /pub/ (checked against the tag) and
#                    upload it. The fallback when the CI secrets are missing or the
#                    job failed.

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

# --- guard: specialdlp submodule is pushed ------------------------------------
# CI checks out the recorded commit of external/specialdlp (ModderMule/specialdlp);
# one that only exists locally fails every platform build after the tag push.
DLP_DIR="external/specialdlp"
if [[ ! -f "$DLP_DIR/CMakeLists.txt" ]]; then
  echo "error: ${DLP_DIR} is missing -- run: git submodule update --init" >&2
  exit 1
fi
if [[ "$PUBLISH_ONLY" -eq 0 ]]; then
  DLP_COMMIT="$(git rev-parse "HEAD:${DLP_DIR}" 2>/dev/null || true)"
  if [[ -z "$DLP_COMMIT" ]]; then
    echo "error: ${DLP_DIR} is not recorded as a submodule in HEAD (git add it first)" >&2
    exit 1
  fi
  if ! git -C "$DLP_DIR" fetch --quiet origin; then
    echo "error: could not fetch ${DLP_DIR} from its origin" >&2
    exit 1
  fi
  if [[ -z "$(git -C "$DLP_DIR" branch -r --contains "$DLP_COMMIT" 2>/dev/null)" ]]; then
    echo "error: ${DLP_DIR} commit ${DLP_COMMIT:0:12} is not pushed to its origin" >&2
    echo "  push it first:  git -C ${DLP_DIR} push origin HEAD:master" >&2
    exit 1
  fi
fi

# --- guard: packmule is pushed ----------------------------------------------
# CI checks out scripts/packmule.ref; a commit that only exists locally fails
# every platform build after the tag push.
PM_DIR="libs/packmule"
PM_REF_FILE="scripts/packmule.ref"
PM_COMMIT=""
if [[ "$PUBLISH_ONLY" -eq 0 ]]; then
  if [[ ! -f "$PM_DIR/Cargo.toml" ]]; then
    echo "error: ${PM_DIR} is missing (clone ModderMule/packmule there)" >&2
    exit 1
  fi
  if [[ -n "$(git -C "$PM_DIR" status --porcelain)" ]]; then
    echo "error: ${PM_DIR} has uncommitted changes; commit and push them first" >&2
    exit 1
  fi
  PM_COMMIT="$(git -C "$PM_DIR" rev-parse HEAD)"
  if ! git -C "$PM_DIR" fetch --quiet origin; then
    echo "error: could not fetch ${PM_DIR} from its origin" >&2
    exit 1
  fi
  if [[ -z "$(git -C "$PM_DIR" branch -r --contains "$PM_COMMIT" 2>/dev/null)" ]]; then
    echo "error: ${PM_DIR} commit ${PM_COMMIT:0:12} is not pushed to its origin" >&2
    echo "  push it first:  git -C ${PM_DIR} push origin HEAD:main" >&2
    exit 1
  fi
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
# and '#' comments, split on the first '=', trim surrounding whitespace, strip
# one pair of matching quotes. Only
# PUBLISH_* keys and JD_DIR are imported -- .env also holds SMTP credentials that
# have no business in this script's environment.
JD_DIR_ENV="${JD_DIR:-}"
while IFS= read -r line || [[ -n "$line" ]]; do
  line="${line#"${line%%[![:space:]]*}"}"
  [[ -z "$line" || "$line" == \#* ]] && continue
  [[ "$line" != *=* ]] && continue
  key="${line%%=*}"
  val="${line#*=}"
  key="${key//[[:space:]]/}"
  [[ "$key" == PUBLISH_* || "$key" == JD_DIR ]] || continue
  [[ "$key" =~ ^[A-Za-z_][A-Za-z0-9_]*$ ]] || continue
  val="${val#"${val%%[![:space:]]*}"}"
  val="${val%"${val##*[![:space:]]}"}"
  # a quoted value keeps its spaces, not its quotes
  if [[ ${#val} -ge 2 && ( "$val" == \"*\" || "$val" == \'*\' ) ]]; then
    val="${val:1:${#val}-2}"
  fi
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

# hoster resolver sources (scripts/build-hoster-resolver.sh)
JD_SRC="${JD_DIR_ENV:-${JD_DIR:-/Volumes/MySSD/Coding/Java}}"
if [[ "$PUBLISH_ONLY" -eq 0 && ! -d "$JD_SRC/JDownloader/src" ]]; then
  echo "error: JDownloader sources not found at ${JD_SRC} (set JD_DIR in .env)" >&2
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

# --- bump version files if a new version was chosen -------------------------
BUMPED=0
if [[ "$PUBLISH_ONLY" -eq 0 && "$NEW" != "$CURRENT" ]]; then
  echo
  echo "Bumping version files ${CURRENT} -> ${NEW} via scripts/bump-version.sh ..."
  scripts/bump-version.sh "$NEW"
  BUMPED=1
fi

# --- pin packmule for CI ------------------------------------------------------
# BUMPED also means "files changed": the abort paths revert, the commit step commits
PM_PIN_CHANGED=0
if [[ "$PUBLISH_ONLY" -eq 0 && "$(tr -d '[:space:]' < "$PM_REF_FILE" 2>/dev/null)" != "$PM_COMMIT" ]]; then
  echo "$PM_COMMIT" > "$PM_REF_FILE"
  PM_PIN_CHANGED=1
  BUMPED=1
fi

# --- confirm ----------------------------------------------------------------
echo
echo "About to:"
if [[ "$PUBLISH_ONLY" -eq 1 ]]; then
  echo "  git      : (nothing -- --publish-only, reusing existing tag ${TAG})"
else
  if [[ "$BUMPED" -eq 1 ]]; then
    what=()
    [[ "$NEW" != "$CURRENT" ]] && what+=("version files -> ${NEW}")
    [[ "$PM_PIN_CHANGED" -eq 1 ]] && what+=("${PM_REF_FILE} -> ${PM_COMMIT:0:12}")
    echo "  commit   : release: ${TAG}   ($(IFS=,; echo "${what[*]}" | sed 's/,/, /g'))"
  else
    echo "  commit   : (none -- tagging existing HEAD at version ${CURRENT})"
  fi
  echo "  geoip    : scripts/fetch-geoip.sh -> rsync to ${PUBLISH_REMOTE_PATH}"
  echo "  resolver : scripts/build-hoster-resolver.sh (${JD_SRC}) -> rsync to ${PUBLISH_REMOTE_PATH}"
  echo "  packmule : ${PM_COMMIT:0:12} (CI checks out ${PM_REF_FILE})"
  echo "  tag      : ${TAG}  (annotated, carries the GeoLite2 + resolver SHA-256)"
  echo "  push     : ${REMOTE} ${BRANCH}  and  ${REMOTE} ${TAG}"
fi
if [[ "$PUBLISH_ONLY" -eq 1 ]]; then
  echo "  manifest : pull ${PUBLISH_SSH_HOST}:${PUBLISH_REMOTE_PATH} -> ${PUBLISH_LOCAL_JSON}"
  echo "             fill from ${RELEASE_URL} + /pub/ GeoLite2 + resolver"
  echo "  publish  : rsync -> ${PUBLISH_SSH_USER}@${PUBLISH_SSH_HOST}:${PUBLISH_REMOTE_PATH}"
else
  echo "  manifest : (by CI after the build -- release.yml job 'manifest')"
fi
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

# --- GeoLite2 database --------------------------------------------------------
# Fetched here because CI has no MaxMind credentials. Uploaded before the tag
# push: release.yml's geoip job downloads it within seconds of the push.
abort_before_tag() {
  echo "error: $1 -- nothing committed, tagged or pushed" >&2
  if [[ "$BUMPED" -eq 1 ]]; then
    echo "reverting version files" >&2
    git checkout -- .
  fi
  exit 1
}

GEOIP_SHA=""
if [[ "$PUBLISH_ONLY" -eq 0 ]]; then
  GEOIP_DB="$ROOT/data/config/GeoLite2-Country.mmdb"
  scripts/fetch-geoip.sh || abort_before_tag "GeoLite2 download failed"
  GEOIP_SHA="$(shasum -a 256 "$GEOIP_DB" | awk '{print $1}')"
  [[ "$GEOIP_SHA" =~ ^[0-9a-f]{64}$ ]] || abort_before_tag "could not hash ${GEOIP_DB}"

  echo "Uploading GeoLite2-Country.mmdb (sha256 ${GEOIP_SHA}) ..."
  if ! SSHPASS="$PUBLISH_SSH_PASS" rsync -avz \
         -e 'sshpass -e ssh -o StrictHostKeyChecking=accept-new' \
         "$GEOIP_DB" \
         "${PUBLISH_SSH_USER}@${PUBLISH_SSH_HOST}:${PUBLISH_REMOTE_PATH}"; then
    abort_before_tag "GeoLite2 upload failed"
  fi
fi

# --- hoster resolver ----------------------------------------------------------
# Built here because CI has no JD sources; same upload-then-tag order as GeoLite2.
RESOLVER_SHA=""
if [[ "$PUBLISH_ONLY" -eq 0 ]]; then
  RESOLVER_ZIP="$ROOT/build/hoster-resolver/resolver.zip"
  echo "Building the hoster resolver from ${JD_SRC} ..."
  scripts/build-hoster-resolver.sh "$JD_SRC" "$ROOT/build/hoster-resolver" \
    || abort_before_tag "hoster resolver build failed"
  RESOLVER_SHA="$(shasum -a 256 "$RESOLVER_ZIP" | awk '{print $1}')"
  [[ "$RESOLVER_SHA" =~ ^[0-9a-f]{64}$ ]] || abort_before_tag "could not hash ${RESOLVER_ZIP}"

  echo "Uploading resolver.zip (sha256 ${RESOLVER_SHA}) ..."
  if ! SSHPASS="$PUBLISH_SSH_PASS" rsync -avz \
         -e 'sshpass -e ssh -o StrictHostKeyChecking=accept-new' \
         "$RESOLVER_ZIP" \
         "${PUBLISH_SSH_USER}@${PUBLISH_SSH_HOST}:${PUBLISH_REMOTE_PATH}"; then
    abort_before_tag "resolver upload failed"
  fi
fi

# --- commit / tag / push ----------------------------------------------------
if [[ "$PUBLISH_ONLY" -eq 0 ]]; then
  if [[ "$BUMPED" -eq 1 ]]; then
    git add -A
    git commit -m "release: ${TAG}"
  fi
  # release.yml's geoip job reads these lines to verify the hosted files
  # the tag push only re-dispatches release.yml onto main, to keep build caches warm
  git tag -a "${TAG}" -m "Release ${TAG}" \
    -m "GeoLite2-Country-SHA256: ${GEOIP_SHA}
Hoster-Resolver-SHA256: ${RESOLVER_SHA}"
  git push "${REMOTE}" "${BRANCH}"
  git push "${REMOTE}" "${TAG}"

  echo
  echo "Released ${TAG}."
fi

# --- update manifest ----------------------------------------------------------
# A normal release stops here: release.yml's manifest job announces the version
# once the assets exist.
if [[ "$PUBLISH_ONLY" -eq 0 ]]; then
  echo
  echo "The tag push started the release build for all platforms (a short dispatch"
  echo "run on the tag, then the build as a second 'Release ${TAG}' run on main):"
  echo "  https://github.com/${SLUG}/actions/workflows/release.yml"
  echo "It publishes the release (with assets) at ${RELEASE_URL} and then the"
  echo "update manifest. Without the PUBLISH_SSH_* repo secrets that last job only"
  echo "warns -- run: scripts/publish-release.sh --publish-only"
  exit 0
fi

# --publish-only: the same as CI's manifest job, from local tools
SSH_CMD='sshpass -e ssh -o StrictHostKeyChecking=accept-new'
REMOTE_JSON="${PUBLISH_SSH_USER}@${PUBLISH_SSH_HOST}:${PUBLISH_REMOTE_PATH%/}/emuleqt-version.json"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# Pull first: the live copy may carry entries CI wrote since the last local run.
echo "Pulling the live manifest ..."
SSHPASS="$PUBLISH_SSH_PASS" rsync -az -e "$SSH_CMD" "$REMOTE_JSON" "$WORK/manifest.json" \
  || { echo "error: could not download ${REMOTE_JSON}" >&2; exit 1; }

# GeoLite2 + resolver as served on /pub/, checked against the SHA lines of the tag
# (git cat-file keeps the annotated message; same lines release.yml reads)
TAG_MSG="$(git cat-file tag "${TAG}" 2>/dev/null || true)"
fetch_checked() {   # <url> <out> <tag line prefix>
  local want got
  want="$(sed -n "s/^$3: *\([0-9a-f]\{64\}\) *$/\1/p" <<< "$TAG_MSG" | head -n1)"
  [[ -n "$want" ]] || return 1
  curl -fsSL --retry 3 --retry-delay 5 -o "$2" "$1?t=$(date +%s)" || return 1
  got="$(shasum -a 256 "$2" | awk '{print $1}')"
  if [[ "$got" != "$want" ]]; then
    echo "warning: $1 is not the file tagged in ${TAG} -- its manifest entry is left as is" >&2
    return 1
  fi
}
EXTRA=()
if fetch_checked https://emule-qt.org/pub/GeoLite2-Country.mmdb "$WORK/GeoLite2-Country.mmdb" GeoLite2-Country-SHA256; then
  EXTRA+=(--geoip "$WORK/GeoLite2-Country.mmdb")
fi
if fetch_checked https://emule-qt.org/pub/resolver.zip "$WORK/resolver.zip" Hoster-Resolver-SHA256; then
  EXTRA+=(--resolver "$WORK/resolver.zip")
fi

# specialdlp versions as bundled in the tag (its recorded submodule commit),
# not whatever the working tree has checked out now
DLP_ROOT="$WORK/tagtree"
DLP_COMMIT="$(git rev-parse "${TAG}:${DLP_DIR}")"
mkdir -p "$DLP_ROOT/$DLP_DIR/specialdlp/specialdlp" "$DLP_ROOT/$DLP_DIR/include"
for f in CMakeLists.txt specialdlp/specialdlp/antiLeech.h include/specialdlp.h; do
  git -C "$DLP_DIR" show "${DLP_COMMIT}:${f}" > "$DLP_ROOT/$DLP_DIR/${f}" \
    || { echo "error: specialdlp commit ${DLP_COMMIT:0:12} not available locally" >&2; exit 1; }
done

scripts/update-manifest.sh --manifest "$WORK/manifest.json" --version "$NEW" --slug "$SLUG" \
  --from-github --repo-root "$DLP_ROOT" ${EXTRA[@]+"${EXTRA[@]}"} \
  || { echo "error: manifest not updated" >&2; exit 1; }
cat "$WORK/manifest.json" > "$PUBLISH_LOCAL_JSON"

echo "Publishing to ${PUBLISH_SSH_USER}@${PUBLISH_SSH_HOST}:${PUBLISH_REMOTE_PATH} ..."
if ! SSHPASS="$PUBLISH_SSH_PASS" rsync -avz -e "$SSH_CMD" "$PUBLISH_LOCAL_JSON" "$REMOTE_JSON"; then
  echo "error: upload failed -- the local manifest IS updated; re-run --publish-only" >&2
  exit 1
fi
echo
echo "Published manifest: latest=${NEW}  ->  ${RELEASE_URL}"
