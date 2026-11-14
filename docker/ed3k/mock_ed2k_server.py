#!/usr/bin/env python3
"""ed3knet.py --index: minimal eD2K server: answers a plain OP_LOGINREQUEST with
OP_IDCHANGE, OP_SERVERSTATUS and OP_SERVERIDENT carrying ST_ED3K (0xE3) = an ed3k:node link.
TCP only, no obfuscation.

usage: mock_ed2k_server.py <tcp port> <ed3k:node link> [bind address, default 127.0.0.1]
"""
import socket
import struct
import sys
import threading
import time

PROT = 0xE3
OP_LOGINREQUEST = 0x01
OP_IDCHANGE = 0x40
OP_SERVERSTATUS = 0x34
OP_SERVERIDENT = 0x41
OP_SERVERMESSAGE = 0x38
OP_GETSERVERLIST = 0x14
OP_OFFERFILES = 0x15
ST_SERVERNAME = 0x01
ST_DESCRIPTION = 0x0B
ST_ED3K = 0xE3


def log(*a):
    print(time.strftime("%H:%M:%S"), *a, flush=True)


def packet(op, payload=b""):
    return struct.pack("<BIB", PROT, len(payload) + 1, op) + payload


def str_tag(tag_id, value):
    data = value.encode("utf-8")
    # old-style tag: type 2 (string), name length 1, name id
    return struct.pack("<BHB", 2, 1, tag_id) + struct.pack("<H", len(data)) + data


def recv_exact(conn, n):
    buf = b""
    while len(buf) < n:
        chunk = conn.recv(n - len(buf))
        if not chunk:
            raise ConnectionError("closed")
        buf += chunk
    return buf


def serve(conn, addr, link):
    log("connection from", addr)
    try:
        while True:
            hdr = recv_exact(conn, 6)
            prot, length, op = struct.unpack("<BIB", hdr)
            body = recv_exact(conn, length - 1)
            log(f"<<< prot=0x{prot:02X} op=0x{op:02X} len={length}")
            if prot != PROT:
                log("not a plain eD2K packet (obfuscated?), closing")
                return
            if op == OP_LOGINREQUEST:
                ip = socket.inet_aton(addr[0])
                client_id = struct.unpack("<I", ip)[0]
                conn.sendall(packet(OP_SERVERMESSAGE, struct.pack("<H", 11) + b"mock server"))
                conn.sendall(packet(OP_IDCHANGE, struct.pack("<II", client_id, 0)))
                conn.sendall(packet(OP_SERVERSTATUS, struct.pack("<II", 7, 42)))
                tags = [str_tag(ST_SERVERNAME, "mock ed3k server"),
                        str_tag(ST_DESCRIPTION, "names an ed3k index node"),
                        str_tag(ST_ED3K, link)]
                ident = bytes(16) + ip + struct.pack("<H", conn.getsockname()[1])
                ident += struct.pack("<I", len(tags)) + b"".join(tags)
                conn.sendall(packet(OP_SERVERIDENT, ident))
                log(">>> IDCHANGE, SERVERSTATUS, SERVERIDENT (ST_ED3K)", link)
    except (ConnectionError, OSError) as e:
        log("connection ended:", addr, e)
    finally:
        conn.close()


def main():
    port, link = int(sys.argv[1]), sys.argv[2]
    host = sys.argv[3] if len(sys.argv) > 3 else "127.0.0.1"
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind((host, port))
    srv.listen()
    log("listening on %s:%d" % (host, port))
    while True:
        conn, addr = srv.accept()
        threading.Thread(target=serve, args=(conn, addr, link), daemon=True).start()


if __name__ == "__main__":
    main()
