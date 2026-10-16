#!/bin/sh
# emuleqt-run — runs as `emule`: emulecored, plus the GUI stack with EMULEQT_GUI=1
#
# The daemon is the main process: when it exits the container stops. The GUI
# is restarted when closed; closing it never stops the core (daemonPath: local).
set -eu

CONFIG_DIR=/config
# GUI keeps its own prefs: ipc.listenAddress is the daemon's bind (0.0.0.0) but the
# GUI's connect target (loopback here); one shared file would fight over it
GUI_CONFIG_DIR="$CONFIG_DIR/gui"

log() { echo "[run] $*"; }

is_on() {
    case "$(echo "${1:-0}" | tr '[:upper:]' '[:lower:]')" in
        1|yes|true|on) return 0 ;;
        *) return 1 ;;
    esac
}

GUI_PIDS=""

start_gui() {
    export DISPLAY=:0
    rm -f /tmp/.X0-lock /tmp/.X11-unix/X0

    if [ ! -f "$GUI_CONFIG_DIR/preferences.yml" ]; then
        mkdir -p "$GUI_CONFIG_DIR"
        cat > "$GUI_CONFIG_DIR/preferences.yml" <<EOF
general:
  promptOnExit: false

ipc:
  enabled: true
  port: 4712
  listenAddress: "127.0.0.1"
  daemonPath: "local"
EOF
    fi

    Xvfb :0 -screen 0 "${EMULEQT_GUI_RES:-1600x1000x24}" -nolisten tcp &
    GUI_PIDS="$GUI_PIDS $!"
    i=0
    while [ ! -e /tmp/.X11-unix/X0 ] && [ $i -lt 50 ]; do sleep 0.1; i=$((i + 1)); done

    openbox &
    GUI_PIDS="$GUI_PIDS $!"

    VNC_ARGS="-display :0 -forever -shared -quiet -rfbport 5900"
    if [ -n "${EMULEQT_VNC_PASSWORD:-}" ]; then
        x11vnc -storepasswd "$EMULEQT_VNC_PASSWORD" "${HOME:-/tmp}/.vncpass" >/dev/null
        VNC_ARGS="$VNC_ARGS -rfbauth ${HOME:-/tmp}/.vncpass"
    else
        VNC_ARGS="$VNC_ARGS -nopw"
        log "WARNING: EMULEQT_VNC_PASSWORD not set - the GUI on :5800 is open to anyone who can reach it"
    fi
    is_on "${EMULEQT_VNC_EXPOSE:-0}" || VNC_ARGS="$VNC_ARGS -localhost"
    # shellcheck disable=SC2086
    x11vnc $VNC_ARGS &
    GUI_PIDS="$GUI_PIDS $!"

    websockify --web /usr/share/novnc 5800 localhost:5900 >/dev/null 2>&1 &
    GUI_PIDS="$GUI_PIDS $!"

    # Restart the GUI whenever the user closes it
    (
        while :; do
            emuleqt --config "$GUI_CONFIG_DIR" || true
            sleep 2
        done
    ) &
    GUI_PIDS="$GUI_PIDS $!"

    log "GUI on http://<host>:5800"
}

stop_gui() {
    [ -n "$GUI_PIDS" ] || return 0
    # shellcheck disable=SC2086
    kill $GUI_PIDS 2>/dev/null || true
    pkill -u "$(id -u)" -x emuleqt.bin 2>/dev/null || true
}

is_on "${EMULEQT_GUI:-0}" && start_gui

emulecored --config "$CONFIG_DIR" "$@" &
DAEMON_PID=$!

# tini signals only us; pass the stop on to the daemon so it saves its part files
STOPPING=0
trap 'STOPPING=1; kill -TERM "$DAEMON_PID" 2>/dev/null || true' TERM INT HUP

status=0
# wait returns early on a trapped signal; keep waiting until the daemon is gone
while kill -0 "$DAEMON_PID" 2>/dev/null; do
    wait "$DAEMON_PID" || status=$?
done

# A trapped signal interrupts wait with 128+n; the daemon itself stopped cleanly
[ "$STOPPING" = 1 ] && [ "$status" -gt 128 ] && status=0

stop_gui
log "emulecored exited ($status)"
exit "$status"
