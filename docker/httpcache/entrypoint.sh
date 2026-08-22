#!/bin/sh
# ---------------------------------------------------------------------------
# entrypoint.sh — configure and launch one emulecored node of the HTTP Cache rig.
#
# Bind-mounted into the container and selected via `entrypoint:` in the
# generated compose file, so editing rig behaviour needs no image rebuild.
# The image itself (docker/daemon.Dockerfile) bakes in the Kad rig's entrypoint.
#
# Environment (all set by cachenet.py):
#   NODE_NICK        display name, also used for the log prefix
#   NODE_ROLE        "seeder" (shares the file) or "leecher" (downloads it)
#   USER_HASH        32 hex chars — a stable identity across restarts
#   TCP_PORT/UDP_PORT/IPC_PORT/IPC_TOKEN
#   CACHE_BASE_URL   e.g. http://cache
#   CACHE_API_KEY    upload credential; empty disables publishing
#   ALLOW_UPLOAD     "true" to publish chunks (needs CACHE_API_KEY too)
#   MIN_CLIENTS      httpCache.minClients
#   PUBLISH_RATE_KBS httpCache.publishRateKBs
#   MAX_FETCHES      httpCache.maxConcurrentFetches
#   MAX_UPLOAD_KBS   ed2k upload cap, 0 = unlimited
#   DOWNLOAD_LINK    ed2k link (with a |sources,…| block) to add once we are up
# ---------------------------------------------------------------------------
set -e

# Qt AppConfigLocation on Linux: ~/.config/<OrgName>/<AppName>
CONFIG_DIR="${HOME}/.config/eMule/Core"
# The Config subdirectory too: the RSA keypair for secure identification and the
# collection signing key are written there on first start, and a daemon that
# cannot create them logs "secure ident disabled" and carries on without it.
mkdir -p "$CONFIG_DIR/Config"

NODE_NICK="${NODE_NICK:-Node-$(hostname)}"
NODE_ROLE="${NODE_ROLE:-leecher}"
TCP_PORT="${TCP_PORT:-5662}"
UDP_PORT="${UDP_PORT:-5672}"
IPC_PORT="${IPC_PORT:-4712}"
IPC_TOKEN="${IPC_TOKEN:-cachenet-test-token}"
USER_HASH="${USER_HASH:-}"
CACHE_BASE_URL="${CACHE_BASE_URL:-http://cache}"
CACHE_API_KEY="${CACHE_API_KEY:-}"
ALLOW_UPLOAD="${ALLOW_UPLOAD:-false}"
MIN_CLIENTS="${MIN_CLIENTS:-2}"
PUBLISH_RATE_KBS="${PUBLISH_RATE_KBS:-8192}"
MAX_FETCHES="${MAX_FETCHES:-2}"
MAX_UPLOAD_KBS="${MAX_UPLOAD_KBS:-0}"
DOWNLOAD_LINK="${DOWNLOAD_LINK:-}"

mkdir -p "${HOME}/incoming" "${HOME}/temp"

# Generate preferences.yml on first run only, so a restart keeps the identity
# and the download queue it built up.
if [ ! -f "$CONFIG_DIR/preferences.yml" ]; then
    cat > "$CONFIG_DIR/preferences.yml" <<EOF
general:
  nick: "${NODE_NICK}"
  ${USER_HASH:+userHash: "${USER_HASH}"}
  autoConnect: false
  # Load-bearing twice over: isGoodIP() would otherwise reject every 172.x peer
  # on this bridge network, AND HttpCacheManager::urlIsAcceptable() would refuse
  # the cache URL once its hostname resolves to one.
  filterLANIPs: false
  promptOnExit: false
  reconnect: false

network:
  port: ${TCP_PORT}
  udpPort: ${UDP_PORT}
  # No ed2k server in this rig: leechers learn the seeder from the |sources,…|
  # block of the link, and each other through source exchange.
  networkED2K: false
  maxConnections: 200
  maxHalfConnections: 9

kademlia:
  enabled: false

bandwidth:
  # 0 is eMuleQt's "unlimited" sentinel. A modest cap on the seeder is what makes
  # the cache offload visible — with unlimited upstream ed2k would deliver the
  # whole file before a chunk was ever published.
  maxUpload: ${MAX_UPLOAD_KBS}
  maxDownload: 0

directories:
  incomingDir: "${HOME}/incoming"
  tempDirs:
    - "${HOME}/temp"

ipc:
  enabled: true
  port: ${IPC_PORT}
  listenAddress: "0.0.0.0"
  tokens:
    - "${IPC_TOKEN}"

upnp:
  enableUPnP: false

httpCache:
  enabled: true
  allowDownload: true
  allowUpload: ${ALLOW_UPLOAD}
  baseUrl: "${CACHE_BASE_URL}"
  apiKey: "${CACHE_API_KEY}"
  minClients: ${MIN_CLIENTS}
  # Left at 0 the publish rate is a quarter of the upload limit — 62 KB/s at the
  # default 250, i.e. over two minutes for one 9.28 MB part. The cache server is
  # one hop away here, so there is nothing to be gentle with.
  publishRateKBs: ${PUBLISH_RATE_KBS}
  # 0 = unlimited (HttpCacheManager::process()).
  maxPublishBytesPerDay: 0
  maxConcurrentFetches: ${MAX_FETCHES}
  chunkTtlSeconds: 21600
EOF
    echo "[entrypoint] Generated preferences.yml for ${NODE_NICK} (${NODE_ROLE})"
fi

# Report crash dumps left by a previous run (bind-mounted, so they survive).
CRASH_DIR="$CONFIG_DIR/crashes"
if [ -d "$CRASH_DIR" ] && ls "$CRASH_DIR"/*.crash 1>/dev/null 2>&1; then
    echo "[entrypoint] WARNING: Previous crash dumps found:"
    ls -lt "$CRASH_DIR"/*.crash
fi
ulimit -c unlimited 2>/dev/null || true

# A leecher hands its link to the daemon once IPC is listening. `emulecored
# --add-link` is the supported path (CommandLineExec -> DownloadSearchFile ->
# DownloadQueue::addDownloadFromED2KLink), and from inside the container the
# connection is local, so it needs no token.
#
# The marker lives on the config volume, so a restarted container does not spend
# two minutes re-offering a link the daemon already has — a duplicate add is
# refused, and refusal is indistinguishable from "the daemon is not up yet".
LINK_MARKER="$CONFIG_DIR/.download-link-added"
if [ -n "$DOWNLOAD_LINK" ] && [ ! -f "$LINK_MARKER" ]; then
    (
        i=0
        while [ "$i" -lt 24 ]; do
            sleep 5
            i=$((i + 1))
            if emulecored --add-link "$DOWNLOAD_LINK" 2>&1; then
                touch "$LINK_MARKER"
                echo "[entrypoint] Added download link after $((i * 5))s"
                exit 0
            fi
        done
        echo "[entrypoint] ERROR: could not add the download link after 2 minutes"
    ) &
fi

echo "[entrypoint] Starting emulecored as ${NODE_NICK} role=${NODE_ROLE} (TCP=${TCP_PORT}, UDP=${UDP_PORT}, IPC=${IPC_PORT}, publish=${ALLOW_UPLOAD})"
exec emulecored
