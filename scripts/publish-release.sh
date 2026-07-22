#!/usr/bin/env bash
set -euo pipefail

# Interactive release helper for eMuleQt.
#
# Reads the current version from CMakeLists.txt (the VERSION field), lets you
# either tag that version as-is or pick a new one. When a new version is chosen
# it delegates the multi-file version bump to scripts/bump-version.sh (which
# keeps CMakeLists.txt, AppConfig.h, config_win.h, LogWidget.cpp, tst_Smoke.cpp
# and vcpkg.json in lockstep), then creates a release commit + annotated git tag
# and pushes both.
#
# Tags use the vMAJOR.MINOR.PATCH form to match existing tags (v0.1.6, ...).
#
# NOTE: the GitHub build workflows (.github/workflows/{linux,macos,windows*}.yml)
# are currently workflow_dispatch-only -- pushing a tag does NOT auto-build.
# Trigger builds manually from GitHub -> Actions after releasing.
#
# Usage: scripts/publish-release.sh

# --- locate repo root -------------------------------------------------------
if ! ROOT="$(git rev-parse --show-toplevel 2>/dev/null)"; then
  echo "error: not inside a git repository" >&2
  exit 1
fi
cd "$ROOT"

# --- guard: clean working tree ---------------------------------------------
if [[ -n "$(git status --porcelain)" ]]; then
  echo "error: working tree has uncommitted changes; commit or stash them first" >&2
  git status --short >&2
  exit 1
fi

BRANCH="$(git rev-parse --abbrev-ref HEAD)"
REMOTE="origin"

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
echo "Branch:          ${BRANCH} (remote: ${REMOTE})"
echo
echo "Press Enter to release the current version (${CURRENT}) as tag v${CURRENT},"
echo "or type a new version to bump to (suggested next: ${suggest})."
echo

# --- prompt for version -----------------------------------------------------
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

# --- bump version files if a new version was chosen -------------------------
BUMPED=0
if [[ "$NEW" != "$CURRENT" ]]; then
  echo
  echo "Bumping version files ${CURRENT} -> ${NEW} via scripts/bump-version.sh ..."
  scripts/bump-version.sh "$NEW"
  BUMPED=1
fi

# --- confirm ----------------------------------------------------------------
echo
echo "About to:"
if [[ "$BUMPED" -eq 1 ]]; then
  echo "  commit : release: ${TAG}   (version files -> ${NEW})"
else
  echo "  commit : (none -- tagging existing HEAD at version ${CURRENT})"
fi
echo "  tag    : ${TAG}  (annotated)"
echo "  push   : ${REMOTE} ${BRANCH}  and  ${REMOTE} ${TAG}"
echo
read -r -p "Proceed? [y/N]: " CONFIRM
if [[ "${CONFIRM,,}" != "y" ]]; then
  echo "aborted"
  if [[ "$BUMPED" -eq 1 ]]; then
    echo "reverting version files"
    git checkout -- .
  fi
  exit 1
fi

# --- commit / tag / push ----------------------------------------------------
if [[ "$BUMPED" -eq 1 ]]; then
  git add -A
  git commit -m "release: ${TAG}"
fi
git tag -a "${TAG}" -m "Release ${TAG}"
git push "${REMOTE}" "${BRANCH}"
git push "${REMOTE}" "${TAG}"

echo
echo "Released ${TAG}."
echo "Next: GitHub -> Actions -> run the Linux/macOS/Windows workflows for this tag."
