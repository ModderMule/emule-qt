#!/usr/bin/env bash
# Build, then start daemon and GUI separately for screenshot/debug testing.
# Usage: scripts/debug-gui.sh [emuleqt args...]
# Example: scripts/debug-gui.sh --screenshot /tmp/kad.png --tab kad --subtab 1 --delay 8000
set -e
cd "$(dirname "$0")/.."

. scripts/build.sh

# Kill any leftover daemon from a previous run
pkill -f emulecored 2>/dev/null || true
sleep 0.5

# Start daemon in background
./build/src/daemon/emulecored &
DAEMON_PID=$!
trap 'kill $DAEMON_PID 2>/dev/null; wait $DAEMON_PID 2>/dev/null' EXIT

# Give the daemon time to start its IPC server
sleep 2

# Run GUI with any extra arguments passed to this script
./build/src/gui/emuleqt.app/Contents/MacOS/emuleqt "$@"
