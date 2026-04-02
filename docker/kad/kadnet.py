#!/usr/bin/env python3
"""
kadnet.py — Orchestrate a Kademlia LAN test network with Docker.

Generates a nodes.dat seed file, a docker-compose YAML, and manages
the lifecycle of N emulecored containers on a private bridge network.

Usage:
    python3 docker/kad/kadnet.py                    # 100 nodes, build + start
    python3 docker/kad/kadnet.py --nodes 10         # 10 nodes
    python3 docker/kad/kadnet.py --nodes 50 --build # rebuild image first
    python3 docker/kad/kadnet.py --down             # tear down
    python3 docker/kad/kadnet.py --logs             # follow logs
"""

import argparse
import hashlib
import ipaddress
import os
import struct
import subprocess
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, "..", ".."))
COMPOSE_FILE = os.path.join(SCRIPT_DIR, "docker-compose.kadnet.yml")
SEED_DIR = os.path.join(SCRIPT_DIR, "seed")
NODES_DAT_FILE = os.path.join(SEED_DIR, "nodes.dat")
DOCKERFILE = os.path.join(SCRIPT_DIR, "Dockerfile")
IMAGE_NAME = "emuleqt-daemon:latest"

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
    print(f"Generated {path} with {num_contacts} seed contacts")


def generate_compose(
    num_nodes: int,
    subnet: str,
    base_ip_offset: int,
    tcp_port: int,
    udp_port: int,
    ipc_port: int,
    ipc_token: str,
    path: str,
):
    """Generate docker-compose.kadnet.yml with N node services."""
    network = ipaddress.IPv4Network(subnet)
    gateway = str(list(network.hosts())[0])  # .1

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
                f"{volume_name}:/root/.config/eMule/Core",
                "./seed/nodes.dat:/seed/nodes.dat:ro",
                f"./seed/node-{i}-preferencesKad.dat:/seed/preferencesKad.dat:ro",
                f"./crashes/node-{i}:/root/.config/eMule/Core/crashes",
            ],
            "networks": {
                "kadnet": {"ipv4_address": node_ip},
            },
            "restart": "unless-stopped",
        }
        volumes[volume_name] = {"driver": "local"}

    # Write YAML manually to avoid pyyaml dependency
    with open(path, "w") as f:
        f.write("# Auto-generated by kadnet.py — do not edit\n\n")

        # Networks
        f.write("networks:\n")
        f.write("  kadnet:\n")
        f.write("    driver: bridge\n")
        f.write("    ipam:\n")
        f.write("      config:\n")
        f.write(f"        - subnet: {subnet}\n")
        f.write(f"          gateway: {gateway}\n")
        f.write("\n")

        # Services
        f.write("services:\n")
        for name, svc in services.items():
            f.write(f"  {name}:\n")
            f.write(f"    image: {svc['image']}\n")
            f.write(f"    container_name: {svc['container_name']}\n")
            f.write(f"    restart: {svc['restart']}\n")
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


def node_ips(num_nodes: int, subnet: str, base_ip_offset: int) -> list[str]:
    """Compute the list of node IP addresses."""
    network = ipaddress.IPv4Network(subnet)
    return [
        str(network.network_address + base_ip_offset + i)
        for i in range(num_nodes)
    ]


def docker_compose(*args):
    """Run docker compose with the generated file."""
    cmd = ["docker", "compose", "-f", COMPOSE_FILE, *args]
    print(f"$ {' '.join(cmd)}")
    subprocess.run(cmd, cwd=SCRIPT_DIR, check=True)


def main():
    parser = argparse.ArgumentParser(
        description="Manage an eMuleQt Kademlia LAN test network"
    )
    parser.add_argument(
        "--nodes", type=int, default=100, help="Number of nodes (default: 100)"
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
        "--down", action="store_true", help="Tear down the network and remove volumes"
    )
    parser.add_argument(
        "--logs", action="store_true", help="Follow container logs"
    )
    args = parser.parse_args()

    if args.down:
        docker_compose("down", "-v")
        return

    if args.logs:
        docker_compose("logs", "-f")
        return

    if args.nodes > 256:
        print(
            f"WARNING: {args.nodes} nodes exceeds Kademlia LAN mode limit of 256. "
            "LAN mode will NOT activate.",
            file=sys.stderr,
        )

    # Build image if requested
    if args.build:
        print(f"Building {IMAGE_NAME} from {DOCKERFILE}...")
        subprocess.run(
            [
                "docker", "build",
                "-f", DOCKERFILE,
                "-t", IMAGE_NAME,
                PROJECT_ROOT,
            ],
            check=True,
        )

    # Compute IPs and KadIDs
    ips = node_ips(args.nodes, args.subnet, args.base_ip_offset)
    kad_ids = [generate_kad_id(i + 1) for i in range(args.nodes)]

    # Create per-node crash dump directories on host for bind mounts
    crashes_dir = os.path.join(SCRIPT_DIR, "crashes")
    for i in range(1, args.nodes + 1):
        os.makedirs(os.path.join(crashes_dir, f"node-{i}"), exist_ok=True)

    # Generate per-node preferencesKad.dat files (so each daemon starts
    # with a known KadID that matches the entries in nodes.dat)
    os.makedirs(SEED_DIR, exist_ok=True)
    for i in range(args.nodes):
        pref_path = os.path.join(SEED_DIR, f"node-{i + 1}-preferencesKad.dat")
        generate_preferences_kad_dat(kad_ids[i], ips[i], pref_path)
    print(f"Generated {args.nodes} preferencesKad.dat files in {SEED_DIR}")

    # Generate seed nodes.dat (use first 10 nodes or all if fewer)
    seed_count = min(10, args.nodes)
    generate_nodes_dat(ips[:seed_count], kad_ids[:seed_count],
                       args.tcp_port, args.udp_port, NODES_DAT_FILE)

    # Generate docker-compose
    generate_compose(
        args.nodes,
        args.subnet,
        args.base_ip_offset,
        args.tcp_port,
        args.udp_port,
        args.ipc_port,
        args.ipc_token,
        COMPOSE_FILE,
    )

    # Start the network
    docker_compose("up", "-d")
    print(f"\nKademlia LAN network started with {args.nodes} nodes.")
    print(f"  Subnet:    {args.subnet}")
    print(f"  Ports:     TCP={args.tcp_port}  UDP={args.udp_port}")
    print(f"  IPC ports: {args.ipc_port}–{args.ipc_port + args.nodes - 1} (token: {args.ipc_token})")
    print(f"\nConnect GUI to a node:")
    print(f"  ./docker/kad/gui.sh        # connect to node-1 (port {args.ipc_port})")
    print(f"  ./docker/kad/gui.sh 3      # connect to node-3 (port {args.ipc_port + 2})")
    print(f"\nUseful commands:")
    print(f"  Logs:      python3 docker/kad/kadnet.py --logs")
    print(f"  Tear down: python3 docker/kad/kadnet.py --down")


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\nInterrupted.")
        sys.exit(0)
