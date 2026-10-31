#!/bin/sh
# emuleqt-entrypoint — first-run preferences.yml, PUID/PGID, then drop to `emule`
#
# Env vars are read on the first start only (no /config/preferences.yml yet).
# The daemon owns the file afterwards and rewrites it; later changes go through
# the web UI or the GUI. See docs/docker.md.
set -eu

CONFIG_DIR=/config
DATA_DIR=/data
PREFS="$CONFIG_DIR/preferences.yml"

log() { echo "[entrypoint] $*"; }

# 0/1, yes/no, true/false -> YAML bool
yml_bool() {
    case "$(echo "${1:-0}" | tr '[:upper:]' '[:lower:]')" in
        1|yes|true|on) echo true ;;
        *) echo false ;;
    esac
}

# -- PUID/PGID ---------------------------------------------------------------
if [ "$(id -u)" = "0" ]; then
    PUID="${PUID:-1000}"
    PGID="${PGID:-1000}"
    [ "$(id -g emule)" = "$PGID" ] || groupmod -o -g "$PGID" emule
    [ "$(id -u emule)" = "$PUID" ] || usermod -o -u "$PUID" emule
fi

mkdir -p "$CONFIG_DIR" "$DATA_DIR/incoming" "$DATA_DIR/temp"

# -- First run: write preferences.yml ----------------------------------------
if [ ! -f "$PREFS" ]; then
    IPC_TOKEN="${EMULEQT_IPC_TOKEN:-}"
    if [ -z "$IPC_TOKEN" ]; then
        IPC_TOKEN="$(head -c 24 /dev/urandom | od -An -tx1 | tr -d ' \n')"
        log "Generated IPC token (needed by a GUI outside the container): $IPC_TOKEN"
    fi

    ADMIN_HASH=""
    if [ -n "${EMULEQT_WEB_PASSWORD:-}" ]; then
        # Web server stores the lowercase SHA-256 hex of the password
        ADMIN_HASH="$(printf '%s' "$EMULEQT_WEB_PASSWORD" | sha256sum | cut -d' ' -f1)"
    elif [ "$(yml_bool "${EMULEQT_WEB:-1}")" = true ]; then
        log "WARNING: EMULEQT_WEB_PASSWORD not set - web UI login is denied until a password is set"
    fi

    # one port for eD2K TCP, eD2K/Kad UDP and ed3k (ed3k Protocol.md §14.1) unless set apart
    TCP_PORT="${EMULEQT_TCP_PORT:-4662}"
    UDP_PORT="${EMULEQT_UDP_PORT:-$TCP_PORT}"

    NICK_LINE=""
    [ -z "${EMULEQT_NICK:-}" ] || NICK_LINE="  nick: \"${EMULEQT_NICK}\""

    cat > "$PREFS" <<EOF
general:
  promptOnExit: false
${NICK_LINE}

network:
  port: ${TCP_PORT}
  udpPort: ${UDP_PORT}

ed3k:
  udpPort: ${TCP_PORT}

upnp:
  enableUPnP: false

directories:
  incomingDir: "${DATA_DIR}/incoming"
  tempDirs:
    - "${DATA_DIR}/temp"

ipc:
  enabled: true
  port: 4712
  listenAddress: "0.0.0.0"
  tokens:
    - "${IPC_TOKEN}"

webserver:
  enabled: $(yml_bool "${EMULEQT_WEB:-1}")
  port: 4711
  listenAddress: ""
  adminPassword: "${ADMIN_HASH}"
  restApiEnabled: $(yml_bool "${EMULEQT_REST:-0}")
  mcpEnabled: $(yml_bool "${EMULEQT_MCP:-0}")
  apiKey: "${EMULEQT_API_KEY:-}"
EOF
    log "Wrote first-run $PREFS"
fi

# -- Drop privileges ---------------------------------------------------------
if [ "$(id -u)" = "0" ]; then
    # Config is small: fix it all. Data may be huge: top-level dirs only.
    chown -R emule:emule "$CONFIG_DIR"
    chown emule:emule "$DATA_DIR" "$DATA_DIR/incoming" "$DATA_DIR/temp"
    # Home follows a PUID remap: GUI icons, fontconfig cache
    export HOME=/home/emule
    chown -R emule:emule "$HOME"
    exec setpriv --reuid=emule --regid=emule --init-groups /usr/local/bin/emuleqt-run "$@"
fi

exec /usr/local/bin/emuleqt-run "$@"
