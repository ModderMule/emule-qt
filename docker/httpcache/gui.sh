#!/bin/sh
# gui.sh — Launch the eMuleQt GUI connected to a node of the HTTP Cache rig.
#
# Usage: ./docker/httpcache/gui.sh [NODE_NUMBER]   (default: 1, the seeder)
#
# Temporarily repoints ~/eMuleQt/Config/preferences.yml at the chosen node's
# mapped IPC port and restores the original file on exit. Same mechanism as
# docker/kad/gui.sh; only the port range and the token differ.
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
GUI_BIN="$PROJECT_ROOT/build/src/gui/emuleqt.app/Contents/MacOS/emuleqt"
[ -x "$GUI_BIN" ] || GUI_BIN="$PROJECT_ROOT/build/src/gui/emuleqt"

NODE="${1:-1}"
IPC_BASE_PORT=4812
IPC_TOKEN="cachenet-test-token"
IPC_PORT=$((IPC_BASE_PORT + NODE - 1))

PREFS_DIR="$HOME/eMuleQt/Config"
PREFS_FILE="$PREFS_DIR/preferences.yml"
PREFS_BACKUP="$PREFS_DIR/preferences.yml.gui-backup"

# Docker maps the container IPC ports onto localhost.
HOST_IP="127.0.0.1"

if [ ! -x "$GUI_BIN" ]; then
    echo "ERROR: GUI binary not found at $GUI_BIN"
    echo "  Build with: scripts/build.sh"
    exit 1
fi

# Back up the real preferences, unless a backup from a crashed run is already
# there — that one is the genuine original.
mkdir -p "$PREFS_DIR"
if [ -f "$PREFS_BACKUP" ]; then
    echo "[gui.sh] Found stale backup from a previous run — keeping it as the restore source"
elif [ -f "$PREFS_FILE" ]; then
    cp "$PREFS_FILE" "$PREFS_BACKUP"
fi

restore_prefs() {
    if [ -f "$PREFS_BACKUP" ]; then
        mv "$PREFS_BACKUP" "$PREFS_FILE"
        echo "[gui.sh] Restored original preferences.yml"
    elif [ -f "$PREFS_FILE" ]; then
        rm "$PREFS_FILE"
    fi
}
trap restore_prefs EXIT INT TERM

cat > "$PREFS_FILE" <<EOF
general:
  nick: "GUI-Debug"
  autoConnect: false
  filterLANIPs: false
  promptOnExit: false

ipc:
  enabled: true
  port: ${IPC_PORT}
  listenAddress: "${HOST_IP}"
  daemonPath: "local"
  tokens:
    - "${IPC_TOKEN}"
EOF

echo "[gui.sh] Connecting to cachenet-node-${NODE} at ${HOST_IP}:${IPC_PORT}"
echo "[gui.sh] Token: ${IPC_TOKEN}"
echo ""

"$GUI_BIN"
