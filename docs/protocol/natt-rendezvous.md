# LowID-to-LowID rendezvous (NAT traversal signalling)

Status 2026-10-21: implemented in `src/core/net/natt/`. Two LowID peers learn each other's
public UDP endpoint from an introducer, open their NAT mappings with `OP_HOLEPUNCH`, and swap
IPv6 addresses over the opened path. The target then dials the requester over ordinary
TCP/IPv6. A UDP transport for pairs without IPv6 (eMuleAI's QUIC NAT-T dialect,
`0xB2`/`0x01`) is planned and will hook into the same place.

Live-tested 2026-10-21 with all three servers: ed2k-server 0.9.84, eNode-go `PR_NAT` and
Lugdunum eserver 17.15, each introducing two LowID daemons that then transfer a 25 MiB file
over IPv6 (MD5-verified). The repeatable rig is `docker/natt/natnet.py`, see
[Live rig](#live-rig). The Kad buddy path is live-tested with `docker/kad/kadnet.py --nat-test`.

Before this, a LowID source was dropped while we were firewalled, and a LowID-to-LowID pair
ended in `LowToLowIP`.

## Introducers

Four paths, tried in this order for a source we want to download from:

| Path | Used when | Wire |
| --- | --- | --- |
| ed2k-server 0.9.84+ | our server sets `OP_IDCHANGE` flag `0x20000`, the peer is a LowID on it | TCP `0x60`/`0x61`/`0x62`, UDP keepalive `0x9F` |
| Lugdunum eserver 17.x | flag `0x1000` **and** a 24-byte `OP_IDCHANGE`, plus the server UDP key | server UDP `0xA6` out, UDP `0xA7` / TCP `0x37` in |
| eNode-go `PR_NAT` | flag `0x40000`, peer's user hash known | UDP protocol `0xF1` to the `ST_NAT_PORT` (default 2004) |
| Kad serving buddy | the source has a serving buddy, Kad connected | `OP_REASKCALLBACKUDP` with an embedded `OP_RENDEZVOUS` |

All UDP leaves the client UDP socket, so every introducer sees the mapping the peer must punch.

### ed2k-server

Spec: the ed2k-server README, "Implementing a compatible client".

- Login: tag `CT_EMULE_UDPPORTS` (`0xF9`) = `(kadPort << 16) | udpPort` whenever the feature is on.
- Keepalive: `0xE3 0x9F <user hash 16>` every 60 s from the client UDP socket to the server's
  TCP port + 4, plain, while logged in to a `0x20000` server. The server trusts the observed
  port for 600 s.
- Request: `0x60 <target id 4><our udp port 2>`, re-sent every 10 s (3 times) until an answer.
- Answer to both sides: `0x61 <ip 4><tcp 2><udp 2><hash 16><role 1>[<peer id 4>]`. `peer id`
  (0.9.84+) is read only when the payload has 29 bytes or more. A requester matches it by that
  ID, else by hash, else the single request in flight. Any other `0x61` makes us the target.
  The role byte is ignored: both sides punch.
- `0x62 <target id 4><reason 1>` ends the request.

### Lugdunum

Details and live captures: `lugdunum-nat-callback.local.md`.

- Login bit `0x1000` only while we hold a confident public IPv6: the server then returns LowID
  sources to us, and the IPv6 handoff is the only data path we have for them.
- Request: `0xE3 0xA6 <target id 4><our id 4>`, obfuscated with the server UDP key, to the
  server's UDP obfuscation port. eserver ignores it in plaintext. Repeated every 5 s.
- Notice: UDP `0xA7` (obfuscated with the same key, from the server's IP) or TCP `0x37`:
  `<ip 4><udp port 2 LE><requester id 4><flags 1>[<hash 16> if flags & 0x80]`.
- The requester hears nothing back, so the target sends its own `0xA6` naming the requester
  (mutual callback). The requester gets that as a `0xA7` with the target's endpoint.
- A notice naming our own ID only reflects our mapped port and is ignored.
- No `0xA6` for an even-LowID target: callback-capable clients always get an odd LowID.
- The UDP key and obfuscation port come from the first stat ping, a few seconds after the
  LowID sources. A request waits for them and asks for that ping at once; after 3 unanswered
  requests it asks for a fresh key once, because a restarted eserver ignores the old one
  without a word. Both are read from the server list entry: the session's copy of the
  server is never updated by a stat ping.
- eserver returns LowID sources only to requesters it counts as eMule, which needs the user
  hash markers (byte 5 = `0x0E`, byte 14 = `0x6F`).

### eNode-go `PR_NAT`

Spec: `eNode-go/docs/ipv6-client-implementation-spec.md` §9.

- Frame `<0xF1><size 4 LE = payload + 1><opcode 1><payload>`; endpoints are big-endian.
- While logged in to a `0x40000` server and firewalled: `REGISTER 0xE4 <hash 16>` per family
  (IPv6 too when both sides have one), every 5 s until acked, then `KEEPALIVE 0xE6` every 20 s
  (server TTL 30 s).
- Request: `SYNC2 0xE9 <our hash 16><connAck 4><peer hash 16>`, up to 3 times.
- Both sides get `SYNC 0xE1` / `SYNC_EX 0xE7` (IPv4) or `SYNC_IPV6 0xED` with the other's
  endpoint; `FAILED 0xE5 <reason 1><hash 16>` ends the request.
- Only frames from the server's IP, on its NAT port or main UDP port, are read.
- `SYNC2` goes to the family whose `REGISTER` was acked. On an IPv6 server session only the v6
  register is accepted, and LowID sources from that session carry no server IPv4; they still
  count as "on our server" (as for `OP_CALLBACKREQUEST`).
- Same-server only. Cross-server use needs the target's `(hash, rendezvous server)` carried in
  Kad or source exchange, which nothing defines yet.

### Kad serving buddy

The compatibility target's dialect, adopted unchanged (`natt-transports-emuleai.local.md` §9.6).

- Requester: 6 `OP_HOLEPUNCH` to the source's Kad endpoint (when known), then
  `OP_REASKCALLBACKUDP` to its serving buddy, plain:
  `<buddy id 16><zero 16><0xA0><our hash 16><opts | 0x20><file hash 16>[<our ip 4><udp port 2><hint 1>]`
  (66 bytes, or 73 when we know our public IPv4). Hint is 0 until we speak QUIC.
- Buddy (we serve one): the existing relay forwards it as a stock `OP_REASKCALLBACKTCP`. When
  the envelope has the zero hash, `0xA0` and option `0x20`, we also send the requester
  `OP_NATT_ENDPOINT_HINT` (`0xAA`, 56 bytes:
  `<ver 1><target hash 16><buddy id 16><file hash 16><ip 4><port 2><opts 1>`) and one
  `OP_HOLEPUNCH`. The port is the served client's Kad port (ToDo: a per-client observed port).
- Target: `processReaskCallbackTCP` sees the zero file hash, reads the embedded opcode and
  starts a target attempt at the endpoint the buddy saw. The self-reported tail is used only
  when that endpoint is not a good public address.
- The requester accepts a hint only from the source's buddy endpoint, naming the buddy ID it
  sent to. The target's punch from the target's IP also gives the endpoint.
- A firewalled source with a confident IPv6 publishes it next to its buddy, and the
  downloader dials it first. When that dial fails (the source's v6 inbound is firewalled),
  `UpDownClient::disconnected` drops the v6 route and re-asks the source the LowID way, which
  is this path. Without it the source died on the failed dial.

## Punch and IPv6 handoff

Once an attempt knows the peer's UDP endpoint:

- `0xC5 0xA1` (`OP_HOLEPUNCH`, empty) every second for 20 s. A punch from the peer's IP on
  another port moves the endpoint there (the datagram source wins).
- The handoff, `OP_UDPRESERVEDPROT2` (`0xB2`) frame type `0x11`, every 2 s until the peer's
  arrives, then once as an answer:

```
<version 1 = 1><flags 1><client id 4><user hash 16><connect options 1><tcp port 2>[<ipv6 16>]
flags: 0x01 IPv6 present, 0x02 answer (do not reply)
```

- Sent obfuscated with the client key of the peer's hash when we know it, else plain.
- Accepted only for a running attempt, from the peer's IP, with a matching hash when known.
  The address must be a good, unfiltered, unbanned global IPv6.
- The target dials as soon as it has the peer's address; the requester dials only if no
  connection is up 5 s later. The connection is a normal eD2K TCP connection and attaches to
  the waiting source by its hello.
- No IPv6 on one side: the attempt fails (ToDo: hand the endpoint to the QUIC transport).
- `peerEndpointReady(hash, endpoint, role)` is emitted once per attempt for that transport.

`0x11` is not in use by the other `0xB2` frame types (`0x00`-`0x03`, `0xFF`: eMuleAI).

## Limits and failure handling

| Rule | Value |
| --- | --- |
| attempt timeout | 45 s, stretched to 25 s after the endpoint arrived |
| concurrent attempts | 32 |
| new target attempts per peer IP | 1 per 5 s |
| failed requester attempt | peer not tried again this session; the source stays as `LowToLowIP` |
| failed target attempt | notices from that peer ignored for 30 min |
| attempt ended (either way) | repeat notices and handoffs from that peer ignored for 10 s (ed2k-server re-sends `0x61` twice over ~3 s) |

## Client flow

- `UpDownClient::tryToConnect`: while we are firewalled (no HighID, Kad not open) and a path
  can serve the source, a LowID source goes to `ConnectingState::NatRendezvous` instead of
  failing. `askForDownload` and `PartFile::process` treat such a source as reachable.
- LowID sources are kept while firewalled when a path could serve them: server sources,
  Kad firewalled sources (types 3/5) and source exchange.
- Nothing works without a data path, which today is a confident public IPv6.
- Preference `natTraversal` (default on), Options → Server.

## Not advertised

`CT_MOD_MISCOPTIONS` bit 4 (QUIC NAT-T) and connect options `0x40`/`0x80` stay unset until the
QUIC transport works. Setting them would make eMuleAI peers try a transport we cannot serve.

## Live rig

`docker/natt/natnet.py` runs one server and two `emulecored` containers on a Docker bridge
network (`172.30.77.0/24`, `2001:db8:4677::/64`, lab mode). Each node refuses the server's
IPv4 dial-back with iptables, so both are LowIDs; A shares a random file, B downloads it.
The run passes when B's rendezvous used the chosen server, a side dialed the peer's IPv6,
and the MD5 matches.

```bash
cp docker/natt/natnet.env.example docker/natt/natnet.env    # point at your server copies
python3 docker/natt/natnet.py --server lugdunum --build      # first run: build the images
python3 docker/natt/natnet.py --server ed2k-server
python3 docker/natt/natnet.py --server enode --keep          # leave it up; --down later
```

- Server binaries and checkouts stay outside the repo; `natnet.env` (git-ignored) names them.
  ed2k-server is built in a `rust` container, eNode-go is cross-built with the host's `go`,
  Lugdunum takes the i686 binary and runs it under qemu-i386.
- `--build` rebuilds the daemon image from this tree (`docker/emuleqt/Dockerfile` plus
  `docker/natt/daemon.Dockerfile`); without it the last image is reused.
- Run dirs, logs and per-node packet captures (`config/capture.pcap`) are in
  `~/.cache/emuleqt-natnet/run` (`NATNET_WORK`), cleared at the next run.
- `--console vc` sends a command to the Lugdunum console (`vc` clients, `g` stats).
- Each run starts a fresh server and fresh `server.met` files: a UDP key kept from an
  earlier eserver instance is dead, and a few logins per IP blacklist an address
  (`blacktime=1` in the rig).

Timings on one Apple Silicon host: ed2k-server and eNode-go about 4 s from adding the link
to the verified file; Lugdunum about 20 s, most of it eserver's ~12.5 s TCP `0x37` delay.

Kad buddy: `python3 docker/kad/kadnet.py --nat-test` (20 nodes; `docker/kad/Readme.md`). The
source sits behind a NAT and an IPv6 firewall, so the downloader's direct v6 dial is refused
and it falls back to the source's buddy. About 5.5 s from adding the link to the verified
file, after ~5.5 min waiting for the source's buddy.
