#!/usr/bin/env bash
# Writes data/config/nodes.ed3knod: the ed3k contacts a profile saved, as a signed node
# list (ed3k bootstrap.md §1 source 5, §3). Ships with each release like nodes.dat.
#
# Usage: scripts/export-ed3k-nodes.sh [config-dir] [emulecored]
#   config-dir  profile whose Config/ed3k/kad.dat and node.key are used
#               (default: $HOME/eMuleQt/Config)
#   emulecored  daemon binary (default: build/src/daemon/emulecored)
#
# Run it with a profile whose node ran a while (ed3k on, contacts > 10), after its core
# stopped: the core saves kad.dat when it stops, the export reads that copy.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CONFIG="${1:-$HOME/eMuleQt/Config}"
DAEMON="${2:-$ROOT/build/src/daemon/emulecored}"
OUT="$ROOT/data/config/nodes.ed3knod"

if [[ ! -x "$DAEMON" ]]; then
    echo "no emulecored at $DAEMON (build it, or pass its path)" >&2
    exit 1
fi

"$DAEMON" --config "$CONFIG" --export-ed3k-nodes "$OUT" --export-comment "eMule Qt $(date -u +%Y-%m-%d)"
ls -l "$OUT"
