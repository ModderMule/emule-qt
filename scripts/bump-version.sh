#!/usr/bin/env bash
set -euo pipefail

usage() {
    echo "Usage: $0 [new-version]  (e.g. $0 0.2.0; omit to be prompted)" >&2
}

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OLD=$(grep -m1 '^ *VERSION [0-9]' "$ROOT/CMakeLists.txt" | grep -oE '[0-9]+\.[0-9]+\.[0-9]+')
if [[ -z "$OLD" ]]; then
    echo "error: could not read VERSION from CMakeLists.txt" >&2
    exit 1
fi

if [[ $# -gt 1 ]]; then
    usage
    exit 1
elif [[ $# -eq 1 ]]; then
    NEW="$1"
else
    if [[ ! -t 0 ]]; then
        usage
        exit 1
    fi
    # suggest patch+1
    SUGGEST="$OLD"
    if [[ "$OLD" =~ ^([0-9]+)\.([0-9]+)\.([0-9]+)$ ]]; then
        SUGGEST="${BASH_REMATCH[1]}.${BASH_REMATCH[2]}.$(( BASH_REMATCH[3] + 1 ))"
    fi
    echo "Current version: ${OLD}"
    read -r -p "New version [${SUGGEST}]: " NEW
    NEW="${NEW:-$SUGGEST}"
    # normalize away a leading 'v'
    NEW="${NEW#v}"
fi

if [[ ! "$NEW" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo "error: '${NEW}' is not a valid MAJOR.MINOR.PATCH version" >&2
    exit 1
fi

IFS='.' read -r MAJOR MINOR PATCH <<< "$NEW"

echo "Bumping $OLD → $NEW"

# These three are the only places the version is written by hand. Everything
# else derives from them: CMakeLists.txt generates config.h from config.h.in,
# config_win.h is its hand-maintained twin for the non-CMake MSVC build, and
# AppConfig.h/LogWidget.cpp/tst_Smoke.cpp all read EMULE_VERSION_STRING.

# 1. CMakeLists.txt
sed -i '' "s/VERSION ${OLD}/VERSION ${NEW}/" "$ROOT/CMakeLists.txt"

# 2. config_win.h (4 defines)
sed -i '' \
    -e "s/EMULE_VERSION_MAJOR  [0-9]*/EMULE_VERSION_MAJOR  ${MAJOR}/" \
    -e "s/EMULE_VERSION_MINOR  [0-9]*/EMULE_VERSION_MINOR  ${MINOR}/" \
    -e "s/EMULE_VERSION_PATCH  [0-9]*/EMULE_VERSION_PATCH  ${PATCH}/" \
    -e "s/EMULE_VERSION_STRING \"${OLD}\"/EMULE_VERSION_STRING \"${NEW}\"/" \
    "$ROOT/src/core/config_win.h"

# 3. vcpkg.json
sed -i '' "s/\"version-string\": \"${OLD}\"/\"version-string\": \"${NEW}\"/" \
    "$ROOT/src/vcpkg.json"

echo "Done. Verify with: grep -n '${NEW}' CMakeLists.txt src/core/config_win.h src/vcpkg.json"
echo "Next: run scripts/publish-release.sh to commit, tag (v${NEW}) and push the release."
