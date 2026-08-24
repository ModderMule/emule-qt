#!/usr/bin/env bash
# Build, then start daemon and GUI separately for screenshot/debug testing.
# Usage: scripts/debug-gui.sh [--lldb] [emuleqt args...]
# Example: scripts/debug-gui.sh --screenshot /tmp/kad.png --tab kad --subtab 1 --delay 8000
# Example: scripts/debug-gui.sh --lldb
set -e
cd "$(dirname "$0")/.."

USE_LLDB=false
if [ "$1" = "--lldb" ]; then
    USE_LLDB=true
    shift
fi

# Run, don't source: build.sh parses "$@", so sourcing hands it this script's GUI
# arguments and the first one becomes its build directory ("cmake -B --tab").
scripts/build.sh

# Seed the config directory on its first run.  AppConfig::seedBundledData()
# only reaches data/config through its EMULE_DEV_BUILD candidate, and that is
# Debug-only (cmake/CompilerSettings.cmake) while build.sh always builds
# Release -- so without this a fresh checkout starts with no server.met and no
# Kad bootstrap nodes.  All-or-nothing on the directory: an existing config dir
# is left alone, preferences.yml and uistate.yml included.
if [ "$(uname -s)" = "Darwin" ]; then
    CONFIG_DIR="$HOME/eMuleQt/Config"
    if [ ! -d "$CONFIG_DIR" ]; then
        mkdir -p "$CONFIG_DIR"
        cp -R data/config/. "$CONFIG_DIR/"
        echo "Seeded config from data/config -> $CONFIG_DIR"
    fi
fi

# Kill any leftover daemon from a previous run
pkill -f emulecored 2>/dev/null || true
sleep 0.5

# Symlink daemon next to the GUI so resolveDaemonPath() finds it
#ln -sf "$(pwd)/build/src/daemon/emulecored" ./build/src/gui/emuleqt.app/Contents/MacOS/emulecored

# Start daemon in background
./build/src/daemon/emulecored &
DAEMON_PID=$!
trap 'kill $DAEMON_PID 2>/dev/null; wait $DAEMON_PID 2>/dev/null' EXIT

# Give the daemon time to start its IPC server
sleep 2

# Run GUI with any extra arguments passed to this script
GUI=./build/src/gui/emuleqt.app/Contents/MacOS/emuleqt
if $USE_LLDB; then
    lldb -o run -o 'settings set auto-confirm true' --batch -- "$GUI" "$@"
else
    "$GUI" "$@"
fi
