#!/usr/bin/env python3
"""
kadnet.py — Orchestrate a Kademlia LAN test network with Docker.

Generates a nodes.dat seed file per node, a docker-compose YAML, and manages
the lifecycle of N emulecored containers on a private bridge network.

Usage:
    python3 docker/kad/kadnet.py                    # 100 nodes, build + start
    python3 docker/kad/kadnet.py --nodes 10         # 10 nodes
    python3 docker/kad/kadnet.py --nodes 50 --build # rebuild image first
    python3 docker/kad/kadnet.py --netmon           # with the network monitor
    python3 docker/kad/kadnet.py --raise-arp-limit  # lift the host ARP table limit first
    python3 docker/kad/kadnet.py --down             # tear down
    python3 docker/kad/kadnet.py --logs             # follow logs
    python3 docker/kad/kadnet.py --firewall 3,4     # nodes 3 and 4 behind a NAT
    python3 docker/kad/kadnet.py --nat-test         # Kad buddy NAT rendezvous test, 20 nodes

The entrypoint is bind-mounted and every per-node option is a container variable,
so only daemon code changes need --build.
"""

import argparse
import hashlib
import re
import time
import ipaddress
import os
import random
import struct
import subprocess
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, "..", ".."))
COMPOSE_FILE = os.path.join(SCRIPT_DIR, "docker-compose.kadnet.yml")
SEED_DIR = os.path.join(SCRIPT_DIR, "seed")
# Contacts per node's nodes.dat. Each node gets its own set: a node that sits
# in everyone's routing table from the start can never pass the UDP firewall
# check, because testers refuse requesters they already know.
SEEDS_PER_NODE = 6
# Shared with docker/httpcache: both rigs run the same emulecored image, so the
# Dockerfile lives one level up rather than inside either rig.
DOCKERFILE = os.path.join(PROJECT_ROOT, "docker", "daemon.Dockerfile")
IMAGE_NAME = "emuleqt-daemon:latest"
SUBNET6 = "2001:db8:20::/64"
CONFIG_DIR = "/root/eMuleQt/Config"
# --nat-test: the second-to-last node shares, the last one downloads. 20, not fewer: in a
# smaller LAN every node knows every other, each UDP firewall check ends "cancelled",
# and an unverified node never serves as a buddy (10 nodes: none did).
NAT_TEST_NODES = 20
NAT_TEST_FILE = "kadnat-test.bin"
# ARP table limits for --raise-arp-limit. The table is one per Docker host,
# shared by all containers; N nodes can need N*(N-1) entries, the default
# hard limit is 1024.
ARP_GC_THRESH2 = 16384
ARP_GC_THRESH3 = 32768

# nodes.dat v2 constants
NODES_FILE_VERSION2 = 0x00000002
KADEMLIA_VERSION = 0x0A  # KADEMLIA_VERSION8_49b


def ip_to_host_order_uint32(ip_str: str) -> int:
    """Convert an IP string to the host-order uint32 that emulecored stores in nodes.dat.

    On little-endian (x86), this equals int(IPv4Address(ip)) which is the
    big-endian numeric value — because eMule stores ntohl(inet_addr(ip))
    which on LE produces the same numeric value as the standard integer
    representation of an IPv4 address.
    """
    return int(ipaddress.IPv4Address(ip_str))


def generate_kad_id(node_index: int) -> bytes:
    """Generate a deterministic 128-bit KadID for a node.

    Uses SHA-256 of a fixed seed + node index, truncated to 16 bytes.
    Deterministic so that re-running kadnet.py produces the same IDs.
    """
    h = hashlib.sha256(f"kadnet-node-{node_index}".encode()).digest()
    return h[:16]


def generate_user_hash(node_index: int) -> str:
    """Generate a deterministic 16-byte eMule user hash for a node.

    Returns a 32-char lowercase hex string.  Bytes 5 and 14 are set to the
    eMule client markers (0x0E and 0x6F) required by servers and anti-leecher
    checks.
    """
    h = bytearray(hashlib.sha256(f"kadnet-userhash-{node_index}".encode()).digest()[:16])
    h[5] = 0x0E   # eMule client marker
    h[14] = 0x6F  # eMule magic value
    return h.hex()


def generate_preferences_kad_dat(kad_id: bytes, ip_str: str, path: str):
    """Write a preferencesKad.dat file matching the daemon's binary format.

    Format: uint32 IP (LE) | uint16 reserved (LE) | 16-byte KadID | uint8 tagCount
    """
    ip_val = ip_to_host_order_uint32(ip_str)
    with open(path, "wb") as f:
        f.write(struct.pack("<I", ip_val))   # IP
        f.write(struct.pack("<H", 0))        # reserved
        f.write(kad_id)                      # 16-byte KadID
        f.write(struct.pack("<B", 0))        # tag count


def generate_nodes_dat(seed_ips: list[str], kad_ids: list[bytes],
                       tcp_port: int, udp_port: int, path: str):
    """Generate a nodes.dat v2 file with seed contacts using real KadIDs."""
    num_contacts = len(seed_ips)
    with open(path, "wb") as f:
        # Header
        f.write(struct.pack("<II", NODES_FILE_VERSION2, num_contacts))
        for ip_str, kad_id in zip(seed_ips, kad_ids):
            ip_val = ip_to_host_order_uint32(ip_str)
            f.write(kad_id)
            f.write(struct.pack("<I", ip_val))      # IP (host-order uint32)
            f.write(struct.pack("<H", udp_port))     # UDP port
            f.write(struct.pack("<H", tcp_port))     # TCP port
            f.write(struct.pack("<B", KADEMLIA_VERSION))  # version
            f.write(struct.pack("<II", 0, 0))        # KadUDPKey (value=0, ip=0)
            f.write(struct.pack("<B", 0))            # ipVerified = false


def pick_seeds(index: int, num_nodes: int) -> list[int]:
    """Pick the node indices (0-based) seeded into node `index`'s nodes.dat.

    The predecessor is always included so the seed graph is a connected ring;
    the rest are a deterministic random sample of the other nodes.
    """
    others = [i for i in range(num_nodes) if i != index]
    count = min(SEEDS_PER_NODE, len(others))
    prev = (index - 1) % num_nodes
    picked = [prev] if others else []
    pool = [i for i in others if i != prev]
    rng = random.Random(f"emuleqt-kadnet-seeds-{index}")
    picked += rng.sample(pool, count - len(picked))
    return picked


def generate_compose(
    num_nodes: int,
    subnet: str,
    base_ip_offset: int,
    tcp_port: int,
    udp_port: int,
    ipc_port: int,
    ipc_token: str,
    path: str,
    netmon: bool = False,
    ipv6: bool = False,
    node_env: dict | None = None,
    node_volumes: dict | None = None,
):
    """Generate docker-compose.kadnet.yml with N node services.

    node_env / node_volumes: extra variables and mounts per 1-based node index.
    """
    network = ipaddress.IPv4Network(subnet)
    gateway = str(list(network.hosts())[0])  # .1
    network6 = ipaddress.IPv6Network(SUBNET6)
    node_env = node_env or {}
    node_volumes = node_volumes or {}

    services = {}
    volumes = {}
    for i in range(1, num_nodes + 1):
        node_ip = str(network.network_address + base_ip_offset + i - 1)
        service_name = f"node-{i}"
        volume_name = f"node-{i}-config"

        host_ipc_port = ipc_port + i - 1
        user_hash = generate_user_hash(i)
        services[service_name] = {
            "image": IMAGE_NAME,
            "container_name": f"kadnet-node-{i}",
            "environment": {
                "NODE_NICK": f"Node-{i}",
                "TCP_PORT": str(tcp_port),
                "UDP_PORT": str(udp_port),
                "IPC_PORT": str(ipc_port),
                "IPC_TOKEN": ipc_token,
                "USER_HASH": user_hash,
            },
            "ports": [f"{host_ipc_port}:{ipc_port}"],
            "volumes": [
                "./entrypoint.sh:/entrypoint-kad.sh:ro",
                f"{volume_name}:{CONFIG_DIR}",
                f"./seed/node-{i}-nodes.dat:/seed/nodes.dat:ro",
                f"./seed/node-{i}-preferencesKad.dat:/seed/preferencesKad.dat:ro",
                f"./crashes/node-{i}:{CONFIG_DIR}/crashes",
                *node_volumes.get(i, []),
            ],
            "networks": {
                "kadnet": {"ipv4_address": node_ip},
            },
            "restart": "unless-stopped",
        }
        extra = node_env.get(i, {})
        services[service_name]["environment"].update(extra)
        if extra.get("FIREWALL4") == "1" or extra.get("FIREWALL6") == "1":
            services[service_name]["cap_add"] = ["NET_ADMIN"]
        if ipv6:
            services[service_name]["networks"]["kadnet"]["ipv6_address"] = node_ip6(
                network6, base_ip_offset, i)
        if netmon:
            services[service_name]["environment"]["NETMON"] = "1"
            services[service_name]["volumes"].append(f"./netmon/node-{i}:/netmon")
        volumes[volume_name] = {"driver": "local"}

    # Write YAML manually to avoid pyyaml dependency
    with open(path, "w") as f:
        f.write("# Auto-generated by kadnet.py — do not edit\n\n")

        # Networks
        f.write("networks:\n")
        f.write("  kadnet:\n")
        f.write("    driver: bridge\n")
        if ipv6:
            f.write("    enable_ipv6: true\n")
        f.write("    ipam:\n")
        f.write("      config:\n")
        f.write(f"        - subnet: {subnet}\n")
        f.write(f"          gateway: {gateway}\n")
        if ipv6:
            f.write(f"        - subnet: {SUBNET6}\n")
        f.write("\n")

        # Services
        f.write("services:\n")
        for name, svc in services.items():
            f.write(f"  {name}:\n")
            f.write(f"    image: {svc['image']}\n")
            f.write(f"    container_name: {svc['container_name']}\n")
            f.write(f"    restart: {svc['restart']}\n")
            f.write("    entrypoint: [\"/entrypoint-kad.sh\"]\n")
            if svc.get("cap_add"):
                f.write("    cap_add:\n")
                for cap in svc["cap_add"]:
                    f.write(f"      - {cap}\n")
            f.write(f"    environment:\n")
            for k, v in svc["environment"].items():
                f.write(f"      {k}: \"{v}\"\n")
            f.write(f"    ports:\n")
            for port in svc["ports"]:
                f.write(f"      - \"{port}\"\n")
            f.write(f"    volumes:\n")
            for vol in svc["volumes"]:
                f.write(f"      - {vol}\n")
            f.write(f"    networks:\n")
            f.write(f"      kadnet:\n")
            for nk, nv in svc["networks"]["kadnet"].items():
                f.write(f"        {nk}: {nv}\n")
        f.write("\n")

        # Volumes
        f.write("volumes:\n")
        for name in volumes:
            f.write(f"  {name}:\n")
            f.write(f"    driver: local\n")

    print(f"Generated {path} with {num_nodes} nodes on {subnet}")


def node_ip6(network6: ipaddress.IPv6Network, base_ip_offset: int, index: int) -> str:
    """IPv6 of 1-based node `index`; same host part as its IPv4."""
    return str(network6.network_address + base_ip_offset + index - 1)


def node_ips(num_nodes: int, subnet: str, base_ip_offset: int) -> list[str]:
    """Compute the list of node IP addresses."""
    network = ipaddress.IPv4Network(subnet)
    return [
        str(network.network_address + base_ip_offset + i)
        for i in range(num_nodes)
    ]


def raise_arp_limit():
    """Raise the Docker host's ARP table limits (until the Docker VM restarts).

    Not settable from a normal container, hence a privileged one on the host
    network. Without it, first packets to a new peer are dropped once the
    table is full: stalled TCP connects, lost Kad packets.
    """
    cmd = [
        "docker", "run", "--rm", "--privileged", "--network", "host",
        "--entrypoint", "sysctl", IMAGE_NAME, "-w",
        f"net.ipv4.neigh.default.gc_thresh2={ARP_GC_THRESH2}",
        f"net.ipv4.neigh.default.gc_thresh3={ARP_GC_THRESH3}",
    ]
    print(f"$ {' '.join(cmd)}")
    subprocess.run(cmd, check=True)


def docker_compose(*args, capture=False):
    """Run docker compose with the generated file."""
    cmd = ["docker", "compose", "-f", COMPOSE_FILE, *args]
    if not capture:
        print(f"$ {' '.join(cmd)}")
    return subprocess.run(cmd, cwd=SCRIPT_DIR, check=True, text=True,
                          capture_output=capture)


def parse_node_list(text: str) -> list[int]:
    return [int(x) for x in text.split(",") if x.strip()] if text else []


def build_image():
    print(f"Building {IMAGE_NAME} from {DOCKERFILE}...")
    subprocess.run(["docker", "build", "-f", DOCKERFILE, "-t", IMAGE_NAME, PROJECT_ROOT],
                   check=True)


def start_network(args, node_env: dict, node_volumes: dict):
    """Seed files, compose file, `up -d`."""
    if args.nodes > 256:
        print(
            f"WARNING: {args.nodes} nodes exceeds Kademlia LAN mode limit of 256. "
            "LAN mode will NOT activate.",
            file=sys.stderr,
        )

    # Compute IPs and KadIDs
    ips = node_ips(args.nodes, args.subnet, args.base_ip_offset)
    kad_ids = [generate_kad_id(i + 1) for i in range(args.nodes)]

    # Create per-node crash dump directories on host for bind mounts
    crashes_dir = os.path.join(SCRIPT_DIR, "crashes")
    for i in range(1, args.nodes + 1):
        os.makedirs(os.path.join(crashes_dir, f"node-{i}"), exist_ok=True)

    # Per-node network monitor output, only when asked for
    if args.netmon:
        netmon_dir = os.path.join(SCRIPT_DIR, "netmon")
        for i in range(1, args.nodes + 1):
            os.makedirs(os.path.join(netmon_dir, f"node-{i}"), exist_ok=True)

    # Generate per-node preferencesKad.dat files (so each daemon starts
    # with a known KadID that matches the entries in nodes.dat)
    os.makedirs(SEED_DIR, exist_ok=True)
    for i in range(args.nodes):
        pref_path = os.path.join(SEED_DIR, f"node-{i + 1}-preferencesKad.dat")
        generate_preferences_kad_dat(kad_ids[i], ips[i], pref_path)
    print(f"Generated {args.nodes} preferencesKad.dat files in {SEED_DIR}")

    # Generate a nodes.dat per node, each with its own handful of seeds
    for i in range(args.nodes):
        seeds = pick_seeds(i, args.nodes)
        generate_nodes_dat([ips[s] for s in seeds], [kad_ids[s] for s in seeds],
                           args.tcp_port, args.udp_port,
                           os.path.join(SEED_DIR, f"node-{i + 1}-nodes.dat"))
    print(f"Generated {args.nodes} nodes.dat files with "
          f"{min(SEEDS_PER_NODE, args.nodes - 1)} seed contacts each")

    for i in parse_node_list(args.firewall):
        node_env.setdefault(i, {})["FIREWALL4"] = "1"

    generate_compose(
        args.nodes,
        args.subnet,
        args.base_ip_offset,
        args.tcp_port,
        args.udp_port,
        args.ipc_port,
        args.ipc_token,
        COMPOSE_FILE,
        args.netmon,
        args.ipv6,
        node_env,
        node_volumes,
    )

    if args.raise_arp_limit:
        raise_arp_limit()
    elif args.nodes * (args.nodes - 1) > 1024:
        print(
            f"WARNING: {args.nodes} nodes can overflow the Docker host's ARP table "
            "(1024 entries by default) — connects to new peers will stall. "
            "Use --raise-arp-limit.",
            file=sys.stderr,
        )

    docker_compose("up", "-d")


# ---------------------------------------------------------------------------
# --nat-test: Kad buddy NAT rendezvous between two firewalled nodes
# ---------------------------------------------------------------------------

def stamp(msg: str):
    print(f"[kadnat {time.strftime('%H:%M:%S')}] {msg}", flush=True)


def node_file(index: int, name: str) -> str:
    """A file from a node's config dir, '' while missing."""
    r = subprocess.run(["docker", "exec", f"kadnet-node-{index}", "cat", f"{CONFIG_DIR}/{name}"],
                       capture_output=True, text=True, errors="replace")
    return r.stdout if r.returncode == 0 else ""


def node_log(index: int) -> str:
    return node_file(index, "emulecored.log") + node_file(index, "emulecored_Verbose.log")


def wait_until(what: str, predicate, timeout: float, interval: float = 5.0):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        result = predicate()
        if result:
            return result
        time.sleep(interval)
    stamp(f"timed out waiting for {what} ({timeout:.0f}s)")
    return None


def md4(data: bytes) -> bytes:
    try:
        return hashlib.new("md4", data).digest()
    except ValueError:
        sys.path.insert(0, os.path.join(PROJECT_ROOT, "docker", "natt"))
        from natnet import _md4_pure   # OpenSSL 3 without the legacy provider
        return _md4_pure(data)


def ed2k_hash(path: str) -> str:
    """eD2K file hash: MD4 of the 9728000-byte part MD4s (one part: its MD4)."""
    part = 9728000
    hashes = []
    with open(path, "rb") as f:
        while chunk := f.read(part):
            hashes.append(md4(chunk))
    if os.path.getsize(path) % part == 0:
        hashes.append(md4(b""))
    return (hashes[0] if len(hashes) == 1 else md4(b"".join(hashes))).hex().upper()


def nat_test(args) -> int:
    """Node N-1 shares behind a NAT and a v6 firewall, node N downloads behind a NAT.

    N's direct IPv6 dial to the source is refused, so it falls back to the source's
    Kad buddy, which introduces the two (docs/protocol/natt-rendezvous.md).
    """
    src, dst = args.nodes - 1, args.nodes
    network6 = ipaddress.IPv6Network(SUBNET6)
    share_dir = os.path.join(SEED_DIR, "kadnat-share")
    os.makedirs(share_dir, exist_ok=True)
    test_file = os.path.join(share_dir, NAT_TEST_FILE)
    with open(test_file, "wb") as f:
        for _ in range(args.size_mb):
            f.write(os.urandom(1024 * 1024))
    expected_md5 = hashlib.md5(open(test_file, "rb").read()).hexdigest()
    link = f"ed2k://|file|{NAT_TEST_FILE}|{args.size_mb * 1024 * 1024}|{ed2k_hash(test_file)}|/"

    common = {"FIREWALL4": "1", "LOG_TO_DISK": "1", "UNLIMITED_BANDWIDTH": "1",
              "NAT_TRAVERSAL": "1"}
    node_env = {
        src: {**common, "FIREWALL6": "1",
              "PUBLIC_IPV6": node_ip6(network6, args.base_ip_offset, src)},
        dst: {**common, "PUBLIC_IPV6": node_ip6(network6, args.base_ip_offset, dst)},
    }
    node_volumes = {src: [f"./seed/kadnat-share/{NAT_TEST_FILE}:/root/incoming/{NAT_TEST_FILE}:ro"]}

    if os.path.exists(COMPOSE_FILE):
        docker_compose("down", "-v", "--remove-orphans")
    start_network(args, node_env, node_volumes)
    t_start = time.monotonic()
    stamp(f"{args.nodes} nodes up; node-{src} shares {NAT_TEST_FILE}, node-{dst} will download it")
    stamp("waiting for the source's Kad buddy (first search ~5 min after start)")

    def buddy_published():
        kad = node_file(src, "emulecored_Kad.log")
        at = kad.rfind("Kad: buddy set")
        if at < 0:
            return None
        return "yes" if re.search(r"PUBLISH_SOURCE_REQ .*srcType=[35]", kad[at:]) else None

    if not wait_until("buddy + source publish", buddy_published, args.buddy_timeout):
        return nat_fail(args, "the source never published with a buddy")
    kad = node_file(src, "emulecored_Kad.log")
    stamp(re.findall(r"Kad: buddy set.*", kad)[-1].strip())
    stamp(f"source published with its buddy after {time.monotonic() - t_start:.0f}s; adding {link}")

    t0 = time.monotonic()
    subprocess.run(["docker", "exec", f"kadnet-node-{dst}", "emulecored", "--add-link", link],
                   check=True, capture_output=True)
    target = f"/root/incoming/{NAT_TEST_FILE}"
    failed_re = re.compile(r"NAT-T: #\d+ \S+ failed: (.*)")

    def done():
        r = subprocess.run(["docker", "exec", f"kadnet-node-{dst}", "test", "-f", target])
        if r.returncode == 0:
            return "complete"
        m = failed_re.search(node_log(dst))
        return f"rendezvous failed: {m.group(1)}" if m else None

    outcome = wait_until("transfer", done, args.timeout)
    elapsed = time.monotonic() - t0
    logs = {i: node_log(i) for i in (src, dst)}
    for i in (src, dst):
        stamp(f"node-{i} NAT-T log:")
        for line in sorted({ln for ln in logs[i].splitlines()
                            if "NAT-T:" in ln or "IPv6 dial to LowID" in ln}):
            print("    " + line)
    if outcome != "complete":
        return nat_fail(args, outcome or f"no transfer within {args.timeout}s")

    md5 = subprocess.run(["docker", "exec", f"kadnet-node-{dst}", "md5sum", target],
                         capture_output=True, text=True).stdout.split()[:1]
    checks = {
        "IPv6 dial refused, LowID fallback": "IPv6 dial to LowID source" in logs[dst],
        "rendezvous via Kad buddy": "via Kad buddy for" in logs[dst],
        "IPv6 handoff dial": bool(re.search(r"NAT-T: #\d+ dialing \[", logs[src] + logs[dst])),
        "MD5 match": md5 == [expected_md5],
    }
    for what, ok in checks.items():
        stamp(f"{'PASS' if ok else 'FAIL'}  {what}")
    if not all(checks.values()):
        return nat_fail(args, "checks failed")
    stamp(f"PASS: {args.size_mb} MiB firewalled-to-firewalled via Kad buddy in {elapsed:.1f}s")
    nat_finish(args)
    return 0


def nat_fail(args, why: str) -> int:
    stamp(f"FAIL: {why}")
    nat_finish(args)
    return 1


def nat_finish(args):
    if args.keep:
        stamp("--keep: rig left running; logs in each node's " + CONFIG_DIR
              + "; tear down with --down")
    else:
        docker_compose("down", "-v")


def main():
    parser = argparse.ArgumentParser(
        description="Manage an eMuleQt Kademlia LAN test network"
    )
    parser.add_argument(
        "--nodes", type=int, default=None,
        help=f"Number of nodes (default: 100, {NAT_TEST_NODES} with --nat-test)"
    )
    parser.add_argument(
        "--subnet", default="172.20.0.0/16", help="Docker subnet (default: 172.20.0.0/16)"
    )
    parser.add_argument(
        "--base-ip-offset",
        type=int,
        default=10,
        help="Offset from network address for first node IP (default: 10)",
    )
    parser.add_argument(
        "--tcp-port", type=int, default=5662, help="TCP port (default: 5662)"
    )
    parser.add_argument(
        "--udp-port", type=int, default=5672, help="UDP port (default: 5672)"
    )
    parser.add_argument(
        "--ipc-port", type=int, default=4712, help="IPC base port mapped to host (default: 4712)"
    )
    parser.add_argument(
        "--ipc-token", default="kadnet-test-token", help="Shared IPC auth token (default: kadnet-test-token)"
    )
    parser.add_argument(
        "--build", action="store_true", help="Rebuild Docker image before starting"
    )
    parser.add_argument(
        "--netmon", action="store_true",
        help="Start the network monitor in every node (output in docker/kad/netmon/)"
    )
    parser.add_argument(
        "--raise-arp-limit", action="store_true",
        help="Raise the Docker host's ARP table limit first (privileged container; "
             "needed above ~30 nodes, lasts until Docker restarts)"
    )
    parser.add_argument(
        "--ipv6", action="store_true",
        help=f"Give the network an IPv6 subnet ({SUBNET6}); on with --nat-test"
    )
    parser.add_argument(
        "--firewall", default="",
        help="Comma list of node numbers behind a NAT: inbound IPv4 TCP/UDP only as replies"
    )
    parser.add_argument(
        "--nat-test", action="store_true",
        help="Run the Kad buddy NAT rendezvous test: the last two nodes are firewalled, "
             "the second-to-last shares a file, the last downloads it"
    )
    parser.add_argument("--size-mb", type=int, default=25,
                        help="--nat-test file size in MiB (default: 25)")
    parser.add_argument("--buddy-timeout", type=int, default=900,
                        help="--nat-test seconds to wait for the source's buddy (default: 900)")
    parser.add_argument("--timeout", type=int, default=300,
                        help="--nat-test seconds for rendezvous and transfer (default: 300)")
    parser.add_argument("--keep", action="store_true",
                        help="--nat-test: leave the rig running afterwards")
    parser.add_argument(
        "--down", action="store_true", help="Tear down the network and remove volumes"
    )
    parser.add_argument(
        "--logs", action="store_true", help="Follow container logs"
    )
    args = parser.parse_args()

    if args.down:
        docker_compose("down", "-v")
        return 0

    if args.logs:
        docker_compose("logs", "-f")
        return 0

    if args.nodes is None:
        args.nodes = NAT_TEST_NODES if args.nat_test else 100
    if args.build:
        build_image()

    if args.nat_test:
        if args.nodes < 4:
            print("--nat-test needs at least 4 nodes (20 for a buddy to appear)", file=sys.stderr)
            return 2
        args.ipv6 = True
        return nat_test(args)

    start_network(args, {}, {})
    print(f"\nKademlia LAN network started with {args.nodes} nodes.")
    print(f"  Subnet:    {args.subnet}" + (f" + {SUBNET6}" if args.ipv6 else ""))
    print(f"  Ports:     TCP={args.tcp_port}  UDP={args.udp_port}")
    print(f"  IPC ports: {args.ipc_port}–{args.ipc_port + args.nodes - 1} (token: {args.ipc_token})")
    if args.firewall:
        print(f"  Firewalled: nodes {args.firewall}")
    print(f"\nConnect GUI to a node:")
    print(f"  ./docker/kad/gui.sh        # connect to node-1 (port {args.ipc_port})")
    print(f"  ./docker/kad/gui.sh 3      # connect to node-3 (port {args.ipc_port + 2})")
    print(f"\nUseful commands:")
    print(f"  Logs:      python3 docker/kad/kadnet.py --logs")
    print(f"  Tear down: python3 docker/kad/kadnet.py --down")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        print("\nInterrupted.")
        sys.exit(0)
    except subprocess.CalledProcessError as e:
        print(f"command failed ({e.returncode}): {' '.join(map(str, e.cmd))}", file=sys.stderr)
        sys.exit(1)
