#!/bin/sh
# entrypoint.sh — generate per-node config and launch emulecored
set -e

# Qt AppConfigLocation on Linux: ~/.config/<OrgName>/<AppName>
CONFIG_DIR="${HOME}/.config/eMule/eMule Qt Core"
mkdir -p "$CONFIG_DIR"

# Defaults
NODE_NICK="${NODE_NICK:-Node-$(hostname)}"
TCP_PORT="${TCP_PORT:-5662}"
UDP_PORT="${UDP_PORT:-5672}"
IPC_PORT="${IPC_PORT:-4712}"
IPC_TOKEN="${IPC_TOKEN:-kadnet-test-token}"

# Generate preferences.yml on first run only (preserve identity on restart)
if [ ! -f "$CONFIG_DIR/preferences.yml" ]; then
    cat > "$CONFIG_DIR/preferences.yml" <<EOF
general:
  nick: "${NODE_NICK}"
  autoConnect: true
  filterLANIPs: false
  reconnect: true

network:
  port: ${TCP_PORT}
  udpPort: ${UDP_PORT}
  networkED2K: false
  maxConnections: 200
  maxHalfConnections: 9

kademlia:
  enabled: true

ipc:
  enabled: true
  port: ${IPC_PORT}
  listenAddress: "0.0.0.0"
  tokens:
    - "${IPC_TOKEN}"

webServer:
  enabled: false

directories:
  incomingDir: "${HOME}/incoming"
  tempDirs:
    - "${HOME}/temp"
EOF
    echo "[entrypoint] Generated preferences.yml for ${NODE_NICK}"
fi

# Seed nodes.dat into the Config subdirectory (where the daemon looks for it)
KAD_DIR="$CONFIG_DIR/Config"
mkdir -p "$KAD_DIR"
if [ -f /seed/nodes.dat ] && [ ! -f "$KAD_DIR/nodes.dat" ]; then
    cp /seed/nodes.dat "$KAD_DIR/nodes.dat"
    echo "[entrypoint] Seeded nodes.dat into $KAD_DIR"
fi

# Create data directories
mkdir -p "${HOME}/incoming" "${HOME}/temp"

# Generate shared test files for Kad publish testing
for i in 1 2 3 4 5; do
    touch "${HOME}/incoming/testfile${i}.txt"
done

echo "[entrypoint] Starting emulecored as ${NODE_NICK} (TCP=${TCP_PORT}, UDP=${UDP_PORT}, IPC=${IPC_PORT})"
exec emulecored
