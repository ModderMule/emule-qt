#!/bin/sh
# entrypoint.sh — generate per-node config and launch emulecored
#
# Bind-mounted by kadnet.py, so edits here need no image rebuild. Optional
# runtime variables (unset = the plain Kad rig):
#   FIREWALL4=1          refuse unsolicited inbound IPv4 TCP and UDP on our ports
#                        (a NAT: replies to our own traffic still pass)
#   FIREWALL6=1          refuse unsolicited inbound IPv6 TCP (a home router's v6 firewall)
#   PUBLIC_IPV6=addr     network.publicIPv6Override
#   NAT_TRAVERSAL=0|1    serverConnection.natTraversal (daemon default: on)
#   LOG_TO_DISK=1        emulecored.log, _Verbose.log and _Kad.log in the config dir
#   UNLIMITED_BANDWIDTH=1  no upload/download limit
#   SKIP_LAN_FW_CHECKS=1 skipFirewalledChecksInLanMode (LAN mode: count as open, run no checks;
#                        such a node still refuses buddy requests, as in MFC)
set -e

# Same layout as macOS; preferences.yml and the data files share one directory
CONFIG_DIR="${HOME}/eMuleQt/Config"
mkdir -p "$CONFIG_DIR"

# Defaults
NODE_NICK="${NODE_NICK:-Node-$(hostname)}"
TCP_PORT="${TCP_PORT:-5662}"
UDP_PORT="${UDP_PORT:-5672}"
IPC_PORT="${IPC_PORT:-4712}"
IPC_TOKEN="${IPC_TOKEN:-kadnet-test-token}"
USER_HASH="${USER_HASH:-}"

# Firewall first, before the daemon opens a socket
if [ "${FIREWALL4:-0}" = "1" ]; then
    iptables -A INPUT -m conntrack --ctstate ESTABLISHED,RELATED -j ACCEPT
    iptables -A INPUT -p tcp --dport "$TCP_PORT" -j REJECT --reject-with tcp-reset
    iptables -A INPUT -p udp --dport "$UDP_PORT" -j DROP
    echo "[entrypoint] IPv4 firewalled: inbound TCP ${TCP_PORT} / UDP ${UDP_PORT} only as replies"
fi
if [ "${FIREWALL6:-0}" = "1" ]; then
    ip6tables -A INPUT -m conntrack --ctstate ESTABLISHED,RELATED -j ACCEPT
    ip6tables -A INPUT -p tcp --dport "$TCP_PORT" -j REJECT --reject-with tcp-reset
    echo "[entrypoint] IPv6 firewalled: inbound TCP ${TCP_PORT} only as replies"
fi

# Optional preference lines
[ "${SKIP_LAN_FW_CHECKS:-0}" = "1" ] && SKIP_FW=true || SKIP_FW=false
NET_EXTRA=""
[ -n "${PUBLIC_IPV6:-}" ] && NET_EXTRA="  publicIPv6Override: \"${PUBLIC_IPV6}\""
EXTRA=""
if [ -n "${NAT_TRAVERSAL:-}" ]; then
    [ "$NAT_TRAVERSAL" = "1" ] && NT=true || NT=false
    EXTRA="${EXTRA}
serverConnection:
  natTraversal: ${NT}
"
fi
if [ "${LOG_TO_DISK:-0}" = "1" ]; then
    EXTRA="${EXTRA}
logging:
  logToDiskCore: true
  verbose: true
  kadVerboseLog: true
  logUlDlEvents: true
"
fi
if [ "${UNLIMITED_BANDWIDTH:-0}" = "1" ]; then
    EXTRA="${EXTRA}
bandwidth:
  maxUpload: 0
  maxDownload: 0
"
fi

# Generate preferences.yml on first run only (preserve identity on restart)
if [ ! -f "$CONFIG_DIR/preferences.yml" ]; then
    cat > "$CONFIG_DIR/preferences.yml" <<EOF
general:
  nick: "${NODE_NICK}"
  ${USER_HASH:+userHash: "${USER_HASH}"}
  autoConnect: true
  filterLANIPs: false
  skipFirewalledChecksInLanMode: ${SKIP_FW}
  reconnect: true

network:
  port: ${TCP_PORT}
  udpPort: ${UDP_PORT}
  networkED2K: false
  maxConnections: 200
  maxHalfConnections: 9
${NET_EXTRA}

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

webserver:
  enabled: false

directories:
  incomingDir: "${HOME}/incoming"
  tempDirs:
    - "${HOME}/temp"
${EXTRA}
EOF
    echo "[entrypoint] Generated preferences.yml for ${NODE_NICK}"
fi

# Seed nodes.dat and preferencesKad.dat
KAD_DIR="$CONFIG_DIR"
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

# Optional network monitor (kadnet.py --netmon)
if [ "${NETMON:-0}" = "1" ]; then
    echo "[entrypoint] Network monitor on — writing to ${NETMON_DIR:-/netmon}"
    /usr/local/bin/netmon.sh &
fi

echo "[entrypoint] Starting emulecored as ${NODE_NICK} (TCP=${TCP_PORT}, UDP=${UDP_PORT}, IPC=${IPC_PORT})"
exec emulecored
