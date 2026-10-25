#!/usr/bin/env python3
"""
natnet.py — LowID-to-LowID NAT rendezvous rig: one or two servers, two firewalled emulecored nodes.

Node A shares a random file, node B downloads it. Both are LowIDs (the server's IPv4
dial-back is refused), so the transfer only works if the server introduces them and
the IPv6 handoff opens a connection (docs/protocol/natt-rendezvous.md).

Servers (--server):
    lugdunum      Lugdunum eserver 17.x under qemu-i386   UDP 0xA6 / 0xA7, TCP 0x37
    ed2k-server   Rust ed2k-server 0.9.84+                TCP 0x60 / 0x61, UDP 0x9F
    enode         eNode-go                                PR_NAT (UDP 0xF1, port 2004)
    enode-lugdunum  eNode-go with PR_NAT off              UDP 0xA6 / 0xA7, TCP 0x37
    enode2        two eNode-go servers, A on 1, B on 2    PR_NAT cross-server sweep

enode2: B may not log in to server 1 (its TCP there is rejected) and learns A from a
saved source list (SLS): A's LowID on server 1 plus its user hash, as a source kept from
an earlier session. Server 1 sits in B's server.met with its UDP flags (0x40000) and NAT
port, as if saved by an earlier run. B's SYNC2 goes to its own server 2 first (FAILED:
A is not registered there), then to server 1, which pairs them.

Server binaries live outside this repo; their paths come from docker/natt/natnet.env
(see natnet.env.example) or the environment.

Usage:
    python3 docker/natt/natnet.py --server lugdunum --build   # build images, run once
    python3 docker/natt/natnet.py --server ed2k-server        # reuse the images
    python3 docker/natt/natnet.py --server enode --keep       # leave it running afterwards
    python3 docker/natt/natnet.py --server enode2             # cross-server PR_NAT sweep
    python3 docker/natt/natnet.py --console vc                # Lugdunum console command
    python3 docker/natt/natnet.py --logs                      # follow node and server logs
    python3 docker/natt/natnet.py --down                      # tear down
"""

import argparse
import hashlib
import ipaddress
import os
import re
import shutil
import socket
import struct
import subprocess
import sys
import time

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, "..", ".."))
DEFAULT_ENV_FILE = os.path.join(SCRIPT_DIR, "natnet.env")

BASE_IMAGE = "emuleqt:natnet-base"      # docker/emuleqt/Dockerfile, built from this tree
DAEMON_IMAGE = "emuleqt:natnet"         # + iptables/tcpdump (daemon.Dockerfile)
LUGDUNUM_IMAGE = "natnet-lugdunum:latest"
RUNTIME_IMAGE = "debian:trixie-slim"    # runs the ed2k-server and eNode-go binaries
RUST_IMAGE = "rust:1-trixie"         # full image: jemalloc-sys needs make
GO_IMAGE = "golang:1.25"                # GOTOOLCHAIN=auto fetches what go.mod asks for

NETWORK = "natnet"
SUBNET4 = "172.30.77.0/24"
SUBNET6 = "2001:db8:4677::/64"
SERVER_IP = "172.30.77.10"
SERVER_IP6 = "2001:db8:4677::10"
SERVER_CONTAINER = "natnet-server"
# enode2 only: the server B logs in to
SERVER2_IP = "172.30.77.20"
SERVER2_IP6 = "2001:db8:4677::20"
SERVER2_CONTAINER = "natnet-server2"
NAT_PORT = 2004                         # eNode-go natTraversal.port
ST_SERVERNAME = 0x01
ST_UDPFLAGS = 0x92
ST_NAT_PORT = 0x9D
UDP_FLAG_NAT_RENDEZVOUS = 0x40000
NODES = {
    # name: (IPv4, IPv6, user hash). Bytes 5 and 14 are eMule's 0x0E / 0x6F markers:
    # Lugdunum withholds LowID sources from a requester whose hash lacks them.
    "A": ("172.30.77.11", "2001:db8:4677::11", "a1a1a1a1a10ea1a1a1a1a1a1a1a16fa1"),
    "B": ("172.30.77.12", "2001:db8:4677::12", "b2b2b2b2b20eb2b2b2b2b2b2b2b26fb2"),
}
TCP_PORT = 4662
UDP_PORT = 4672
TEST_FILE = "natnet-test.bin"

SERVERS = {
    "lugdunum":    {"port": 4661, "path": "Lugdunum"},
    "ed2k-server": {"port": 4661, "path": "ed2k-server"},
    "enode":       {"port": 5555, "path": "PR_NAT"},
    # eNode-go reports 0 instead of a private observed IPv4 to a LowID, and without a
    # public IPv4 eMuleQt holds back the server UDP key that 0xA6 is sent with. A
    # documentation range is not private, so the callback runs as it does on the internet.
    "enode-lugdunum": {"port": 5555, "path": "Lugdunum", "odd_ids": True,
                       "subnet4": "198.51.100"},
    "enode2":      {"port": 5555, "path": "PR_NAT", "two_servers": True},
}


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def log(msg: str) -> None:
    print(f"[natnet {time.strftime('%H:%M:%S')}] {msg}", flush=True)


def die(msg: str) -> None:
    log(f"ERROR: {msg}")
    sys.exit(2)


def run(*args, check=True, capture=False, **kw):
    """Run a command; args may be nested lists."""
    flat = []
    for a in args:
        flat.extend(a if isinstance(a, (list, tuple)) else [a])
    return subprocess.run([str(a) for a in flat], check=check, text=True,
                          capture_output=capture, **kw)


def docker(*args, check=True, capture=False):
    return run("docker", *args, check=check, capture=capture)


def image_exists(name: str) -> bool:
    return docker("image", "inspect", name, check=False, capture=True).returncode == 0


def load_env(path: str) -> dict:
    """KEY=VALUE lines; the process environment wins over the file."""
    env = {}
    if os.path.isfile(path):
        with open(path, encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith("#") or "=" not in line:
                    continue
                key, value = line.split("=", 1)
                env[key.strip()] = value.strip().strip('"').strip("'")
    for key, value in os.environ.items():
        if key.startswith("NATNET_"):
            env[key] = value
    return {k: os.path.expanduser(os.path.expandvars(v)) for k, v in env.items()}


def need_path(env: dict, key: str, what: str) -> str:
    value = env.get(key, "")
    if not value:
        die(f"{key} is not set: {what} (docker/natt/natnet.env, see natnet.env.example)")
    if not os.path.exists(value):
        die(f"{key}={value} does not exist")
    return value


def wait_for(what: str, predicate, timeout: float, interval: float = 1.0):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        result = predicate()
        if result:
            return result
        time.sleep(interval)
    return None


def read_text(path: str) -> str:
    try:
        with open(path, encoding="utf-8", errors="replace") as f:
            return f.read()
    except OSError:
        return ""


# ---------------------------------------------------------------------------
# Config patching (line based, section aware: no TOML/YAML module in the stdlib)
# ---------------------------------------------------------------------------

def patch_toml(text: str, section: str, key: str, value: str) -> str:
    """Replace `key = ...` inside [section], including a multi-line array."""
    lines = text.split("\n")
    current = ""
    for i, line in enumerate(lines):
        header = re.match(r"^\s*\[([^\]]+)\]\s*$", line)
        if header:
            current = header.group(1)
            continue
        if current == section and re.match(rf"^{re.escape(key)}\s*=", line):
            end = i
            if re.match(rf"^{re.escape(key)}\s*=\s*\[", line) and "]" not in line.split("#")[0]:
                while end < len(lines) - 1 and not lines[end].strip().startswith("]"):
                    end += 1
            lines[i:end + 1] = [f"{key} = {value}"]
            return "\n".join(lines)
    die(f"config.toml has no {key} in [{section}]")
    return text


def patch_yaml(text: str, path: str, value: str) -> str:
    """Replace the scalar at a dotted path (e.g. ipv6.publishSources), two-space indent."""
    parts = path.split(".")
    lines = text.split("\n")
    depth = 0
    for i, line in enumerate(lines):
        indent = len(line) - len(line.lstrip(" "))
        if line.strip().startswith("#") or not line.strip():
            continue
        if indent < depth * 2 and depth > 0:
            die(f"yaml path {path} not found")
        m = re.match(rf"^{' ' * (depth * 2)}{re.escape(parts[depth])}:(\s*)(.*)$", line)
        if not m:
            continue
        if depth == len(parts) - 1:
            comment = ""
            if "#" in m.group(2):
                comment = "  #" + m.group(2).split("#", 1)[1]
            lines[i] = f"{' ' * (depth * 2)}{parts[depth]}: {value}{comment}"
            return "\n".join(lines)
        depth += 1
    die(f"yaml path {path} not found")
    return text


# ---------------------------------------------------------------------------
# Images and server binaries
# ---------------------------------------------------------------------------

def build_daemon_image(force: bool) -> None:
    if force or not image_exists(BASE_IMAGE):
        log(f"building {BASE_IMAGE} from this tree (docker/emuleqt/Dockerfile)")
        docker("build", "-f", os.path.join(PROJECT_ROOT, "docker", "emuleqt", "Dockerfile"),
               "-t", BASE_IMAGE, PROJECT_ROOT)
    # Thin and cached: rebuilt every run so rig-wrap.sh edits apply
    docker("build", "-q", "-f", os.path.join(SCRIPT_DIR, "daemon.Dockerfile"),
           "--build-arg", f"BASE={BASE_IMAGE}", "-t", DAEMON_IMAGE, SCRIPT_DIR, capture=True)


def prepare_lugdunum(env: dict, work: str, force: bool) -> None:
    binary = need_path(env, "NATNET_LUGDUNUM_BIN", "the eserver 17.x i686 Linux binary")
    if force or not image_exists(LUGDUNUM_IMAGE):
        ctx = os.path.join(work, "build", "lugdunum")
        shutil.rmtree(ctx, ignore_errors=True)
        os.makedirs(ctx)
        shutil.copy(binary, os.path.join(ctx, "eserver"))
        for name in ("Dockerfile", "entrypoint.sh"):
            shutil.copy(os.path.join(SCRIPT_DIR, "lugdunum", name), ctx)
        log(f"building {LUGDUNUM_IMAGE} from {binary}")
        docker("build", "-t", LUGDUNUM_IMAGE, ctx)


def prepare_ed2k_server(env: dict, work: str) -> str:
    """Path of a Linux ed2k-server binary: NATNET_ED2K_SERVER_BIN, else built from the source."""
    if env.get("NATNET_ED2K_SERVER_BIN"):
        return need_path(env, "NATNET_ED2K_SERVER_BIN", "a Linux ed2k-server binary")
    src = need_path(env, "NATNET_ED2K_SERVER_SRC", "the ed2k-server checkout")
    target = os.path.join(work, "build", "ed2k-server")
    registry = os.path.join(work, "cache", "cargo-registry")
    os.makedirs(target, exist_ok=True)
    os.makedirs(registry, exist_ok=True)
    log(f"building ed2k-server from {src} (cargo, {RUST_IMAGE})")
    docker("run", "--rm", "-v", f"{src}:/src:ro", "-v", f"{target}:/target",
           "-v", f"{registry}:/usr/local/cargo/registry", "-e", "CARGO_TARGET_DIR=/target",
           RUST_IMAGE, "cargo", "build", "--release", "--locked",
           "--manifest-path", "/src/Cargo.toml")
    return os.path.join(target, "release", "ed2k-server")


def prepare_enode(env: dict, work: str) -> str:
    """Path of a Linux eNode-go binary: NATNET_ENODE_BIN, else built from the source."""
    if env.get("NATNET_ENODE_BIN"):
        return need_path(env, "NATNET_ENODE_BIN", "a Linux eNode-go binary")
    src = need_path(env, "NATNET_ENODE_SRC", "the eNode-go checkout")
    out_dir = os.path.join(work, "build", "enode")
    os.makedirs(out_dir, exist_ok=True)
    out = os.path.join(out_dir, "enode")
    arch = docker("version", "--format", "{{.Server.Arch}}", capture=True).stdout.strip()
    log(f"building eNode-go from {src} (linux/{arch})")
    if shutil.which("go"):
        env_vars = dict(os.environ, GOOS="linux", GOARCH=arch, CGO_ENABLED="0",
                        GOFLAGS="-buildvcs=false")
        run("go", "build", "-o", out, "./cmd/enode", cwd=src, env=env_vars)
    else:
        cache = os.path.join(work, "cache", "go")
        os.makedirs(cache, exist_ok=True)
        docker("run", "--rm", "-v", f"{src}:/src:ro", "-v", f"{out_dir}:/out",
               "-v", f"{cache}:/go/pkg", "-w", "/src", "-e", "CGO_ENABLED=0",
               "-e", "GOFLAGS=-buildvcs=false", "-e", "GOTOOLCHAIN=auto",
               GO_IMAGE, "go", "build", "-o", "/out/enode", "./cmd/enode")
    return out


# ---------------------------------------------------------------------------
# Server configs
# ---------------------------------------------------------------------------

def write_lugdunum_config(server_dir: str) -> None:
    text = read_text(os.path.join(SCRIPT_DIR, "lugdunum", "donkey.ini.tmpl"))
    text = text.replace("__THIS_IP__", SERVER_IP).replace("__PORT__", str(SERVERS["lugdunum"]["port"]))
    with open(os.path.join(server_dir, "donkey.ini"), "w", encoding="utf-8") as f:
        f.write(text)


def write_ed2k_server_config(env: dict, server_dir: str) -> None:
    template = env.get("NATNET_ED2K_SERVER_CONFIG") or os.path.join(
        need_path(env, "NATNET_ED2K_SERVER_SRC", "the ed2k-server checkout (for config.toml)"),
        "config", "config.toml")
    text = read_text(template)
    if not text:
        die(f"cannot read {template}")
    for section, key, value in (
            ("server", "name", '"natnet-ed2k-server"'),
            ("server", "desc", '"eMuleQt NAT rendezvous rig"'),
            ("server", "public", "false"),
            ("server", "this_ip", f'"{SERVER_IP}"'),
            ("server", "seed_servers", "[]"),
            ("network", "tcp_port", str(SERVERS["ed2k-server"]["port"])),
            # A published IPv6 source is a direct source: the rendezvous would never run
            ("network", "ipv6_enabled", "false"),
            ("network", "ipv6_publish_sources", "false"),
            ("log", "level", '"debug"'),
            ("admin", "enabled", "false"),
            ("updates", "enabled", "false"),
            ("updates", "dest_dir", '"/srv"')):
        text = patch_toml(text, section, key, value)
    # Filter and database files are optional; point them into the run dir
    text = re.sub(r'"/etc/ed2k-server/', '"/srv/', text)
    with open(os.path.join(server_dir, "config.toml"), "w", encoding="utf-8") as f:
        f.write(text)


def write_enode_config(env: dict, server_dir: str, pr_nat: bool = True, name: str = "natnet-enode",
                       ip: str = "", ip6: str = "") -> None:
    template = env.get("NATNET_ENODE_CONFIG") or os.path.join(
        need_path(env, "NATNET_ENODE_SRC", "the eNode-go checkout (for enode.config.yaml)"),
        "enode.config.yaml")
    text = read_text(template)
    if not text:
        die(f"cannot read {template}")
    for path, value in (
            ("name", f'"{name}"'),
            ("address", '""'),
            ("dynIp", f'"{ip or SERVER_IP}"'),
            ("logLevel", '"debug"'),
            ("logFile", '"/srv/enode.log"'),
            ("pidFile", '"/srv/enode.pid"'),
            ("tcp.port", str(SERVERS["enode"]["port"])),
            ("ipv6.dynIp6", f'"{ip6 or SERVER_IP6}"'),
            # A published IPv6 source is a direct source: the rendezvous would never run
            ("ipv6.publishSources", "false"),
            # Off for enode-lugdunum: the client then uses eNode-go's Lugdunum callback
            ("natTraversal.enabled", "true" if pr_nat else "false"),
            ("natTraversal.lugdunumCallback", "true"),
            # The callback is IPv4-only: eMuleQt skips it on an IPv6 server session, so
            # enode-lugdunum keeps the session on IPv4, as a real Lugdunum server does
            ("ipv6.enabled", "true" if pr_nat else "false"),
            ("metaApi.enabled", "false"),
            ("admin.enabled", "false"),
            ("debug.seedFixtures", "false")):
        text = patch_yaml(text, path, value)
    with open(os.path.join(server_dir, "enode.yaml"), "w", encoding="utf-8") as f:
        f.write(text)


# ---------------------------------------------------------------------------
# Nodes
# ---------------------------------------------------------------------------

PREFS = """general:
  nick: natnet-{name}
  userHash: {hash}
  autoConnect: true
  reconnect: true
  filterLANIPs: false
  promptOnExit: false
  versionCheckEnabled: false
serverConnection:
  safeServerConnect: false
  natTraversal: true
  addServersFromServer: false
  addServersFromClients: false
  autoUpdateServerList: false
  smartLowIdCheck: false
network:
  port: {tcp}
  udpPort: {udp}
  publicIPv6Override: "{ipv6}"
  networkED2K: true
bandwidth:
  maxUpload: 0
  maxDownload: 0
encryption:
  cryptLayerSupported: true
  cryptLayerRequested: true
  cryptLayerRequired: false
directories:
  incomingDir: /data/incoming
  tempDirs:
    - /data/temp
upnp:
  enableUPnP: false
logging:
  logToDiskCore: true
  verbose: true
  serverVerboseLog: true
  logUlDlEvents: true
ipc:
  enabled: true
  port: 4712
  listenAddress: 127.0.0.1
webserver:
  enabled: false
kademlia:
  enabled: false
usenet:
  enabled: false
bittorrent:
  enabled: false
httpCache:
  enabled: false
"""


def server_met(entries) -> bytes:
    """server.met from (ip, port, name, {tag id: uint32}) entries. A fresh file each run:
    a kept UDP key from an earlier server instance would be stale."""
    out = b"\x0e" + struct.pack("<I", len(entries))
    for ip, port, name, ints in entries:
        ip_le = int.from_bytes(ipaddress.IPv4Address(ip).packed, "little")
        raw = name.encode()
        tags = [struct.pack("<BHBH", 0x02, 1, ST_SERVERNAME, len(raw)) + raw]
        tags += [struct.pack("<BHBI", 0x03, 1, tag, value) for tag, value in ints.items()]
        out += struct.pack("<IHI", ip_le, port, len(tags)) + b"".join(tags)
    return out


def prepare_node(work: str, name: str, size_mb: int, servers) -> str:
    """servers: server.met entries, see server_met()."""
    ipv4, ipv6, user_hash = NODES[name]
    node_dir = os.path.join(work, "run", name)
    for sub in ("config", "data/incoming", "data/temp"):
        os.makedirs(os.path.join(node_dir, sub), exist_ok=True)
    with open(os.path.join(node_dir, "config", "preferences.yml"), "w", encoding="utf-8") as f:
        f.write(PREFS.format(name=name, hash=user_hash.upper(), tcp=TCP_PORT, udp=UDP_PORT, ipv6=ipv6))
    with open(os.path.join(node_dir, "config", "server.met"), "wb") as f:
        f.write(server_met(servers))
    if name == "A":
        with open(os.path.join(node_dir, "data", "incoming", TEST_FILE), "wb") as f:
            for _ in range(size_mb):
                f.write(os.urandom(1024 * 1024))
    return node_dir


def start_node(work: str, name: str, server_ips: str, reject_tcp: str = "") -> None:
    """server_ips: servers whose dial-back is refused; reject_tcp: "ip:port" not to log in to."""
    ipv4, ipv6, _ = NODES[name]
    node_dir = os.path.join(work, "run", name)
    docker("run", "-d", "--name", f"natnet-{name}", "--hostname", f"natnet-{name}",
           "--network", NETWORK, "--ip", ipv4, "--ip6", ipv6,
           "--cap-add", "NET_ADMIN", "--cap-add", "NET_RAW",
           "-e", f"SERVER_IP={server_ips}", "-e", f"RIG_REJECT_TCP={reject_tcp}",
           "-e", f"PUID={os.getuid()}", "-e", f"PGID={os.getgid()}",
           "-e", f"EMULEQT_TCP_PORT={TCP_PORT}",
           "-v", f"{node_dir}/config:/config", "-v", f"{node_dir}/data:/data",
           DAEMON_IMAGE, capture=True)


def node_logs(work: str, name: str) -> str:
    cfg = os.path.join(work, "run", name, "config")
    return read_text(os.path.join(cfg, "emulecored.log")) + read_text(
        os.path.join(cfg, "emulecored_Verbose.log"))


def login_count(work: str, name: str) -> int:
    return len(re.findall(r"onLoginReceived: clientID=", node_logs(work, name)))


def login_state(work: str, name: str):
    """'LowID' / 'HighID' once the node logged in, else None."""
    m = re.findall(r"onLoginReceived: clientID=\d+ \((LowID|HighID)\)", node_logs(work, name))
    return m[-1] if m else None


def login_id(work: str, name: str):
    m = re.findall(r"onLoginReceived: clientID=(\d+) ", node_logs(work, name))
    return int(m[-1]) if m else None


def offered(work: str, name: str) -> bool:
    return bool(re.search(r"Sent [1-9]\d* shared files to server", node_logs(work, name)))


def _md4_pure(data: bytes) -> bytes:
    """RFC 1320 MD4, for OpenSSL builds without the legacy provider."""
    def f(x, y, z): return (x & y) | (~x & z)
    def g(x, y, z): return (x & y) | (x & z) | (y & z)
    def h(x, y, z): return x ^ y ^ z
    def rotl(v, s): v &= 0xFFFFFFFF; return ((v << s) | (v >> (32 - s))) & 0xFFFFFFFF
    msg = bytearray(data) + b"\x80"
    msg += b"\x00" * ((56 - len(msg) % 64) % 64) + struct.pack("<Q", (len(data) * 8) & (2**64 - 1))
    a, b, c, d = 0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476
    for off in range(0, len(msg), 64):
        x = struct.unpack("<16I", msg[off:off + 64])
        aa, bb, cc, dd = a, b, c, d
        for i in (0, 4, 8, 12):
            a = rotl(a + f(b, c, d) + x[i], 3); d = rotl(d + f(a, b, c) + x[i + 1], 7)
            c = rotl(c + f(d, a, b) + x[i + 2], 11); b = rotl(b + f(c, d, a) + x[i + 3], 19)
        for i in (0, 1, 2, 3):
            a = rotl(a + g(b, c, d) + x[i] + 0x5A827999, 3)
            d = rotl(d + g(a, b, c) + x[i + 4] + 0x5A827999, 5)
            c = rotl(c + g(d, a, b) + x[i + 8] + 0x5A827999, 9)
            b = rotl(b + g(c, d, a) + x[i + 12] + 0x5A827999, 13)
        for i in (0, 2, 1, 3):
            a = rotl(a + h(b, c, d) + x[i] + 0x6ED9EBA1, 3)
            d = rotl(d + h(a, b, c) + x[i + 8] + 0x6ED9EBA1, 9)
            c = rotl(c + h(d, a, b) + x[i + 4] + 0x6ED9EBA1, 11)
            b = rotl(b + h(c, d, a) + x[i + 12] + 0x6ED9EBA1, 15)
        a, b, c, d = ((a + aa) & 0xFFFFFFFF, (b + bb) & 0xFFFFFFFF,
                      (c + cc) & 0xFFFFFFFF, (d + dd) & 0xFFFFFFFF)
    return struct.pack("<4I", a, b, c, d)


def md4(data: bytes) -> bytes:
    try:
        return hashlib.new("md4", data).digest()
    except ValueError:
        return _md4_pure(data)


def ed2k_hash(path: str) -> str:
    """eD2K file hash: MD4 of the 9728000-byte part MD4s (one part: its MD4)."""
    part = 9728000
    hashes = []
    with open(path, "rb") as f:
        while chunk := f.read(part):
            hashes.append(md4(chunk))
    if os.path.getsize(path) % part == 0:
        hashes.append(md4(b""))     # a full last part is followed by an empty one
    return (hashes[0] if len(hashes) == 1 else md4(b"".join(hashes))).hex().upper()


def nat_lines(work: str, name: str) -> list[str]:
    # "NAT-T:" from the hub, "NAT-T <path>:" from a path
    return sorted({ln for ln in node_logs(work, name).splitlines() if "NAT-T" in ln})


# ---------------------------------------------------------------------------
# Lifecycle
# ---------------------------------------------------------------------------

def down() -> None:
    servers = [SERVER_CONTAINER, SERVER2_CONTAINER]
    names = [f"natnet-{n}" for n in NODES] + servers
    existing = docker("ps", "-a", "--format", "{{.Names}}", capture=True).stdout.split()
    nodes = [n for n in names if n in existing and n not in servers]
    if nodes:
        # SIGTERM lets the daemons save their files
        docker("stop", "-t", "30", *nodes, check=False, capture=True)
    present = [n for n in names if n in existing]
    if present:
        docker("rm", "-f", *present, check=False, capture=True)
    docker("network", "rm", NETWORK, check=False, capture=True)


def create_network() -> None:
    docker("network", "create", "--ipv6", "--subnet", SUBNET4, "--subnet", SUBNET6,
           NETWORK, capture=True)


def start_server(kind: str, work: str, binary: str, container: str = SERVER_CONTAINER,
                 ip: str = "", ip6: str = "", subdir: str = "server") -> None:
    server_dir = os.path.join(work, "run", subdir)
    common = ["-d", "--name", container, "--network", NETWORK,
              "--ip", ip or SERVER_IP, "--ip6", ip6 or SERVER_IP6]
    if kind == "lugdunum":
        docker("run", *common, "-v", f"{server_dir}:/rig", LUGDUNUM_IMAGE, capture=True)
    elif kind == "ed2k-server":
        docker("run", *common, "-v", f"{binary}:/usr/local/bin/ed2k-server:ro",
               "-v", f"{server_dir}:/srv", "-w", "/srv", RUNTIME_IMAGE,
               "/usr/local/bin/ed2k-server", "--config", "/srv/config.toml", capture=True)
    else:
        docker("run", *common, "-v", f"{binary}:/usr/local/bin/enode:ro",
               "-v", f"{server_dir}:/srv", "-w", "/srv", RUNTIME_IMAGE,
               "/usr/local/bin/enode", "-config", "/srv/enode.yaml", capture=True)


def server_listening(port: int, ip: str = "") -> bool:
    """TCP connect from inside the rig network (the server has no published port)."""
    probe = (f"import socket,sys\ns=socket.socket();s.settimeout(1)\n"
             f"sys.exit(s.connect_ex(('{ip or SERVER_IP}',{port})))")
    return docker("run", "--rm", "--network", NETWORK, "--entrypoint", "python3",
                  DAEMON_IMAGE, "-c", probe, check=False, capture=True).returncode == 0


def server_alive(container: str = SERVER_CONTAINER) -> bool:
    out = docker("inspect", "-f", "{{.State.Running}}", container, check=False, capture=True)
    return out.stdout.strip() == "true"


def use_subnet4(prefix: str) -> None:
    """Move the rig's IPv4 addresses to prefix.0/24 (the IPv6 ones stay)."""
    global SUBNET4, SERVER_IP, SERVER2_IP
    SUBNET4 = f"{prefix}.0/24"
    SERVER_IP = f"{prefix}.10"
    SERVER2_IP = f"{prefix}.20"
    for name, (ipv4, ipv6, user_hash) in NODES.items():
        NODES[name] = (f"{prefix}.{ipv4.rsplit('.', 1)[1]}", ipv6, user_hash)


def probe_udp_flags(node: str, ip: str, port: int):
    """UDP flags from OP_GLOBSERVSTATRES, asked from inside a node container."""
    probe = (
        "import socket,struct,sys\n"
        "s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);s.settimeout(2)\n"
        "for _ in range(5):\n"
        f"    s.sendto(b'\\xe3\\x96'+struct.pack('<I',0x55AA1234),('{ip}',{port}))\n"
        "    try:\n"
        "        d,_=s.recvfrom(4096)\n"
        "    except socket.timeout:\n"
        "        continue\n"
        "    if d[:2]==b'\\xe3\\x97' and len(d)>=2+28:\n"
        "        print(struct.unpack_from('<I',d,2+24)[0]);sys.exit(0)\n"
        "sys.exit(1)\n")
    out = docker("exec", node, "python3", "-c", probe, check=False, capture=True)
    return int(out.stdout.strip()) if out.returncode == 0 else None


def seed_saved_source(work: str, node: str, source: str, source_id: int, server_ip: str,
                      server_port: int) -> str:
    """Write an SLS list naming `source` (a LowID on server_ip) for the node's one download.
    The node must be stopped. Returns the list's path."""
    temp = os.path.join(work, "run", node, "data", "temp")
    part = sorted(f for f in os.listdir(temp) if f.endswith(".part.met"))[0]
    lists = os.path.join(temp, "Source Lists")
    os.makedirs(lists, exist_ok=True)
    # srcExchangeVer >= 3: the hybrid id, its bytes in memory order (MorphXT's ipstr())
    host = ".".join(str((source_id >> s) & 0xFF) for s in (0, 8, 16, 24))
    expiry = time.strftime("%y%m%d%H%M", time.gmtime(time.time() + 30 * 86400))
    line = f"{host}:{TCP_PORT},{expiry},4,{server_ip}:{server_port};h={NODES[source][2].upper()}"
    path = os.path.join(lists, part + ".txtsrc")
    with open(path, "w", encoding="utf-8", newline="") as f:
        f.write("#format: a.b.c.d:port,expirationdate(yymmddhhmm);\r\n#emuleqt-sls: 1\r\n"
                + line + "\r\n")
    return path


def start_servers(kind: str, work: str, binary: str, servers, port: int) -> bool:
    for container, ip, ip6, name, subdir in servers:
        log(f"starting {kind} {name} at {ip}:{port}")
        start_server(kind, work, binary, container, ip, ip6, subdir)
    for container, ip, *_ in servers:
        if not wait_for(container, lambda: server_alive(container) and server_listening(port, ip), 60, 2):
            print(docker("logs", "--tail", "40", container, check=False, capture=True).stdout)
            return False
    return True


def run_test(args, env: dict) -> int:
    kind = args.server
    spec = SERVERS[kind]
    two = bool(spec.get("two_servers"))
    if spec.get("subnet4"):
        use_subnet4(spec["subnet4"])
    work = os.path.abspath(env.get("NATNET_WORK") or os.path.expanduser("~/.cache/emuleqt-natnet"))
    run_dir = os.path.join(work, "run")
    port = spec["port"]

    down()
    build_daemon_image(args.build)
    binary = ""
    if kind == "lugdunum":
        prepare_lugdunum(env, work, args.build)
    elif kind == "ed2k-server":
        binary = prepare_ed2k_server(env, work)
    else:
        binary = prepare_enode(env, work)

    shutil.rmtree(run_dir, ignore_errors=True)
    servers = [(SERVER_CONTAINER, SERVER_IP, SERVER_IP6, "natnet-enode", "server")]
    if two:
        servers.append((SERVER2_CONTAINER, SERVER2_IP, SERVER2_IP6, "natnet-enode-2", "server2"))
    for _, ip, ip6, name, subdir in servers:
        server_dir = os.path.join(run_dir, subdir)
        os.makedirs(server_dir)
        if kind == "lugdunum":
            write_lugdunum_config(server_dir)
        elif kind == "ed2k-server":
            write_ed2k_server_config(env, server_dir)
        else:
            write_enode_config(env, server_dir, kind != "enode-lugdunum", name, ip, ip6)
    server_ips = " ".join(s[1] for s in servers)
    met1 = (SERVER_IP, port, "natnet-enode", {})
    prepare_node(work, "A", args.size_mb, [met1])
    test_file = os.path.join(run_dir, "A", "data", "incoming", TEST_FILE)
    expected_md5 = hashlib.md5(open(test_file, "rb").read()).hexdigest()
    file_hash = ed2k_hash(test_file)

    create_network()
    if not start_servers(kind, work, binary, servers, port):
        return fail(args, work, "server did not come up")

    # A first: its offer must be on the server before B asks for sources
    start_node(work, "A", server_ips)
    state = wait_for("A login", lambda: login_state(work, "A"), 90)
    if state != "LowID":
        return fail(args, work, f"node A logged in as {state or 'nothing'}, need a LowID")
    if not wait_for("A offer", lambda: offered(work, "A"), 60):
        return fail(args, work, "node A did not offer its file")
    link = f"ed2k://|file|{TEST_FILE}|{args.size_mb * 1024 * 1024}|{file_hash}|/"
    log(f"A is a LowID and offers {TEST_FILE}")

    checks = {}
    reject = ""
    if two:
        # Server 1 as B's earlier session left it in server.met: flags and NAT port
        flags = probe_udp_flags("natnet-A", SERVER_IP, port + 4)
        log(f"server 1 UDP flags: {'none' if flags is None else hex(flags)}")
        checks["server 1 advertises 0x40000"] = bool(flags and flags & UDP_FLAG_NAT_RENDEZVOUS)
        if not checks["server 1 advertises 0x40000"]:
            return fail(args, work, "server 1 does not advertise cross-server PR_NAT")
        met1 = (SERVER_IP, port, "natnet-enode", {ST_UDPFLAGS: flags, ST_NAT_PORT: NAT_PORT})
        prepare_node(work, "B", args.size_mb, [(SERVER2_IP, port, "natnet-enode-2", {}), met1])
        reject = f"{SERVER_IP}:{port}"
    else:
        prepare_node(work, "B", args.size_mb, [met1])

    start_node(work, "B", server_ips, reject)
    state = wait_for("B login", lambda: login_state(work, "B"), 90)
    if state != "LowID":
        return fail(args, work, f"node B logged in as {state or 'nothing'}, need a LowID")
    log(f"B is a LowID; adding {link}")
    docker("exec", "-u", "emule", "natnet-B", "emulecored", "--add-link", link, capture=True)

    if two:
        a_id = login_id(work, "A")
        temp = os.path.join(run_dir, "B", "data", "temp")
        if not wait_for("B part file", lambda: any(f.endswith(".part.met") for f in os.listdir(temp)), 30):
            return fail(args, work, "node B did not create the part file")
        # A source kept from an earlier session: restart B with A in its saved source list
        logins = login_count(work, "B")
        docker("stop", "-t", "30", "natnet-B", capture=True)
        path = seed_saved_source(work, "B", "A", a_id, SERVER_IP, port)
        log(f"seeded {os.path.basename(path)}: A = LowID {a_id} on server 1; restarting B")
        docker("start", "natnet-B", capture=True)
        if not wait_for("B login", lambda: login_count(work, "B") > logins, 90):
            return fail(args, work, "node B did not log in again")
    t0 = time.monotonic()

    target = os.path.join(run_dir, "B", "data", "incoming", TEST_FILE)
    failed_re = re.compile(r"NAT-T: #\d+ \S+ failed: (.*)")

    def done():
        if os.path.isfile(target):
            return "complete"
        m = failed_re.search("\n".join(nat_lines(work, "B")))
        return f"rendezvous failed: {m.group(1)}" if m else None

    outcome = wait_for("transfer", done, args.timeout, 2)
    elapsed = time.monotonic() - t0
    for name in NODES:
        log(f"node {name} NAT-T log:")
        for line in nat_lines(work, name):
            print("    " + line)
    if outcome != "complete":
        return fail(args, work, outcome or f"no transfer within {args.timeout}s")

    got_md5 = wait_for("md5", lambda: hashlib.md5(open(target, "rb").read()).hexdigest()
                       if os.path.getsize(target) == args.size_mb * 1024 * 1024 else None, 10)
    b_lines = "\n".join(nat_lines(work, "B"))
    all_lines = b_lines + "\n".join(nat_lines(work, "A"))
    checks.update({
        f"rendezvous via {spec['path']}": f"via {spec['path']} for" in b_lines,
        "IPv6 dial": bool(re.search(r"NAT-T: #\d+ dialing \[", all_lines)),
        "MD5 match": got_md5 == expected_md5,
    })
    if two:
        checks["B logged in to server 2 only"] = (
            "Connected to natnet-enode-2 " in node_logs(work, "B")
            and "Connected to natnet-enode " not in node_logs(work, "B"))
        checks["sweep of 2 servers"] = bool(re.search(r"sweeping 2 server\(s\)", b_lines))
        checks["server 2 asked first, FAILED"] = bool(
            re.search(r"PR_NAT FAILED reason 1 from \S+", b_lines))
        # The NAT port came from server.met: without it B would use port + 4
        checks["paired via server 1's saved NAT port"] = f"paired via {SERVER_IP}:{NAT_PORT}" in b_lines
    if spec.get("odd_ids"):
        ids = {n: login_id(work, n) for n in NODES}
        log(f"LowIDs: {ids}")
        checks["odd LowIDs (login bit 0x1000)"] = all(i is not None and i & 1 for i in ids.values())
    for what, ok in checks.items():
        log(f"{'PASS' if ok else 'FAIL'}  {what}")
    if not all(checks.values()):
        return fail(args, work, "checks failed")
    log(f"PASS: {args.size_mb} MiB LowID-to-LowID via {kind} in {elapsed:.1f}s, md5 {got_md5}")
    finish(args, work)
    return 0


def fail(args, work: str, why: str) -> int:
    log(f"FAIL: {why}")
    finish(args, work)
    return 1


def finish(args, work: str) -> None:
    log(f"logs and captures: {os.path.join(work, 'run')}")
    if args.keep:
        log("--keep: rig left running; tear down with --down")
    else:
        down()


def follow_logs(env: dict) -> None:
    work = os.path.abspath(env.get("NATNET_WORK") or os.path.expanduser("~/.cache/emuleqt-natnet"))
    files = [os.path.join(work, "run", n, "config", "emulecored.log") for n in NODES]
    existing = docker("ps", "--format", "{{.Names}}", capture=True).stdout.split()
    procs = [subprocess.Popen(["docker", "logs", "-f", "--tail", "20", c])
             for c in (SERVER_CONTAINER, SERVER2_CONTAINER) if c in existing]
    procs.append(subprocess.Popen(["tail", "-F", *files]))
    try:
        for p in procs:
            p.wait()
    except KeyboardInterrupt:
        for p in procs:
            p.terminate()


def lugdunum_console(command: str) -> None:
    docker("exec", SERVER_CONTAINER, "sh", "-c", f"echo '{command}' > /tmp/console")
    time.sleep(1.5)
    print(docker("logs", "--since", "3s", SERVER_CONTAINER, check=False, capture=True).stdout)


def main() -> int:
    parser = argparse.ArgumentParser(description="LowID-to-LowID NAT rendezvous rig")
    parser.add_argument("--server", choices=sorted(SERVERS), default="lugdunum",
                        help="introducer to test (default: lugdunum)")
    parser.add_argument("--build", action="store_true",
                        help="rebuild the daemon image from this tree (and the Lugdunum image)")
    parser.add_argument("--keep", action="store_true", help="leave the rig running afterwards")
    parser.add_argument("--timeout", type=int, default=180,
                        help="seconds for the rendezvous and transfer (default: 180)")
    parser.add_argument("--size-mb", type=int, default=25, help="test file size in MiB (default: 25)")
    parser.add_argument("--env-file", default=DEFAULT_ENV_FILE,
                        help="server paths (default: docker/natt/natnet.env)")
    parser.add_argument("--down", action="store_true", help="tear the rig down and exit")
    parser.add_argument("--logs", action="store_true", help="follow server and node logs")
    parser.add_argument("--console", metavar="CMD", help="send a Lugdunum console command")
    args = parser.parse_args()

    if not shutil.which("docker"):
        die("docker not found")
    env = load_env(args.env_file)
    if args.down:
        down()
        return 0
    if args.logs:
        follow_logs(env)
        return 0
    if args.console:
        lugdunum_console(args.console)
        return 0
    try:
        return run_test(args, env)
    except subprocess.CalledProcessError as e:
        die(f"command failed ({e.returncode}): {' '.join(e.cmd[:6])} ...")
        return 2


if __name__ == "__main__":
    sys.exit(main())
