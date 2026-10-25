# LowID-to-LowID rendezvous (NAT traversal signalling)

Status 2026-10-26 (foreign callback added, `PR_NAT` cross-server sweep retired, ed2k-server#40;
QUIC data path since 2026-10-25): implemented in `src/core/net/natt/`. Two LowID peers learn each other's
public UDP endpoint from an introducer, open their NAT mappings with `OP_HOLEPUNCH`, and swap
IPv6 addresses over the opened path. With IPv6 on both sides the target then dials the
requester over ordinary TCP/IPv6. Otherwise the requester dials the QUIC NAT-T transport
(`0xB2`/`0x01`, eMuleAI's dialect) over the punched UDP path, see [natt-quic.md](natt-quic.md)
(2026-10-25).

Live-tested 2026-10-21 with all three servers: ed2k-server 0.9.84, eNode-go `PR_NAT` and
Lugdunum eserver 17.15, each introducing two LowID daemons that then transfer a 25 MiB file
over IPv6 (MD5-verified). The repeatable rig is `docker/natt/natnet.py`, see
[Live rig](#live-rig). The Kad buddy path is live-tested with `docker/kad/kadnet.py --nat-test`.

Before this, a LowID source was dropped while we were firewalled, and a LowID-to-LowID pair
ended in `LowToLowIP`.

## Introducers

Five paths, tried in this order for a source we want to download from:

| Path | Used when | Wire |
| --- | --- | --- |
| ed2k-server 0.9.84+ | our server sets `OP_IDCHANGE` flag `0x20000`, the peer is a LowID on it | TCP `0x60`/`0x61`/`0x62`, UDP keepalive `0x9F` |
| Lugdunum eserver 17.x, eNode-go | flag `0x1000` **and** a 24-byte `OP_IDCHANGE`, plus the server UDP key, an odd target ID, and an IPv4 session | server UDP `0xA6` out, UDP `0xA7` / TCP `0x37` in |
| eNode-go `PR_NAT` | flag `0x40000`, or `0x200000` when our login carried it; the peer is on our server and its user hash is known | UDP protocol `0xF1` to the `ST_NAT_PORT` (default 2004) |
| Foreign callback | the peer is a LowID on **another** server, which announces UDP status flag `0x100000` | server UDP `0xA6` with our ID 0 to that server, UDP `0xA7` back |
| Kad serving buddy | the source has a serving buddy, Kad connected | `OP_REASKCALLBACKUDP` with an embedded `OP_RENDEZVOUS` |

All UDP leaves the client UDP socket, so every introducer sees the mapping the peer must punch.

eNode-go (since 2026-10-25) sets both `0x1000` and `0x40000`. There `PR_NAT` goes before
Lugdunum whenever it can serve the peer, that is when the peer's hash is known: it is faster
and not IPv4-only. Lugdunum serves the hashless `OP_FOUNDSOURCES` rows. On any server, a
requester whose path produced no endpoint within 45 s moves to the next path that can serve
the peer; the peer is marked failed only when none is left.

The 24-byte `OP_IDCHANGE` is also the proof that our own login carried `0x1000` and crypt
support: both servers answer 24 bytes only then. So a public IPv6 that became confident
after login, or a crypt layer that is off, never leads to a request the server would drop.

### ed2k-server

Spec: the ed2k-server README, "Implementing a compatible client".

- Login: tag `CT_EMULE_UDPPORTS` (`0xF9`) = `(kadPort << 16) | udpPort` whenever the feature is on.
- Keepalive: `0xE3 0x9F <user hash 16>` every 60 s from the client UDP socket to the server's
  TCP port + 4, plain (obfuscated on an extension server, below), while logged in to a
  `0x20000` server. The server trusts the observed
  port for 600 s.
- Request: `0x60 <target id 4><our udp port 2>`, re-sent every 10 s (3 times) until an answer.
- Answer to both sides: `0x61 <ip 4><tcp 2><udp 2><hash 16><role 1>[<peer id 4>]`. `peer id`
  (0.9.84+) is read only when the payload has 29 bytes or more. A requester matches it by that
  ID, else by hash, else the single request in flight. Any other `0x61` makes us the target.
  Both sides punch. The role byte is advisory here; only a 30-byte `0x61` (below) makes it binding.
- `0x62 <target id 4><reason 1>` ends the request.

#### Hole-punch extension (`0x100000`, ed2k-server 0.9.85+)

Agreed in andrey23127/ed2k-server#39. It applies when the server sets `0x100000` in
`OP_IDCHANGE` and both clients logged in with `0x100000 | 0x1000`; we always send the two
together. Any other pair gets the plain flow above.

- Keepalive: on such a server, server-key obfuscated to the server's UDP obfuscation port
  (as `0xA6`) once we hold the key; plain until then. After the first obfuscated one the
  server ignores plain ones for the session, so we never go back to plain: without a key the
  keepalive is skipped and a new key requested.
- Requester: one `0x9F` right before each `0x60`, so the server has our fresh port.
- Target: the server wakes us with Lugdunum's `0x37`, byte-identical. We punch towards its
  endpoint at once, send `0x9F` at 0, 0.5 and 1 s (100 ms timer) and do not dial yet.
- The `0x61` that follows is 30 bytes: `peer_crypt(1)` appended, the peer's
  `(CT_SERVER_FLAGS >> 9) & 7` (support, request, require = `CONNECT_OPT_CRYPT_*`).
  Role `0` dials (at once, also as requester), role `1` never dials and waits.
- `peer_crypt` (and the crypt byte of a `0x37`/`0xA7`) decides obfuscation from the first
  packet: punch and handoff are obfuscated iff the peer supports it, and the TCP dial uses the
  same bits. A pair where one side requires what the other can't do fails at once.
- No `0x61` within 4 s of a `0x37`: it came from a plain `0xA6` (a Lugdunum-only requester), so
  we answer with the reciprocal `0xA6` and go on as a Lugdunum target. Not for a `0x37` naming
  requester ID 0 (a [foreign callback](#foreign-callback-0x100000)): there is no ID to name,
  so we just punch on.

### Lugdunum

Details and live captures: `lugdunum-nat-callback.local.md`.

- Login bit `0x1000` only while we have a data path: a confident public IPv6 (TCP handoff) or
  the QUIC transport. The server then returns LowID sources to us.
- Request: `0xE3 0xA6 <target id 4><our id 4>`, obfuscated with the server UDP key, to the
  server's UDP obfuscation port. eserver ignores it in plaintext. Repeated every 5 s.
- Notice: UDP `0xA7` (obfuscated with the same key, from the server's IP) or TCP `0x37`:
  `<ip 4><udp port 2 LE><requester id 4><flags 1>[<hash 16> if flags & 0x80]`.
- The requester hears nothing back, so the target sends its own `0xA6` naming the requester
  (mutual callback). The requester gets that as a `0xA7` with the target's endpoint.
- A notice naming requester ID 0 comes from a requester logged in elsewhere (foreign
  callback). We send no `0xA6` for it, which would name nobody; it goes to the wake-up
  handling below.
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

Spec: `eNode-go/docs/pr-nat-rendezvous-spec.md`.

- Frame `<0xF1><size 4 LE = payload + 1><opcode 1><payload>`; endpoints are big-endian.
- While logged in to a `0x40000` server and firewalled: `REGISTER 0xE4 <hash 16>` per family
  (IPv6 too when both sides have one), every 5 s until acked (after 6 unanswered, one a
  minute: a refused REGISTER gets no answer). The ack (`0xE4 <port BE><ip4>`,
  `0xEC <port BE><ip6>`, IPv6 ack only over IPv6) names the server's preferred endpoint; we
  send that family's later traffic there and accept frames from it (eNode-go: 2004 → its
  main UDP port 5559). All-zero parts keep the default. Then `KEEPALIVE 0xE6` every 60 s
  (server TTL 90 s; 30 s on eNode-go v0.5.1 and older). After 65 s without a `PING` (or ack),
  one unanswered keepalive, we register again, at the default endpoint: the server lost the
  registration or our NAT moved us to another port. A server change or logout drops the
  learned endpoint. The 60 s keepalive also holds our NAT mapping open: RFC 4787
  (REQ-5) asks NATs to keep an idle UDP mapping for at least 2 minutes, but some home routers
  drop it after 30–60 s, and then a late `SYNC` may not reach us. The wake-up opens a fresh
  mapping each time.
- Wake-up: our login sets `0x200000` whenever NAT traversal is on (`0x1000` still needs a
  data path at login: a trusted public IPv6 or the QUIC transport). A wake that comes while we have no data path is ignored. On a server that also sets flag
  `0x200000` we keep **no** registration. The server sends TCP `OP_NAT_WAKE 0x46 <families 1>`
  (`0x01` IPv4, `0x02` IPv6) when a peer asks for us, and we `REGISTER` once per family, IPv6
  first. As requester we send the `SYNC2` over IPv6 when we can and `REGISTER` over IPv4 just
  before it. An empty `OP_OFFERFILES` after 20 min of TCP silence replaces the keepalives as
  session activity (eNode-go closes a session after 3600 s without any).
- Request: `SYNC2 0xE9 <our hash 16><connAck 4><peer hash 16>`, up to 3 times.
- Both sides get `SYNC 0xE1` / `SYNC_EX 0xE7` (IPv4) or `SYNC_IPV6 0xED` with the other's
  endpoint; `FAILED 0xE5 <reason 1><hash 16>` moves the request to the next path (Lugdunum),
  like a timeout.
- Only frames from the server's IP, on its NAT port or main UDP port, are read.
- **Same server only.** We use `PR_NAT` only for a peer logged in to our own server, and we
  register (or get woken) only there. The cross-server sweep (`SYNC2` to other servers with UDP
  flag `0x40000`, `ST_NAT_PORT` saved in server.met) was retired on 2026-10-26: a registry
  keyed by user hash let anyone locate a known user without being in a transfer with them
  (ed2k-server#40). Peers on other servers go through the foreign callback. `ST_NAT_PORT`
  from older builds' server.met is dropped on load.
- `SYNC2` goes to the family whose `REGISTER` was acked. An IPv6-connected session has no
  IPv4 on eNode-go, so an IPv4 `REGISTER` there falls under the "no takeover from another IP"
  rule and is accepted. LowID sources from that session carry no server IPv4; they still
  count as "on our server" (as for `OP_CALLBACKREQUEST`).
- We match the peer on the punched path by source IP and the user hash in the handoff, not
  by `connAck`; a `SYNC` is tied to its attempt by the peer hash.

### Foreign callback (`0x100000`)

Agreed in andrey23127/ed2k-server#40. A requester asks the target's **own** server, where the
requester is not logged in. It knows that server and the target's LowID from source exchange,
a server's source answer or a saved source list; it names nothing else, so the server hands an
endpoint only to someone who already knows `server + LowID`, as on one server today.

- **Server flag.** UDP status flag `0x100000` (from `OP_GLOBSERVSTATRES` +24), the same
  number as the extension's TCP bit: "the extension, including requests from clients logged
  in elsewhere". The server sends it in every status reply, server probes included. It takes
  an `0xA6` from a client that is not logged in to it. `ST_UDPFLAGS` keeps the word in
  server.met. Without it this path does not serve the peer: no sweep, no other server is
  tried. The peer is then left to the same-server paths and the Kad buddy.
- **Requester → server.** `0xE3 0xA6 <target id 4><0 4><crypt 1>`, obfuscated with the UDP
  key that server gave our IP in a status ping, to its UDP obfuscation port, from the client
  UDP socket. The key is valid only for the address that received it, so it proves our source
  address. `crypt` is our `CONNECT_OPT_CRYPT_*` bits (servers that read only 8 bytes ignore
  it). Never plain. Re-sent every 5 s, 3 times. A missing key on a listed server asks for a
  stat ping first.
- **Server → target.** TCP `0x37 <requester ip 4><udp port 2><0 4><crypt 1>`: requester ID 0,
  no hash. Only to a target whose login carried `0x1000 | 0x100000` (it answers with the
  `0x9F` burst), so Lugdunum-only clients (NeoMule, NeoLoader) are never woken for this.
  ed2k-server also wakes those; they dial as on Lugdunum and the requester gets no `0xA7`.
  With our `crypt` byte the server applies the one-server crypt rule (mismatch: dropped) and
  puts the result in the `0x37`; without it, crypt 0.
- **Target.** Sends no `0xA6`. Punches the requester's endpoint at once and sends `0x9F`
  (obfuscated with its server's key) at 0, 0.5 and 1 s, so the server learns its fresh port.
  The attempt is keyed by the requester's address (it has no ID or hash yet); a repeat `0x37`
  from that address joins it. Crypt bits 0 mean unknown, not "unsupported". On an extension
  server a `0x61` may follow and is matched by the requester's address; without one the
  target punches on.
- **Server → requester.** On the target's first `0x9F`: UDP `0xA7 <target ip 4><udp port 2>
  <target id 4><crypt 1>[<hash 16>]`, obfuscated with the requester's key, from the server's
  UDP port. The hash (flag `0x80`) comes only when both sides use the crypt layer. No `0x9F`
  within 3 s: the same `0xA7` with the last port the server knows for the target (its recent
  `0xA6` or `0x9F`), or nothing when it knows none. The requester reads it only obfuscated,
  only from that server's IP, and matches it by target ID.
- **No oracle.** An unknown, HighID, gone or incapable target gets the requester no answer.
- **Server limits** (no session to kick, so per source IP): a burst of 10, then 1/s (eNode-go:
  `natTraversal` config); at most 60 different targets per 10 min; per target at most 6
  foreign wake-ups a minute; IP-filtered addresses refused.
- **Then** punch and handoff as for any path: TCP/IPv6 when both have IPv6, else QUIC. So
  IPv4-only pairs work too.

#### Servers outside our list

A source names its server; it need not be in our list. Pref `natDialUnlistedServers`
(Options → Server, "Always use a source's server for NAT callback", default **on**):

- **On.** An unlisted server is confirmed first by one obfuscated status ping, as eMule sends
  to listed servers: a raw random 4-byte challenge plus 0-15 random bytes to TCP port + 12.
  Only a reply encrypted with that challenge and echoing it counts. It carries the UDP flags, our UDP key and the obfuscation port.
  The server is kept in a small cache (64 entries, dropped after 1 h idle), never added to the
  list, saved or shown.
- **Off.** Only servers already in our list (got3nks' rule in #40).

A peer can name any address, so a bad one could aim pings at a victim. Limits:
- one ping per address per session, answered or not;
- at most 10 such pings per 10 minutes;
- only for a source of an active download;
- the address must pass the server IP rules (LAN only with `filterLANIPs` off) and the IP
  filter, and is never our own address;
- no answer within 15 s, or no `0x100000`, marks the address unusable for the session.

So each client sends a victim at most one datagram of 4-19 bytes per session.

### Kad serving buddy

The compatibility target's dialect, adopted unchanged (`natt-transports-emuleai.local.md` §9.6).

- Requester: 6 `OP_HOLEPUNCH` to the source's Kad endpoint (when known), then
  `OP_REASKCALLBACKUDP` to its serving buddy, plain:
  `<buddy id 16><zero 16><0xA0><our hash 16><opts | 0x20><file hash 16>[<our ip 4><udp port 2><hint 1>]`
  (66 bytes, or 73 when we know our public IPv4). Hint is 2 (QUIC) while the QUIC transport
  runs, else 0.
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
- No IPv6 on one side: the QUIC transport carries the connection
  ([natt-quic.md § Who dials](natt-quic.md#who-dials)). The requester dials QUIC 1 s after the
  endpoint is known (2.5 s with an IPv6 of our own, so that a handoff can win first); the
  target only after 6 s with nothing connected. Without QUIC the attempt fails as before.
- When the endpoint is known (and again when a punch moves it), the hub arms the QUIC
  endpoint for the peer's address and sends a CAPS.
- Peers that never send the `0x11` handoff (eMuleAI, aMule) are no longer waited for: QUIC
  does not need it.
- `peerEndpointReady(hash, endpoint, role)` is still emitted once per attempt.

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
- Nothing works without a data path: the QUIC transport (OpenSSL ≥ 3.5 build) or a confident
  public IPv6.
- Preference `natTraversal` (default on), Options → Server.

## Advertised

While the QUIC transport runs: `CT_MOD_MISCOPTIONS` bit 4 (`0x10`), connect option `0x40`
(where the callback bit is computed), Kad buddy hint 2 and the `0x40` caps bit. `0x80` (uTP)
is never set. Details: [natt-quic.md § Advertising](natt-quic.md#advertising).

## Live rig

`docker/natt/natnet.py` runs one server (two for `enode-foreign`) and two `emulecored` containers on
a Docker bridge network (`172.30.77.0/24`, `2001:db8:4677::/64`, lab mode). Each node refuses the server's
IPv4 dial-back with iptables, so both are LowIDs; A shares a random file, B downloads it.
The run passes when B's rendezvous used the chosen server, a side dialed the peer's IPv6,
and the MD5 matches.

```bash
cp docker/natt/natnet.env.example docker/natt/natnet.env    # point at your server copies
python3 docker/natt/natnet.py --server lugdunum --build      # first run: build the images
python3 docker/natt/natnet.py --server ed2k-server
python3 docker/natt/natnet.py --server enode --keep          # leave it up; --down later
python3 docker/natt/natnet.py --server enode-foreign         # foreign callback
python3 docker/natt/natnet.py --server enode-foreign --unlisted
```

`enode-foreign` tests the foreign callback with two eNode-go servers:
- A logs in to server 1. B logs in to server 2; iptables rejects B's TCP to server 1.
- Servers run on IPv4 sessions with `PR_NAT` off, in `198.51.100.0/24` (as
  `enode-lugdunum`): the callback and the `0x9F` burst are IPv4, and the UDP key needs a
  public IPv4.
- B learns A from a saved source list (SLS): B adds the link, stops, gets a `.txtsrc` naming
  A's LowID on server 1, and restarts.
- By default B's `server.met` lists server 1 with its live UDP flags (`0x100000`). With
  `--unlisted` it does not, and B confirms server 1 with one obfuscated status ping first.
- Passes when B used `ForeignCallback` and read server 1's `0xA7`, A was woken with
  requester ID 0, B never logged in to server 1, and the file arrived with a matching MD5.
  `--unlisted` also checks that exactly one ping went to server 1 and it answered.
- One-bridge mode only: `--nat real|symmetric` runs a single server.

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

### Real NAT (`--nat real`, `--nat symmetric`)

The default `--nat none` is the one-bridge rig above. `--nat real` tests the QUIC NAT-T
transport through real Linux NATs, with IPv6 off, so the IPv6 handoff cannot carry anything:

| Network | Subnet | Members |
| --- | --- | --- |
| `natpub` | `11.78.0.0/24` | server `.10`, router A `.21`, router B `.22` |
| `lanA` | `10.77.1.0/24` | router A `.254`, node A `.11` |
| `lanB` | `10.77.2.0/24` | router B `.254`, node B `.12` |

- Routers (`docker/natt/router`, alpine): `MASQUERADE` out of the public side; `FORWARD` lets
  LAN→pub out and only `ESTABLISHED,RELATED` back in. Linux conntrack keeps the source port
  and filters by remote address and port: a port-restricted cone. `--nat symmetric` adds
  `--random-fully` (a new port per destination), so the punch must fail.
- Nodes sit on their LAN only, default route via the router's `.254` (docker's `.1` gateway is
  never used), `disable_ipv6=1`, no `publicIPv6Override`. They are LowIDs on their own: the
  router drops the server's dial-back. One server only (`enode2` needs `--nat none`).
- Each node shares its own random file and downloads the other's at the same time.
- `--strict` sets `encryption.natTraversalStrictObfuscation` on both nodes (any mode).
- The routers drop unsolicited input to themselves, as a home router does. Accepted, the
  peer's first punch confirms a conntrack entry on the router, our own mapping towards that
  peer then clashes with it and gets a new port, and the peer punches the wrong one.
- `natpub` is public address space on purpose. A server reports a LowID's own IPv4 in
  `OP_IDCHANGE` only when it is public (ed2k-server sends 0 for RFC 1918), and without it the
  node cannot key UDP obfuscation: every QUIC frame would go out plain. The rig checks that
  each node learnt its router's address at login. The TEST-NET ranges fail `isPublicIP()` too.
- Captures: `run/routerA|B/pub.pcap` (public side) and `lan.pcap`; `rules.txt` has the rules.

`--nat real` passes when: both are LowIDs and learnt their router's address; the rendezvous
ran via the chosen server; one side logged `NAT-T: #<n> QUIC dialing`, both `NAT-T: QUIC connected <ip:port> (out|in)`; both files
arrived with matching MD5s; the router captures show no TCP between `.21` and `.22`; the first
UDP datagram of 1200 bytes or more each way does not start with `0xB2` (the QUIC Initial was
obfuscated). With `--strict`, no datagram between the routers starts with `0xB2` at all.
`--nat symmetric` passes when the QUIC dial was made and failed cleanly (`NAT-T: QUIC to …
failed`, or no link before `--timeout`) with both daemons still running.

```bash
python3 docker/natt/natnet.py --server ed2k-server --nat real
python3 docker/natt/natnet.py --server ed2k-server --nat real --strict
python3 docker/natt/natnet.py --server ed2k-server --nat symmetric --timeout 120
```

Timings on one Apple Silicon host: ed2k-server and eNode-go about 4 s from adding the link
to the verified file; Lugdunum about 20 s, most of it eserver's ~12.5 s TCP `0x37` delay.

Kad buddy: `python3 docker/kad/kadnet.py --nat-test` (20 nodes; `docker/kad/Readme.md`). The
source sits behind a NAT and an IPv6 firewall, so the downloader's direct v6 dial is refused
and it falls back to the source's buddy. About 5.5 s from adding the link to the verified
file, after ~5.5 min waiting for the source's buddy.
