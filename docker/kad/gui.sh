#!/bin/sh
# gui.sh — Launch the eMuleQt GUI connected to a Docker Kad node
#
# Usage: ./docker/kad/gui.sh [NODE_NUMBER]   (default: 1)
#
# Temporarily patches ~/eMuleQt/Config/preferences.yml to point at the
# chosen node's IPC port, then restores the original file on exit.
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
GUI_BIN="$PROJECT_ROOT/build/src/gui/emuleqt.app/Contents/MacOS/emuleqt"

NODE="${1:-1}"
IPC_BASE_PORT=4712
IPC_TOKEN="kadnet-test-token"
IPC_PORT=$((IPC_BASE_PORT + NODE - 1))

PREFS_DIR="$HOME/eMuleQt/Config"
PREFS_FILE="$PREFS_DIR/preferences.yml"
PREFS_BACKUP="$PREFS_DIR/preferences.yml.gui-backup"

# Detect host LAN IP (non-loopback required for IPC auth)
# Use route to find the default interface, works regardless of interface name
HOST_IP=$(route -n get default 2>/dev/null | awk '/interface:/{iface=$2} END{if(iface) system("ipconfig getifaddr " iface)}' 2>/dev/null || true)
if [ -z "$HOST_IP" ]; then
    echo "ERROR: Could not detect host LAN IP. Connect manually via the GUI dialog."
    echo "  Port:  $IPC_PORT"
    echo "  Token: $IPC_TOKEN"
    exit 1
fi

if [ ! -x "$GUI_BIN" ]; then
    echo "ERROR: GUI binary not found at $GUI_BIN"
    echo "  Build with: scripts/build.sh"
    exit 1
fi

# Backup existing preferences
mkdir -p "$PREFS_DIR"
if [ -f "$PREFS_FILE" ]; then
    cp "$PREFS_FILE" "$PREFS_BACKUP"
fi

# Restore preferences on exit
restore_prefs() {
    if [ -f "$PREFS_BACKUP" ]; then
        mv "$PREFS_BACKUP" "$PREFS_FILE"
        echo "[gui.sh] Restored original preferences.yml"
    elif [ -f "$PREFS_FILE" ]; then
        rm "$PREFS_FILE"
    fi
}
trap restore_prefs EXIT INT TERM

# Write temporary preferences pointing at the Docker node
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
  tokens:
    - "${IPC_TOKEN}"
EOF

echo "[gui.sh] Connecting to Docker node-${NODE} at ${HOST_IP}:${IPC_PORT}"
echo "[gui.sh] Token: ${IPC_TOKEN}"
echo ""

"$GUI_BIN"
