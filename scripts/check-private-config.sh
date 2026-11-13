#!/usr/bin/env bash
# Fails if any listed config file is user-private (scripts/private-config.txt).
#
# Usage: scripts/check-private-config.sh <dir>     check all files under <dir>
#        ... | scripts/check-private-config.sh -   check file paths read from stdin
#
# Used by publish-release.sh (tracked data/config files) and bundle-{linux,macos}.sh
# (staged config/). bundle-win.ps1 has the same check in PowerShell.
set -euo pipefail

LIST="$(cd "$(dirname "$0")" && pwd)/private-config.txt"
[[ -f "$LIST" ]] || { echo "error: ${LIST} is missing" >&2; exit 1; }
[[ $# -eq 1 ]] || { echo "usage: $0 <dir> | -" >&2; exit 2; }

PATTERNS=()
while IFS= read -r pat || [[ -n "$pat" ]]; do
  pat="${pat%$'\r'}"
  pat="${pat#"${pat%%[![:space:]]*}"}"
  pat="${pat%"${pat##*[![:space:]]}"}"
  [[ -z "$pat" || "$pat" == \#* ]] && continue
  PATTERNS+=("$pat")
done < "$LIST"

SRC="$1"
if [[ "$SRC" == "-" ]]; then
  files() { cat; }
else
  [[ -d "$SRC" ]] || exit 0
  files() { find "$SRC" -type f; }
fi

HITS=()
shopt -s nocasematch
while IFS= read -r f; do
  name="${f##*/}"
  for pat in "${PATTERNS[@]}"; do
    # shellcheck disable=SC2053  # glob match intended
    if [[ "$name" == $pat ]]; then HITS+=("$f  (${pat})"); break; fi
  done
done < <(files)
shopt -u nocasematch

if [[ "${#HITS[@]}" -gt 0 ]]; then
  echo "error: private config files would ship in the release (scripts/private-config.txt):" >&2
  printf '  %s\n' "${HITS[@]}" >&2
  exit 1
fi
