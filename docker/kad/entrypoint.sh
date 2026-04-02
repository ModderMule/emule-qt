#!/bin/sh
# entrypoint.sh — generate per-node config and launch emulecored
set -e

# Qt AppConfigLocation on Linux: ~/.config/<OrgName>/<AppName>
CONFIG_DIR="${HOME}/.config/eMule/Core"
mkdir -p "$CONFIG_DIR"

# Defaults
NODE_NICK="${NODE_NICK:-Node-$(hostname)}"
TCP_PORT="${TCP_PORT:-5662}"
UDP_PORT="${UDP_PORT:-5672}"
IPC_PORT="${IPC_PORT:-4712}"
IPC_TOKEN="${IPC_TOKEN:-kadnet-test-token}"
USER_HASH="${USER_HASH:-}"

# Generate preferences.yml on first run only (preserve identity on restart)
if [ ! -f "$CONFIG_DIR/preferences.yml" ]; then
    cat > "$CONFIG_DIR/preferences.yml" <<EOF
general:
  nick: "${NODE_NICK}"
  ${USER_HASH:+userHash: "${USER_HASH}"}
  autoConnect: true
  filterLANIPs: false
  skipFirewalledChecksInLanMode: false
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

upnp:
  enableUPnP: false

webServer:
  enabled: false

directories:
  incomingDir: "${HOME}/incoming"
  tempDirs:
    - "${HOME}/temp"
EOF
    echo "[entrypoint] Generated preferences.yml for ${NODE_NICK}"
fi

# Seed nodes.dat and preferencesKad.dat into the Config subdirectory
KAD_DIR="$CONFIG_DIR/Config"
mkdir -p "$KAD_DIR"
if [ -f /seed/nodes.dat ]; then
    cp /seed/nodes.dat "$KAD_DIR/nodes.dat"
    echo "[entrypoint] Seeded nodes.dat into $KAD_DIR"
fi
if [ -f /seed/preferencesKad.dat ]; then
    cp /seed/preferencesKad.dat "$KAD_DIR/preferencesKad.dat"
    echo "[entrypoint] Seeded preferencesKad.dat into $KAD_DIR"
fi

# Create data directories
mkdir -p "${HOME}/incoming" "${HOME}/temp"

# Generate shared test files for Kad publish testing (must be non-empty;
# SharedFileList skips 0-byte files).  Use NODE_NICK so each node's files
# hash differently — better for Kad publish/search testing.
for i in 1 2 3 4 5; do
    f="${HOME}/incoming/testfile${i}.txt"
    [ -f "$f" ] || printf "eMuleQt test file %d for node %s\n" "$i" "${NODE_NICK}" > "$f"
done

# Report any crash dumps from a previous run (visible in docker logs on restart)
CRASH_DIR="$CONFIG_DIR/crashes"
if [ -d "$CRASH_DIR" ] && ls "$CRASH_DIR"/*.crash 1>/dev/null 2>&1; then
    echo "[entrypoint] WARNING: Previous crash dumps found:"
    ls -lt "$CRASH_DIR"/*.crash
fi

# Enable kernel core dumps as fallback
ulimit -c unlimited 2>/dev/null || true

echo "[entrypoint] Starting emulecored as ${NODE_NICK} (TCP=${TCP_PORT}, UDP=${UDP_PORT}, IPC=${IPC_PORT})"
exec emulecored
