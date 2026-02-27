#!/usr/bin/env bash
# test-daemon-lifecycle.sh — Verify daemon survives client disconnect (Scenario 2)
#                             and shuts down cleanly on IPC Shutdown (Scenario 1).
#
# Usage:  ./test-scripts/test-daemon-lifecycle.sh [build_dir]
#
# Requires: python3 with cbor2 module (pip3 install cbor2)

set -euo pipefail

BUILD_DIR="${1:-build}"
DAEMON="$BUILD_DIR/src/daemon/emulecored"
PASS=0
FAIL=0

cleanup() {
    # Kill any leftover daemon
    if [[ -n "${DAEMON_PID:-}" ]] && kill -0 "$DAEMON_PID" 2>/dev/null; then
        kill "$DAEMON_PID" 2>/dev/null
        wait "$DAEMON_PID" 2>/dev/null || true
    fi
}
trap cleanup EXIT

start_daemon() {
    "$DAEMON" &
    DAEMON_PID=$!
    sleep 2
    if ! kill -0 "$DAEMON_PID" 2>/dev/null; then
        echo "FAIL: Daemon failed to start"
        exit 1
    fi
}

# ============================================================================
# Scenario 1: Shutdown IPC makes daemon exit cleanly
# ============================================================================
echo "=== Scenario 1: Shutdown via IPC ==="
start_daemon

python3 -c "
import socket, struct, cbor2, time

sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
sock.connect(('127.0.0.1', 4712))

# Handshake (type=100, seqId=1, version='1.0')
payload = cbor2.dumps([100, 1, '1.0'])
sock.sendall(struct.pack('>I', len(payload)) + payload)
time.sleep(0.3)
sock.recv(4096)

# Shutdown (type=215, seqId=2)
payload = cbor2.dumps([215, 2])
sock.sendall(struct.pack('>I', len(payload)) + payload)
time.sleep(0.3)
sock.recv(4096)
sock.close()
"

sleep 2

if kill -0 "$DAEMON_PID" 2>/dev/null; then
    echo "FAIL: Daemon still alive after Shutdown IPC"
    FAIL=$((FAIL + 1))
    kill "$DAEMON_PID" 2>/dev/null
    wait "$DAEMON_PID" 2>/dev/null || true
else
    wait "$DAEMON_PID" 2>/dev/null || true
    echo "PASS: Daemon exited cleanly after Shutdown IPC"
    PASS=$((PASS + 1))
fi
DAEMON_PID=""

# ============================================================================
# Scenario 2: Client disconnect — daemon must survive
# ============================================================================
echo ""
echo "=== Scenario 2: Client disconnect (daemon must survive) ==="
start_daemon

python3 -c "
import socket, struct, cbor2, time

sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
sock.connect(('127.0.0.1', 4712))

# Handshake
payload = cbor2.dumps([100, 1, '1.0'])
sock.sendall(struct.pack('>I', len(payload)) + payload)
time.sleep(0.3)
sock.recv(4096)

# Abruptly close
sock.close()
"

sleep 2

if kill -0 "$DAEMON_PID" 2>/dev/null; then
    echo "PASS: Daemon survived client disconnect"
    PASS=$((PASS + 1))
else
    wait "$DAEMON_PID" 2>/dev/null || true
    echo "FAIL: Daemon died on client disconnect (exit $?)"
    FAIL=$((FAIL + 1))
    DAEMON_PID=""
fi

# ============================================================================
# Scenario 3: 5 rapid connect/disconnect cycles
# ============================================================================
echo ""
echo "=== Scenario 3: 5 rapid connect/disconnect cycles ==="

# Reuse daemon from Scenario 2 if still alive, otherwise start fresh
if [[ -z "${DAEMON_PID:-}" ]] || ! kill -0 "$DAEMON_PID" 2>/dev/null; then
    start_daemon
fi

python3 -c "
import socket, struct, cbor2, time

for i in range(5):
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.connect(('127.0.0.1', 4712))
    payload = cbor2.dumps([100, 1, '1.0'])
    sock.sendall(struct.pack('>I', len(payload)) + payload)
    time.sleep(0.2)
    sock.recv(4096)
    sock.close()
    time.sleep(0.3)
    print(f'  Cycle {i+1}/5 OK')
"

sleep 1

if kill -0 "$DAEMON_PID" 2>/dev/null; then
    echo "PASS: Daemon survived 5 rapid connect/disconnect cycles"
    PASS=$((PASS + 1))
else
    wait "$DAEMON_PID" 2>/dev/null || true
    echo "FAIL: Daemon died during stress test (exit $?)"
    FAIL=$((FAIL + 1))
    DAEMON_PID=""
fi

# ============================================================================
# Summary
# ============================================================================
echo ""
echo "==============================="
echo "Results: $PASS passed, $FAIL failed"
echo "==============================="

[[ $FAIL -eq 0 ]]
