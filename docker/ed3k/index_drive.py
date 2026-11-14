#!/usr/bin/env python3
"""ed3knet.py --index: runs in the daemon container and drives emulecored over CBOR IPC
through the index node checks (ST_ED3K of an eD2K server, auto connect, server Disconnect).

usage: index_drive.py <ipc port> <server ip> <server port> <daemon log> <index node id hex>
"""
import ipaddress
import re
import socket
import struct
import sys
import time

# IPC message types (src/ipc/IpcProtocol.h)
HANDSHAKE, GET_SERVERS, ADD_SERVER, CONNECT_SERVER, DISCONNECT_SERVER = 100, 130, 135, 141, 142
SET_PREFS, GET_ED3K_STATUS, SET_ED3K_CONNECTED = 191, 1000, 1205


# --- minimal CBOR (what IPC uses: ints, strings, bytes, arrays, maps, simple values, floats) ---

def cbor_head(major: int, n: int) -> bytes:
    if n < 24:
        return bytes([major << 5 | n])
    for ai, fmt in ((24, ">B"), (25, ">H"), (26, ">I"), (27, ">Q")):
        if n < 1 << (8 * struct.calcsize(fmt)):
            return bytes([major << 5 | ai]) + struct.pack(fmt, n)
    raise ValueError(n)


def cbor_dumps(v) -> bytes:
    if v is False or v is True:
        return bytes([0xF5 if v else 0xF4])
    if v is None:
        return b"\xf6"
    if isinstance(v, int):
        return cbor_head(0, v) if v >= 0 else cbor_head(1, -1 - v)
    if isinstance(v, float):
        return b"\xfb" + struct.pack(">d", v)
    if isinstance(v, str):
        b = v.encode()
        return cbor_head(3, len(b)) + b
    if isinstance(v, bytes):
        return cbor_head(2, len(v)) + v
    if isinstance(v, (list, tuple)):
        return cbor_head(4, len(v)) + b"".join(cbor_dumps(x) for x in v)
    if isinstance(v, dict):
        return cbor_head(5, len(v)) + b"".join(cbor_dumps(k) + cbor_dumps(x) for k, x in v.items())
    raise TypeError(type(v))


def cbor_loads(b: bytes):
    v, _ = cbor_item(b, 0)
    return v


def cbor_item(b: bytes, i: int):
    ib = b[i]
    major, ai = ib >> 5, ib & 31
    i += 1
    if major == 7:
        if ai in (20, 21, 22, 23):
            return {20: False, 21: True, 22: None, 23: None}[ai], i
        if ai == 25:   # half float
            h = struct.unpack(">H", b[i:i + 2])[0]
            return struct.unpack(">e", struct.pack(">H", h))[0], i + 2
        if ai == 26:
            return struct.unpack(">f", b[i:i + 4])[0], i + 4
        if ai == 27:
            return struct.unpack(">d", b[i:i + 8])[0], i + 8
        return None, i
    if ai < 24:
        n = ai
    elif ai == 31:
        n = -1   # indefinite
    else:
        size = 1 << (ai - 24)
        n = int.from_bytes(b[i:i + size], "big")
        i += size
    if major == 0:
        return n, i
    if major == 1:
        return -1 - n, i
    if major in (2, 3):
        if n < 0:
            parts = []
            while b[i] != 0xFF:
                p, i = cbor_item(b, i)
                parts.append(p)
            return (b"" if major == 2 else "").join(parts), i + 1
        raw = b[i:i + n]
        return (raw if major == 2 else raw.decode(errors="replace")), i + n
    if major == 4:
        out = []
        while (n < 0 and b[i] != 0xFF) or len(out) < n:
            x, i = cbor_item(b, i)
            out.append(x)
        return out, i + (1 if n < 0 else 0)
    if major == 5:
        out = {}
        while (n < 0 and b[i] != 0xFF) or (n >= 0 and len(out) < n):
            k, i = cbor_item(b, i)
            out[k], i = cbor_item(b, i)
        return out, i + (1 if n < 0 else 0)
    if major == 6:   # tag: the value only
        return cbor_item(b, i)
    raise ValueError(ib)


class Ipc:
    def __init__(self, port: int):
        self.s = socket.create_connection(("127.0.0.1", port), timeout=10)
        self.seq = 0
        self.call(HANDSHAKE, ["1.0", ""])

    def call(self, mtype: int, fields=()):
        self.seq += 1
        body = cbor_dumps([mtype, self.seq, *fields])
        self.s.sendall(struct.pack(">I", len(body)) + body)
        while True:
            f = cbor_loads(self.exact(struct.unpack(">I", self.exact(4))[0]))
            if len(f) > 1 and f[1] == self.seq:
                return f

    def exact(self, n: int) -> bytes:
        b = b""
        while len(b) < n:
            c = self.s.recv(n - len(b))
            if not c:
                raise ConnectionError("closed")
            b += c
        return b


# --- checks ---

PORT, SRV_IP, SRV_PORT, LOG, INDEX_ID = int(sys.argv[1]), sys.argv[2], int(sys.argv[3]), sys.argv[4], sys.argv[5].lower()
SRV_NUM = int(ipaddress.IPv4Address(SRV_IP))   # GetServers' "ip" (network order as uint32)
failures = []


def say(*a):
    print("[index]", *a, flush=True)


def check(ok, what):
    say("PASS" if ok else "FAIL", what)
    if not ok:
        failures.append(what)


def log_text():
    with open(LOG, errors="replace") as f:
        return f.read()


def status(ipc):
    return ipc.call(GET_ED3K_STATUS)[3]


def index_nodes(ipc):
    return [n for n in status(ipc).get("indexNodes", []) if n["node"].lower() == INDEX_ID]


def connected(ipc):
    return any(n["state"] == 2 for n in index_nodes(ipc))


def wait(pred, secs, step=1.0):
    end = time.time() + secs
    while time.time() < end:
        v = pred()
        if v:
            return v
        time.sleep(step)
    return pred()


def server_row(ipc):
    rows = ipc.call(GET_SERVERS)[3]
    return next((r for r in rows if r.get("addr") == SRV_IP and r.get("port") == SRV_PORT), None)


def starts():
    return len(re.findall(r"ed3k node [0-9a-f]+ started", log_text()))


def stops():
    return log_text().count("ed3k node stopped")


ipc = Ipc(PORT)
check(ipc.call(SET_PREFS, ["ed3kAutoIndexNode", False])[2], "auto connect set off")
check(ipc.call(SET_ED3K_CONNECTED, [True])[2], "ed3k node started")
check(wait(lambda: status(ipc).get("running"), 20), "ed3k node running")
node0 = status(ipc)["nodeId"]
starts0, stops0 = starts(), stops()
say("ed3k node", node0)

# 1. auto connect off: the server names the index node, nothing is dialled
check(ipc.call(ADD_SERVER, [SRV_IP, SRV_PORT, "mock"])[2], "mock server added")
check(ipc.call(CONNECT_SERVER, [SRV_NUM, SRV_PORT, SRV_IP])[2], "connect to the mock server")
row = wait(lambda: (r := server_row(ipc)) and r.get("ed3k") and r, 20)
check(bool(row) and INDEX_ID in row.get("ed3k", ""), "server row carries the ST_ED3K link")
check(wait(lambda: "(auto connect off)" in log_text(), 10), "log: index node named, auto connect off")
time.sleep(5)
check(not index_nodes(ipc), "off: index node not dialled")

# 2. on: dialled live
check(ipc.call(SET_PREFS, ["ed3kAutoIndexNode", True])[2], "auto connect set on")
check(bool(wait(lambda: connected(ipc), 60)), "on: index node connected")
n = index_nodes(ipc)
check(bool(n) and n[0].get("agent", "") != "", "index node Hello agent shown")

# 3. off again: no restart, connection kept
check(ipc.call(SET_PREFS, ["ed3kAutoIndexNode", False])[2], "auto connect set off again")
time.sleep(15)
st = status(ipc)
check(st["running"] and st["nodeId"] == node0, "off again: same ed3k node running")
check(starts() == starts0 and stops() == stops0, "off again: no ed3k restart")
check(connected(ipc), "off again: index node still connected")

# 4. the server list's Disconnect: eD2K and the index node, no restart, no redial
mark = len(log_text())
check(ipc.call(DISCONNECT_SERVER, [SRV_NUM, SRV_PORT, SRV_IP])[2], "Disconnect of the mock server")
time.sleep(2)
st = status(ipc)
check(not index_nodes(ipc), "disconnect: index node gone from the list")
check(st["running"] and st["nodeId"] == node0, "disconnect: same ed3k node running")
check(starts() == starts0 and stops() == stops0, "disconnect: no ed3k restart")
check("index node of a disconnected eD2K server dropped" in log_text()[mark:], "log: index node dropped")
say("waiting 70 s: the index node must not be dialled again")
time.sleep(70)
check(not re.search(r"index node " + INDEX_ID[:8] + r" \S+: dialling", log_text()[mark:]),
      "disconnect: no redial within 70 s")
check(not index_nodes(ipc), "disconnect: still no index node")

# 5. connect again with auto off: named, not dialled; on: dialled again
check(ipc.call(CONNECT_SERVER, [SRV_NUM, SRV_PORT, SRV_IP])[2], "reconnect to the mock server")
check(wait(lambda: "(auto connect off)" in log_text()[mark:], 20), "reconnect: named again (auto off)")
time.sleep(3)
check(not index_nodes(ipc), "reconnect, off: not dialled")
check(ipc.call(SET_PREFS, ["ed3kAutoIndexNode", True])[2], "auto connect on")
check(bool(wait(lambda: connected(ipc), 60)), "on: connected again")
check(starts() == starts0 and stops() == stops0, "whole run: one ed3k start, no restart")

say("RESULT", "OK" if not failures else "FAILED: " + "; ".join(failures))
sys.exit(1 if failures else 0)
