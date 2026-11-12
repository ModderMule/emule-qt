#!/usr/bin/env python3
"""
ed3knet.py — ed3k Kad rig: bootstrap of fresh networks and LowID-to-LowID transfer behind NAT.

Every node is a `packmule-node` (libs/packmule/crates/packmule-node), built for Linux in a
rust container and mounted into a slim image. All run in the named test network `ed3klab`.

Topology (no RFC 1918 address anywhere: a private address in a named network is adopted as
the own endpoint, Protocol.md §9.2, and would hide the NAT):

    ed3kpub   11.79.0.0/16   one /24 per host, so each is a site of its own (ip-diversity.md
                             §1): pubN 11.79.N.10; pub1-pub12 --force-open (a new network's
                             first routers, operator-declared), pub13-pub14 must earn
                             ROUTER; seed 11.79.20.10 (--no-router: reachable leaf);
                             router A 11.79.21.1, router B 11.79.22.1
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
    5  promotion  of pub13-pub14, started with check 1, one or both became ROUTER by reach
                  tests (§9.2).
                  A PROBE counts only from a host the tester sent nothing to for 60 min, and
                  B never probes a node it is connected to (§9.3): in a network so small
                  that a newcomer's lookups touch every router this takes the hour

Usage:
    python3 docker/ed3k/ed3knet.py --build        # build binary + images, run once
    python3 docker/ed3k/ed3knet.py                # reuse the image, rebuild the binary
    python3 docker/ed3k/ed3knet.py --keep         # leave it running afterwards
    python3 docker/ed3k/ed3knet.py --down         # tear down
"""
import argparse
import hashlib
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
PUB_NET, PUB_SUBNET, PUB = "ed3kpub", "11.79.0.0/16", "11.79"
# name: (container, public IP, LAN network, LAN subnet, docker gateway, router LAN IP)
ROUTERS = {
    "A": (f"{PREFIX}-routerA", f"{PUB}.21.1", "ed3klanA", "12.79.1.0/24", "12.79.1.1", "12.79.1.254"),
    "B": (f"{PREFIX}-routerB", f"{PUB}.22.1", "ed3klanB", "12.79.2.0/24", "12.79.2.1", "12.79.2.254"),
}
PUBLIC = {f"pub{i}": f"{PUB}.{i}.10" for i in range(1, 15)}
FORCED = tuple(f"pub{i}" for i in range(1, 13))
TESTERS = tuple(n for n in PUBLIC if n not in FORCED)
SEED_IP = f"{PUB}.20.10"
# NAT nodes: name -> (router, IP)
NATTED = {"nodeA": ("A", "12.79.1.11"), "nodeB": ("B", "12.79.2.12"), "nodeF": ("B", "12.79.2.13")}
ALL_NODES = [*PUBLIC, "seed", *NATTED]
SHARE_MB = 4
MIN_CONTACTS = 3


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
    names = [container(n) for n in ALL_NODES] + [r[0] for r in ROUTERS.values()]
    existing = docker("ps", "-a", "--format", "{{.Names}}").stdout.split()
    present = [n for n in names if n in existing]
    if present:
        docker("rm", "-f", *present, check=False)
    for net in (PUB_NET, *[r[2] for r in ROUTERS.values()]):
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
        # the operator-declared routers first, each given the ones before it (a lookup returns
        # only contacts closer than the answerer, §10.3: in a network this small a router
        # found nobody else until its hourly bucket refresh, §10.2)
        for i, name in enumerate(FORCED[1:], 1):
            start_node(name, "--force-open", *boot_args(FORCED[:i]))
        ok = wait_for("forced routers know each other", lambda: all(contacts(n) >= 3 for n in FORCED), 90, 1)
        # the others as from a shipped node list: three of them; checked last (promotion)
        for name in TESTERS:
            start_node(name, *boot_args(FORCED[:3]))
        detail = ", ".join(f"{n}: {contacts(n)}" for n in FORCED)
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


def sha256(path: str) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--build", action="store_true", help="rebuild the images too")
    parser.add_argument("--no-binary", action="store_true", help="reuse the built packmule-node")
    parser.add_argument("--keep", action="store_true", help="leave the rig running afterwards")
    parser.add_argument("--timeout", type=int, default=420, help="seconds per check (default 420)")
    parser.add_argument("--down", action="store_true", help="tear the rig down and exit")
    args = parser.parse_args()

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

    create_networks()
    rig = Rig(args.timeout)
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
