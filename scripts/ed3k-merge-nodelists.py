#!/usr/bin/env python3
"""Merge ed3k node list files (nodes.ed3knod, bootstrap.md §2) into one signed list.

  scripts/ed3k-merge-nodelists.py A.ed3knod B.ed3knod [...] [-o data/config/nodes.ed3knod]
                                  [--key FILE] [--comment TEXT] [--no-verify]

- Inputs are read in the order given; the first file's nodes come first (a seed's export
  lists the seed itself first, bootstrap.md §3).
- Each input is checked: magic, signature (the key in the file, as a shipped list is),
  node lengths, one net_tag for all.
- Duplicates dropped, first kept: the same node ID in a family, the same IP address in a
  family (a reader takes one entry per IP, §4).
- Then the rules of a made list (§3, §5): at most 2 nodes per site and 8 per block
  (ip-diversity.md §1), 200 per family, file at most 32 KiB.
- The output is signed with Ed25519: the 32-byte seed in --key (hex or raw), else a fresh
  random key. A shipped list is checked against the key in the file, so any key works.

No third-party modules: protobuf and Ed25519 (RFC 8032) are done here.
"""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import ipaddress
import os
import sys
import time
from pathlib import Path

MAGIC = b"ed3knod\x01"
SIGN_CTX = b"ed3k v1 nodes"
ALG_ED25519 = 1
NODE4, NODE6 = 22, 34
PER_FAMILY = 200
LIST_MAX = 32 * 1024
SITE_MAX, BLOCK_MAX = 2, 8
# prefix bits of a site / block per family (ip-diversity.md §1)
SITE_BITS = {4: 24, 6: 48}
BLOCK_BITS = {4: 16, 6: 32}
DEFAULT_OUT = Path(__file__).resolve().parent.parent / "data" / "config" / "nodes.ed3knod"


class MergeError(Exception):
    pass


# --------------------------------------------------------------------------- protobuf

def varint(n: int) -> bytes:
    out = bytearray()
    while True:
        b = n & 0x7F
        n >>= 7
        if n:
            out.append(b | 0x80)
        else:
            out.append(b)
            return bytes(out)


def read_varint(buf: bytes, i: int) -> tuple[int, int]:
    n = shift = 0
    while True:
        if i >= len(buf) or shift > 63:
            raise MergeError("truncated varint")
        b = buf[i]
        i += 1
        n |= (b & 0x7F) << shift
        if not b & 0x80:
            return n, i
        shift += 7


def fields(buf: bytes) -> list[tuple[int, int | bytes]]:
    """(field number, value) of a message; varints as int, length-delimited as bytes."""
    out, i = [], 0
    while i < len(buf):
        key, i = read_varint(buf, i)
        num, wt = key >> 3, key & 7
        if wt == 0:
            v, i = read_varint(buf, i)
        elif wt == 2:
            n, i = read_varint(buf, i)
            if i + n > len(buf):
                raise MergeError("truncated field")
            v, i = buf[i:i + n], i + n
        elif wt == 1:
            v, i = buf[i:i + 8], i + 8
        elif wt == 5:
            v, i = buf[i:i + 4], i + 4
        else:
            raise MergeError(f"wire type {wt}")
        out.append((num, v))
    return out


def f_varint(num: int, v: int) -> bytes:
    return varint(num << 3) + varint(v) if v else b""


def f_bytes(num: int, v: bytes) -> bytes:
    return varint(num << 3 | 2) + varint(len(v)) + v if v else b""


# --------------------------------------------------------------------------- Ed25519 (RFC 8032)

P = 2**255 - 19
L = 2**252 + 27742317777372353535851937790883648493
D = -121665 * pow(121666, P - 2, P) % P
SQRT_M1 = pow(2, (P - 1) // 4, P)


def _add(a, b):
    x1, y1, z1, t1 = a
    x2, y2, z2, t2 = b
    A = (y1 - x1) * (y2 - x2) % P
    B = (y1 + x1) * (y2 + x2) % P
    C = 2 * t1 * t2 * D % P
    Dd = 2 * z1 * z2 % P
    E, F, G, H = B - A, Dd - C, Dd + C, B + A
    return E * F % P, G * H % P, F * G % P, E * H % P


def _mul(s: int, p):
    q = (0, 1, 1, 0)
    while s:
        if s & 1:
            q = _add(q, p)
        p = _add(p, p)
        s >>= 1
    return q


def _eq(a, b) -> bool:
    return (a[0] * b[2] - b[0] * a[2]) % P == 0 and (a[1] * b[2] - b[1] * a[2]) % P == 0


def _recover_x(y: int, sign: int) -> int | None:
    if y >= P:
        return None
    x2 = (y * y - 1) * pow(D * y * y + 1, P - 2, P)
    if x2 == 0:
        return None if sign else 0
    x = pow(x2, (P + 3) // 8, P)
    if (x * x - x2) % P:
        x = x * SQRT_M1 % P
    if (x * x - x2) % P:
        return None
    if x & 1 != sign:
        x = P - x
    return x


_GY = 4 * pow(5, P - 2, P) % P
_GX = _recover_x(_GY, 0)
G = (_GX, _GY, 1, _GX * _GY % P)


def _compress(p) -> bytes:
    zinv = pow(p[2], P - 2, P)
    x, y = p[0] * zinv % P, p[1] * zinv % P
    return int.to_bytes(y | ((x & 1) << 255), 32, "little")


def _decompress(s: bytes):
    if len(s) != 32:
        return None
    y = int.from_bytes(s, "little")
    sign = y >> 255
    y &= (1 << 255) - 1
    x = _recover_x(y, sign)
    return None if x is None else (x, y, 1, x * y % P)


def _h(*parts: bytes) -> int:
    return int.from_bytes(hashlib.sha512(b"".join(parts)).digest(), "little")


def _expand(seed: bytes) -> tuple[int, bytes]:
    h = hashlib.sha512(seed).digest()
    a = int.from_bytes(h[:32], "little")
    a &= (1 << 254) - 8
    a |= 1 << 254
    return a, h[32:]


def public_key(seed: bytes) -> bytes:
    return _compress(_mul(_expand(seed)[0], G))


def sign(seed: bytes, msg: bytes) -> bytes:
    a, prefix = _expand(seed)
    pub = _compress(_mul(a, G))
    r = _h(prefix, msg) % L
    R = _compress(_mul(r, G))
    s = (r + _h(R, pub, msg) * a) % L
    return R + int.to_bytes(s, 32, "little")


def verify(pub: bytes, msg: bytes, sig: bytes) -> bool:
    if len(sig) != 64:
        return False
    A, R = _decompress(pub), _decompress(sig[:32])
    s = int.from_bytes(sig[32:], "little")
    if A is None or R is None or s >= L:
        return False
    k = _h(sig[:32], pub, msg) % L
    return _eq(_mul(s, G), _add(R, _mul(k, A)))


# --------------------------------------------------------------------------- node lists

class NodeList:
    def __init__(self, path: Path, created: int, key: bytes, nodes4: bytes, nodes6: bytes, comment: str, net_tag: bytes):
        self.path, self.created, self.key, self.comment, self.net_tag = path, created, key, comment, net_tag
        self.nodes = split(nodes4, 4) + split(nodes6, 6)


def split(raw: bytes, family: int) -> list[tuple[int, bytes, bytes, int]]:
    """(family, node id, ip bytes, port) of each compact node: id(16) ‖ ip ‖ port(2, BE)."""
    size = NODE4 if family == 4 else NODE6
    if len(raw) % size:
        raise MergeError(f"nodes{family} is not a multiple of {size} bytes")
    out = []
    for i in range(0, len(raw), size):
        e = raw[i:i + size]
        out.append((family, e[:16], e[16:-2], int.from_bytes(e[-2:], "big")))
    return out


def read_list(path: Path, check_sig: bool) -> NodeList:
    data = path.read_bytes()
    if not data.startswith(MAGIC):
        raise MergeError(f"{path}: not a node list (magic)")
    if len(data) > LIST_MAX:
        raise MergeError(f"{path}: larger than {LIST_MAX} bytes")
    body = key = b""
    sigs = []
    for num, v in fields(data[len(MAGIC):]):
        if num == 1:
            body = v
        elif num == 2:
            key = v
        elif num == 3:
            sigs.append(dict(fields(v)))
    if len(key) != 32:
        raise MergeError(f"{path}: key is not 32 bytes")
    if check_sig:
        ed = [s for s in sigs if s.get(1) == ALG_ED25519]
        if len(ed) != 1 or not verify(key, SIGN_CTX + body, ed[0].get(2, b"")):
            raise MergeError(f"{path}: signature does not verify (--no-verify to merge anyway)")
    f = {}
    for num, v in fields(body):
        f[num] = v
    comment = f.get(4, b"")
    return NodeList(path, int(f.get(1, 0)), key, f.get(2, b""), f.get(3, b""),
                    comment.decode("utf-8", "replace") if isinstance(comment, bytes) else "", f.get(5, b""))


def group(family: int, ip: bytes, bits: int) -> tuple[int, int]:
    n = int.from_bytes(ip, "big")
    total = 32 if family == 4 else 128
    return family, n >> (total - bits)


def merge(lists: list[NodeList]) -> tuple[list, dict[str, int]]:
    seen_id, seen_ip, site, block, per_family = set(), set(), {}, {}, {4: 0, 6: 0}
    out, dropped = [], {"duplicate ID": 0, "duplicate IP": 0, "site/block": 0, "200 per family": 0}
    for lst in lists:
        for n in lst.nodes:
            family, nid, ip, _port = n
            if (family, nid) in seen_id:
                dropped["duplicate ID"] += 1
                continue
            if (family, ip) in seen_ip:
                dropped["duplicate IP"] += 1
                continue
            s, b = group(family, ip, SITE_BITS[family]), group(family, ip, BLOCK_BITS[family])
            if site.get(s, 0) >= SITE_MAX or block.get(b, 0) >= BLOCK_MAX:
                dropped["site/block"] += 1
                continue
            if per_family[family] >= PER_FAMILY:
                dropped["200 per family"] += 1
                continue
            seen_id.add((family, nid))
            seen_ip.add((family, ip))
            site[s], block[b] = site.get(s, 0) + 1, block.get(b, 0) + 1
            per_family[family] += 1
            out.append(n)
    return out, dropped


def write_list(nodes: list, net_tag: bytes, comment: str, seed: bytes) -> bytes:
    raw = {4: bytearray(), 6: bytearray()}
    for family, nid, ip, port in nodes:
        raw[family] += nid + ip + port.to_bytes(2, "big")
    body = (f_varint(1, int(time.time())) + f_bytes(2, bytes(raw[4])) + f_bytes(3, bytes(raw[6]))
            + f_bytes(4, comment.encode()) + f_bytes(5, net_tag))
    sig = f_varint(1, ALG_ED25519) + f_bytes(2, sign(seed, SIGN_CTX + body))
    data = MAGIC + f_bytes(1, body) + f_bytes(2, public_key(seed)) + f_bytes(3, sig)
    if len(data) > LIST_MAX:
        raise MergeError(f"merged list is {len(data)} bytes, more than {LIST_MAX}")
    return data


def load_seed(path: Path | None) -> bytes:
    if path is None:
        return os.urandom(32)
    raw = path.read_bytes()
    if len(raw) != 32:
        try:
            raw = bytes.fromhex(raw.decode().strip())
        except ValueError as e:
            raise MergeError(f"{path}: not 32 bytes or 64 hex characters") from e
    if len(raw) != 32:
        raise MergeError(f"{path}: key seed is not 32 bytes")
    return raw


def show(n) -> str:
    family, nid, ip, port = n
    addr = ipaddress.ip_address(ip)
    ep = f"{addr}:{port}" if family == 4 else f"[{addr}]:{port}"
    return f"{nid.hex()}  {ep}"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("inputs", nargs="+", type=Path, help="node list files, first one first")
    ap.add_argument("-o", "--out", type=Path, default=DEFAULT_OUT)
    ap.add_argument("--key", type=Path, help="file with the 32-byte Ed25519 seed (raw or hex); default random")
    ap.add_argument("--comment", default=f"eMule Qt {dt.date.today().isoformat()}")
    ap.add_argument("--no-verify", action="store_true", help="merge lists whose signature does not verify")
    ap.add_argument("-v", "--verbose", action="store_true", help="print the merged nodes")
    a = ap.parse_args()
    try:
        lists = [read_list(p, not a.no_verify) for p in a.inputs]
        tags = {lst.net_tag for lst in lists}
        if len(tags) != 1:
            raise MergeError("the lists are for different networks (net_tag)")
        for lst in lists:
            print(f"{lst.path}: {len(lst.nodes)} nodes, created {dt.datetime.fromtimestamp(lst.created):%Y-%m-%d %H:%M}"
                  f"{', ' + repr(lst.comment) if lst.comment else ''}")
        nodes, dropped = merge(lists)
        if not nodes:
            raise MergeError("no node left to write")
        data = write_list(nodes, tags.pop(), a.comment, load_seed(a.key))
        a.out.parent.mkdir(parents=True, exist_ok=True)
        tmp = a.out.with_suffix(".tmp")
        tmp.write_bytes(data)
        tmp.replace(a.out)
    except (MergeError, OSError) as e:
        print(f"error: {e}", file=sys.stderr)
        return 1
    n4 = sum(1 for n in nodes if n[0] == 4)
    gone = ", ".join(f"{v} {k}" for k, v in dropped.items() if v) or "none"
    print(f"wrote {a.out} ({len(data)} bytes): {n4} IPv4 + {len(nodes) - n4} IPv6 nodes; dropped: {gone}")
    if a.verbose:
        for n in nodes:
            print("  " + show(n))
    return 0


if __name__ == "__main__":
    sys.exit(main())
