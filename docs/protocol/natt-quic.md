# QUIC NAT-T transport (eD2K LowID to LowID)

Status 2026-10-25: implemented in eMuleQt (`src/core/net/quic/`), on by default with
`natTraversal`. It speaks the QUIC NAT-T dialect that eMuleAI introduced and aMule answers,
agreed in [emule-qt#39](https://github.com/ModderMule/emule-qt/issues/39). This page is the
wire contract as eMuleQt implements it. It lists every difference from the two other
implementations, so that they and new clients can line up with it.

The signalling that brings two LowIDs together (ed2k-server `0x60`–`0x62`, Lugdunum
`0xA6`/`0xA7`, eNode-go `PR_NAT`, the Kad buddy) is in [natt-rendezvous.md](natt-rendezvous.md).
This page starts once a peer's public UDP endpoint is known.

## Contents

- [What it is](#what-it-is)
- [Datagram framing](#datagram-framing)
- [Obfuscation](#obfuscation) (the part that differs)
- [QUIC and TLS profile](#quic-and-tls-profile)
- [Streams and the identity proof](#streams-and-the-identity-proof)
- [Capability exchange](#capability-exchange)
- [Who dials](#who-dials)
- [Advertising](#advertising)
- [Interop matrix](#interop-matrix)
- [Recommendations for other clients](#recommendations-for-other-clients)
- [Censorship (the Great Firewall)](#censorship-the-great-firewall)
- [Tests and rig](#tests-and-rig)

## What it is

QUIC is used as a socket layer under the normal eD2K client connection. Once QUIC is up, the
whole TCP byte stream rides inside it unchanged: hello, secure ident, file requests, blocks.
The RC4 stream obfuscation runs inside it too, as on TCP. No eD2K opcode knows about it.
HighID↔HighID connections stay on TCP. QUIC is used only where TCP cannot work: two firewalled
peers whose NATs were opened by a hole punch.

## Datagram framing

Everything shares the client UDP port. `OP_UDPRESERVEDPROT2` (`0xB2`) is followed by a frame
type byte, not an opcode:

| Type | Name | Payload |
|---|---|---|
| `0x00` | uTP (eMuleAI) | not spoken by eMuleQt, dropped |
| `0x01` | QUIC | one QUIC datagram |
| `0x02` | CAPS | 54-byte `EAQC` frame |
| `0x03` | CAPS_ACK | 54-byte `EAQC` frame |
| `0x11` | IPv6 handoff (eMuleQt) | [natt-rendezvous.md](natt-rendezvous.md) |
| `0xFF` | key frame | sender's user hash (16) |

Unknown types are dropped. A receiver must run the stock client-UDP de-obfuscation first: a
datagram whose first byte is `0xB2` is plain, anything else is tried as obfuscated, and an
inner `0xB2` is then dispatched the same way. eMuleAI, aMule and eMuleQt all do this.

## Obfuscation

**This is where eMuleQt differs from both reference implementations.** It is wire-compatible:
the receiving side needs no change.

Stock eMule client-UDP obfuscation (RC4, key = MD5(receiver hash | sender's public IP | `0x5B`
| 2 random bytes), 8-byte header) can hide `0xB2` frames, but it needs the receiver's user
hash.

| | eMuleAI | aMule | eMuleQt |
|---|---|---|---|
| Where the hash comes from | an inbound `0xFF` key frame only | never sends obfuscated | the rendezvous (ed2k-server `0x61`, Lugdunum `0xA7` with crypt bit `0x80`, PR_NAT, Kad buddy), then key frames, caps |
| First client Initial | usually **plain** (the peer's key frame has not arrived yet) | plain | **obfuscated** |
| Key frame `0xFF` | sent plain unless the hash is known: exposes the user hash | ignored, never sent | sent obfuscated |
| Caps `0x02`/`0x03` | always plain | always plain | obfuscated |
| Padding | 0 | 0 | random 0–15 bytes per datagram (`padLen & 0x0F`, which every receiver already skips) |
| Peer cannot decrypt | n/a | n/a | after 2 s with no answer, alternate plain and obfuscated until one is answered; plain is then kept for that peer |

Rules eMuleQt applies, per destination endpoint:

1. A hash from the rendezvous wins over one from a key frame. A key frame never overwrites an
   entry, it only refreshes it (as eMuleAI does). Entries expire after 10 min unused.
2. Obfuscate when the hash is known, the peer can read obfuscated datagrams (its crypt bits
   from the server say "supported", or are unknown), and we know our own public IP of the
   destination's family (it is part of the key). A LowID learns its IPv4 from `OP_IDCHANGE`
   bytes 12–15 (Lugdunum; ed2k-server ≥ 0.9.77, which sends 0 for a private source address),
   from Kad or from peers' `CT_MOD_YOUR_IP`. Without it every frame goes plain, and strict
   mode sends nothing.
3. Before the first long-header packet to a peer, and at most every 10 s, send `B2 FF <own
   hash>`, so that eMuleAI can obfuscate back to us.
4. **Plain fallback:** if obfuscated datagrams get nothing back for 2 s, the peer probably
   cannot decrypt them (we know a wrong public IP, CGNAT, several egress addresses). From then
   on every other datagram goes plain. If the first answer comes during this phase, plain is
   kept for that peer.
5. **Strict mode** (preference `natTraversalStrictObfuscation`, Options → Security → Protocol
   Obfuscation, default off): a plain `0xB2` frame never leaves. No fallback, no plain key
   frame or caps. Such a link fails rather than leak.

## QUIC and TLS profile

| Item | Value | Notes |
|---|---|---|
| QUIC version | 1 | no Version Negotiation is sent for others |
| Connection IDs | 8-byte SCID; client's first DCID 8 bytes | eMuleAI rejects an empty SCID |
| Datagram size sent | ≤ 1200 bytes of QUIC (`max_tx_udp_payload_size=1200`, no shaping, no PMTUD) | the Initial is exactly 1200: 1202 plain, 1210–1225 obfuscated |
| Datagram size accepted | up to 1452 | aMule probes the path MTU up to there |
| `initial_max_streams_bidi` / `_uni` | 1 / 0 | |
| `initial_max_stream_data_bidi_local` / `_remote` | 512 KiB / 512 KiB | local must be > 0: aMule writes on our stream |
| `initial_max_data` | 4 MiB | |
| `max_idle_timeout` | 60 s | |
| Keepalive | PING every 20 s once up | aMule closes idle links after 60 s and sends no keepalive |
| Congestion control | BBR | local choice |
| Handshake datagram repeat | the last long-header datagram again every 250 ms, up to 12×, until anything comes back | punches through a NAT that opened late; aMule admits one new connection per IP per 3 s, so repeats, not new connections |
| TLS | 1.3 only, OpenSSL ≥ 3.5 QUIC API (`ngtcp2_crypto_ossl`) | |
| ALPN | `ed2k-ai-natt-quic-v1`, mandatory on both sides | |
| Cipher suites | AES-128-GCM, AES-256-GCM, ChaCha20-Poly1305 | both GnuTLS peers offer these |
| Groups | **X25519, P-256 only** | OpenSSL 3.5's default hybrid ML-KEM share is over 1 KiB: it would split the ClientHello over several Initials and cost a HelloRetryRequest with GnuTLS |
| Client | SNI `emuleai-natt` (nobody checks it), no certificate, no verification | |
| Server certificate | self-signed ECDSA P-256, new per process | eMuleAI uses RSA-2048, aMule ECDSA P-256; nobody verifies, identity is the proof below |
| Session tickets, 0-RTT | none | |

## Streams and the identity proof

- Each side opens **its own** bidirectional stream after the handshake and writes only there.
  The client's is stream 0, the server's stream 1.
- Each side reads from the **first stream the peer delivers data on**. That makes eMuleQt
  work with both shapes:
  - eMuleAI writes on its own stream (stream 1 when it is the server);
  - aMule (server only) never opens a stream and replies on the client's stream 0.
- The first 37 bytes each way are the proof:

```
"EAQN1" | sender user hash 16 | expected peer hash 16 (zeros when unknown)
```

- eMuleQt sends its proof **as soon as the handshake completes**, without waiting for the
  peer's. aMule waits for the client's proof, so a client that waited too would deadlock.
- Validation: the magic must match, and the target field must be our hash or zero. When the
  rendezvous named the peer, the sender field must equal that hash, otherwise the link is
  closed. eMuleAI relaxes this for peers it has caps from; aMule doesn't check it.
- The socket above is attached only after both proofs, so nothing else precedes them.
- No FIN until the session ends. aMule tears the connection down on a FIN, and eMuleQt maps a
  FIN to "peer closed".

## Capability exchange

`B2 02` (CAPS) / `B2 03` (CAPS_ACK), 54 bytes:

```
"EAQC" (uint32 LE 0x43514145) | version 1 = 1 | options 1 | sender hash 16 | expected hash 16 | file hash 16
options: 0x01/0x02/0x04 crypt supported/requested/required, 0x08 direct callback, 0x40 QUIC, 0x80 uTP
```

- Dropped when the magic or version is wrong, or the expected hash is non-zero and not ours.
- eMuleQt sends a CAPS when an attempt learns the peer's endpoint, and answers every CAPS
  with a CAPS_ACK (at most one per address per second). Options are our crypt bits plus
  `0x40`; never `0x80`. The file hash is echoed.
- A non-zero sender hash teaches the endpoint the peer's hash if it had none.

## Who dials

- **The downloader (the rendezvous requester) dials QUIC.** It does so 1 s after the endpoint
  is known, or 2.5 s when we have a public IPv6 of our own, so that an IPv6 handoff from an
  eMuleQt peer can win first (TCP over IPv6 is preferred when both sides have it). This
  matches eMuleAI, where the uploader waits for the downloader's dial, and aMule, which can
  only be dialled.
- **IPv6.** QUIC prefers the peer's IPv6 UDP endpoint when the introducer gave one (eNode-go
  `PR_NAT` `SYNC_IPV6`, kept as `Attempt::peerUdp6`; both families are punched and armed).
  Then QUIC goes first, with no wait for the handoff (requester 1 s, target 6 s): the punch
  opens stateful IPv6 firewalls, which a TCP/IPv6 connect does not get through. TCP/IPv6
  follows on both sides 5 s later when nothing connected.
- **QUIC after TCP/IPv6.** On any other path where the handoff makes TCP/IPv6 the first choice,
  QUIC is the fallback: the requester dials it 7 s after the TCP decision, the target after
  12 s, when nothing connected and no QUIC link is up.
- The uploader (target) arms a passive expectation for the peer's address (20 s, any port on
  that IP, because NATs rebind). Only Initials from armed peers are accepted.
- Direct UDP path (Kad type 6 + `nat`, [natt-rendezvous.md](natt-rendezvous.md#direct-udp-kad-type-6--nat)):
  the source's UDP is open, so the downloader dials it right away (same delays); its
  `OP_DIRECTCALLBACKREQ` with `0x40` arms the source.
- The target dials itself only as a fallback: when nothing has connected 6 s after the
  endpoint was known (a peer that cannot dial, like aMule as downloader), or when a 30-byte
  ed2k-server `0x61` names it the dialer.
- Once the proofs are through:
  - the dialing side attaches the stream to the waiting source and sends the hello, as after
    a TCP connect;
  - the accepting side treats it like an accepted TCP connection, and the hello finds the
    known client by hash.

## Advertising

Advertised only while the transport runs (built in, TLS initialised, `natTraversal` on):

- `CT_MOD_MISCOPTIONS` bit 4 (`0x10`) in the hello;
- connect options `0x40` wherever the callback bit is computed (Kad source publishing, as
  eMuleAI's `GetMyConnectOptions`);
- Kad buddy rendezvous tail: transport hint `2`;
- the `0x40` bit in CAPS/CAPS_ACK;
- the Lugdunum login bit and LowID sources being kept while firewalled now also follow from
  QUIC, not only from a confident IPv6.

`0x80` (uTP) is never set: eMuleQt does not speak uTP.

## Interop matrix

| | eMuleAI | aMule (HEAD 2753015c2) | eMuleQt |
|---|---|---|---|
| Roles | client and server | server only | client and server |
| TLS library | GnuTLS | GnuTLS | OpenSSL ≥ 3.5 |
| Server cert | RSA-2048 | ECDSA P-256 | ECDSA P-256 |
| Max QUIC datagram | 1200 | 1452 + PMTUD | 1200 sent, 1452 accepted |
| Stream windows / connection | 512 KiB / 4 MiB | 256 KiB / 256 KiB, `bidi_local` 0 | 512 KiB / 4 MiB |
| Writes on | own stream | client's stream | own stream |
| Proof order | first | after the client's | first |
| Keepalive / idle | 25 s / none | none / 60 s | 20 s / 60 s |
| Obfuscated `0xB2` out | after a key frame | never | from the first Initial |
| Key frame | sends (plain until hash known) | ignores | sends obfuscated, honours |
| Plain fallback | n/a | n/a | after 2 s, unless strict |
| CAPS | sends and answers | answers only | sends and answers |
| uTP | yes (fallback) | separate | no |

Expected results with eMuleQt:

- **eMuleQt ↔ eMuleQt:** fully obfuscated, either side downloads.
- **eMuleQt downloads from eMuleAI:** we dial. Our obfuscated Initial is decrypted by eMuleAI.
  Our key frame lets it obfuscate back.
- **eMuleAI downloads from eMuleQt:** it dials, usually plain at first; we answer obfuscated.
- **eMuleQt downloads from aMule:** we dial and aMule replies plain. Works.
- **aMule downloads from eMuleQt:** aMule cannot dial. Our target fallback dials it after 6 s.
  aMule's QUIC server accepts that, and the eD2K session then runs normally.

## Recommendations for other clients

For eMuleAI, aMule and anyone adding this dialect:

1. **Obfuscate from the first packet.** The rendezvous already gives the peer's hash:
   ed2k-server `0x61` carries it, Lugdunum `0xA7` carries it when crypt bit `0x80` is set,
   and PR_NAT and the Kad buddy know it. Use it for the Initial, the key frame and the caps.
   Receivers need no change. This hides the Initial, SNI and ALPN from passive DPI.
2. **Never send the key frame plain**, unless there is no hash at all: a plain one publishes
   the user hash.
3. **Add random padding** (0–15 bytes, `padLen` low nibble). It costs nothing and every stock
   receiver skips it.
4. aMule: a **client role** (dial when downloading) and a **keepalive** under 60 s. Also send
   the proof without waiting for the peer's, so that two such stacks can meet.
5. Accept data on **either** stream (own-stream and reply-on-client-stream both exist in the
   field). Give `initial_max_stream_data_bidi_local` > 0.
6. Keep TLS groups to X25519/P-256 when using OpenSSL ≥ 3.5, and keep datagrams at 1200.
   aMule's PMTUD probes reach 1452 bytes of QUIC: with the 2-byte envelope, up to 23 bytes of
   obfuscation header and padding, and IP/UDP headers, that is about 1505 bytes. It is lost on
   a 1500 MTU path. Capping it at 1200 avoids the dead probes.
7. Don't FIN a stream that is still in use.

## Censorship (the Great Firewall)

eMuleAI made **no China- or GFW-specific change**. There is no such code, release note or
commit. Its only protection is the stock UDP obfuscation above, which in practice starts
after the first Initial. The "Gecko" size randomisation discussed on the eMule forum was
Andrey23's retired 0.72a NAT-T client, which no current peer understands. Background:
`quic-utp-china-feasibility.local.md`.

What eMuleQt's obfuscation does and doesn't do:

- **Hidden:** the QUIC long header, the salt-decryptable Initial, the ClientHello, SNI, ALPN,
  the `EAQC` magic and the user hashes. The datagram starts with a random marker byte.
- **Still visible:** sizes. The Initial is 1210–1225 bytes, still near the ~1200 cluster a
  statistical classifier keys on. Padding beyond 15 bytes or full size randomisation would
  need a new caps bit and peers that understand it.
- **Plain fallback:** it can still show a plain Initial when obfuscation fails. Strict mode
  removes that at the cost of such peers.

## Tests and rig

- `tests/tst_QuicNatProtocol`: `EAQN1`/`EAQC` codecs and rules.
- `tests/tst_QuicNatTransport`: two endpoints over an in-process pipe with real client-UDP
  obfuscation. Covers:
  - handshake and proof;
  - 8 MiB each way (≈ 0.9 s);
  - 5 % loss with reordering;
  - an obfuscated first Initial of the right size;
  - plain fallback when the peer cannot decrypt;
  - strict mode;
  - caps; close.
- `tests/tst_EMSocket` `transport_*`: the eD2K socket over a non-TCP stream (framing, the
  throttler thread, back-pressure, loss).
- `tests/tst_NatRendezvous` `quic_*`: who dials when; the IPv6 handoff still wins, QUIC after
  a TCP/IPv6 dial, QUIC first on a `SYNC_IPV6` endpoint.
- `tests/interop/quic_interop_driver.cpp` (option `EMULE_QUIC_INTEROP_TEST`, manual): our
  endpoint over real localhost UDP against a GnuTLS + ngtcp2 peer that mimics eMuleAI's client
  and server and aMule's server, byte for byte in settings.
  - Checked 2026-10-25: handshake with X25519 and no HelloRetryRequest; proof; 32 MiB each way
    at ~17–19 MiB/s; 5 % and 30 % loss; a key update started by the peer; 75 s of silence
    (our keepalive holds the link); a wrong proof rejected; NAT rebinding mid-transfer; a late
    peer reached by the 250 ms Initial repeats.
- `docker/natt/natnet.py --nat real`: two LowID daemons behind real NAT routers transfer
  files to each other over QUIC. The router captures are checked for no TCP between the
  peers and an obfuscated first Initial.
- `docker/natt/natnet.py --fw6`: both nodes behind a stateful IPv6 firewall. With `enode`
  QUIC over IPv6 carries the transfer; with `ed2k-server` TCP/IPv6 is dropped and the QUIC
  fallback connects ~8 s after the rendezvous.
