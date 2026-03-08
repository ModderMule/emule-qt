#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
    echo "Usage: $0 <new-version>  (e.g. $0 0.2.0)" >&2
    exit 1
fi

NEW="$1"
IFS='.' read -r MAJOR MINOR PATCH <<< "$NEW"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OLD=$(grep -m1 '^ *VERSION [0-9]' "$ROOT/CMakeLists.txt" | grep -oE '[0-9]+\.[0-9]+\.[0-9]+')

echo "Bumping $OLD → $NEW"

# 1. CMakeLists.txt
sed -i '' "s/VERSION ${OLD}/VERSION ${NEW}/" "$ROOT/CMakeLists.txt"

# 2. AppConfig.h
sed -i '' "s/kAppVersion{\"${OLD}\"}/kAppVersion{\"${NEW}\"}/" \
    "$ROOT/src/core/app/AppConfig.h"

# 3. config_win.h (4 defines)
sed -i '' \
    -e "s/EMULE_VERSION_MAJOR  [0-9]*/EMULE_VERSION_MAJOR  ${MAJOR}/" \
    -e "s/EMULE_VERSION_MINOR  [0-9]*/EMULE_VERSION_MINOR  ${MINOR}/" \
    -e "s/EMULE_VERSION_PATCH  [0-9]*/EMULE_VERSION_PATCH  ${PATCH}/" \
    -e "s/EMULE_VERSION_STRING \"${OLD}\"/EMULE_VERSION_STRING \"${NEW}\"/" \
    "$ROOT/src/core/config_win.h"

# 4. LogWidget.cpp (2 occurrences)
sed -i '' "s/eMule Qt v${OLD} ready/eMule Qt v${NEW} ready/g" \
    "$ROOT/src/gui/controls/LogWidget.cpp"

# 5. tst_Smoke.cpp
sed -i '' "s/\"${OLD}\"/\"${NEW}\"/" "$ROOT/tests/tst_Smoke.cpp"

# 6. vcpkg.json
sed -i '' "s/\"version-string\": \"${OLD}\"/\"version-string\": \"${NEW}\"/" \
    "$ROOT/src/vcpkg.json"

echo "Done. Verify with: grep -r '${NEW}' CMakeLists.txt src/ tests/"
