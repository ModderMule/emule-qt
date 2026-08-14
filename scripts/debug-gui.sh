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
