#!/usr/bin/env python3
"""Local mock eD2K peer vs a running emulecored: "View Shared Files" allowed / denied.

Connects to the daemon's TCP port as a plain eDonkey peer, asks for its file list over
IPC (RequestClientSharedFiles) and checks the outcome: a PushClientSharedFiles tab with
the mock's files, or the "denied" / "does not allow" status-log lines. A GUI connected
to the same daemon jumps to Search and shows the tab.

Needs a running emulecored (TCP 5662, IPC 4712, filterLANIPs=false) and python cbor2.
Note: an attached GUI stores the mock tab in StoredSearches.json; close it afterwards.

usage: mock-browse-peer.py [allow|deny|both] [--hold SECONDS]
"""
import socket, struct, time, select, sys, threading, os, cbor2

ED2K = 0xE3
OP_HELLO, OP_HELLOANSWER = 0x01, 0x4C
OP_ASKSHAREDFILES, OP_ASKSHAREDFILESANSWER = 0x4A, 0x4B
OP_ASKSHAREDDIRS, OP_ASKSHAREDFILESDIR = 0x5D, 0x5E
OP_ASKSHAREDDIRSANS, OP_ASKSHAREDFILESDIRANS, OP_ASKSHAREDDENIEDANS = 0x5F, 0x60, 0x61
DAEMON_TCP, IPC_PORT = 5662, 4712

def tag_str(tid, s):
    b = s.encode()
    return bytes([0x02]) + struct.pack("<H", 1) + bytes([tid]) + struct.pack("<H", len(b)) + b

def tag_u32(tid, v):
    return bytes([0x03]) + struct.pack("<H", 1) + bytes([tid]) + struct.pack("<I", v)

def packet(op, payload=b""):
    return bytes([ED2K]) + struct.pack("<I", len(payload) + 1) + bytes([op]) + payload

def file_list(files):
    out = struct.pack("<I", len(files))
    for i, (name, size) in enumerate(files):
        out += bytes([0x40 + i]) * 16 + struct.pack("<IH", 0, 0)
        out += struct.pack("<I", 2) + tag_str(0x01, name) + tag_u32(0x02, size)
    return out

class MockPeer(threading.Thread):
    def __init__(self, name, user_hash, port, allow):
        super().__init__(daemon=True)
        self.name, self.hash, self.port, self.allow = name, user_hash, port, allow
        self.seen = []
        self.sock = socket.create_connection(("127.0.0.1", DAEMON_TCP))
        # plain eDonkey hello: no mule tags → no "no view shared" bit, no shared-dirs support
        hello = bytes([16]) + self.hash + struct.pack("<IH", 0x0100007F, self.port)
        hello += struct.pack("<I", 2) + tag_str(0x01, name) + tag_u32(0x11, 0x3C)
        hello += struct.pack("<IH", 0, 0)
        self.sock.sendall(packet(OP_HELLO, hello))

    def run(self):
        buf = b""
        while True:
            try:
                chunk = self.sock.recv(65536)
            except OSError:
                return
            if not chunk:
                return
            buf += chunk
            while len(buf) >= 6:
                n = struct.unpack("<I", buf[1:5])[0]
                if len(buf) < 5 + n:
                    break
                proto, op, body = buf[0], buf[5], buf[6:5 + n]
                buf = buf[5 + n:]
                self.seen.append((proto, op))
                if proto != ED2K:
                    continue
                if op in (OP_ASKSHAREDFILES, OP_ASKSHAREDDIRS) and not self.allow:
                    self.sock.sendall(packet(OP_ASKSHAREDDENIEDANS))
                elif op == OP_ASKSHAREDFILES:
                    self.sock.sendall(packet(OP_ASKSHAREDFILESANSWER, file_list([
                        ("Mock Movie (2026).avi", 734003200),
                        ("mock song.mp3", 5242880),
                        ("mock-docs.pdf", 1048576)])))
                elif op == OP_ASKSHAREDDIRS:
                    d = "Incoming".encode()
                    self.sock.sendall(packet(OP_ASKSHAREDDIRSANS,
                                             struct.pack("<IH", 1, len(d)) + d))
                elif op == OP_ASKSHAREDFILESDIR:
                    self.sock.sendall(packet(OP_ASKSHAREDFILESDIRANS,
                                             body + file_list([("in dir.mkv", 99999999)])))

# ---------------------------------------------------------------- IPC
ipc = socket.create_connection(("127.0.0.1", IPC_PORT))
seq, ibuf, pending, status, pushes = 0, b"", {}, [], []

def send(t, *fields):
    global seq
    seq += 1
    body = cbor2.dumps([t, seq, *fields])
    ipc.sendall(struct.pack(">I", len(body)) + body)
    return seq

def pump(timeout):
    global ibuf
    end = time.time() + timeout
    while time.time() < end:
        r, _, _ = select.select([ipc], [], [], max(0.0, end - time.time()))
        if not r:
            return
        ibuf += ipc.recv(1 << 20)
        while len(ibuf) >= 4:
            n = struct.unpack(">I", ibuf[:4])[0]
            if len(ibuf) < 4 + n:
                break
            f = cbor2.loads(ibuf[4:4 + n]); ibuf = ibuf[4 + n:]
            if f[1] and f[1] in pending:
                pending[f[1]] = f
            elif f[0] == 450 and f[3] == "emule.status":
                status.append(f[5]); print("  STATUS:", f[5], flush=True)
            elif f[0] == 520:
                pushes.append(f[2:]); print("  PUSH ClientSharedFiles:", f[2:], flush=True)

def call(t, *fields, timeout=10):
    sq = send(t, *fields); pending[sq] = None
    end = time.time() + timeout
    while pending[sq] is None and time.time() < end:
        pump(0.3)
    return pending.pop(sq)

call(100, "1.0", "")
pump(1)

def scenario(allow, hash_byte, port):
    label = "allow" if allow else "deny"
    print(f"== {label}", flush=True)
    h = bytes([hash_byte]) * 16
    peer = MockPeer(f"MockBrowse-{label}", h, port, allow)
    peer.start()
    pump(2)   # hello / hello answer
    n0s, n0p = len(status), len(pushes)
    r = call(251, h.hex().upper())
    print("  RequestClientSharedFiles ->", r[2:], flush=True)
    pump(4)
    ok = True
    if allow:
        mine = [p for p in pushes[n0p:] if p[1] == peer.name]
        ok = bool(mine)
        if mine:
            res = call(151, mine[-1][2])[3] or []
            print(f"  tab #{mine[-1][2]}: {len(res)} results:",
                  sorted(x['fileName'] for x in res), flush=True)
            ok = len(res) >= 3
    else:
        ok = any("denied access" in s for s in status[n0s:]) and len(pushes) == n0p
        r = call(251, h.hex().upper()); pump(1)
        ok = ok and any("does not allow" in s for s in status[n0s:])
    print(f"  opcodes seen by mock: {[hex(o) for _, o in peer.seen]}")
    print(f"  RESULT {label}: {'PASS' if ok else 'FAIL'}", flush=True)
    return peer, ok

mode = sys.argv[1] if len(sys.argv) > 1 else "both"
hold = float(sys.argv[sys.argv.index("--hold") + 1]) if "--hold" in sys.argv else 0
results, peers = [], []
if mode in ("allow", "both"):
    p, ok = scenario(True, 0x5A, 4881); results.append(ok); peers.append(p)
if mode in ("deny", "both"):
    p, ok = scenario(False, 0x5B, 4882); results.append(ok); peers.append(p)
if hold:
    pump(hold)
print("ALL PASS" if all(results) else "SOME FAILED")
sys.exit(0 if all(results) else 1)
