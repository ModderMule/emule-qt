#!/usr/bin/env python3
"""
cachenet.py — Orchestrate an HTTP Cache swarm with Docker.

Spins up one nginx + php-fpm cache server and N emulecored peers on a private
bridge network. One peer (or more, see --seeders) starts with a complete
multi-part test file; the rest are handed an ed2k link that points at it and
download it. The uploader publishes each whole part to the cache server once and
offers it to everybody, which is the behaviour this rig exists to observe.

Every peer gets its own IP: eMule refuses a 4th client from the same address
(UploadQueue::addClientToQueue, sameIPCount >= 3), so a swarm on one address
would silently stop being a swarm at three.

Usage:
    python3 docker/httpcache/cachenet.py                     # 20 peers, start
    python3 docker/httpcache/cachenet.py --peers 4 --build   # rebuild images first
    python3 docker/httpcache/cachenet.py --logs              # follow logs
    python3 docker/httpcache/cachenet.py --down              # tear down

    ./docker/httpcache/gui.sh 1                              # attach the GUI to node-1
    python3 docker/httpcache/analyze_log.py nodes.log        # read the run
"""

import argparse
import hashlib
import ipaddress
import json
import os
import random
import struct
import subprocess
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, "..", ".."))
COMPOSE_FILE = os.path.join(SCRIPT_DIR, "docker-compose.cachenet.yml")
SEED_DIR = os.path.join(SCRIPT_DIR, "seed")
RIG_FILE = os.path.join(SEED_DIR, "rig.json")

# Shared with docker/kad — both rigs run the same daemon build.
DAEMON_DOCKERFILE = os.path.join(PROJECT_ROOT, "docker", "daemon.Dockerfile")
DAEMON_IMAGE = "emuleqt-daemon:latest"
CACHE_DOCKERFILE = os.path.join(SCRIPT_DIR, "cache", "Dockerfile")
CACHE_IMAGE = "emuleqt-httpcache:latest"

PARTSIZE = 9_728_000
MIN_PARTS = 3            # "at least 3 chunks" — whole parts, the tail does not count

TEST_FILE_NAME = "cachenet-testfile.bin"
FILE_SEED = 1337         # fixed, so the file and therefore the link never move


# ---------------------------------------------------------------------------
# eD2K hashing
#
# MD4 is not reachable from Python on a modern host: OpenSSL 3 dropped it from
# the default provider, so both hashlib.new("md4") and `openssl dgst -md4` fail.
# The fallback below is the plain RFC 1320 algorithm; ~8 s for 30 MB, and the
# result is cached in rig.json so it is paid once per file size.
# ---------------------------------------------------------------------------

def _md4_native(data: bytes):
    try:
        h = hashlib.new("md4")
    except ValueError:
        return None
    h.update(data)
    return h.digest()


def _md4_python(message: bytes) -> bytes:
    """RFC 1320 MD4, one shot."""
    h = [0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476]

    msg = bytearray(message)
    bit_len = (8 * len(message)) & 0xFFFFFFFFFFFFFFFF
    msg.append(0x80)
    while len(msg) % 64 != 56:
        msg.append(0)
    msg += struct.pack("<Q", bit_len)

    def rl(x, n):
        x &= 0xFFFFFFFF
        return ((x << n) | (x >> (32 - n))) & 0xFFFFFFFF

    # Round 2 and 3 walk the 16 words in their own order (RFC 1320 §3.4).
    order2 = [0, 4, 8, 12, 1, 5, 9, 13, 2, 6, 10, 14, 3, 7, 11, 15]
    order3 = [0, 8, 4, 12, 2, 10, 6, 14, 1, 9, 5, 13, 3, 11, 7, 15]

    for off in range(0, len(msg), 64):
        X = struct.unpack("<16I", msg[off:off + 64])
        a, b, c, d = h

        for i in range(16):
            k = i
            if i % 4 == 0:
                a = rl(a + ((b & c) | (~b & d)) + X[k], 3)
            elif i % 4 == 1:
                d = rl(d + ((a & b) | (~a & c)) + X[k], 7)
            elif i % 4 == 2:
                c = rl(c + ((d & a) | (~d & b)) + X[k], 11)
            else:
                b = rl(b + ((c & d) | (~c & a)) + X[k], 19)

        for i in range(16):
            k = order2[i]
            if i % 4 == 0:
                a = rl(a + ((b & c) | (b & d) | (c & d)) + X[k] + 0x5A827999, 3)
            elif i % 4 == 1:
                d = rl(d + ((a & b) | (a & c) | (b & c)) + X[k] + 0x5A827999, 5)
            elif i % 4 == 2:
                c = rl(c + ((d & a) | (d & b) | (a & b)) + X[k] + 0x5A827999, 9)
            else:
                b = rl(b + ((c & d) | (c & a) | (d & a)) + X[k] + 0x5A827999, 13)

        for i in range(16):
            k = order3[i]
            if i % 4 == 0:
                a = rl(a + (b ^ c ^ d) + X[k] + 0x6ED9EBA1, 3)
            elif i % 4 == 1:
                d = rl(d + (a ^ b ^ c) + X[k] + 0x6ED9EBA1, 9)
            elif i % 4 == 2:
                c = rl(c + (d ^ a ^ b) + X[k] + 0x6ED9EBA1, 11)
            else:
                b = rl(b + (c ^ d ^ a) + X[k] + 0x6ED9EBA1, 15)

        h = [(h[0] + a) & 0xFFFFFFFF, (h[1] + b) & 0xFFFFFFFF,
             (h[2] + c) & 0xFFFFFFFF, (h[3] + d) & 0xFFFFFFFF]

    return struct.pack("<4I", *h)


def md4(data: bytes) -> bytes:
    native = _md4_native(data)
    return native if native is not None else _md4_python(data)


def ed2k_hash(path: str) -> str:
    """eD2K file hash: MD4 over the concatenated per-part MD4s.

    Mirrors KnownFile::createFromFile() — part count is ceil(size / PARTSIZE) and
    a single-part file's hash IS its part hash. eMuleQt does not append the empty
    trailing part hash that classic eMule adds for an exact multiple of PARTSIZE;
    the rig sizes its file off a part boundary so the two conventions cannot
    disagree here.
    """
    part_hashes = []
    with open(path, "rb") as f:
        while True:
            part = f.read(PARTSIZE)
            if not part:
                break
            part_hashes.append(md4(part))

    if len(part_hashes) == 1:
        return part_hashes[0].hex()
    return md4(b"".join(part_hashes)).hex()


# ---------------------------------------------------------------------------
# Test file
# ---------------------------------------------------------------------------

def generate_test_file(path: str, size: int) -> None:
    """Write `size` deterministic pseudo-random bytes.

    Incompressible, so ed2k's packed-block compression cannot distort the byte
    accounting the analyzer does, and seeded so every run shares one hash.
    """
    rng = random.Random(FILE_SEED)
    written = 0
    with open(path, "wb") as f:
        while written < size:
            block = min(1 << 20, size - written)
            f.write(rng.randbytes(block))
            written += block


def sha256_of(path: str) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def user_hash(node_index: int) -> str:
    """Deterministic 16-byte eMule user hash, hex.

    Bytes 5 and 14 carry the eMule client markers. A stable identity per node
    means a restarted container is recognised as the same client rather than as
    a stranger from the same address.
    """
    h = bytearray(hashlib.sha256(f"cachenet-userhash-{node_index}".encode()).digest()[:16])
    h[5] = 0x0E
    h[14] = 0x6F
    return h.hex()


# ---------------------------------------------------------------------------
# Compose generation
# ---------------------------------------------------------------------------

def write_service(f, name: str, svc: dict, network: str) -> None:
    f.write(f"  {name}:\n")
    f.write(f"    image: {svc['image']}\n")
    f.write(f"    container_name: {svc['container_name']}\n")
    f.write(f"    restart: {svc.get('restart', 'unless-stopped')}\n")
    if svc.get("entrypoint"):
        f.write(f"    entrypoint: {json.dumps(svc['entrypoint'])}\n")
    if svc.get("depends_on"):
        f.write("    depends_on:\n")
        for dep, condition in svc["depends_on"].items():
            f.write(f"      {dep}:\n")
            f.write(f"        condition: {condition}\n")
    if svc.get("healthcheck"):
        f.write("    healthcheck:\n")
        f.write(f"      test: {json.dumps(svc['healthcheck'])}\n")
        f.write("      interval: 3s\n")
        f.write("      timeout: 3s\n")
        f.write("      retries: 30\n")
    if svc.get("environment"):
        f.write("    environment:\n")
        for k, v in svc["environment"].items():
            f.write(f"      {k}: \"{v}\"\n")
    if svc.get("ports"):
        f.write("    ports:\n")
        for port in svc["ports"]:
            f.write(f"      - \"{port}\"\n")
    if svc.get("volumes"):
        f.write("    volumes:\n")
        for vol in svc["volumes"]:
            f.write(f"      - {vol}\n")
    f.write("    networks:\n")
    f.write(f"      {network}:\n")
    f.write(f"        ipv4_address: {svc['ip']}\n")


def generate_compose(args, ips: dict, link: str, path: str) -> None:
    network = ipaddress.IPv4Network(args.subnet)
    gateway = str(list(network.hosts())[0])

    services = {}
    volumes = {"cache-data": None}

    services["cache"] = {
        "image": CACHE_IMAGE,
        "container_name": "cachenet-cache",
        "ip": ips["cache"],
        "environment": {
            "CACHE_API_KEY_ID": "cachenet",
            "CACHE_API_KEY": args.api_key,
            "PUBLIC_BASE_URL": args.cache_url,
        },
        "volumes": ["cache-data:/data"],
        # Peers wait for this rather than for the container to merely exist: a
        # publish that lands before nginx is up fails as a transport error, and
        # that failure is server-wide — it would stand the whole feature down for
        # a minute at the very start of every run.
        "healthcheck": ["CMD", "wget", "-q", "-O", "/dev/null",
                        "http://127.0.0.1/v1/info"],
    }
    if args.cache_src:
        services["cache"]["volumes"].append(f"{args.cache_src}:/app-src:ro")

    for i in range(1, args.peers + 1):
        is_seeder = i <= args.seeders
        volume_name = f"node-{i}-config"
        volumes[volume_name] = None

        node_volumes = [
            f"{volume_name}:/root/.config/eMule/Core",
            "./entrypoint.sh:/entrypoint-httpcache.sh:ro",
            f"./crashes/node-{i}:/root/.config/eMule/Core/crashes",
        ]
        if is_seeder:
            node_volumes.append(
                f"./seed/{TEST_FILE_NAME}:/root/incoming/{TEST_FILE_NAME}:ro")

        publishes = is_seeder or args.all_publish
        env = {
            "NODE_NICK": f"Node-{i}",
            "NODE_ROLE": "seeder" if is_seeder else "leecher",
            "USER_HASH": user_hash(i),
            "TCP_PORT": str(args.tcp_port),
            "UDP_PORT": str(args.udp_port),
            "IPC_PORT": str(args.ipc_port),
            "IPC_TOKEN": args.ipc_token,
            "CACHE_BASE_URL": args.cache_url,
            # Without a key the manager's uploadEnabled() is false whatever
            # allowUpload says, so this is the real switch.
            "CACHE_API_KEY": args.api_key if publishes else "",
            "ALLOW_UPLOAD": "true" if publishes else "false",
            "MIN_CLIENTS": str(args.min_clients),
            "PUBLISH_RATE_KBS": str(args.publish_rate_kbs),
            "MAX_FETCHES": str(args.max_fetches),
            "MAX_UPLOAD_KBS": str(args.seed_upload_kbs if is_seeder else 0),
        }
        if not is_seeder:
            env["DOWNLOAD_LINK"] = link

        services[f"node-{i}"] = {
            "image": DAEMON_IMAGE,
            "container_name": f"cachenet-node-{i}",
            "ip": ips[f"node-{i}"],
            "entrypoint": ["/entrypoint-httpcache.sh"],
            "depends_on": {"cache": "service_healthy"},
            "environment": env,
            "ports": [f"{args.ipc_port_host + i - 1}:{args.ipc_port}"],
            "volumes": node_volumes,
        }

    with open(path, "w") as f:
        f.write("# Auto-generated by cachenet.py — do not edit\n\n")
        f.write("networks:\n")
        f.write("  cachenet:\n")
        f.write("    driver: bridge\n")
        f.write("    ipam:\n")
        f.write("      config:\n")
        f.write(f"        - subnet: {args.subnet}\n")
        f.write(f"          gateway: {gateway}\n\n")

        f.write("services:\n")
        for name, svc in services.items():
            write_service(f, name, svc, "cachenet")
        f.write("\n")

        f.write("volumes:\n")
        for name in volumes:
            f.write(f"  {name}:\n")
            f.write("    driver: local\n")

    print(f"Generated {path}: 1 cache server + {args.peers} peers on {args.subnet}")


def docker_compose(*cmd) -> None:
    full = ["docker", "compose", "-f", COMPOSE_FILE, *cmd]
    print(f"$ {' '.join(full)}")
    subprocess.run(full, cwd=SCRIPT_DIR, check=True)


def build_images(args) -> None:
    print(f"Building {DAEMON_IMAGE} from {DAEMON_DOCKERFILE}...")
    subprocess.run(
        ["docker", "build", "-f", DAEMON_DOCKERFILE, "-t", DAEMON_IMAGE, PROJECT_ROOT],
        check=True,
    )
    print(f"Building {CACHE_IMAGE} from {CACHE_DOCKERFILE}...")
    subprocess.run(
        ["docker", "build",
         "-f", CACHE_DOCKERFILE,
         "--build-arg", f"CACHE_REPO={args.cache_repo}",
         "--build-arg", f"CACHE_REF={args.cache_ref}",
         "-t", CACHE_IMAGE, PROJECT_ROOT],
        check=True,
    )


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def parse_args():
    p = argparse.ArgumentParser(
        description="Manage an eMuleQt HTTP Cache swarm",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__.split("Usage:")[1],
    )
    p.add_argument("--peers", type=int, default=20,
                   help="total emulecored containers (default: 20)")
    p.add_argument("--seeders", type=int, default=1,
                   help="how many of them start with the complete file (default: 1)")
    p.add_argument("--file-size", type=int, default=30_000_000,
                   help="test file size in bytes; must hold at least 3 whole parts "
                        "(default: 30000000 = 3 parts + an 816000-byte tail)")
    p.add_argument("--subnet", default="172.30.0.0/16",
                   help="docker subnet (default: 172.30.0.0/16 — the Kad rig uses 172.20/16)")
    p.add_argument("--all-publish", action="store_true",
                   help="let every peer publish to the cache, not just the seeders")
    p.add_argument("--min-clients", type=int, default=2,
                   help="httpCache.minClients (default: 2)")
    p.add_argument("--publish-rate-kbs", type=int, default=8192,
                   help="httpCache.publishRateKBs (default: 8192)")
    p.add_argument("--max-fetches", type=int, default=2,
                   help="httpCache.maxConcurrentFetches (default: 2)")
    p.add_argument("--seed-upload-kbs", type=int, default=256,
                   help="ed2k upload cap on seeders, 0 = unlimited (default: 256)")
    p.add_argument("--cache-url", default="http://cache",
                   help="base URL peers publish to and fetch from (default: http://cache)")
    p.add_argument("--cache-repo", default="https://github.com/ModderMule/emule-http-cache-php",
                   help="backend git repository to clone into the cache image")
    p.add_argument("--cache-ref", default="main", help="branch or tag to clone")
    p.add_argument("--cache-src", default="",
                   help="bind-mount a local backend checkout instead of using the clone")
    p.add_argument("--api-key", default="cachenet-dev-key-0123456789abcdef",
                   help="upload credential shared by the rig")
    p.add_argument("--ipc-token", default="cachenet-test-token")
    p.add_argument("--tcp-port", type=int, default=5662)
    p.add_argument("--udp-port", type=int, default=5672)
    p.add_argument("--ipc-port", type=int, default=4712, help="IPC port inside each container")
    p.add_argument("--ipc-port-host", type=int, default=4812,
                   help="first host port the IPC ports map to (default: 4812, clear of the "
                        "Kad rig's 4712 range)")
    p.add_argument("--base-ip-offset", type=int, default=10,
                   help="offset from the network address for node-1 (default: 10)")
    p.add_argument("--build", action="store_true", help="rebuild both images first")
    p.add_argument("--down", action="store_true", help="tear down and remove volumes")
    p.add_argument("--logs", action="store_true", help="follow container logs")
    return p.parse_args()


def main():
    args = parse_args()

    if args.down:
        docker_compose("down", "-v")
        return
    if args.logs:
        docker_compose("logs", "-f")
        return

    if args.seeders < 1 or args.seeders >= args.peers:
        sys.exit(f"--seeders must be between 1 and {args.peers - 1}")
    if args.file_size < MIN_PARTS * PARTSIZE:
        sys.exit(f"--file-size must be at least {MIN_PARTS * PARTSIZE} "
                 f"({MIN_PARTS} whole parts); the short tail part is never published")
    if args.cache_src:
        args.cache_src = os.path.abspath(args.cache_src)
        if not os.path.isdir(args.cache_src):
            sys.exit(f"--cache-src is not a directory: {args.cache_src}")

    if args.build:
        build_images(args)

    os.makedirs(SEED_DIR, exist_ok=True)
    for i in range(1, args.peers + 1):
        os.makedirs(os.path.join(SCRIPT_DIR, "crashes", f"node-{i}"), exist_ok=True)

    # Addresses: the cache sits below the node range so node-N's address stays
    # predictable when --peers changes.
    network = ipaddress.IPv4Network(args.subnet)
    ips = {"cache": str(network.network_address + 5)}
    for i in range(1, args.peers + 1):
        ips[f"node-{i}"] = str(network.network_address + args.base_ip_offset + i - 1)

    # Test file and link. Both are deterministic, so an unchanged size skips the
    # regeneration and the ~8 s MD4 pass with it.
    file_path = os.path.join(SEED_DIR, TEST_FILE_NAME)
    rig = {}
    if os.path.isfile(RIG_FILE):
        with open(RIG_FILE) as f:
            rig = json.load(f)

    needs_file = (not os.path.isfile(file_path)
                  or os.path.getsize(file_path) != args.file_size
                  or rig.get("fileSize") != args.file_size
                  or not rig.get("ed2kHash"))
    if needs_file:
        print(f"Generating {file_path} ({args.file_size} bytes)...")
        generate_test_file(file_path, args.file_size)
        print("Hashing (MD4 in Python — OpenSSL 3 no longer offers it)...")
        rig = {
            "fileName": TEST_FILE_NAME,
            "fileSize": args.file_size,
            "ed2kHash": ed2k_hash(file_path),
            "sha256": sha256_of(file_path),
        }

    whole_parts = args.file_size // PARTSIZE
    part_count = (args.file_size + PARTSIZE - 1) // PARTSIZE

    # ED2KFileLink::toLink(): the '/' terminates the parameter section and comes
    # BEFORE the source block — stock eMule's tokenizer stops at the first empty
    # token, so a link without it is unparseable to it.
    sources = ",".join(f"{ips[f'node-{i}']}:{args.tcp_port}"
                       for i in range(1, args.seeders + 1))
    link = (f"ed2k://|file|{rig['fileName']}|{rig['fileSize']}|{rig['ed2kHash']}|/"
            f"|sources,{sources}|/")

    rig.update({
        "peers": args.peers,
        "seeders": [f"node-{i}" for i in range(1, args.seeders + 1)],
        "allPublish": args.all_publish,
        "subnet": args.subnet,
        "ips": ips,
        "partCount": part_count,
        "wholeParts": whole_parts,
        "partSize": PARTSIZE,
        "cacheUrl": args.cache_url,
        "ed2kLink": link,
        "ipcPortHost": args.ipc_port_host,
        "ipcToken": args.ipc_token,
    })
    with open(RIG_FILE, "w") as f:
        json.dump(rig, f, indent=2)
    print(f"Test file: {rig['fileName']} — {part_count} parts "
          f"({whole_parts} whole, publishable), ed2k {rig['ed2kHash']}")

    generate_compose(args, ips, link, COMPOSE_FILE)
    docker_compose("up", "-d")

    leechers = args.peers - args.seeders
    print(f"\nHTTP Cache swarm started.")
    print(f"  Cache:     {args.cache_url} at {ips['cache']} (container cachenet-cache)")
    print(f"  Seeders:   {args.seeders}  ({', '.join(rig['seeders'])})")
    print(f"  Leechers:  {leechers}, each downloading {rig['fileName']}")
    print(f"  IPC ports: {args.ipc_port_host}–{args.ipc_port_host + args.peers - 1} "
          f"(token: {args.ipc_token})")
    print(f"\nExpect {whole_parts} chunks published and about "
          f"{whole_parts * leechers} cache fetches.")
    print(f"\nConnect the GUI to a node:")
    print(f"  ./docker/httpcache/gui.sh        # node-1, the seeder")
    print(f"  ./docker/httpcache/gui.sh 3      # node-3")
    print(f"\nWatch and analyze:")
    print(f"  timeout 420 python3 docker/httpcache/cachenet.py --logs 2>&1 "
          f"| tee docker/httpcache/nodes.log")
    print(f"  python3 docker/httpcache/analyze_log.py")
    print(f"  python3 docker/httpcache/cachenet.py --down")


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\nInterrupted.")
        sys.exit(0)
