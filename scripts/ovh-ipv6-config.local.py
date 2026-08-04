#!/usr/bin/env python3
"""Configure and diagnose outbound IPv6 for the eMule port test on an OVH VPS.

Two independent things must hold before `/wp-json/emqt/v1/porttest` can judge an IPv6 port, and
each one failed here in turn, with the same misleading symptom:

  1. **A route.** OVH assigns an address but never autoconfigures it (see below). Without it the
     probe cannot leave the host and the test reports `serverIpv6: false`.
  2. **Permission to dial the port.** With the route fixed, CSF still blocked outbound 5662, and
     because it rejects rather than drops, the test read that as the *visitor's* port being
     closed. `--check-egress` audits this half; it needs no metadata and runs on any host.

Why this exists
---------------
OVH allocates every VPS an IPv6 address but never autoconfigures it: there is no router
advertisement, so the interface comes up with a link-local `fe80::` address and nothing else.
`emule-qt.org` sat in that state, which made the IPv6 half of the eMule port test
(`/wp-json/emqt/v1/porttest`) structurally unable to answer — it reported
`serverIpv6: false, errno 101 Network is unreachable` because the probe could not leave the host.

The one thing people get wrong
------------------------------
The address is a **/128**. The gateway shares the /64 but a /128 has no on-link neighbours, so the
kernel treats the gateway as unreachable and refuses a plain default route:

    # ip -6 route add default via 2001:41d0:401:3200::1
    Error: Nexthop has invalid gateway.

Both fixes appear in the metadata itself: a link-scope host route to the gateway, and `onlink` on
the default route. This script derives both rather than hardcoding them, so it keeps working if
OVH reassigns the address.

Usage
-----
On the server (reads the metadata service and detects the network stack):

    sudo python3 ovh-ipv6-config.local.py

Firewall audit only — the check worth re-running whenever a port test says a port is closed.
Needs root to read csf.conf and the live rules; exits non-zero if a needed port is blocked:

    sudo python3 ovh-ipv6-config.local.py --check-egress

Offline, or to preview another host's config:

    python3 ovh-ipv6-config.local.py --metadata-file network_data.json --format netplan
    python3 ovh-ipv6-config.local.py --check-egress --csf-conf ./csf.conf
    curl -s http://169.254.169.254/openstack/latest/network_data.json | python3 ovh-ipv6-config.local.py

Nothing is written or applied — the script only prints. Apply the change yourself, from a console
session that survives losing network access (OVH KVM), because a bad route can lock you out.
"""

from __future__ import annotations

import argparse
import glob
import ipaddress
import json
import os
import shutil
import subprocess
import sys
import urllib.request
from dataclasses import dataclass, field

METADATA_URL = "http://169.254.169.254/openstack/latest/network_data.json"
METADATA_TIMEOUT_SEC = 5

# Probed by the port test's capability check, so a working setup makes it flip to serverIpv6: true.
VERIFY_TARGET = "2606:4700:4700::1111"

FORMATS = ("netplan", "networkd", "ifupdown", "nmcli", "iproute")

CSF_CONF = "/etc/csf/csf.conf"

# Ports the port test must be allowed to dial *outbound*. It connects to the visitor's client, so
# every port a visitor might run eMule on has to be permitted; these are the defaults plus the
# eMuleQt test rig. A port missing here is a port the tester structurally cannot check.
EGRESS_PORTS: tuple[tuple[str, int, str], ...] = (
    ("tcp", 4662, "eMule default TCP"),
    ("udp", 4672, "eMule default UDP"),
    ("tcp", 5662, "eMuleQt test rig TCP"),
    ("udp", 5672, "eMuleQt test rig UDP"),
)

# CSF key per (protocol, family). TCP6_OUT/UDP6_OUT only take effect when IPV6 = "1".
CSF_OUT_KEYS = {
    ("tcp", 4): "TCP_OUT",
    ("udp", 4): "UDP_OUT",
    ("tcp", 6): "TCP6_OUT",
    ("udp", 6): "UDP6_OUT",
}


@dataclass
class Ipv6Config:
    """Everything needed to configure IPv6, as derived from the metadata."""

    address: str
    prefix: int
    gateway: str
    onlink_routes: list[str] = field(default_factory=list)
    nameservers: list[str] = field(default_factory=list)
    mac: str = ""
    interface: str = ""

    @property
    def cidr(self) -> str:
        return f"{self.address}/{self.prefix}"

    @property
    def gateway_is_offlink(self) -> bool:
        """Whether the gateway falls outside the configured address's own prefix.

        True for every OVH /128, and the reason `onlink` is not optional.
        """
        net = ipaddress.ip_network(f"{self.address}/{self.prefix}", strict=False)
        return ipaddress.ip_address(self.gateway) not in net


# ---------------------------------------------------------------------------
# Metadata
# ---------------------------------------------------------------------------


def load_metadata(path: str | None) -> dict:
    """Read network_data.json from a file, stdin, or the metadata service."""
    if path:
        with open(path, encoding="utf-8") as handle:
            return json.load(handle)

    if not sys.stdin.isatty():
        piped = sys.stdin.read().strip()
        if piped:
            return json.loads(piped)

    try:
        with urllib.request.urlopen(METADATA_URL, timeout=METADATA_TIMEOUT_SEC) as response:
            return json.loads(response.read().decode("utf-8"))
    except Exception as exc:  # noqa: BLE001 - any failure means "not on the VPS"
        raise SystemExit(
            f"Could not read {METADATA_URL} ({exc}).\n"
            "Run this on the VPS, or pass --metadata-file with a saved copy."
        ) from exc


def prefix_from_netmask(netmask: str) -> int:
    """Convert an IPv6 netmask to a prefix length. OVH sends an all-ones mask, i.e. /128."""
    packed = ipaddress.IPv6Address(netmask).packed
    return sum(bin(byte).count("1") for byte in packed)


def parse_metadata(meta: dict) -> Ipv6Config:
    """Pull the IPv6 network out of the metadata document."""
    networks = [n for n in meta.get("networks", []) if n.get("type", "").startswith("ipv6")]
    if not networks:
        raise SystemExit(
            "No IPv6 network in the metadata. This VPS may genuinely have no IPv6 assigned —\n"
            "check OVH Manager > your VPS > IP before going further."
        )
    if len(networks) > 1:
        print(f"# note: {len(networks)} IPv6 networks present, using the first", file=sys.stderr)

    network = networks[0]
    address = network.get("ip_address", "")
    if not address:
        raise SystemExit("IPv6 network has no ip_address field.")

    if network.get("netmask"):
        prefix = prefix_from_netmask(network["netmask"])
    else:
        prefix = int(network.get("prefix", 128))

    gateway = ""
    onlink_routes: list[str] = []
    for route in network.get("routes", []):
        dest = route.get("network", "")
        via = route.get("gateway", "")
        # The default route carries the real gateway.
        if dest in ("::", "::/0") and via not in ("", "::"):
            gateway = via
            continue
        # gateway "::" means "reachable on this link" — the host route that makes the /128 work.
        if via in ("", "::") and dest not in ("::", "::/0"):
            onlink_routes.append(f"{dest}/{route.get('prefix', 128)}")

    if not gateway:
        raise SystemExit("Metadata has no IPv6 default gateway; cannot build a config.")

    mac = ""
    link_id = network.get("link", "")
    for link in meta.get("links", []):
        if link.get("id") == link_id:
            mac = link.get("ethernet_mac_address", "")
            break

    nameservers = [
        service["address"]
        for service in meta.get("services", [])
        if service.get("type") == "dns" and service.get("address")
    ]

    return Ipv6Config(
        address=address,
        prefix=prefix,
        gateway=gateway,
        onlink_routes=onlink_routes,
        nameservers=nameservers,
        mac=mac,
    )


# ---------------------------------------------------------------------------
# Host inspection
# ---------------------------------------------------------------------------


def find_interface(mac: str) -> str:
    """Resolve the interface name from the metadata MAC, so `ens3` is never assumed."""
    if not mac:
        return ""
    for path in sorted(glob.glob("/sys/class/net/*/address")):
        try:
            with open(path, encoding="utf-8") as handle:
                if handle.read().strip().lower() == mac.lower():
                    return os.path.basename(os.path.dirname(path))
        except OSError:
            continue
    return ""


def detect_format() -> str:
    """Guess which network stack manages this host. Order matters: most specific first."""
    if shutil.which("netplan") and glob.glob("/etc/netplan/*.yaml"):
        return "netplan"
    if _unit_active("systemd-networkd"):
        return "networkd"
    if shutil.which("nmcli") and _unit_active("NetworkManager"):
        return "nmcli"
    if os.path.exists("/etc/network/interfaces"):
        return "ifupdown"
    return "netplan"


def _unit_active(unit: str) -> bool:
    try:
        result = subprocess.run(
            ["systemctl", "is-active", unit],
            capture_output=True,
            text=True,
            timeout=5,
            check=False,
        )
        return result.stdout.strip() == "active"
    except (OSError, subprocess.SubprocessError):
        return False


# ---------------------------------------------------------------------------
# Egress firewall audit
# ---------------------------------------------------------------------------
#
# A route is only half the problem. Once emule-qt.org had working IPv6 the port test still
# reported TCP 5662 "closed", because CSF ran `-P OUTPUT DROP` with everything not on its allow
# list sent to `REJECT --reject-with icmp6-port-unreachable`, and 5662 was not on that list.
#
# Two things made that hard to see:
#   * CSF exempts root (`--uid-owner 0`), so `nc -6 -vz host 5662` as root *succeeds* while PHP
#     running as the web user fails. Testing by hand from a root shell proves nothing.
#   * A REJECT surfaces to the caller as ECONNREFUSED, which is indistinguishable from a real
#     remote refusal — so the tester blames the visitor's firewall for the test server's own.
#
# Hence this audit: read the intent from CSF's config and the live rules, and say plainly which
# ports the tester can and cannot reach.


@dataclass
class EgressFinding:
    """Whether one protocol/port/family combination may leave this host."""

    proto: str
    port: int
    family: int
    label: str
    allowed: bool | None  # None: undetermined
    detail: str

    @property
    def status(self) -> str:
        if self.allowed is None:
            return "UNKNOWN"
        return "ok" if self.allowed else "BLOCKED"


@dataclass
class EgressAudit:
    findings: list[EgressFinding] = field(default_factory=list)
    csf_path: str = CSF_CONF
    csf_present: bool = False
    csf_readable: bool = False
    csf_ipv6: bool | None = None
    policies: dict[int, str] = field(default_factory=dict)
    reject_targets: dict[int, str] = field(default_factory=dict)
    root_exempt: dict[int, bool] = field(default_factory=dict)
    rules: dict[int, list[str]] = field(default_factory=dict)
    warnings: list[str] = field(default_factory=list)

    @property
    def blocked(self) -> list[EgressFinding]:
        return [f for f in self.findings if f.allowed is False]

    @property
    def unknown(self) -> list[EgressFinding]:
        return [f for f in self.findings if f.allowed is None]

    @property
    def clean(self) -> bool:
        return not self.blocked and not self.unknown and bool(self.findings)


def parse_port_list(value: str) -> list[tuple[int, int]]:
    """Parse a CSF port list (`20,21,35000:35999`) into inclusive ranges."""
    ranges: list[tuple[int, int]] = []
    for chunk in value.replace(" ", "").split(","):
        if not chunk:
            continue
        try:
            if ":" in chunk:
                low, high = chunk.split(":", 1)
                ranges.append((int(low), int(high)))
            else:
                ranges.append((int(chunk), int(chunk)))
        except ValueError:
            continue  # a malformed entry is CSF's problem to report, not ours to guess at
    return ranges


def port_in_ranges(port: int, ranges: list[tuple[int, int]]) -> bool:
    return any(low <= port <= high for low, high in ranges)


def read_csf_conf(path: str = CSF_CONF) -> dict[str, str] | None:
    """Parse `KEY = "value"` lines out of csf.conf. None if absent or unreadable."""
    if not os.path.exists(path):
        return None
    try:
        with open(path, encoding="utf-8", errors="replace") as handle:
            lines = handle.readlines()
    except OSError:
        return None

    config: dict[str, str] = {}
    for line in lines:
        line = line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, _, raw = line.partition("=")
        config[key.strip()] = raw.strip().strip('"').strip("'")
    return config


def iptables_output_rules(family: int) -> list[str] | None:
    """`iptables -S` / `ip6tables -S` as a list of rules. None if unavailable."""
    binary = "ip6tables" if family == 6 else "iptables"
    if not shutil.which(binary):
        return None
    try:
        result = subprocess.run(
            [binary, "-S"], capture_output=True, text=True, timeout=15, check=False
        )
    except (OSError, subprocess.SubprocessError):
        return None
    if result.returncode != 0:
        return None
    return result.stdout.splitlines()


def audit_egress(csf_path: str = CSF_CONF) -> EgressAudit:
    """Report which of the port test's outbound ports this host actually permits."""
    audit = EgressAudit(csf_path=csf_path)
    csf = read_csf_conf(csf_path)
    audit.csf_present = os.path.exists(csf_path)
    audit.csf_readable = csf is not None

    if audit.csf_present and not audit.csf_readable:
        audit.warnings.append(
            f"{csf_path} exists but could not be read — re-run with sudo for a verdict."
        )
    if csf is not None:
        audit.csf_ipv6 = csf.get("IPV6", "0") == "1"

    for family in (4, 6):
        rules = iptables_output_rules(family)
        if rules is None:
            audit.warnings.append(
                f"Could not read {'ip6tables' if family == 6 else 'iptables'} rules"
                " (missing binary or not root) — live rules unverified."
            )
            continue
        audit.rules[family] = rules
        for rule in rules:
            if rule.startswith("-P OUTPUT "):
                audit.policies[family] = rule.split()[-1]
            if "--reject-with" in rule and "OUTPUT" not in rule.split()[:2]:
                audit.reject_targets[family] = rule.split("--reject-with", 1)[1].strip().split()[0]
            if "--uid-owner 0" in rule and "ACCEPT" in rule:
                audit.root_exempt[family] = True

    for proto, port, label in EGRESS_PORTS:
        for family in (4, 6):
            audit.findings.append(_audit_one_port(audit, csf, proto, port, family, label))

    _add_egress_warnings(audit)
    return audit


def print_egress_report(audit: EgressAudit) -> None:
    """Print the egress section. Loud when blocked, brief when clean."""
    print("-" * 78)
    print("EGRESS FIREWALL (can the port test dial back out?)")
    print("-" * 78)

    if audit.csf_present:
        state = "unreadable" if not audit.csf_readable else (
            "IPv6 enabled" if audit.csf_ipv6 else "IPv6 DISABLED"
        )
        print(f"  CSF detected at {audit.csf_path} ({state})")
    else:
        print(f"  No CSF at {audit.csf_path} — treating plain iptables as authoritative.")
    for family in (4, 6):
        policy = audit.policies.get(family)
        if policy:
            reject = audit.reject_targets.get(family)
            suffix = f", non-allowed traffic REJECTed as {reject}" if reject else ""
            print(f"  IPv{family} OUTPUT policy: {policy}{suffix}")
    print()

    width = max(len(f.label) for f in audit.findings) if audit.findings else 0
    for finding in audit.findings:
        mark = {"ok": "  ok  ", "BLOCKED": " FAIL ", "UNKNOWN": "  ??  "}[finding.status]
        print(
            f"  [{mark}] IPv{finding.family} {finding.proto.upper():3} {finding.port:<5}"
            f"  {finding.label:<{width}}  {finding.detail}"
        )
    print()

    if audit.clean:
        print("  All ports the port test needs are permitted outbound.")
        print()
    for warning in audit.warnings:
        for line in _wrap(warning, 74):
            print(f"  {line}")
        print()

    if audit.blocked and audit.csf_readable:
        print(f"  FIX (CSF) — edit {audit.csf_path}, keeping the existing entries:")
        for key, ports in _csf_fix_lines(audit).items():
            print(f'    {key:<9}= "...,{ports}"')
        print("    then: sudo csf -r")
        print()
    elif audit.blocked:
        print("  FIX — allow these ports outbound in whatever manages your OUTPUT chain.")
        print()

    print("  Verify as the *web* user, never as root (root is usually exempt):")
    print("    sudo -u www-data curl -6 -sS --max-time 5 -o /dev/null \\")
    print("         'http://[2606:4700:4700::1111]:5662' ; echo \"exit=$?\"")
    print("  Or end to end, from any machine with the client running:")
    print("    curl -6 -sS 'https://emule-qt.org/wp-json/emqt/v1/porttest"
          "?tcpport=5662&udpport=5672'")
    print("  A 'server_egress_blocked' reason in that response means this section, not the")
    print("  visitor's firewall.")
    print()


# ---------------------------------------------------------------------------
# Config emitters
# ---------------------------------------------------------------------------


def emit_netplan(cfg: Ipv6Config) -> tuple[str, str]:
    routes = []
    for route in cfg.onlink_routes:
        routes.append(f'        - to: "{route}"\n          scope: link')
    # "::/0" rather than "default": this interface carries both families, and older netplan
    # resolves a bare "default" by looking at `via`, which is a needless ambiguity here.
    routes.append(f'        - to: "::/0"\n          via: "{cfg.gateway}"\n          on-link: true')

    dns = ""
    if cfg.nameservers:
        joined = ", ".join(f'"{ns}"' for ns in cfg.nameservers)
        dns = f"      nameservers:\n        addresses: [{joined}]\n"

    body = (
        "network:\n"
        "  version: 2\n"
        "  ethernets:\n"
        f"    {cfg.interface}:\n"
        "      dhcp4: true\n"
        "      addresses:\n"
        f'        - "{cfg.cidr}"\n'
        "      routes:\n" + "\n".join(routes) + "\n" + dns
    )
    return "/etc/netplan/60-ipv6.yaml", body


def emit_networkd(cfg: Ipv6Config) -> tuple[str, str]:
    routes = "".join(
        f"\n[Route]\nDestination={route}\nScope=link\n" for route in cfg.onlink_routes
    )
    dns = "".join(f"DNS={ns}\n" for ns in cfg.nameservers)
    body = (
        "[Match]\n"
        f"Name={cfg.interface}\n"
        "\n[Network]\n"
        "DHCP=ipv4\n"
        f"Address={cfg.cidr}\n"
        f"{dns}"
        f"{routes}"
        "\n[Route]\n"
        "Destination=::/0\n"
        f"Gateway={cfg.gateway}\n"
        "GatewayOnLink=yes\n"
    )
    return f"/etc/systemd/network/60-{cfg.interface}-ipv6.network", body


def emit_ifupdown(cfg: Ipv6Config) -> tuple[str, str]:
    ups = [f"    up ip -6 route add {route} dev {cfg.interface} 2>/dev/null || true"
           for route in cfg.onlink_routes]
    body = (
        f"iface {cfg.interface} inet6 static\n"
        f"    address {cfg.address}\n"
        f"    netmask {cfg.prefix}\n"
        + ("\n".join(ups) + "\n" if ups else "")
        + f"    up ip -6 route add default via {cfg.gateway} dev {cfg.interface} onlink\n"
        f"    down ip -6 route del default via {cfg.gateway} dev {cfg.interface} onlink 2>/dev/null || true\n"
    )
    return "/etc/network/interfaces.d/60-ipv6", body


def emit_nmcli(cfg: Ipv6Config) -> tuple[str, str]:
    dns = f'nmcli con mod "$CON" ipv6.dns "{",".join(cfg.nameservers)}"\n' if cfg.nameservers else ""
    body = (
        f'CON=$(nmcli -g GENERAL.CONNECTION dev show {cfg.interface})\n'
        f'nmcli con mod "$CON" ipv6.method manual ipv6.addresses "{cfg.cidr}"\n'
        f'nmcli con mod "$CON" ipv6.gateway "{cfg.gateway}"\n'
        f"{dns}"
        f'nmcli con up "$CON"\n'
    )
    return "(run these commands)", body


def emit_iproute(cfg: Ipv6Config) -> tuple[str, str]:
    lines = [f"ip -6 addr add {cfg.cidr} dev {cfg.interface}"]
    lines += [f"ip -6 route add {route} dev {cfg.interface}" for route in cfg.onlink_routes]
    lines.append(f"ip -6 route add default via {cfg.gateway} dev {cfg.interface} onlink")
    return "(temporary, lost on reboot)", "\n".join(lines) + "\n"


EMITTERS = {
    "netplan": emit_netplan,
    "networkd": emit_networkd,
    "ifupdown": emit_ifupdown,
    "nmcli": emit_nmcli,
    "iproute": emit_iproute,
}

APPLY = {
    # chmod first: netplan warns about world-readable files and newer versions refuse to load them.
    "netplan": "sudo chmod 600 {path}\n  sudo netplan generate && sudo netplan apply",
    "networkd": "sudo chmod 644 {path}\n  sudo systemctl restart systemd-networkd",
    "ifupdown": "sudo ifdown --force {iface}; sudo ifup {iface}",
    "nmcli": "(applied by the commands above)",
    "iproute": "(applied immediately; re-run after reboot or write a persistent config instead)",
}

NOTES = {
    "netplan": [
        "Keep this in its own file. cloud-init rewrites /etc/netplan/50-cloud-init.yaml on boot,",
        "and netplan merges by interface with the higher-numbered file winning, so a separate",
        "60- file survives while a hand-edit of the 50- file would not.",
    ],
    "ifupdown": [
        "ifupdown treats the two address families as separate stanzas. If IPv6 does not come up",
        "after a reboot, make sure the interface is started for inet6 as well — add",
        "'auto {iface}' alongside the existing IPv4 stanza, or use 'sudo ifup {iface}=inet6'.",
    ],
    "iproute": [
        "This is for testing the route before committing to it. Nothing here survives a reboot;",
        "re-run with --format netplan (or networkd) once it is known to work.",
    ],
}


# ---------------------------------------------------------------------------
# Output
# ---------------------------------------------------------------------------


def print_report(cfg: Ipv6Config, fmt: str, audit: EgressAudit | None = None) -> None:
    path, body = EMITTERS[fmt](cfg)

    print("=" * 78)
    print("OVH VPS IPv6 configuration")
    print("=" * 78)
    print(f"  interface        {cfg.interface}" + (f"  (MAC {cfg.mac})" if cfg.mac else ""))
    print(f"  address          {cfg.cidr}")
    print(f"  gateway          {cfg.gateway}")
    print(f"  on-link routes   {', '.join(cfg.onlink_routes) or '(none in metadata)'}")
    print(f"  nameservers      {', '.join(cfg.nameservers) or '(none in metadata)'}")
    print(f"  target stack     {fmt}")
    print()

    if cfg.gateway_is_offlink:
        print("  The gateway lies outside the configured prefix, so it has no on-link route of")
        print("  its own. A plain default route is rejected with 'Nexthop has invalid gateway'.")
        print("  Hence the link-scope host route plus onlink on the default route below —")
        print("  omitting either is the usual reason OVH IPv6 setups silently fail.")
        print()

    print("-" * 78)
    print(f"WRITE THIS TO: {path}")
    print("-" * 78)
    print(body)

    print("-" * 78)
    print("APPLY")
    print("-" * 78)
    print(f"  {APPLY[fmt].format(iface=cfg.interface, path=path)}")
    print()
    print("  Do this from the OVH KVM console, not over SSH: a wrong route drops the network")
    print("  and you would lose the session you need in order to undo it.")
    print()
    for line in NOTES.get(fmt, []):
        print(f"  {line.format(iface=cfg.interface)}")
    if fmt in NOTES:
        print()

    print("-" * 78)
    print("VERIFY")
    print("-" * 78)
    print(f"  ip -6 addr show dev {cfg.interface}      # expect {cfg.address}, not just fe80::")
    print("  ip -6 route show                          # expect a default via the gateway")
    print(f"  ping6 -c2 {cfg.gateway}")
    print(f"  ping6 -c2 {VERIFY_TARGET}")
    print("  curl -6 -sS https://ifconfig.co            # expect the address above")
    print()
    print("  Then, from anywhere, confirm the port test agrees:")
    print("    curl -6 -sS 'https://emule-qt.org/wp-json/emqt/v1/porttest?tcpport=5662&udpport=5672'")
    print("  serverIpv6 must flip to true. A failed capability check is cached for only 5")
    print("  minutes, so no cache needs clearing — just wait that long.")
    print()
    print("  serverIpv6: true only proves a route exists. The firewall audit below is the")
    print("  second half — a routed host can still be barred from the ports it must dial.")
    print()

    if audit is not None:
        print_egress_report(audit)

    print("-" * 78)
    print("ROLLBACK")
    print("-" * 78)
    if fmt == "netplan":
        print(f"  sudo rm {path} && sudo netplan apply")
    elif fmt == "networkd":
        print(f"  sudo rm {path} && sudo systemctl restart systemd-networkd")
    elif fmt == "ifupdown":
        print(f"  sudo rm {path} && sudo ifdown --force {cfg.interface}; sudo ifup {cfg.interface}")
    else:
        print(f"  ip -6 addr del {cfg.cidr} dev {cfg.interface}")
        print(f"  ip -6 route del default via {cfg.gateway} dev {cfg.interface}")
    print()
    print("  IPv4 is untouched by all of the above, so the site stays reachable either way.")
    print("=" * 78)


# ---------------------------------------------------------------------------
# Private helpers
# ---------------------------------------------------------------------------


def _audit_one_port(
    audit: EgressAudit,
    csf: dict[str, str] | None,
    proto: str,
    port: int,
    family: int,
    label: str,
) -> EgressFinding:
    """Decide one protocol/port/family case, preferring CSF's config as the intent."""
    key = CSF_OUT_KEYS[(proto, family)]

    # CSF only writes ip6tables when IPV6 = "1"; with it off, its *6_OUT keys are inert and the
    # live chain is what counts, so fall through to the rule check instead of trusting the list.
    if csf is not None and (family == 4 or audit.csf_ipv6):
        if key in csf:
            ranges = parse_port_list(csf[key])
            allowed = port_in_ranges(port, ranges)
            return EgressFinding(
                proto, port, family, label, allowed,
                f"{key} {'covers' if allowed else 'does NOT list'} this port",
            )
        return EgressFinding(
            proto, port, family, label, None, f"{key} absent from csf.conf",
        )

    policy = audit.policies.get(family)
    if policy == "ACCEPT":
        return EgressFinding(proto, port, family, label, True, "OUTPUT policy is ACCEPT")
    if policy is None:
        return EgressFinding(proto, port, family, label, None, "OUTPUT policy unknown")

    if _rules_accept_port(audit.rules.get(family, []), proto, port):
        return EgressFinding(
            proto, port, family, label, True, f"explicit ACCEPT rule (policy {policy})",
        )
    return EgressFinding(
        proto, port, family, label, False, f"no ACCEPT rule and policy is {policy}",
    )


def _rules_accept_port(rules: list[str], proto: str, port: int) -> bool:
    """Whether some rule accepts this destination port.

    Deliberately shallow: it does not follow jumps or evaluate rule order, so it can only ever
    say "an allowance exists". That is enough to avoid crying wolf when CSF is not the manager,
    and every stricter claim is left to CSF's own config above.
    """
    needles = (f"--dport {port}", f"--dports {port}", f":{port}", f",{port},")
    for rule in rules:
        if "ACCEPT" not in rule or f"-p {proto}" not in rule:
            continue
        if any(needle in rule for needle in needles):
            return True
    return False


def _add_egress_warnings(audit: EgressAudit) -> None:
    """Attach the interpretation notes: what a block will look like, and stale-config traps."""
    for family, target in audit.reject_targets.items():
        if "unreachable" in target and audit.blocked:
            audit.warnings.append(
                f"IPv{family} blocks are REJECTed as {target}, which reaches the caller as"
                " ECONNREFUSED — identical to a genuine remote refusal. The port test"
                " disambiguates this by probing a documentation address on the same port"
                " (an address that can never refuse anything), and reports"
                " reason 'server_egress_blocked'."
            )
            break

    if any(audit.root_exempt.values()):
        audit.warnings.append(
            "The OUTPUT chain exempts uid 0, so nc/curl/ping run as root will succeed on ports"
            " that are blocked for the web user. Verify as the web user or the result is"
            " meaningless."
        )

    # CSF says yes, the live chain has no matching allowance: almost always an unapplied edit.
    for finding in audit.findings:
        if finding.allowed is not True or finding.family not in audit.rules:
            continue
        policy = audit.policies.get(finding.family)
        if policy in (None, "ACCEPT"):
            continue
        if not _rules_accept_port(audit.rules[finding.family], finding.proto, finding.port):
            audit.warnings.append(
                "csf.conf permits ports that the live chain does not — the edit looks unapplied."
                " Run `sudo csf -r`, then re-run this check."
            )
            break

    if audit.csf_present and audit.csf_readable and audit.csf_ipv6 is False:
        audit.warnings.append(
            'CSF has IPV6 = "0", so it is not filtering IPv6 at all. Any ip6tables rules come'
            " from elsewhere and CSF's TCP6_OUT/UDP6_OUT lists have no effect."
        )


def _csf_fix_lines(audit: EgressAudit) -> dict[str, str]:
    """Group the blocked ports by the csf.conf key that needs to gain them."""
    fixes: dict[str, list[str]] = {}
    for finding in audit.blocked:
        key = CSF_OUT_KEYS[(finding.proto, finding.family)]
        fixes.setdefault(key, [])
        if str(finding.port) not in fixes[key]:
            fixes[key].append(str(finding.port))
    return {key: ",".join(ports) for key, ports in fixes.items()}


def _wrap(text: str, width: int) -> list[str]:
    """Wrap to `width` without pulling in textwrap for one call site."""
    lines: list[str] = []
    current = ""
    for word in text.split():
        if current and len(current) + 1 + len(word) > width:
            lines.append(current)
            current = word
        else:
            current = f"{current} {word}".strip()
    if current:
        lines.append(current)
    return lines


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Print the IPv6 config for an OVH VPS, derived from its OpenStack metadata.",
        epilog="Prints only; never writes or applies anything.",
    )
    parser.add_argument("--metadata-file", help="Saved network_data.json instead of the metadata service.")
    parser.add_argument("--format", choices=FORMATS, help="Target network stack (default: autodetect).")
    parser.add_argument("--interface", help="Override the interface name (default: match the metadata MAC).")
    parser.add_argument("--csf-conf", default=CSF_CONF, help=f"csf.conf to audit (default: {CSF_CONF}).")
    parser.add_argument(
        "--check-egress",
        action="store_true",
        help="Only audit the outbound firewall; skip the address/route config entirely. "
             "Exit 0 all clear, 1 a port is blocked, 2 undetermined.",
    )
    parser.add_argument(
        "--skip-egress", action="store_true", help="Only print the address/route config."
    )
    args = parser.parse_args()

    # Once the routes are in place this is the check worth repeating, and it needs no metadata —
    # so it runs standalone, on any host, including one that never had an IPv6 problem.
    if args.check_egress:
        audit = audit_egress(args.csf_conf)
        print("=" * 78)
        print("Outbound firewall audit for the eMule port test")
        print("=" * 78)
        print_egress_report(audit)
        print("=" * 78)
        # 2 rather than 0 when nothing could be determined: "no block found" and "no evidence
        # either way" must not look alike to a caller, or an unreadable config reads as healthy.
        if audit.blocked:
            return 1
        return 0 if audit.clean else 2

    cfg = parse_metadata(load_metadata(args.metadata_file))

    cfg.interface = args.interface or find_interface(cfg.mac)
    if not cfg.interface:
        cfg.interface = "ens3"
        print(
            f"# note: no interface matches MAC {cfg.mac or '(unknown)'} on this machine; "
            "assuming ens3.\n#       Pass --interface to override.",
            file=sys.stderr,
        )

    audit = None if args.skip_egress else audit_egress(args.csf_conf)
    print_report(cfg, args.format or detect_format(), audit)
    return 0


if __name__ == "__main__":
    sys.exit(main())
