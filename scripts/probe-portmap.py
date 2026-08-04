#!/usr/bin/env python3
"""Probe the local router for port-mapping protocol support.

Phase 0 of the multi-protocol port-mapping work: answers, empirically, whether
this network's gateway speaks PCP (RFC 6887), NAT-PMP (RFC 6886) or only
UPnP IGD -- so the backend priority is chosen from measurement rather than from
vendor documentation.

Standalone: Python 3 stdlib only, no build, no dependencies.

    scripts/probe-portmap.py                  # probe the default gateway
    scripts/probe-portmap.py --gateway 192.168.178.1
    scripts/probe-portmap.py --no-map         # read-only, never creates a mapping
    scripts/probe-portmap.py --skip-upnp      # PCP/NAT-PMP only

By default the PCP/NAT-PMP MAP probes DO create a real mapping (short lease on a
random high port) and then delete it again, because a MAP round-trip is the only
way to learn the assigned external port and whether the router honours the
requested port. Use --no-map to stay strictly read-only: PCP ANNOUNCE and the
NAT-PMP external-address request change no state on the router.
"""

from __future__ import annotations

import argparse
import ipaddress
import os
import random
import re
import socket
import struct
import subprocess
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
import xml.etree.ElementTree as ET

# --------------------------------------------------------------------------
# Constants
# --------------------------------------------------------------------------

PORTMAP_PORT = 5351          # PCP + NAT-PMP server port
ANNOUNCE_PORT = 5350         # PCP/NAT-PMP unsolicited announcements
SSDP_ADDR_V4 = "239.255.255.250"
SSDP_ADDR_V6 = "ff02::c"
SSDP_PORT = 1900

PCP_VERSION = 2
PCP_OP_ANNOUNCE = 0
PCP_OP_MAP = 1
PCP_HEADER_SIZE = 24
PCP_MAP_PACKET_SIZE = 60
PCP_RESPONSE_BIT = 0x80

NATPMP_VERSION = 0
NATPMP_OP_EXTERNAL = 0
NATPMP_OP_MAP_UDP = 1
NATPMP_OP_MAP_TCP = 2

PROTO_TCP = 6
PROTO_UDP = 17

# RFC 6887 section 5: the address-family-specific all-zeros addresses.
PCP_ANY_IPV4 = bytes.fromhex("00000000000000000000ffff00000000")   # ::ffff:0:0
PCP_ANY_IPV6 = bytes(16)                                           # ::

PCP_RESULTS = {
    0: "SUCCESS", 1: "UNSUPP_VERSION", 2: "NOT_AUTHORIZED",
    3: "MALFORMED_REQUEST", 4: "UNSUPP_OPCODE", 5: "UNSUPP_OPTION",
    6: "MALFORMED_OPTION", 7: "NETWORK_FAILURE", 8: "NO_RESOURCES",
    9: "UNSUPP_PROTOCOL", 10: "USER_EX_QUOTA", 11: "CANNOT_PROVIDE_EXTERNAL",
    12: "ADDRESS_MISMATCH", 13: "EXCESSIVE_REMOTE_PEERS",
}

NATPMP_RESULTS = {
    0: "Success", 1: "UnsupportedVersion", 2: "NotAuthorized",
    3: "NetworkFailure", 4: "OutOfResources", 5: "UnsupportedOpcode",
}

IPV6_FIREWALL_SERVICE = "urn:schemas-upnp-org:service:WANIPv6FirewallControl:1"

# --------------------------------------------------------------------------
# Output helpers
# --------------------------------------------------------------------------

_USE_COLOR = sys.stdout.isatty() and os.environ.get("NO_COLOR") is None


def _c(text: str, code: str) -> str:
    return f"\033[{code}m{text}\033[0m" if _USE_COLOR else text


def head(text: str) -> None:
    print(f"\n{_c('=== ' + text + ' ===', '1;36')}")


def ok(text: str) -> None:
    print(f"  {_c('OK  ', '1;32')} {text}")


def bad(text: str) -> None:
    print(f"  {_c('--  ', '1;31')} {text}")


def info(text: str) -> None:
    print(f"       {text}")


def hexdump(data: bytes, indent: str = "       ") -> str:
    """Annotated hex, 16 bytes per line with byte offsets."""
    lines = []
    for off in range(0, len(data), 16):
        chunk = data[off:off + 16]
        cols = " ".join(f"{b:02x}" for b in chunk)
        lines.append(f"{indent}{off:3d}: {cols}")
    return "\n".join(lines)


# --------------------------------------------------------------------------
# Default gateway discovery
# --------------------------------------------------------------------------

def _run(cmd: list[str]) -> str:
    try:
        out = subprocess.run(cmd, capture_output=True, text=True, timeout=5)
        return out.stdout
    except (OSError, subprocess.SubprocessError):
        return ""


def strip_scope(addr: str) -> str:
    return addr.split("%", 1)[0]


def gateways_via_proc(v6: bool) -> list[str]:
    """Linux: parse /proc/net/route (v4) or /proc/net/ipv6_route (v6)."""
    found: list[str] = []
    if not v6:
        try:
            with open("/proc/net/route", "r") as fh:
                next(fh, None)
                for line in fh:
                    f = line.split()
                    # Destination 00000000 + RTF_UP|RTF_GATEWAY (0x3)
                    if len(f) > 3 and f[1] == "00000000" and int(f[3], 16) & 0x2:
                        raw = struct.pack("<I", int(f[2], 16))
                        found.append(str(ipaddress.IPv4Address(raw)))
        except (OSError, ValueError):
            pass
    else:
        try:
            with open("/proc/net/ipv6_route", "r") as fh:
                for line in fh:
                    f = line.split()
                    # dest prefixlen == 0 and a non-zero next hop
                    if len(f) > 4 and f[1] == "00" and f[4] != "0" * 32:
                        found.append(str(ipaddress.IPv6Address(bytes.fromhex(f[4]))))
        except (OSError, ValueError):
            pass
    return found


def gateways_via_route_cmd(v6: bool) -> list[str]:
    """macOS/BSD: `route -n get [-inet6] default`."""
    cmd = ["route", "-n", "get"] + (["-inet6"] if v6 else []) + ["default"]
    text = _run(cmd)
    # Keep the %scope suffix -- an IPv6 default route is normally via a
    # link-local next hop, which is unusable without its zone index.
    return [m.group(1) for m in re.finditer(r"gateway:\s*(\S+)", text)]


def gateways_via_netstat(v6: bool) -> list[str]:
    """Fallback: `netstat -rn -f inet[6]`, take the `default` row's gateway."""
    text = _run(["netstat", "-rn", "-f", "inet6" if v6 else "inet"])
    found = []
    for line in text.splitlines():
        f = line.split()
        if len(f) >= 2 and f[0] in ("default", "::/0", "0.0.0.0/0", "0.0.0.0"):
            found.append(f[1])
    return found


def global_ipv6_addresses() -> list[str]:
    """Our own global-unicast IPv6 addresses, stable ones first."""
    stable, temporary = [], []
    text = _run(["ifconfig"])
    for line in text.splitlines():
        f = line.split()
        if len(f) < 2 or f[0] != "inet6":
            continue
        addr = strip_scope(f[1])
        try:
            parsed = ipaddress.IPv6Address(addr)
        except ValueError:
            continue
        if not parsed.is_global or parsed.is_link_local:
            continue
        (temporary if "temporary" in line else stable).append(addr)
    return stable + temporary


def find_gateways(v6: bool) -> list[str]:
    """Best-first gateway candidates. Empty means 'do not send'."""
    seen: list[str] = []

    def add(addr: str) -> None:
        try:
            parsed = ipaddress.ip_address(strip_scope(addr))
        except ValueError:
            return
        if parsed.version != (6 if v6 else 4):
            return
        if addr not in seen:
            seen.append(addr)

    # An IPv6 PCP exchange has to be sourced from our GUA (RFC 6887 section 8.4),
    # and the source address must match the Client IP field or the server answers
    # ADDRESS_MISMATCH. Sending to a link-local next hop forces a link-local
    # source, so try the router's global address first -- conventionally
    # <our prefix>::1 -- and keep the link-local next hop as a fallback.
    if v6:
        for own in global_ipv6_addresses():
            net = ipaddress.IPv6Network(f"{own}/64", strict=False)
            add(str(net.network_address + 1))

    for probe in (gateways_via_proc, gateways_via_route_cmd, gateways_via_netstat):
        for addr in probe(v6):
            add(addr)
    return seen


def _sockaddr(host: str, port: int):
    """Resolve to a sockaddr, preserving an IPv6 zone index."""
    infos = socket.getaddrinfo(host, port, proto=socket.IPPROTO_UDP)
    return infos[0][0], infos[0][4]


def local_address_towards(gateway: str) -> str | None:
    """The source address the kernel picks for this gateway (PCP client-IP field)."""
    try:
        fam, sa = _sockaddr(gateway, PORTMAP_PORT)
        with socket.socket(fam, socket.SOCK_DGRAM) as s:
            s.connect(sa)
            return strip_scope(s.getsockname()[0])
    except OSError:
        return None


# --------------------------------------------------------------------------
# Packet builders
# --------------------------------------------------------------------------

def encode_addr128(addr: str | None, want_ipv4_family: bool = True) -> bytes:
    """RFC 6887 section 5: 128-bit address. IPv4 -> IPv4-mapped."""
    if addr is None:
        return PCP_ANY_IPV4 if want_ipv4_family else PCP_ANY_IPV6
    parsed = ipaddress.ip_address(addr)
    if parsed.version == 6:
        return parsed.packed
    return bytes.fromhex("00000000000000000000ffff") + parsed.packed


def decode_addr128(raw: bytes) -> str:
    """Inverse. Checks all 96 leading bits for the v4-mapped pattern."""
    addr = ipaddress.ip_address(raw)
    if raw[:12] == bytes.fromhex("00000000000000000000ffff"):
        return str(ipaddress.IPv4Address(raw[12:]))
    return str(addr)


def pcp_header(opcode: int, lifetime: int, client_ip: str | None) -> bytes:
    return (struct.pack("!BBHI", PCP_VERSION, opcode, 0, lifetime)
            + encode_addr128(client_ip, want_ipv4_family=(client_ip is None or ":" not in client_ip)))


def pcp_announce(client_ip: str | None) -> bytes:
    return pcp_header(PCP_OP_ANNOUNCE, 0, client_ip)


def pcp_map(client_ip: str | None, nonce: bytes, protocol: int,
            internal_port: int, suggested_port: int, lifetime: int,
            ipv6: bool = False) -> bytes:
    body = (nonce
            + struct.pack("!B3xHH", protocol, internal_port, suggested_port)
            + (PCP_ANY_IPV6 if ipv6 else PCP_ANY_IPV4))
    return pcp_header(PCP_OP_MAP, lifetime, client_ip) + body


def natpmp_external() -> bytes:
    return struct.pack("!BB", NATPMP_VERSION, NATPMP_OP_EXTERNAL)


def natpmp_map(opcode: int, internal_port: int, suggested_port: int,
               lifetime: int) -> bytes:
    return struct.pack("!BBHHHI", NATPMP_VERSION, opcode, 0,
                       internal_port, suggested_port, lifetime)


# --------------------------------------------------------------------------
# Response decoders
# --------------------------------------------------------------------------

def describe_pcp(data: bytes) -> str:
    if len(data) < 4:
        return f"too short for any PCP field ({len(data)} bytes)"
    version, second, reserved, result = data[0], data[1], data[2], data[3]
    is_resp = bool(second & PCP_RESPONSE_BIT)
    opcode = second & 0x7F
    name = PCP_RESULTS.get(result, f"unknown({result})")
    out = [f"version={version} R={int(is_resp)} opcode={opcode} result={result} ({name})"]
    if len(data) >= PCP_HEADER_SIZE:
        lifetime, epoch = struct.unpack("!II", data[4:12])
        out.append(f"lifetime={lifetime}s epoch={epoch}")
    if len(data) >= PCP_MAP_PACKET_SIZE and opcode == PCP_OP_MAP:
        proto = data[36]
        internal, external = struct.unpack("!HH", data[40:44])
        ext_ip = decode_addr128(data[44:60])
        out.append(f"proto={proto} internal={internal} external={external} extIP={ext_ip}")
    return "; ".join(out)


def describe_natpmp(data: bytes) -> str:
    if not data:
        return "empty"
    version = data[0]
    # Observed on FRITZ!OS: a PCP-only box answers our 2-byte NAT-PMP request
    # with a 2-byte reply, where RFC 6886 section 3.5 mandates 8. Byte 0 still
    # carries the highest version it supports, which is all we need.
    if version != NATPMP_VERSION and len(data) < 4:
        return (f"{len(data)}-byte reply, version={version} -- non-conformant "
                f"version-mismatch reply; router speaks PCP v{version}")
    if len(data) < 4:
        return f"too short ({len(data)} bytes)"
    opcode = data[1]
    result = struct.unpack("!H", data[2:4])[0]
    name = NATPMP_RESULTS.get(result, f"unknown({result})")
    out = [f"version={version} opcode={opcode} result={result} ({name})"]
    if version != NATPMP_VERSION:
        # This is really a PCP response: offsets 4-7 are PCP's Lifetime (echoed
        # request bytes), NOT a NAT-PMP epoch. Never feed it to epoch tracking.
        out.append("(PCP-format reply -- bytes 4..7 are Lifetime, not SSSoE)")
        return "; ".join(out)
    if len(data) >= 8:
        out.append(f"sssoe={struct.unpack('!I', data[4:8])[0]}s")
    base = opcode & 0x7F
    if base == NATPMP_OP_EXTERNAL and len(data) >= 12:
        out.append(f"externalIP={ipaddress.IPv4Address(data[8:12])}")
    elif base in (NATPMP_OP_MAP_UDP, NATPMP_OP_MAP_TCP) and len(data) >= 16:
        internal, external = struct.unpack("!HH", data[8:12])
        lifetime = struct.unpack("!I", data[12:16])[0]
        out.append(f"internal={internal} external={external} lifetime={lifetime}s")
    return "; ".join(out)


def classify(data: bytes) -> str:
    """RFC 6887 section 9 / RFC 6886 section 3: byte 0 is the version the server speaks.

    Deliberately tolerant of short replies: the whole point of the version
    handshake is that it must work on datagrams below either protocol's normal
    minimum length. FRITZ!OS sends a 2-byte rejection here.
    """
    if not data:
        return "no data"
    version = data[0]
    if len(data) < 4:
        return f"version {version} only ({len(data)}-byte reply)"
    result = data[3] if version == PCP_VERSION else struct.unpack("!H", data[2:4])[0]
    if version == PCP_VERSION:
        return "PCP" if result != 1 else "PCP server rejecting our version"
    if version == NATPMP_VERSION:
        return "NAT-PMP" if result != 1 else "NAT-PMP only (rejected PCP v2)"
    return f"unknown version {version}"


# --------------------------------------------------------------------------
# UDP exchange
# --------------------------------------------------------------------------

def exchange(gateway: str, payload: bytes, label: str,
             timeout: float = 1.0, retries: int = 3,
             bind_addr: str | None = None) -> bytes | None:
    """Send `payload` to gateway:5351 and collect the first valid reply.

    Uses a connected UDP socket: it enforces the 5-tuple against off-path
    spoofing, gives the source address for the PCP client-IP field, and turns
    an ICMP port-unreachable into an immediate ConnectionRefusedError instead
    of a silent timeout.
    """
    print(f"\n  {_c('-> ' + label, '1;33')}  ({len(payload)} bytes)")
    print(hexdump(payload))
    try:
        fam, sa = _sockaddr(gateway, PORTMAP_PORT)
        with socket.socket(fam, socket.SOCK_DGRAM) as s:
            if bind_addr:
                # Force the source address. Needed to reach a link-local next hop
                # from our GUA -- the kernel would otherwise pick a link-local
                # source, which RFC 6887 section 8.4 says not to use.
                s.bind(_sockaddr(bind_addr, 0)[1])
            s.connect(sa)
            s.settimeout(timeout)
            for attempt in range(1, retries + 1):
                try:
                    s.send(payload)
                except OSError as exc:
                    bad(f"send failed: {exc}")
                    return None
                try:
                    data = s.recv(2048)
                except socket.timeout:
                    info(f"attempt {attempt}/{retries}: no reply within {timeout:.1f}s")
                    continue
                except ConnectionRefusedError:
                    bad("ICMP port unreachable -- nothing is listening on UDP/5351")
                    return None
                except OSError as exc:
                    bad(f"recv failed: {exc}")
                    return None
                print(f"  {_c('<- reply', '1;32')}  ({len(data)} bytes)")
                print(hexdump(data))
                return data
    except OSError as exc:
        bad(f"socket error: {exc}")
        return None
    bad(f"no response after {retries} attempts")
    return None


# --------------------------------------------------------------------------
# Probes
# --------------------------------------------------------------------------

def probe_pcp(gateway: str, client_ip: str | None, do_map: bool,
              ipv6: bool = False, bind_addr: str | None = None) -> dict:
    head(f"PCP (RFC 6887) -> [{gateway}]:{PORTMAP_PORT}" if ipv6
         else f"PCP (RFC 6887) -> {gateway}:{PORTMAP_PORT}")
    if bind_addr:
        info(f"forcing source address {bind_addr}")
    result: dict = {"supported": False, "detail": "", "external_port": None}

    # ANNOUNCE is idempotent and allocates nothing (RFC 6887 section 14.1.2).
    data = exchange(gateway, pcp_announce(client_ip), "PCP ANNOUNCE (read-only)",
                    bind_addr=bind_addr)
    if data:
        info(describe_pcp(data))
        verdict = classify(data)
        info(f"classified as: {_c(verdict, '1;35')}")
        if data[0] == PCP_VERSION and (len(data) < 4 or data[3] != 1):
            result["supported"] = True
            result["detail"] = "answered ANNOUNCE"
        elif data[0] == NATPMP_VERSION:
            result["detail"] = "router answered with NAT-PMP version 0"
    else:
        result["detail"] = "no reply to ANNOUNCE"

    if not do_map:
        info("--no-map: skipping the MAP round-trip")
        return result

    internal = random.randint(40000, 60000)
    nonce = os.urandom(12)
    info(f"MAP probe: TCP internal port {internal}, 120s lease, nonce {nonce.hex()}")
    data = exchange(gateway,
                    pcp_map(client_ip, nonce, PROTO_TCP, internal, internal, 120,
                            ipv6=ipv6),
                    f"PCP MAP TCP {internal} lifetime=120", bind_addr=bind_addr)
    if data:
        info(describe_pcp(data))
        info(f"classified as: {_c(classify(data), '1;35')}")
        if data[0] == PCP_VERSION and len(data) >= PCP_MAP_PACKET_SIZE and data[3] == 0:
            external = struct.unpack("!H", data[42:44])[0]
            result["supported"] = True
            result["external_port"] = external
            result["detail"] = f"MAP succeeded, external port {external}"
            if external == internal:
                ok(f"external port matches internal ({external}) -- usable for eD2K")
            else:
                bad(f"external {external} != internal {internal} -- would cause a LowID")
            # Clean up: lifetime 0 deletes; suggested port/address MUST be zero.
            exchange(gateway,
                     pcp_map(client_ip, nonce, PROTO_TCP, internal, 0, 0,
                             ipv6=ipv6),
                     "PCP MAP delete (cleanup)", retries=2, bind_addr=bind_addr)
        elif data[0] == PCP_VERSION and len(data) >= 4:
            result["detail"] = f"MAP rejected: {PCP_RESULTS.get(data[3], data[3])}"
    return result


def probe_natpmp(gateway: str, do_map: bool) -> dict:
    head(f"NAT-PMP (RFC 6886) -> {gateway}:{PORTMAP_PORT}")
    result: dict = {"supported": False, "detail": "", "external_port": None}
    if ":" in gateway:
        bad("NAT-PMP is IPv4-only by specification -- skipping")
        result["detail"] = "IPv4-only protocol"
        return result

    # The external-address request changes no state on the router.
    data = exchange(gateway, natpmp_external(), "NAT-PMP external address (read-only)")
    if data:
        info(describe_natpmp(data))
        info(f"classified as: {_c(classify(data), '1;35')}")
        if data[0] == NATPMP_VERSION and len(data) >= 12 and \
                struct.unpack("!H", data[2:4])[0] == 0:
            result["supported"] = True
            result["detail"] = f"external IP {ipaddress.IPv4Address(data[8:12])}"
    else:
        result["detail"] = "no reply to external-address request"

    if not do_map:
        info("--no-map: skipping the MAP round-trip")
        return result

    internal = random.randint(40000, 60000)
    info(f"MAP probe: TCP internal port {internal}, 120s lease")
    data = exchange(gateway,
                    natpmp_map(NATPMP_OP_MAP_TCP, internal, internal, 120),
                    f"NAT-PMP MAP TCP {internal} lifetime=120")
    if data:
        info(describe_natpmp(data))
        if data[0] == NATPMP_VERSION and len(data) >= 16 and \
                struct.unpack("!H", data[2:4])[0] == 0:
            external = struct.unpack("!H", data[10:12])[0]
            result["supported"] = True
            result["external_port"] = external
            result["detail"] = f"MAP succeeded, external port {external}"
            if external == internal:
                ok(f"external port matches internal ({external}) -- usable for eD2K")
            else:
                bad(f"external {external} != internal {internal} -- would cause a LowID")
            # Delete: suggested external port MUST be 0 and lifetime 0.
            exchange(gateway, natpmp_map(NATPMP_OP_MAP_TCP, internal, 0, 0),
                     "NAT-PMP MAP delete (cleanup)", retries=2)
    return result


# --------------------------------------------------------------------------
# UPnP
# --------------------------------------------------------------------------

def ssdp_search(timeout: float = 3.0) -> list[str]:
    """M-SEARCH for InternetGatewayDevice; returns LOCATION URLs."""
    msg = (
        "M-SEARCH * HTTP/1.1\r\n"
        f"HOST: {SSDP_ADDR_V4}:{SSDP_PORT}\r\n"
        'MAN: "ssdp:discover"\r\n'
        "MX: 2\r\n"
        "ST: urn:schemas-upnp-org:device:InternetGatewayDevice:1\r\n"
        "\r\n"
    ).encode()
    locations: list[str] = []
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
            s.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 2)
            s.settimeout(timeout)
            s.sendto(msg, (SSDP_ADDR_V4, SSDP_PORT))
            deadline = time.monotonic() + timeout
            while time.monotonic() < deadline:
                try:
                    data, addr = s.recvfrom(4096)
                except socket.timeout:
                    break
                except OSError:
                    break
                for line in data.decode("utf-8", "replace").splitlines():
                    if line.lower().startswith("location:"):
                        url = line.split(":", 1)[1].strip()
                        if url not in locations:
                            locations.append(url)
                            info(f"IGD at {addr[0]} -> {url}")
    except OSError as exc:
        bad(f"SSDP failed: {exc}")
    return locations


def _localname(tag: str) -> str:
    return tag.rsplit("}", 1)[-1]


def parse_services(xml_text: str, base_url: str) -> dict[str, str]:
    """serviceType -> absolute controlURL."""
    services: dict[str, str] = {}
    try:
        root = ET.fromstring(xml_text)
    except ET.ParseError as exc:
        bad(f"device description is not valid XML: {exc}")
        return services
    for node in root.iter():
        if _localname(node.tag) != "service":
            continue
        stype = ctrl = None
        for child in node:
            if _localname(child.tag) == "serviceType":
                stype = (child.text or "").strip()
            elif _localname(child.tag) == "controlURL":
                ctrl = (child.text or "").strip()
        if stype and ctrl:
            services[stype] = urllib.parse.urljoin(base_url, ctrl)
    return services


def soap_call(control_url: str, service_type: str, action: str) -> str | None:
    body = (
        '<?xml version="1.0"?>'
        '<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/" '
        's:encodingStyle="http://schemas.xmlsoap.org/soap/encoding/">'
        f'<s:Body><u:{action} xmlns:u="{service_type}"></u:{action}></s:Body>'
        "</s:Envelope>"
    ).encode()
    req = urllib.request.Request(control_url, data=body, method="POST")
    req.add_header("Content-Type", 'text/xml; charset="utf-8"')
    req.add_header("SOAPAction", f'"{service_type}#{action}"')
    try:
        with urllib.request.urlopen(req, timeout=5) as resp:
            return resp.read().decode("utf-8", "replace")
    except urllib.error.HTTPError as exc:
        detail = exc.read().decode("utf-8", "replace")[:400]
        bad(f"{action} -> HTTP {exc.code}")
        info(detail.replace("\n", " "))
    except (urllib.error.URLError, OSError) as exc:
        bad(f"{action} failed: {exc}")
    return None


def probe_upnp() -> dict:
    head("UPnP IGD (SSDP + device description)")
    result: dict = {"supported": False, "ipv6_firewall": False, "detail": ""}
    locations = ssdp_search()
    if not locations:
        bad("no IGD answered the SSDP M-SEARCH")
        result["detail"] = "no SSDP response"
        return result
    result["supported"] = True

    for url in locations:
        info(f"fetching {url}")
        try:
            with urllib.request.urlopen(url, timeout=5) as resp:
                xml_text = resp.read().decode("utf-8", "replace")
        except (urllib.error.URLError, OSError) as exc:
            bad(f"could not fetch description: {exc}")
            continue

        services = parse_services(xml_text, url)
        for stype in sorted(services):
            marker = " <-- IPv6 pinholes" if "IPv6Firewall" in stype else ""
            info(f"service: {stype}{marker}")

        if IPV6_FIREWALL_SERVICE not in services:
            bad("WANIPv6FirewallControl NOT advertised -- no IGD2 pinhole support here")
            continue

        ok(f"WANIPv6FirewallControl at {services[IPV6_FIREWALL_SERVICE]}")
        resp = soap_call(services[IPV6_FIREWALL_SERVICE], IPV6_FIREWALL_SERVICE,
                         "GetFirewallStatus")
        if not resp:
            continue
        enabled = re.search(r"<FirewallEnabled>(\d+)</FirewallEnabled>", resp)
        allowed = re.search(r"<InboundPinholeAllowed>(\d+)</InboundPinholeAllowed>", resp)
        fw = enabled.group(1) if enabled else "?"
        pin = allowed.group(1) if allowed else "?"
        info(f"FirewallEnabled={fw} InboundPinholeAllowed={pin}")
        # Both must be 1 -- devices advertise the service and then refuse pinholes.
        if fw == "1" and pin == "1":
            ok("IGD2 IPv6 pinholes are available")
            result["ipv6_firewall"] = True
            result["detail"] = "IGD + IPv6 pinholes"
        else:
            bad("service present but pinholes are not permitted")
            result["detail"] = "IGD, pinholes refused"

    if not result["detail"]:
        result["detail"] = "IGD found (IPv4 only)"
    return result


# --------------------------------------------------------------------------
# Main
# --------------------------------------------------------------------------

def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--gateway", help="override the IPv4 gateway address")
    ap.add_argument("--gateway6", help="override the IPv6 gateway address")
    ap.add_argument("--no-map", action="store_true",
                    help="read-only: never create a mapping on the router")
    ap.add_argument("--skip-upnp", action="store_true", help="skip the UPnP/SSDP probe")
    args = ap.parse_args()

    head("Default gateway")
    v4 = [args.gateway] if args.gateway else find_gateways(v6=False)
    v6 = [args.gateway6] if args.gateway6 else find_gateways(v6=True)
    if v4:
        ok(f"IPv4 gateway candidates: {', '.join(v4)}")
    else:
        bad("no IPv4 default gateway found")
    if v6:
        ok(f"IPv6 gateway candidates: {', '.join(v6)}")
    else:
        info("no routable IPv6 default gateway found (link-local next hops are skipped)")

    if not v4 and not v6:
        bad("nothing to probe -- refusing to guess a gateway address")
        return 2

    do_map = not args.no_map
    if do_map:
        info("MAP probes are enabled: a short-lived mapping is created and then deleted."
             " Use --no-map to stay read-only.")

    pcp = {"supported": False, "detail": "not probed", "external_port": None}
    pmp = {"supported": False, "detail": "not probed", "external_port": None}
    pcp6 = None

    if v4:
        gw = v4[0]
        client_ip = local_address_towards(gw)
        info(f"source address towards {gw}: {client_ip or 'unknown'}")
        pcp = probe_pcp(gw, client_ip, do_map)
        pmp = probe_natpmp(gw, do_map)

    gua = global_ipv6_addresses()
    for gw6 in v6:
        client6 = local_address_towards(gw6)
        info(f"source address towards {gw6}: {client6 or 'unknown'}")
        pcp6 = probe_pcp(gw6, client6, do_map, ipv6=True)
        if pcp6.get("external_port"):
            break
        # A link-local next hop forces a link-local source, and RFC 6887
        # section 8.4 says a GUA should be used instead -- so retry bound to our
        # GUA before concluding the router refuses PCP over IPv6.
        if gua and client6 and ipaddress.IPv6Address(client6).is_link_local:
            info(f"retrying sourced from our GUA {gua[0]}")
            retry = probe_pcp(gw6, gua[0], do_map, ipv6=True, bind_addr=gua[0])
            if retry.get("external_port") or retry["supported"]:
                pcp6 = retry
            if pcp6.get("external_port"):
                break

    upnp = {"supported": False, "ipv6_firewall": False, "detail": "not probed"}
    if not args.skip_upnp:
        upnp = probe_upnp()

    head("Verdict")
    rows = [
        ("PCP (IPv4)", pcp["supported"], pcp["detail"]),
        ("NAT-PMP", pmp["supported"], pmp["detail"]),
    ]
    if pcp6 is not None:
        rows.append(("PCP (IPv6)", pcp6["supported"], pcp6["detail"]))
    rows.append(("UPnP IGD", upnp["supported"], upnp["detail"]))
    rows.append(("UPnP IGD2 IPv6 pinholes", upnp["ipv6_firewall"], ""))

    for name, supported, detail in rows:
        mark = _c("YES", "1;32") if supported else _c("no ", "1;31")
        print(f"  {name:<26} {mark}   {detail}")

    if pcp["supported"] or (pcp6 and pcp6["supported"]):
        winner = "PCP"
    elif pmp["supported"]:
        winner = "NAT-PMP"
    elif upnp["supported"]:
        winner = "UPnP"
    else:
        winner = None

    print()
    if winner:
        ok(f"auto-race winner on this network: {_c(winner, '1;36')}")
    else:
        bad("no port-mapping protocol available on this network")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        sys.exit(130)
