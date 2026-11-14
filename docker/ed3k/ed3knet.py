#!/usr/bin/env python3
"""
ed3knet.py — ed3k Kad rig: bootstrap of fresh networks and LowID-to-LowID transfer behind NAT.

Every node is a `packmule-node` (libs/packmule/crates/packmule-node), built for Linux in a
rust container and mounted into a slim image. All run in the named test network `ed3klab`.

Topology (no RFC 1918 address anywhere: a private address in a named network is adopted as
the own endpoint, Protocol.md §9.2, and would hide the NAT):

    ed3kpub   11.0.0.0/8     one /16 per host, so each is a block of its own (ip-diversity.md
                             §1: a table holds ≤ 20 per /16, an answer ≤ 4):
                             pubN 11.N.0.10, N = 1..--nodes (default 14); the last
                             max(2, N/10) must earn ROUTER, the others are --force-open (a
                             new network's first routers, operator-declared);
                             seed 11.200.0.10 (--no-router: reachable leaf);
                             router A 11.201.0.1, router B 11.202.0.1
    ed3klanA  12.79.1.0/24   router A .254, nodeA .11
    ed3klanB  12.79.2.0/24   router B .254, nodeB .12, nodeF .13

Routers are the NAT rig's (docker/natt/router): conntrack MASQUERADE, port-restricted cone.

Checks, in order:
    1  routers    the forced routers find each other
    2  leaves     nodeA (shares a file) and the reachable leaf `seed` bootstrap; nodeA stays
                  no ROUTER behind its NAT
    3  ed2k-seed  nodeF has only `seed` as an eD2K seed (bootstrap.md §1 source 4): `seed` is
                  no ROUTER, so it is asked like a LAN peer; nodeF must get contacts
    4  lowid      nodeB (behind router B) bootstraps, finds nodeA's file in Kad and downloads
                  it: both LowID, the data path is the NAT traversal / relay of the engine
    5  promotion  of the testers (pub13-pub14 by default), started with check 1, at least
                  one became ROUTER by reach tests (§9.2).
                  A PROBE counts only from a host the tester sent nothing to for 60 min, and
                  B never probes a node it is connected to (§9.3): in a network so small
                  that a newcomer's lookups touch every router this takes the hour

    index      only with --index, a rig of its own: emulecored (Linux build), a mock eD2K
               server (mock_ed2k_server.py) whose OP_SERVERIDENT names an index node by
               ST_ED3K, and packmule-node --index, on the internal network ed3kidx
               13.77.0.0/16 (index .1.10, mock .1.20, daemon .1.30). index_drive.py drives the
               daemon over IPC: auto connect off = named only, on = dialled, off again = kept
               (no restart), the server list's Disconnect closes it (no restart, no redial),
               reconnect + on = dialled again. The daemon is the build in the docker volume
               emuleqt-linux-build of image emuleqt-linux-builder (docs: linux container
               verify); --daemon-build runs `cmake --build --target emulecored` there first

Usage:
    python3 docker/ed3k/ed3knet.py --build        # build binary + images, run once
    python3 docker/ed3k/ed3knet.py                # reuse the image, rebuild the binary
    python3 docker/ed3k/ed3knet.py --nodes 60     # 60 public nodes (54 forced, 6 testers)
    python3 docker/ed3k/ed3knet.py --keep         # leave it running afterwards
    python3 docker/ed3k/ed3knet.py --index --daemon-build   # the index node check only
    python3 docker/ed3k/ed3knet.py --down         # tear down
"""
import argparse
import hashlib
import random
import os
import re
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
PACKMULE = os.path.join(REPO, "libs", "packmule")
ROUTER_DIR = os.path.join(REPO, "docker", "natt", "router")
WORK = os.path.join(HERE, "run")

NODE_IMAGE = "ed3knet-node:latest"
ROUTER_IMAGE = "natnet-router:latest"   # shared with docker/natt
RUST_IMAGE = "rust:1-trixie"
CARGO_VOLUME = "ed3knet-cargo"
TARGET_SUB = "target/docker-linux"      # inside libs/packmule (ignored)
BINARY = os.path.join(PACKMULE, TARGET_SUB, "release", "packmule-node")

LAB = "ed3klab"
PORT = 5662
PREFIX = "ed3knet"
PUB_NET, PUB_SUBNET, PUB = "ed3kpub", "11.0.0.0/8", "11"
# name: (container, public IP, LAN network, LAN subnet, docker gateway, router LAN IP)
ROUTERS = {
    "A": (f"{PREFIX}-routerA", f"{PUB}.201.0.1", "ed3klanA", "12.79.1.0/24", "12.79.1.1", "12.79.1.254"),
    "B": (f"{PREFIX}-routerB", f"{PUB}.202.0.1", "ed3klanB", "12.79.2.0/24", "12.79.2.1", "12.79.2.254"),
}
SEED_IP = f"{PUB}.200.0.10"
MIN_NODES, MAX_NODES = 10, 199          # pubN lives in 11.N.0.0/16, below the seed's /16
BOOT_LINKS = 8                          # --bootstrap links per forced router
# NAT nodes: name -> (router, IP)
NATTED = {"nodeA": ("A", "12.79.1.11"), "nodeB": ("B", "12.79.2.12"), "nodeF": ("B", "12.79.2.13")}
# --index: daemon, mock eD2K server and index node on an internal network (no internet)
IDX_NET, IDX_SUBNET = "ed3kidx", "13.77.0.0/16"
IDX_NODE_IP, IDX_NODE_PORT = "13.77.1.10", 4672
MOCK_IP, MOCK_PORT = "13.77.1.20", 4661
DAEMON_IP, DAEMON_IPC = "13.77.1.30", 4712
BUILDER_IMAGE = "emuleqt-linux-builder:latest"
BUILD_VOLUME = "emuleqt-linux-build"
DAEMON_BIN = "/build/src/daemon/emulecored"
# a fresh profile cut off from everything but the mock server and the index node
DAEMON_PREFS = [("general", "autoConnect", "false"), ("general", "reconnect", "false"),
                ("general", "filterLANIPs", "false"), ("kademlia", "enabled", "false"),
                ("usenet", "enabled", "false"), ("bittorrent", "enabled", "false"),
                ("ed3k", "lan", "false"), ("ed3k", "nodeListAuto", "false"),
                ("ed3k", "seedFromEd2k", "false"), ("ed3k", "autoIndexNode", "false"),
                ("serverConnection", "addServersFromServer", "false")]

PUBLIC: dict[str, str] = {}
FORCED: tuple[str, ...] = ()
TESTERS: tuple[str, ...] = ()
ALL_NODES: list[str] = []
SHARE_MB = 4
MIN_CONTACTS = 3


def configure(nodes: int) -> None:
    """Public nodes pub1..pubN: the last max(2, N/10) are testers, the others forced routers."""
    global PUBLIC, FORCED, TESTERS, ALL_NODES
    testers = max(2, nodes // 10)
    PUBLIC = {f"pub{i}": f"{PUB}.{i}.0.10" for i in range(1, nodes + 1)}
    FORCED = tuple(f"pub{i}" for i in range(1, nodes - testers + 1))
    TESTERS = tuple(n for n in PUBLIC if n not in FORCED)
    ALL_NODES = [*PUBLIC, "seed", *NATTED]


def log(msg: str) -> None:
    print(f"[ed3knet] {msg}", flush=True)


def run(*args, check=True, capture=False):
    r = subprocess.run(args, check=False, text=True,
                       stdout=subprocess.PIPE if capture else None,
                       stderr=subprocess.STDOUT if capture else None)
    if check and r.returncode != 0:
        out = r.stdout if capture else ""
        raise SystemExit(f"[ed3knet] failed: {' '.join(args)}\n{out}")
    return r


def docker(*args, check=True, capture=True):
    return run("docker", *args, check=check, capture=capture)


def image_exists(name: str) -> bool:
    return docker("image", "inspect", name, check=False).returncode == 0


def wait_for(what: str, predicate, timeout: float, interval: float = 1.0) -> bool:
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        if predicate():
            return True
        time.sleep(interval)
    log(f"timeout: {what}")
    return False


# --- build ---

def build_binary() -> None:
    log("building packmule-node for Linux (release)")
    run("docker", "run", "--rm", "-v", f"{PACKMULE}:/src", "-w", "/src",
        "-v", f"{CARGO_VOLUME}:/usr/local/cargo/registry",
        "-e", f"CARGO_TARGET_DIR=/src/{TARGET_SUB}",
        RUST_IMAGE, "cargo", "build", "--release", "-p", "packmule-node")
    if not os.path.isfile(BINARY):
        raise SystemExit(f"[ed3knet] no binary at {BINARY}")


def build_images(force: bool) -> None:
    if force or not image_exists(NODE_IMAGE):
        log(f"building {NODE_IMAGE}")
        run("docker", "build", "-t", NODE_IMAGE, HERE)
    if force or not image_exists(ROUTER_IMAGE):
        log(f"building {ROUTER_IMAGE}")
        run("docker", "build", "-t", ROUTER_IMAGE, ROUTER_DIR)


# --- rig ---

def container(name: str) -> str:
    return f"{PREFIX}-{name}"


def down() -> None:
    # by prefix: a rig of another --nodes may be left over
    existing = docker("ps", "-a", "--format", "{{.Names}}").stdout.split()
    present = [n for n in existing if n.startswith(f"{PREFIX}-")]
    if present:
        docker("rm", "-f", *present, check=False)
    for net in (PUB_NET, IDX_NET, *[r[2] for r in ROUTERS.values()]):
        docker("network", "rm", net, check=False)


def create_networks() -> None:
    docker("network", "create", "--subnet", PUB_SUBNET, PUB_NET)
    for _, _, lan, subnet, gateway, _ in ROUTERS.values():
        docker("network", "create", "--subnet", subnet, "--gateway", gateway, lan)


def start_routers() -> bool:
    for name, (cont, pub_ip, lan, subnet, _, lan_ip) in ROUTERS.items():
        cap = os.path.join(WORK, f"router{name}")
        os.makedirs(cap, exist_ok=True)
        log(f"router {name}: {pub_ip} <-> {subnet} (port-restricted cone)")
        docker("create", "--name", cont, "--hostname", cont, "--network", PUB_NET, "--ip", pub_ip,
               "--cap-add", "NET_ADMIN", "--cap-add", "NET_RAW",
               "--sysctl", "net.ipv4.ip_forward=1",
               "-e", f"PUB_IP={pub_ip}", "-e", f"LAN_IP={lan_ip}", "-e", f"LAN_SUBNET={subnet}",
               "-e", "NAT_RANDOM=0", "-v", f"{cap}:/cap", ROUTER_IMAGE)
        docker("network", "connect", "--ip", lan_ip, lan, cont)
        docker("start", cont)
    for name in ROUTERS:
        ready = os.path.join(WORK, f"router{name}", "ready")
        if not wait_for(f"router {name}", lambda: os.path.isfile(ready), 30, 0.5):
            return False
    return True


def start_node(name: str, *args: str) -> None:
    work = os.path.join(WORK, name)
    os.makedirs(work, exist_ok=True)
    if name in NATTED:
        router, ip = NATTED[name]
        _, _, lan, _, _, lan_ip = ROUTERS[router]
        net = ["--network", lan, "--ip", ip, "--cap-add", "NET_ADMIN", "-e", f"RIG_DEFAULT_ROUTE={lan_ip}"]
    else:
        ip = SEED_IP if name == "seed" else PUBLIC[name]
        net = ["--network", PUB_NET, "--ip", ip]
    docker("run", "-d", "--name", container(name), "--hostname", name, *net,
           "-v", f"{BINARY}:/opt/packmule-node:ro", "-v", f"{work}:/work", "-w", "/work",
           NODE_IMAGE, "--port", str(PORT), "--bind", ip, "--network", LAB, "--key", "/work/ed3k.key",
           "--lan", "off", "--log", "verbose", *args)


def logs(name: str) -> str:
    r = docker("logs", container(name), check=False)
    return r.stdout or ""


def node_link(name: str) -> str:
    m = re.search(r"^node (ed3k:node\?\S+)", logs(name), re.M)
    return m.group(1) if m else ""


def contacts(name: str) -> int:
    found = re.findall(r"^contacts (\d+)", logs(name), re.M)
    return int(found[-1]) if found else 0


# the state at start is in the `log on` line, changes come as `router true|false`
ROUTER_RE = re.compile(r"^(?:router (true|false)|log Core: log on: .*router (yes|no))", re.M)


def router_states(name: str) -> list[bool]:
    return [m.group(1) == "true" or m.group(2) == "yes" for m in ROUTER_RE.finditer(logs(name))]


def is_router(name: str) -> bool:
    states = router_states(name)
    return bool(states) and states[-1]


def ever_router(name: str) -> bool:
    return any(router_states(name))


def running(name: str) -> bool:
    r = docker("inspect", "-f", "{{.State.Running}}", container(name), check=False)
    return r.stdout.strip() == "true"


def boot_args(names) -> list[str]:
    return [a for n in names for a in ("--bootstrap", link_of(n))]


def link_of(name: str) -> str:
    if not wait_for(f"{name} link", lambda: bool(node_link(name)), 20, 0.5):
        print(logs(name)[-3000:])
        raise SystemExit(1)
    return node_link(name)


# --- the checks ---

class Rig:
    def __init__(self, timeout: int):
        self.timeout = timeout
        self.results: list[tuple[str, bool, str]] = []

    def result(self, check: str, ok: bool, detail: str) -> bool:
        self.results.append((check, ok, detail))
        log(f"{'PASS' if ok else 'FAIL'} {check}: {detail}")
        return ok

    def routers(self) -> bool:
        start_node("pub1", "--force-open")
        first = link_of("pub1")
        log(f"pub1 = {first}")
        # the operator-declared routers first, each given up to BOOT_LINKS of the ones before
        # it (a lookup returns only contacts closer than the answerer, §10.3: in a network
        # this small a router found nobody else until its hourly bucket refresh, §10.2)
        pick = random.Random(len(FORCED))
        for i, name in enumerate(FORCED[1:], 1):
            earlier = FORCED[:i]
            boot = earlier if i <= BOOT_LINKS else pick.sample(earlier, BOOT_LINKS)
            start_node(name, "--force-open", *boot_args(boot))
        ok = wait_for("forced routers know each other", lambda: all(contacts(n) >= 3 for n in FORCED),
                      90 + 2 * len(FORCED), 2)
        # the others as from a shipped node list: three of them; checked last (promotion)
        for name in TESTERS:
            start_node(name, *boot_args(FORCED[:3]))
        counts = [contacts(n) for n in FORCED]
        detail = (", ".join(f"{n}: {c}" for n, c in zip(FORCED, counts)) if len(FORCED) <= 16 else
                  f"{len(FORCED)} routers, min {min(counts)}, median {sorted(counts)[len(counts) // 2]}, "
                  f"max {max(counts)}")
        return self.result("routers", ok and all(is_router(n) for n in FORCED), f"contacts {detail}")

    def promotion(self) -> bool:
        wait_for("testers ROUTER", lambda: all(is_router(n) for n in TESTERS), self.timeout, 2)
        # one is enough: a tester whose lookups touched every router waits out the hour
        detail = "; ".join(f"{n}: {contacts(n)} contacts, router {is_router(n)}, "
                           f"{logs(n).count('PROBE came')} PROBE, {logs(n).count('no prober')} no prober"
                           for n in TESTERS)
        return self.result("promotion", any(is_router(n) for n in TESTERS), detail)

    def leaves(self, share: str) -> bool:
        start_node("seed", "--no-router", *boot_args(("pub2", "pub7")))
        start_node("nodeA", *boot_args(("pub3", "pub8")), "--share", f"/work/{os.path.basename(share)}")
        enough = lambda: contacts("seed") >= MIN_CONTACTS and contacts("nodeA") >= MIN_CONTACTS
        wait_for("leaves bootstrapped", enough, self.timeout, 2)
        ok = enough() and not ever_router("nodeA") and not ever_router("seed")
        return self.result("leaves", ok, f"seed: {contacts('seed')} contacts, router {ever_router('seed')}; "
                                          f"nodeA: {contacts('nodeA')} contacts, router {ever_router('nodeA')}")

    def ed2k_seed(self) -> bool:
        start_node("nodeF", "--ed2k-seed", node_link("seed"))
        wait_for("nodeF contacts", lambda: contacts("nodeF") >= MIN_CONTACTS, self.timeout, 2)
        text = logs("nodeF")
        asked = "asked like a LAN peer" in text
        dialled = re.search(r"^ed2k seed \S+: dialled", text, re.M) is not None
        n = contacts("nodeF")
        return self.result("ed2k-seed", dialled and asked and n >= MIN_CONTACTS,
                           f"dialled {dialled}, leaf seed asked {asked}, {n} contacts")

    def lowid(self, share: str) -> bool:
        a = logs("nodeA")
        m = re.search(r"^shared (ed3k:file\?\S+)", a, re.M)
        if not m:
            return self.result("lowid", False, "nodeA shared nothing")
        link = m.group(1)
        # nodeA publishes its source once bootstrapped; give it a moment
        time.sleep(15)
        start_node("nodeB", *boot_args(("pub4", "pub1")), "--get", link, "--out", "/work/copy.bin")
        wait_for("nodeB download", lambda: not running("nodeB") or re.search(r"^done ", logs("nodeB"), re.M),
                 self.timeout, 2)
        copy = os.path.join(WORK, "nodeB", "copy.bin")
        same = os.path.isfile(copy) and sha256(copy) == sha256(share)
        b = logs("nodeB")
        how = sorted(set(re.findall(r"(relay\w*|punch\w*|hole\w*)", b)))[:6]
        return self.result("lowid", same, f"copy {'matches' if same else 'missing or different'}; "
                                          f"nodeB {contacts('nodeB')} contacts, router {ever_router('nodeB')}; "
                                          f"seen: {', '.join(how) or '-'}")


# --- index node check (--index) ---

def build_daemon() -> None:
    log(f"building emulecored for Linux in {BUILD_VOLUME}")
    empty_env = os.path.join(WORK, "empty.env")   # the local .env holds Mac paths and keys
    open(empty_env, "w").close()
    run("docker", "run", "--rm", "-v", f"{REPO}:/src:ro", "-v", f"{empty_env}:/src/.env:ro",
        "-v", f"{BUILD_VOLUME}:/build", BUILDER_IMAGE, "cmake", "--build", "/build", "--target", "emulecored")


def isolate_prefs(path: str) -> None:
    with open(path) as f:
        text = f.read()
    for section, key, value in DAEMON_PREFS:
        m = re.search(r"(?m)^" + section + r":\n((?:  .*\n)*)", text)
        if not m:
            raise SystemExit(f"[ed3knet] no section {section} in {path}")
        body, n = re.subn(r"(?m)^  " + key + r":.*$", f"  {key}: {value}", m.group(1), count=1)
        if n != 1:
            raise SystemExit(f"[ed3knet] no {section}.{key} in {path}")
        text = text[:m.start(1)] + body + text[m.end(1):]
    with open(path, "w") as f:
        f.write(text)


def index_check(rig: "Rig") -> bool:
    docker("network", "create", "--internal", "--subnet", IDX_SUBNET, IDX_NET)
    idx_work, daemon_work = os.path.join(WORK, "index"), os.path.join(WORK, "daemon")
    os.makedirs(idx_work)
    os.makedirs(daemon_work)
    # no --network: the daemon joins the public ed3k network; 13.x is global unicast there
    docker("run", "-d", "--name", container("index"), "--network", IDX_NET, "--ip", IDX_NODE_IP,
           "-v", f"{BINARY}:/opt/packmule-node:ro", "-v", f"{idx_work}:/work", "-w", "/work",
           NODE_IMAGE, "--index", "--port", str(IDX_NODE_PORT), "--bind", IDX_NODE_IP, "--force-open",
           "--lan", "off", "--key", "/work/ed3k.key", "--log", "verbose")
    link = link_of("index")
    node_id = re.search(r"id=([0-9a-f]+)", link).group(1)
    log(f"index node = {link}")
    docker("run", "-d", "--name", container("mock"), "--network", IDX_NET, "--ip", MOCK_IP,
           "-v", f"{HERE}:/rig:ro", BUILDER_IMAGE, "python3", "-u", "/rig/mock_ed2k_server.py",
           str(MOCK_PORT), link, "0.0.0.0")
    docker("run", "-d", "--name", container("daemon"), "--network", IDX_NET, "--ip", DAEMON_IP,
           "-v", f"{BUILD_VOLUME}:/build:ro", "-v", f"{HERE}:/rig:ro", "-v", f"{daemon_work}:/work",
           BUILDER_IMAGE, "sleep", "infinity")
    # a first run writes a fresh profile, then it is cut off
    docker("exec", container("daemon"), "sh", "-c",
           f"{DAEMON_BIN} --config /work/cfg --log-level 1 > /work/first.log 2>&1 & p=$!; sleep 10; "
           "kill $p; wait $p || true")
    cfg = os.path.join(daemon_work, "cfg")
    isolate_prefs(os.path.join(cfg, "preferences.yml"))
    for name in ("server.met", "nodes.dat", "nodes.ed3knod", os.path.join("ed3k", "kad.dat")):
        if os.path.exists(os.path.join(cfg, name)):
            os.remove(os.path.join(cfg, name))
    docker("exec", "-d", container("daemon"), "sh", "-c",
           f"exec {DAEMON_BIN} --config /work/cfg --log-level 3 > /work/daemon.log 2>&1")
    time.sleep(8)
    r = run("docker", "exec", container("daemon"), "python3", "-u", "/rig/index_drive.py", str(DAEMON_IPC),
            MOCK_IP, str(MOCK_PORT), "/work/daemon.log", node_id, check=False)
    rig.result("index drive", r.returncode == 0, f"index_drive.py exit {r.returncode}, log {daemon_work}/daemon.log")
    # the Disconnect closed the daemon's connection at the index node, the reconnect made a new one
    text = logs("index")
    ups = len(re.findall(r"conn \d+ up: \S+ " + re.escape(DAEMON_IP), text))
    downs = len(re.findall(r"conn \d+ down", text))
    mock = logs("mock")
    ended = "connection ended" in mock
    return rig.result("index node side", ups >= 2 and downs >= 1 and ended,
                      f"{ups} connections of the daemon, {downs} closed; eD2K session ended at the mock {ended}")


def sha256(path: str) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--nodes", type=int, default=14,
                        help=f"public nodes, {MIN_NODES}-{MAX_NODES} (default 14: 12 forced routers, 2 testers)")
    parser.add_argument("--build", action="store_true", help="rebuild the images too")
    parser.add_argument("--no-binary", action="store_true", help="reuse the built packmule-node")
    parser.add_argument("--keep", action="store_true", help="leave the rig running afterwards")
    parser.add_argument("--timeout", type=int, default=420, help="seconds per check (default 420)")
    parser.add_argument("--down", action="store_true", help="tear the rig down and exit")
    parser.add_argument("--index", action="store_true", help="run only the index node check (emulecored)")
    parser.add_argument("--daemon-build", action="store_true", help="--index: rebuild emulecored for Linux first")
    args = parser.parse_args()
    if not MIN_NODES <= args.nodes <= MAX_NODES:
        parser.error(f"--nodes must be {MIN_NODES}-{MAX_NODES}")
    configure(args.nodes)

    down()
    if args.down:
        return 0
    if not args.no_binary or not os.path.isfile(BINARY):
        build_binary()
    build_images(args.build)
    run("rm", "-rf", WORK)
    os.makedirs(WORK)
    share = os.path.join(WORK, "nodeA", "share.bin")
    os.makedirs(os.path.dirname(share))
    with open(share, "wb") as f:
        f.write(os.urandom(SHARE_MB << 20))

    rig = Rig(args.timeout)
    if args.index:
        if args.daemon_build:
            build_daemon()
        try:
            index_check(rig)
        finally:
            for n in ("index", "mock", "daemon"):
                text = logs(n)
                if text:
                    with open(os.path.join(WORK, f"{n}.log"), "w") as f:
                        f.write(text)
            if not args.keep:
                down()
        failed = [c for c, ok, _ in rig.results if not ok]
        log(f"logs in {WORK}")
        log("all checks passed" if rig.results and not failed else f"failed: {', '.join(failed) or 'setup'}")
        return 0 if rig.results and not failed else 1

    create_networks()
    try:
        if not start_routers():
            return 1
        if rig.routers() and rig.leaves(share):
            rig.ed2k_seed()
            rig.lowid(share)
            rig.promotion()
    finally:
        for n in ALL_NODES:
            text = logs(n)
            if text:
                with open(os.path.join(WORK, f"{n}.log"), "w") as f:
                    f.write(text)
        if not args.keep:
            down()
    failed = [c for c, ok, _ in rig.results if not ok]
    log(f"logs in {WORK}")
    log("all checks passed" if rig.results and not failed else f"failed: {', '.join(failed) or 'setup'}")
    return 0 if rig.results and not failed else 1


if __name__ == "__main__":
    sys.exit(main())
