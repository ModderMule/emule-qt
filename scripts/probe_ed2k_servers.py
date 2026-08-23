#!/usr/bin/env python3
"""
probe_ed2k_servers.py — live eD2K server probe, JSON output.

Sends the same requests eMule/eMuleQt sends and dumps everything the server
answers as JSON:

  UDP  OP_GLOBSERVSTATREQ (0x96) -> OP_GLOBSERVSTATRES (0x97)   users/files/flags/UDP key
       OP_SERVER_DESC_REQ  (0xA2) -> OP_SERVER_DESC_RES  (0xA3)  name/description/version
       both plain (port+4) and obfuscated (port+12, RC4 keyed off the crypt-ping
       challenge, exactly like CServerList::ServerStats / decryptReceivedServer)
  TCP  OP_LOGINREQUEST     (0x01) -> OP_SERVERMESSAGE/OP_SERVERSTATUS/
                                     OP_IDCHANGE/OP_SERVERIDENT
       the login greeting is what actually names the server software.

Usage:
  scripts/probe_ed2k_servers.py                          # the servers from issues #5/#6
  scripts/probe_ed2k_servers.py 1.2.3.4:4232 5.6.7.8:4661
  scripts/probe_ed2k_servers.py --met ~/eMuleQt/Config/server.met --no-tcp
  scripts/probe_ed2k_servers.py --out /tmp/servers.json
"""

from __future__ import annotations

import argparse
import concurrent.futures
import hashlib
import json
import os
import random
import re
import select
import socket
import struct
import sys
import time
import zlib

# --- protocol constants (srchybrid/opcodes.h) ------------------------------

OP_EDONKEYPROT = 0xE3
OP_PACKEDPROT = 0xD4
OP_EMULEPROT = 0xC5

OP_LOGINREQUEST = 0x01
OP_REJECT = 0x05
OP_SERVERLIST = 0x32
OP_SERVERSTATUS = 0x34
OP_CALLBACKREQUESTED = 0x35
OP_SERVERMESSAGE = 0x38
OP_IDCHANGE = 0x40
OP_SERVERIDENT = 0x41
OP_GLOBSERVSTATREQ = 0x96
OP_GLOBSERVSTATRES = 0x97
OP_SERVER_DESC_REQ = 0xA2
OP_SERVER_DESC_RES = 0xA3
INV_SERV_DESC_LEN = 0xF0FF

CT_NAME = 0x01
CT_VERSION = 0x11
CT_SERVER_FLAGS = 0x20
CT_EMULE_VERSION = 0xFB
EDONKEYVERSION = 0x3C

SRVCAP_ZLIB = 0x0001
SRVCAP_NEWTAGS = 0x0008
SRVCAP_UNICODE = 0x0010
SRVCAP_LARGEFILES = 0x0100
SRVCAP_SUPPORTCRYPT = 0x0200
SRVCAP_REQUESTCRYPT = 0x0400

ST_TAGS = {
    0x01: "name", 0x0B: "description", 0x0C: "ping", 0x0D: "fail",
    0x0E: "preference", 0x0F: "port", 0x10: "ip", 0x85: "dynip",
    0x87: "max_users", 0x88: "soft_files", 0x89: "hard_files",
    0x90: "last_ping", 0x91: "version", 0x92: "udp_flags",
    0x93: "aux_ports", 0x94: "lowid_users", 0x95: "udp_key",
    0x96: "udp_key_ip", 0x97: "tcp_obfuscation_port",
    0x98: "udp_obfuscation_port",
}

UDP_FLAGS = [
    (0x00000001, "ExtGetSources"), (0x00000002, "ExtGetFiles"),
    (0x00000008, "NewTags"), (0x00000010, "Unicode"),
    (0x00000020, "ExtGetSources2"), (0x00000100, "LargeFiles"),
    (0x00000200, "UdpObfuscation"), (0x00000400, "TcpObfuscation"),
    (0x00004000, "IPv6"),
]

TCP_FLAGS = [
    (0x00000001, "Compression"), (0x00000008, "NewTags"),
    (0x00000010, "Unicode"), (0x00000040, "RelatedSearch"),
    (0x00000080, "TypeTagInteger"), (0x00000100, "LargeFiles"),
    (0x00000400, "TcpObfuscation"), (0x00004000, "IPv6"),
    (0x00008000, "NatRendezvous"),
]

# obfuscation constants (src/core/net/EncryptedDatagramSocket.cpp)
MAGIC_UDP_SYNC_SERVER = 0x13EF24D5
MAGIC_UDP_CLIENT_SERVER = 0x6B
MAGIC_UDP_SERVER_CLIENT = 0xA5
CRYPT_HEADER_WITHOUT_PADDING = 8

# obfuscated TCP to a server: DH-768 (src/core/net/EncryptedStreamSocket.cpp)
DH768_P = int.from_bytes(bytes([
    0xF2, 0xBF, 0x52, 0xC5, 0x5F, 0x58, 0x7A, 0xDD, 0x53, 0x71, 0xA9, 0x36,
    0xE8, 0x86, 0xEB, 0x3C, 0x62, 0x17, 0xA3, 0x3E, 0xC3, 0x4C, 0xB4, 0x0D,
    0xC7, 0x3A, 0x41, 0xA6, 0x43, 0xAF, 0xFC, 0xE7, 0x21, 0xFC, 0x28, 0x63,
    0x66, 0x53, 0x5B, 0xDB, 0xCE, 0x25, 0x9F, 0x22, 0x86, 0xDA, 0x4A, 0x91,
    0xB2, 0x07, 0xCB, 0xAA, 0x52, 0x55, 0xD4, 0xF6, 0x1C, 0xCE, 0xAE, 0xD4,
    0x5A, 0xD5, 0xE0, 0x74, 0x7D, 0xF7, 0x78, 0x18, 0x28, 0x10, 0x5F, 0x34,
    0x0F, 0x76, 0x23, 0x87, 0xF8, 0x8B, 0x28, 0x91, 0x42, 0xFB, 0x42, 0x68,
    0x8F, 0x05, 0x15, 0x0F, 0x54, 0x8B, 0x5F, 0x43, 0x6A, 0xF7, 0x0D, 0xF3]), "big")
PRIME_SIZE = 96
MAGIC_VALUE_SYNC = 0x835E6FC4
MAGIC_VALUE_REQUESTER = 34
MAGIC_VALUE_SERVER = 203
ENCRYPTION_METHOD_OBFUSCATION = 0x00

DEFAULT_SERVERS = [
    ("141.227.139.101", 4235),   # issue #6: "the test server you provided"
    ("213.141.198.207", 4232),   # issue #6: Mazinga Server
    ("91.208.162.55", 4235),     # !! Sharing-Devils No.3 !! TEST
    ("141.227.165.99", 4232),    # MO-ad-free / AT-Server
    ("176.125.231.98", 18357),   # issue #5 log: Akteon Server No2
    ("145.239.2.134", 4661),     # issue #5 log: GrupoTS Server
]


# --- RC4 (src/core/utils/OtherFunctions.cpp) --------------------------------

class RC4:
    def __init__(self, key_data: bytes, skip_discard: bool = True):
        self.s = list(range(256))
        j = 0
        for i in range(256):
            j = (j + self.s[i] + key_data[i % len(key_data)]) & 0xFF
            self.s[i], self.s[j] = self.s[j], self.s[i]
        self.x = 0
        self.y = 0
        if not skip_discard:
            self.crypt(bytes(1024))

    def crypt(self, data: bytes) -> bytes:
        out = bytearray(len(data))
        s = self.s
        for i, b in enumerate(data):
            self.x = (self.x + 1) & 0xFF
            self.y = (self.y + s[self.x]) & 0xFF
            s[self.x], s[self.y] = s[self.y], s[self.x]
            out[i] = b ^ s[(s[self.x] + s[self.y]) & 0xFF]
        return bytes(out)

    def skip(self, count: int) -> None:
        s = self.s
        for _ in range(count):
            self.x = (self.x + 1) & 0xFF
            self.y = (self.y + s[self.x]) & 0xFF
            s[self.x], s[self.y] = s[self.y], s[self.x]


def _rc4_for(base_key: int, magic: int, random_key_part: int) -> RC4:
    key_data = struct.pack("<I", base_key) + bytes([magic]) + struct.pack("<H", random_key_part)
    return RC4(hashlib.md5(key_data).digest(), skip_discard=True)


def decrypt_server_datagram(buf: bytes, base_key: int) -> bytes | None:
    """Port of EncryptedDatagramSocket::decryptReceivedServer()."""
    if len(buf) <= CRYPT_HEADER_WITHOUT_PADDING or base_key == 0:
        return None
    if buf[0] == OP_EDONKEYPROT:
        return buf
    random_key_part = struct.unpack_from("<H", buf, 1)[0]
    rc4 = _rc4_for(base_key, MAGIC_UDP_SERVER_CLIENT, random_key_part)
    magic = struct.unpack("<I", rc4.crypt(buf[3:7]))[0]
    if magic != MAGIC_UDP_SYNC_SERVER:
        return None
    padding = rc4.crypt(buf[7:8])[0] & 0x0F
    remaining = len(buf) - CRYPT_HEADER_WITHOUT_PADDING
    if remaining <= padding:
        return None
    if padding:
        rc4.skip(padding)
        remaining -= padding
    return rc4.crypt(buf[len(buf) - remaining:])


def encrypt_server_datagram(plain: bytes, base_key: int) -> bytes:
    """Port of EncryptedDatagramSocket::encryptSendServer()."""
    random_key_part = random.getrandbits(16)
    rc4 = _rc4_for(base_key, MAGIC_UDP_CLIENT_SERVER, random_key_part)
    marker = random.getrandbits(8)
    if marker == OP_EDONKEYPROT:
        marker = 0x01
    header = bytes([marker]) + struct.pack("<H", random_key_part)
    body = struct.pack("<I", MAGIC_UDP_SYNC_SERVER) + b"\x00" + plain
    return header + rc4.crypt(body)


# --- eD2K tag reader --------------------------------------------------------

class Reader:
    def __init__(self, data: bytes):
        self.d = data
        self.p = 0

    def need(self, n: int) -> None:
        if self.p + n > len(self.d):
            raise EOFError("truncated packet")

    def u8(self) -> int:
        self.need(1); v = self.d[self.p]; self.p += 1; return v

    def u16(self) -> int:
        self.need(2); v = struct.unpack_from("<H", self.d, self.p)[0]; self.p += 2; return v

    def u32(self) -> int:
        self.need(4); v = struct.unpack_from("<I", self.d, self.p)[0]; self.p += 4; return v

    def u64(self) -> int:
        self.need(8); v = struct.unpack_from("<Q", self.d, self.p)[0]; self.p += 8; return v

    def raw(self, n: int) -> bytes:
        self.need(n); v = self.d[self.p:self.p + n]; self.p += n; return v

    def string(self) -> str:
        return self.raw(self.u16()).decode("utf-8", "replace").lstrip("﻿")

    def tag(self) -> tuple[str, object]:
        t = self.u8()
        if t & 0x80:
            t &= 0x7F
            name = self.u8()
        else:
            n = self.u16()
            name = self.u8() if n == 1 else self.raw(n).decode("utf-8", "replace")
        if t == 0x01:
            val: object = self.raw(16).hex().upper()
        elif t == 0x02:
            val = self.string()
        elif t == 0x03:
            val = self.u32()
        elif t == 0x04:
            val = struct.unpack("<f", self.raw(4))[0]
        elif t == 0x05:
            val = bool(self.u8())
        elif t == 0x06:
            val = self.raw(self.u16() // 8 + 1).hex()
        elif t == 0x07:
            val = self.raw(self.u32()).hex()
        elif t == 0x08:
            val = self.u16()
        elif t == 0x09:
            val = self.u8()
        elif t == 0x0A:
            val = self.raw(self.u8()).hex()
        elif t == 0x0B:
            val = self.u64()
        elif 0x11 <= t <= 0x20:
            val = self.raw(t - 0x10).decode("utf-8", "replace")
        else:
            raise ValueError(f"unknown tag type 0x{t:02X}")
        key = ST_TAGS.get(name, f"0x{name:02X}") if isinstance(name, int) else name
        return key, val

    def taglist(self) -> dict:
        out: dict = {}
        for _ in range(self.u32()):
            k, v = self.tag()
            out[k] = v
        return out


def decode_flags(value: int, table) -> list[str]:
    known = [name for bit, name in table if value & bit]
    rest = value & ~sum(bit for bit, _ in table)
    known += [f"unknown_bit:0x{1 << b:04X}" for b in range(32) if rest & (1 << b)]
    return known


def fmt_version(v) -> str:
    if isinstance(v, int):
        return f"{v >> 16}.{v & 0xFFFF:02d}"
    return str(v)


# --- UDP probe --------------------------------------------------------------

def _recv_ed2k(sock, deadline: float, ip: str, base_key: int | None):
    """Return (opcode, payload, from_port, obfuscated) or None on timeout."""
    while True:
        left = deadline - time.monotonic()
        if left <= 0:
            return None
        r, _, _ = select.select([sock], [], [], left)
        if not r:
            return None
        data, addr = sock.recvfrom(8192)
        if addr[0] != ip or len(data) < 2:
            continue
        if data[0] == OP_EDONKEYPROT:
            return data[1], data[2:], addr[1], False
        if base_key:
            dec = decrypt_server_datagram(data, base_key)
            if dec and len(dec) >= 2 and dec[0] == OP_EDONKEYPROT:
                return dec[1], dec[2:], addr[1], True


def parse_status(payload: bytes) -> dict:
    """Layout per ServerList::processStatusResponse()."""
    out: dict = {"raw_len": len(payload)}
    if len(payload) >= 12:
        out["users"] = struct.unpack_from("<I", payload, 4)[0]
        out["files"] = struct.unpack_from("<I", payload, 8)[0]
    if len(payload) >= 16:
        out["max_users"] = struct.unpack_from("<I", payload, 12)[0]
    if len(payload) >= 24:
        out["soft_files"] = struct.unpack_from("<I", payload, 16)[0]
        out["hard_files"] = struct.unpack_from("<I", payload, 20)[0]
    if len(payload) >= 28:
        flags = struct.unpack_from("<I", payload, 24)[0]
        out["udp_flags"] = f"0x{flags:08X}"
        out["udp_flags_decoded"] = decode_flags(flags, UDP_FLAGS)
    if len(payload) >= 32:
        out["lowid_users"] = struct.unpack_from("<I", payload, 28)[0]
    if len(payload) >= 40:
        out["udp_obfuscation_port"] = struct.unpack_from("<H", payload, 32)[0]
        out["tcp_obfuscation_port"] = struct.unpack_from("<H", payload, 34)[0]
        out["server_udp_key"] = f"0x{struct.unpack_from('<I', payload, 36)[0]:08X}"
        out["_udp_key_int"] = struct.unpack_from("<I", payload, 36)[0]
    if len(payload) == 44:
        out["observed_client_ip"] = socket.inet_ntoa(payload[40:44])
    elif len(payload) > 40:
        out["vendor_extension_hex"] = payload[40:].hex()
        out["vendor_extension"] = parse_vendor_extension(payload[40:])
    return out


# DER SubjectPublicKeyInfo prefix for an X25519 key (OID 1.3.101.110)
X25519_SPKI_PREFIX = bytes.fromhex("302a300506032b656e032100")


def parse_vendor_extension(blob: bytes) -> dict:
    """The tail some servers append to OP_GLOBSERVSTATRES: <count u8><eD2K tags>."""
    out: dict = {}
    try:
        r = Reader(blob)
        count = r.u8()
        tags = {}
        for _ in range(count):
            k, v = r.tag()
            tags[k] = v
        out["tags"] = tags
        for key, value in tags.items():
            if isinstance(value, str) and value.startswith(X25519_SPKI_PREFIX.hex()):
                out["x25519_public_key"] = value[len(X25519_SPKI_PREFIX) * 2:]
                out["x25519_tag"] = key
    except (EOFError, ValueError) as exc:
        out["parse_error"] = str(exc)
    return out


def parse_desc(payload: bytes) -> dict:
    out: dict = {}
    if len(payload) >= 8 and struct.unpack_from("<H", payload, 0)[0] == INV_SERV_DESC_LEN:
        out["format"] = "tagged"
        r = Reader(payload)
        r.u32()
        try:
            out.update(r.taglist())
        except (EOFError, ValueError) as exc:
            out["parse_error"] = str(exc)
    else:
        out["format"] = "legacy"
        r = Reader(payload)
        try:
            out["name"] = r.string()
            out["description"] = r.string()
        except EOFError as exc:
            out["parse_error"] = str(exc)
    if "version" in out:
        out["version"] = fmt_version(out["version"])
    return out


def udp_probe(ip: str, port: int, timeout: float, retries: int) -> dict:
    res: dict = {"status": None, "desc": None, "attempts": []}
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setblocking(False)
    try:
        udp_key = 0
        # --- plain OP_GLOBSERVSTATREQ on port+4
        for attempt in range(retries + 1):
            challenge = 0x55AA0000 | random.getrandbits(16)
            pkt = bytes([OP_EDONKEYPROT, OP_GLOBSERVSTATREQ]) + struct.pack("<I", challenge)
            sent = time.monotonic()
            sock.sendto(pkt, (ip, port + 4))
            got = _recv_ed2k(sock, sent + timeout, ip, None)
            res["attempts"].append({"kind": "plain-status", "try": attempt + 1,
                                    "answered": bool(got)})
            if got and got[0] == OP_GLOBSERVSTATRES:
                st = parse_status(got[1])
                st["mode"] = "plain"
                st["rtt_ms"] = round((time.monotonic() - sent) * 1000, 1)
                st["challenge_ok"] = struct.unpack_from("<I", got[1], 0)[0] == challenge
                udp_key = st.pop("_udp_key_int", 0)
                res["status"] = st
                break

        # --- obfuscated crypt-ping on port+12 (what eMuleQt tries first)
        if res["status"] is None:
            for attempt in range(retries + 1):
                challenge = random.getrandbits(32) or 1
                padding = random.randrange(16)
                raw = struct.pack("<I", challenge) + bytes(random.getrandbits(8) for _ in range(padding))
                sent = time.monotonic()
                sock.sendto(raw, (ip, port + 12))
                got = _recv_ed2k(sock, sent + timeout, ip, challenge)
                res["attempts"].append({"kind": "obfuscated-crypt-ping", "try": attempt + 1,
                                        "answered": bool(got)})
                if got and got[0] == OP_GLOBSERVSTATRES:
                    st = parse_status(got[1])
                    st["mode"] = "obfuscated"
                    st["rtt_ms"] = round((time.monotonic() - sent) * 1000, 1)
                    st["challenge_ok"] = struct.unpack_from("<I", got[1], 0)[0] == challenge
                    udp_key = st.pop("_udp_key_int", 0)
                    res["status"] = st
                    break

        # --- OP_SERVER_DESC_REQ: plain first, obfuscated with the learned key second
        desc_challenge = (random.getrandbits(16) << 16) | INV_SERV_DESC_LEN
        plain = bytes([OP_EDONKEYPROT, OP_SERVER_DESC_REQ]) + struct.pack("<I", desc_challenge)
        sent = time.monotonic()
        sock.sendto(plain, (ip, port + 4))
        got = _recv_ed2k(sock, sent + timeout, ip, None)
        res["attempts"].append({"kind": "plain-desc", "try": 1, "answered": bool(got)})
        if got and got[0] == OP_SERVER_DESC_RES:
            res["desc"] = parse_desc(got[1])
            res["desc"]["mode"] = "plain"
        elif udp_key:
            obf_port = (res["status"] or {}).get("udp_obfuscation_port") or (port + 12)
            sent = time.monotonic()
            sock.sendto(encrypt_server_datagram(plain, udp_key), (ip, obf_port))
            got = _recv_ed2k(sock, sent + timeout, ip, udp_key)
            res["attempts"].append({"kind": "obfuscated-desc", "try": 1, "answered": bool(got)})
            if got and got[0] == OP_SERVER_DESC_RES:
                res["desc"] = parse_desc(got[1])
                res["desc"]["mode"] = "obfuscated"
    except OSError as exc:
        res["error"] = f"{type(exc).__name__}: {exc}"
    finally:
        sock.close()
    res["reachable"] = res["status"] is not None or res["desc"] is not None
    return res


# --- obfuscated TCP handshake (EncryptedStreamSocket, PendingServer path) ----

def semi_random_marker() -> int:
    for _ in range(32):
        marker = random.getrandbits(8)
        if marker not in (OP_EDONKEYPROT, OP_PACKEDPROT, OP_EMULEPROT):
            return marker
    return 0x01


def _recv_exact(sock, count: int, deadline: float) -> bytes:
    buf = b""
    while len(buf) < count:
        left = deadline - time.monotonic()
        if left <= 0:
            raise TimeoutError(f"handshake stalled after {len(buf)}/{count} bytes")
        r, _, _ = select.select([sock], [], [], left)
        if not r:
            continue
        chunk = sock.recv(count - len(buf))
        if not chunk:
            raise ConnectionError("server closed during handshake")
        buf += chunk
    return buf


def dh_server_handshake(sock, timeout: float) -> tuple[RC4, RC4, dict]:
    """Client side of eMule's obfuscated server connection (DH-768 + RC4)."""
    info: dict = {}
    exponent = random.getrandbits(128)
    ga = pow(2, exponent, DH768_P).to_bytes(PRIME_SIZE, "big")
    padding = random.randrange(16)
    sock.sendall(bytes([semi_random_marker()]) + ga + bytes([padding])
                 + bytes(random.getrandbits(8) for _ in range(padding)))

    deadline = time.monotonic() + timeout
    gb = _recv_exact(sock, PRIME_SIZE, deadline)
    secret = pow(int.from_bytes(gb, "big"), exponent, DH768_P).to_bytes(PRIME_SIZE, "big")
    send_key = RC4(hashlib.md5(secret + bytes([MAGIC_VALUE_REQUESTER])).digest(), skip_discard=False)
    recv_key = RC4(hashlib.md5(secret + bytes([MAGIC_VALUE_SERVER])).digest(), skip_discard=False)

    magic = struct.unpack("<I", recv_key.crypt(_recv_exact(sock, 4, deadline)))[0]
    info["magic_ok"] = magic == MAGIC_VALUE_SYNC
    if not info["magic_ok"]:
        raise ConnectionError(f"wrong magic after DH: 0x{magic:08X}")
    supported, requested, pad_len = recv_key.crypt(_recv_exact(sock, 3, deadline))
    info["encryption_supported"] = supported
    info["encryption_requested"] = requested
    if pad_len:
        recv_key.crypt(_recv_exact(sock, pad_len, deadline))

    pad = random.randrange(16)
    response = (struct.pack("<I", MAGIC_VALUE_SYNC) + bytes([ENCRYPTION_METHOD_OBFUSCATION, pad])
                + bytes(random.getrandbits(8) for _ in range(pad)))
    sock.sendall(send_key.crypt(response))
    return send_key, recv_key, info


# --- TCP probe --------------------------------------------------------------

def _old_tag(tag_id: int, value) -> bytes:
    if isinstance(value, str):
        raw = value.encode("utf-8")
        return bytes([0x02]) + struct.pack("<H", 1) + bytes([tag_id]) + struct.pack("<H", len(raw)) + raw
    return bytes([0x03]) + struct.pack("<H", 1) + bytes([tag_id]) + struct.pack("<I", value)


def build_login(user_hash: bytes, listen_port: int, nick: str, emule_ver: int) -> bytes:
    caps = SRVCAP_NEWTAGS | SRVCAP_LARGEFILES | SRVCAP_UNICODE | SRVCAP_ZLIB | SRVCAP_SUPPORTCRYPT
    body = user_hash + struct.pack("<I", 0) + struct.pack("<H", listen_port) + struct.pack("<I", 4)
    body += _old_tag(CT_NAME, nick)
    body += _old_tag(CT_VERSION, EDONKEYVERSION)
    body += _old_tag(CT_SERVER_FLAGS, caps)
    body += _old_tag(CT_EMULE_VERSION, emule_ver)
    payload = bytes([OP_LOGINREQUEST]) + body
    return bytes([OP_EDONKEYPROT]) + struct.pack("<I", len(payload)) + payload


def tcp_probe(ip: str, port: int, timeout: float, nick: str, listen_port: int,
              linger: float, obfuscated: bool = False) -> dict:
    out: dict = {"connected": False, "obfuscated": obfuscated, "messages": [], "packets": []}
    user_hash = bytearray(random.getrandbits(8) for _ in range(16))
    user_hash[5] = 14      # eMule client marks, as MFC sets them
    user_hash[14] = 111
    out["user_hash"] = bytes(user_hash).hex().upper()
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(timeout)
    started = time.monotonic()
    try:
        sock.connect((ip, port))
        out["connected"] = True
        out["connect_ms"] = round((time.monotonic() - started) * 1000, 1)
        send_key = recv_key = None
        if obfuscated:
            send_key, recv_key, hs = dh_server_handshake(sock, timeout)
            out["handshake"] = hs
            out["handshake_ms"] = round((time.monotonic() - started) * 1000, 1)
        login = build_login(bytes(user_hash), listen_port, nick, EMULE_VERSION_TAG)
        sock.sendall(send_key.crypt(login) if send_key else login)
        buf = b""
        deadline = time.monotonic() + linger
        sock.setblocking(False)
        while time.monotonic() < deadline:
            r, _, _ = select.select([sock], [], [], max(0.05, deadline - time.monotonic()))
            if r:
                chunk = sock.recv(65536)
                if not chunk:
                    out["closed_by_server"] = True
                    break
                if recv_key:
                    chunk = recv_key.crypt(chunk)
                if not buf and "first_byte_ms" not in out:
                    out["first_byte_ms"] = round((time.monotonic() - started) * 1000, 1)
                buf += chunk
            while len(buf) >= 5:
                proto = buf[0]
                size = struct.unpack_from("<I", buf, 1)[0]
                if size == 0 or size > 2_000_000:
                    out["packets"].append({"error": f"bad frame size {size} proto 0x{proto:02X}"})
                    buf = b""
                    break
                if len(buf) < 5 + size:
                    break
                payload = buf[5:5 + size]
                buf = buf[5 + size:]
                opcode, body = payload[0], payload[1:]
                if proto == OP_PACKEDPROT:
                    try:
                        body = zlib.decompress(body)
                    except zlib.error as exc:
                        out["packets"].append({"opcode": f"0x{opcode:02X}",
                                               "error": f"inflate failed: {exc}"})
                        continue
                _handle_tcp_packet(out, proto, opcode, body)
    except OSError as exc:
        out["error"] = f"{type(exc).__name__}: {exc}"
        out["elapsed_ms"] = round((time.monotonic() - started) * 1000, 1)
    finally:
        sock.close()
    return out


def _handle_tcp_packet(out: dict, proto: int, opcode: int, body: bytes) -> None:
    entry = {"proto": f"0x{proto:02X}", "opcode": f"0x{opcode:02X}", "len": len(body)}
    try:
        if opcode == OP_SERVERMESSAGE and proto == OP_EDONKEYPROT:
            entry["type"] = "OP_SERVERMESSAGE"
            text = Reader(body).string()
            entry["text"] = text
            out["messages"].extend(line for line in text.splitlines() if line.strip())
        elif opcode == OP_SERVERSTATUS:
            entry["type"] = "OP_SERVERSTATUS"
            r = Reader(body)
            out["users"] = entry["users"] = r.u32()
            out["files"] = entry["files"] = r.u32()
        elif opcode == OP_IDCHANGE:
            entry["type"] = "OP_IDCHANGE"
            r = Reader(body)
            client_id = r.u32()
            out["client_id"] = client_id
            out["id_type"] = "LowID" if client_id < 16777216 else "HighID"
            entry["client_id"] = client_id
            if len(body) >= 8:
                flags = r.u32()
                out["tcp_flags"] = f"0x{flags:08X}"
                out["tcp_flags_decoded"] = decode_flags(flags, TCP_FLAGS)
            if len(body) >= 12:
                out["reported_tcp_port"] = r.u32()
            if len(body) >= 16:
                out["reported_client_ip"] = socket.inet_ntoa(struct.pack("<I", r.u32()))
        elif opcode == OP_SERVERIDENT:
            entry["type"] = "OP_SERVERIDENT"
            r = Reader(body)
            ident = {"hash": r.raw(16).hex().upper(),
                     "ip": socket.inet_ntoa(struct.pack("<I", r.u32())),
                     "port": r.u16()}
            ident.update(r.taglist())
            if "version" in ident:
                ident["version"] = fmt_version(ident["version"])
            out["server_ident"] = ident
        elif opcode == OP_SERVERLIST:
            entry["type"] = "OP_SERVERLIST"
            r = Reader(body)
            count = r.u8()
            entry["count"] = count
            out["server_list_offered"] = count
        elif opcode == OP_REJECT:
            entry["type"] = "OP_REJECT"
            out["rejected"] = True
        elif opcode == OP_CALLBACKREQUESTED:
            entry["type"] = "OP_CALLBACKREQUESTED"
        else:
            entry["type"] = "unhandled"
            entry["head_hex"] = body[:32].hex()
    except (EOFError, ValueError) as exc:
        entry["parse_error"] = str(exc)
    out["packets"].append(entry)


# --- software fingerprinting ------------------------------------------------

SOFTWARE_PATTERNS = [
    (r"ed2k[-_ ]?rust", "ed2k-rust"),
    (r"\bmldonkey\b", "MLDonkey"),
    (r"\bhybrid\b", "eDonkey hybrid"),
    (r"\bjed2k\b", "jed2k"),
    (r"\bemule[- ]?ai\b", "eMuleAI server"),
    (r"ed2k[-_ ]?net", "ed2kNET (.NET eD2K/eserver reimplementation)"),
    (r"\bopen[- ]?ed2k\b", "OpenED2K"),
    (r"\bsatan\b", "Satan-eDonkey"),
    (r"\beserver\b|\blugdunum\b", "eserver (Lugdunum)"),
]


def guess_software(entry: dict) -> dict:
    tcp = entry.get("tcp") or {}
    if not tcp.get("messages") and entry.get("tcp_obfuscated", {}).get("messages"):
        tcp = entry["tcp_obfuscated"]
    haystack = " ".join(tcp.get("messages", [])).lower()
    ident = tcp.get("server_ident") or {}
    desc = entry.get("udp", {}).get("desc") or {}
    for field in ("name", "description"):
        haystack += " " + str(desc.get(field, "")).lower() + " " + str(ident.get(field, "")).lower()

    version = desc.get("version") or ident.get("version")
    status = entry.get("udp", {}).get("status") or {}
    name = None
    for pattern, label in SOFTWARE_PATTERNS:
        if re.search(pattern, haystack):
            name = label
            break
    if name is None and (status.get("vendor_extension") or {}).get("x25519_public_key"):
        # Only the ed2kNET line publishes an X25519 key in its status reply.
        name = "ed2kNET-family (X25519 key in OP_GLOBSERVSTATRES)"
    if name is None and version:
        # eserver is the only widely deployed server that reports a bare "MM.mm"
        # version tag over UDP, so treat that shape as eserver-compatible.
        name = ("eserver (Lugdunum)-compatible" if re.fullmatch(r"\d+\.\d+", str(version))
                else None)
    banner = next((m for m in tcp.get("messages", [])
                   if re.search(r"version|server\s+\d|\bv\d", m, re.I)), None)
    return {"software": name, "version": version, "version_banner": banner}


# --- server.met (for the names we already had on file) ----------------------

def load_met(path: str) -> dict:
    known: dict = {}
    try:
        with open(path, "rb") as fh:
            data = fh.read()
    except OSError:
        return known
    try:
        r = Reader(data)
        r.u8()
        for _ in range(r.u32()):
            ip = socket.inet_ntoa(r.raw(4))
            port = r.u16()
            tags = r.taglist()
            known[f"{ip}:{port}"] = {"name": tags.get("name"),
                                     "description": tags.get("description"),
                                     "version": fmt_version(tags["version"]) if "version" in tags else None}
    except (EOFError, ValueError, OSError):
        pass
    return known


# --- main -------------------------------------------------------------------

EMULE_VERSION_TAG = (0 << 17) | (70 << 10) | (1 << 7)   # SEND_EMULE_VERSION_* 0.70b


def local_json_path(path: str) -> str:
    """Probe dumps are live-network snapshots, not source — keep them out of git.

    The repo ignores the `*.local.*` family, so every output lands as `.local.json`
    whatever the caller asked for: `foo` / `foo.json` / `foo.local.json` -> `foo.local.json`.
    """
    if path.lower().endswith(".local.json"):
        return path
    for suffix in (".json", ".local"):
        if path.lower().endswith(suffix):
            path = path[: -len(suffix)]
    return path + ".local.json"


def probe_one(ip: str, port: int, args, known: dict) -> dict:
    entry: dict = {"address": f"{ip}:{port}", "ip": ip, "port": port}
    if f"{ip}:{port}" in known:
        entry["known_from_server_met"] = known[f"{ip}:{port}"]
    entry["udp"] = udp_probe(ip, port, args.timeout, args.retries)
    if args.tcp:
        entry["tcp"] = tcp_probe(ip, port, args.timeout, args.nick, args.listen_port, args.linger)
        # A server that accepts the TCP connection and then says nothing to a plain
        # login is the symptom in issue #6 — retry the same login over eMule's
        # obfuscated (DH-768/RC4) server handshake to tell "mute" from "crypt-only".
        silent = not entry["tcp"].get("packets") and entry["tcp"].get("connected")
        if silent and args.obf_retry:
            obf_port = ((entry["udp"].get("status") or {}).get("tcp_obfuscation_port") or port)
            entry["tcp_obfuscated"] = tcp_probe(ip, obf_port, args.timeout, args.nick,
                                                args.listen_port, args.linger, obfuscated=True)
    entry["fingerprint"] = guess_software(entry)
    return entry


def main() -> int:
    ap = argparse.ArgumentParser(description="Probe eD2K servers and dump all server info as JSON")
    ap.add_argument("servers", nargs="*", help="ip:port (defaults to the servers from issues #5/#6)")
    ap.add_argument("--met", help="also probe every server in this server.met")
    ap.add_argument("--known-met", default=os.path.expanduser("~/eMuleQt/Config/server.met"),
                    help="server.met consulted for the names already on file")
    ap.add_argument("--timeout", type=float, default=3.0, help="per-request UDP/TCP timeout (s)")
    ap.add_argument("--retries", type=int, default=2, help="extra UDP attempts per request")
    ap.add_argument("--linger", type=float, default=6.0, help="seconds to read TCP greeting")
    ap.add_argument("--jobs", type=int, default=8, help="parallel probes")
    ap.add_argument("--nick", default="eMuleQt", help="nick sent in OP_LOGINREQUEST")
    ap.add_argument("--listen-port", type=int, default=5662, help="TCP port advertised at login")
    ap.add_argument("--no-tcp", dest="tcp", action="store_false", help="skip the TCP login probe")
    ap.add_argument("--no-obf-retry", dest="obf_retry", action="store_false",
                    help="do not retry a silent plain login over the obfuscated handshake")
    ap.add_argument("--out", help="write JSON here instead of stdout (forced to a .local.json name)")
    args = ap.parse_args()

    targets: list[tuple[str, int]] = []
    for spec in args.servers:
        host, _, p = spec.rpartition(":")
        targets.append((host, int(p)))
    if args.met:
        targets += [tuple(k.split(":")) for k in load_met(args.met)]
        targets = [(h, int(p)) for h, p in targets]
    if not targets:
        targets = DEFAULT_SERVERS

    known = load_met(args.known_met)

    results: list[dict] = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = {pool.submit(probe_one, ip, port, args, known): (ip, port) for ip, port in targets}
        for fut in concurrent.futures.as_completed(futures):
            ip, port = futures[fut]
            try:
                results.append(fut.result())
            except Exception as exc:  # keep one bad server from killing the run
                results.append({"address": f"{ip}:{port}", "error": f"{type(exc).__name__}: {exc}"})

    order = {f"{ip}:{port}": i for i, (ip, port) in enumerate(targets)}
    results.sort(key=lambda e: order.get(e["address"], 1 << 30))
    doc = {"probed_at": time.strftime("%Y-%m-%dT%H:%M:%S%z"),
           "probe": {"timeout": args.timeout, "retries": args.retries,
                     "tcp_login": args.tcp, "nick": args.nick},
           "servers": results}
    text = json.dumps(doc, indent=2, ensure_ascii=False)
    if args.out:
        out_path = local_json_path(args.out)
        with open(out_path, "w", encoding="utf-8") as fh:
            fh.write(text + "\n")
        print(f"wrote {out_path} ({len(results)} servers)", file=sys.stderr)
    else:
        print(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
