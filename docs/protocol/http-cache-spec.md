# HTTP Cache — Encrypted Chunk Offload Specification

**Status:** Implemented in eMuleQt.
**Document version:** 1.0 — 2026-08-17.
**Audience:** implementers of eD2K clients that want to interoperate with eMuleQt's HTTP Cache, and
operators writing a cache-server backend.

HTTP Cache is the successor to **PeerCache**, which shipped in MFC eMule from v0.43a (2004) until it
was deleted in 0.70a (2023). It keeps PeerCache's one good idea — when several peers want the same
bytes, spend the upstream once and let them fetch it over HTTP — and discards everything that made
PeerCache unusable.

| | PeerCache (MFC 0.43a–0.60d) | HTTP Cache |
|---|---|---|
| Cache discovery | DNS probing for `edcache.p2p.<domain>` on the downloader's ISP | configuration: one base URL + API key |
| Unit of transfer | one 184 KB block, one TCP connection each | one whole 9,728,000-byte part, one request |
| Who initiates | downloader asks uploader to push into *its* ISP cache | uploader publishes, then offers |
| Payload | plaintext file data | AES-256-CBC ciphertext |
| Key custody | n/a | uploader generates per chunk; server never sees it |
| Obfuscation | mutually exclusive (`IsPeerCacheDownloadEnabled` requires crypt off) | orthogonal |
| Reach | one ISP | anywhere the URL resolves |

This document specifies **only what is implemented**. Section 9 lists reserved-but-unused elements,
which MUST NOT be relied upon.

---

## 0. Conventions

### 0.1 Requirement levels

MUST, MUST NOT, SHOULD, SHOULD NOT and MAY are to be interpreted as in RFC 2119.

### 0.2 Byte order and framing

eD2K is little-endian. All integer fields below are little-endian. Tags are **new-style (short) eD2K
tags**, written with `Tag::writeNewEd2kTag` (`src/core/protocol/Tag.cpp`).

### 0.3 Authoritative files

| Area | File |
|---|---|
| Opcode, sub-opcodes, tags, capability bit | `src/core/utils/Opcodes.h` |
| Packet codec | `src/core/httpcache/HttpCacheOffer.{h,cpp}` |
| Uploader policy | `src/core/httpcache/HttpCacheManager.{h,cpp}` |
| Publish (encrypt + POST) | `src/core/httpcache/HttpCachePublisher.{h,cpp}` |
| Downloader (GET + decrypt) | `src/core/httpcache/HttpCacheClient.{h,cpp}` |
| Cipher | `src/core/crypto/AesCbc.{h,cpp}` |
| Reference server | `/Applications/XAMPP/xamppfiles/htdocs/emule-http-cache-php` |

---

## 1. Capability negotiation

A client advertises support in `CT_MOD_MISCOPTIONS` (0xAA, uint32) in the eMule hello / hello-answer:

```c
#define MODMISC_HTTPCACHE  (1u << 10)
```

Bit 10, not the first free bit. Bits 1, 3 and 4 belong to the compatibility target, whose own
`UModMiscOptions` block ends at bit 4; bits 6–9 are left as growth room for it, so the two
implementations do not collide as they evolve.

A client MUST NOT send any `OP_HTTPCACHE` packet to a peer that has not set this bit. That single
rule is what makes the feature invisible to every existing eD2K client.

The bit means *"I can parse `OP_HTTPCACHE`"*, not *"I have the feature switched on"*. eMuleQt
advertises it unconditionally and decides locally whether to act, exactly as MFC advertised its
PeerCache bit unconditionally (`srchybrid/BaseClient.cpp:1061`). A peer that toggles the feature at
runtime therefore does not need to re-handshake.

Parsed into `UpDownClient::m_supportsHttpCache`; readable via `supportsHttpCache()`.

## 2. Opcode

```c
#define OP_HTTPCACHE  0xBC   // on OP_EMULEPROT (0xC5)
```

Every message shares one frame:

```
<version 1><sub-opcode 1><tagcount 1>[tags]
```

**Why 0xBC.** Stock eMule 0.50a/0.51d/0.70, MorphXT 12.7, Applejuice and eSE-LiveTV all end their
extended C2C TCP opcode space at `OP_HASHSETANSWER2 0xB2`. The compatibility target claims
**0xB3–0xBB** for its eServer buddy relay. 0xBC is the first value unclaimed in every tree on hand.
It also exists as a `CT_*` tag id, but tags live inside payloads and opcodes in the header, so the
two never meet on the wire.

One opcode with a sub-opcode, rather than PeerCache's three opcodes, keeps the message space open at
no further cost to a scarce namespace.

`OP_PEERCACHE_QUERY/ANSWER/ACK` (0x94–0x96) remain defunct and are never dispatched.

### 2.1 Sub-opcodes

| Value | Name | Direction | Meaning |
|---|---|---|---|
| 0x00 | `HCOP_NONE` | downloader → uploader | Declined without trying. `HCTAG_RESULT` says why. |
| 0x01 | `HCOP_OFFER` | uploader → downloader | A chunk is cached; here is where and how to read it. |
| 0x02 | `HCOP_RESULT` | downloader → uploader | Outcome of a fetch that was attempted. |
| 0x03 | `HCOP_CANCEL` | uploader → downloader | Offer withdrawn; fall back to eD2K. |

`version` MUST be `HCPCK_VERSION` = 0x01. A receiver MUST reject any other value.

An unknown **sub-opcode** MUST be treated as an error, not skipped: the sender is waiting on a reply
that would otherwise never come.

### 2.2 Tags

| Id | Name | Type | Meaning |
|---|---|---|---|
| 0x01 | `HCTAG_FILEID` | hash16 | eD2K file hash |
| 0x02 | `HCTAG_PARTINDEX` | uint32 | part number within that file |
| 0x03 | `HCTAG_PLAINLEN` | uint32 | plaintext bytes in this part |
| 0x04 | `HCTAG_URL` | string | absolute download URL, ≤ 1024 chars |
| 0x05 | `HCTAG_KEYIV` | blob(48) | 32-byte AES-256 key ‖ 16-byte IV |
| 0x06 | `HCTAG_CIPHERLEN` | uint32 | ciphertext byte length |
| 0x07 | `HCTAG_CIPHERSHA` | blob(32) | SHA-256 of the ciphertext |
| 0x08 | `HCTAG_EXPIRES` | uint32 | unix time the URL stops working; 0 = unknown |
| 0x09 | `HCTAG_RESULT` | uint8 | `HttpCacheResult` (§5) |
| 0x0A | `HCTAG_BYTES` | uint32 | bytes actually fetched |

An unknown **tag id** MUST be skipped, so the set can grow without a version bump. `HCTAG_FILEID`
and `HCTAG_PARTINDEX` are mandatory in every message.

`HCOP_OFFER` carries tags 0x01–0x08 (8 tags). `HCOP_RESULT` and `HCOP_NONE` carry 0x01, 0x02, 0x09,
0x0A (4 tags). `HCOP_CANCEL` carries 0x01 and 0x02 (2 tags).

---

## 3. Flow

```
 Uploader U                                    Downloaders D1, D2 …
 ──────────                                    ────────────────────
 hello: MODMISC_HTTPCACHE   <────────────────> hello: MODMISC_HTTPCACHE

 every 5 s: scan the upload queue
   seed  = a part some uploading peer is requesting blocks in
   group = capable peers (uploading OR waiting) on the same file
           that are missing that part
   fire when |group| >= minClients and the part is a full PARTSIZE we hold
        │
        ├─ read part P                          (KnownFile::dataFilePath)
        ├─ AES-256-CBC under a fresh random key+IV, SHA-256 the ciphertext
        └─ POST {baseUrl}/v1/chunks             (§4)
                └─ 201 {"id","url","size","expires"}
        │
        ├─ OP_HTTPCACHE/HCOP_OFFER ──────────>  D1
        ├─ OP_HTTPCACHE/HCOP_OFFER ──────────>  D2
        │     (offline or LowID peers: the packet is queued and
        │      tryToConnect() reaches them via a direct UDP callback,
        │      the shared server's OP_CALLBACKREQUEST, or a Kad buddy)
        │
        └─ any offered peer holding an upload slot is sent
           OP_OUTOFPARTREQS and re-queued — the offer replaces the slot
                                                 │
                                                 ├─ validate hard (§6)
                                                 ├─ GET url, Range-capable
                                                 ├─ verify SHA-256 of ciphertext
                                                 ├─ streaming AES-CBC decrypt
                                                 ├─ PartFile::writeToBuffer()
        <────── OP_HTTPCACHE/HCOP_RESULT ────────┤  (the fetch went fine)
                                                 └─ ordinary MD4 part check, later
        <────── HCOP_RESULT: Corrupt ────────────┘  (only if the part fails; §6.1)
```

### 3.1 When a chunk is published

All of the following MUST hold:

1. `httpCache.enabled` and `httpCache.allowUpload`, with a base URL and an API key configured.
2. At least one peer currently in an upload slot has an outstanding block request inside part P.
   Publishing is never speculative: a part nobody has asked for may never be asked for.
3. At least `minClients` (default 2) HTTP-Cache-capable peers in the upload queue — **uploading or
   waiting** — want the same file and report part P as missing in their `OP_FILESTATUS` part map.
4. The part is a whole `PARTSIZE` = 9,728,000 bytes. A file's short tail part is excluded: it is by
   definition the least-shared part of the file.
5. We hold every byte of it (`KnownFile::isPartComplete`, overridden by `PartFile` to consult its
   gap list — so a partially-downloaded file can publish the parts it already has).
6. No live entry already covers `(fileHash, partIndex)`, and no publish for it is in flight.
7. The daily publish budget has room.

Waiting peers are the ones the feature helps most: they receive their bytes without ever costing an
upload slot.

### 3.2 The slot is replaced, not shared

When an offer goes to a peer that holds an upload slot, the uploader immediately calls
`sendOutOfPartReqsAndAddToWaitingQueue()` — sending `OP_OUTOFPARTREQS` and returning the peer to the
waiting queue. This is the existing "I cannot serve you, re-ask later" path, so no new peer-visible
behaviour is introduced.

The freed slot then goes to a peer the cache cannot help. This is the whole economic argument: the
first offered peer pays for the upload, every peer after that rides along free.

### 3.3 Delivery to unreachable peers

Offers are sent with `UpDownClient::safeConnectAndSendPacket()`. When there is no socket the packet
is queued and `tryToConnect()` runs the normal ladder: direct TCP → direct UDP callback
(`OP_DIRECTCALLBACKREQ`) → the shared server's `OP_CALLBACKREQUEST` → a Kad buddy callback. A LowID
peer that dropped its idle queue connection still receives the offer.

Two constraints follow from that machinery and are handled by the manager, not the socket layer:
`tryToConnect()` throttles retries to once a minute, and the pending-packet queue never expires on
its own, so the manager stops offering an entry once it lapses.

Replies (`HCOP_RESULT` / `HCOP_NONE`) are sent only over an existing connection. Dialling a peer to
deliver a decline is not worth a callback round trip, and by the time one completed the offer would
be stale anyway.

### 3.4 Relaying a chunk you fetched

A downloader that has fetched a chunk holds everything needed to serve it again: the URL,
the key, the IV and the digest. Relaying is handing that same `HCOP_OFFER` on to peers on its
*own* upload queue that are missing the same part.

**There is no wire change and no relay marker.** A relayed offer is byte-for-byte the same shape
as a first-hand one. `HttpCacheOffer` carries no publisher identity, and the chunk id inside the
URL is a random capability the server minted, so a receiver cannot tell who originally published
the chunk — and cannot tell a relay from an original either. That indistinguishability is a
feature to protect: **a hop count, an origin field or any "this was relayed" flag MUST NOT be
added**, because it would be the only thing in the message capable of narrowing down the origin.

A chunk MUST NOT be relayed until the receiving client's own MD4 part check has passed. §6.1
blames the peer that made the offer, and under relay that peer is the relayer; relaying is
therefore vouching for the bytes, and only a completed MD4 check justifies it. Concretely, all
of the following must hold before an entry is promoted:

1. `httpCache.enabled` and `httpCache.allowRelay`. **No base URL and no API key are required** —
   the relayer never talks to the cache server at all. A node with no cache account can relay.
2. The part passed MD4, and a real per-part hash existed to compare against.
3. `HCTAG_EXPIRES` is non-zero and still has margin. A zero expiry means "never lapses", which is
   right for a chunk you published and wrong for one you are borrowing: the owner can `DELETE` it
   at any moment, and the entry would otherwise be offered forever.
4. The peer that supplied the chunk is pre-excluded from the offer list.

A relayer does not own the blob, so it never sends `DELETE`, and its entry lapses at the
*original* publisher's TTL. If the part later fails a hash check — a disk fault, an AICH
recovery that rewrote it — the relay entry is withdrawn.

Once a relayed entry lapses, the ordinary publish scan (§3.1) may pick the same part up and
publish it under a fresh key of its own, since by then the relayer holds the part and has the
demand. That needs no special handling and is simply what the existing rules produce.

Relaying is deliberately scoped to the relayer's own upload queue. Distributing offers over
source exchange was considered and rejected: `OP_ANSWERSOURCES` is a fixed record list with no
room for a URL, a key and a digest, so it would mean pushing `HCOP_OFFER` at peers learned
through XS — which forfeits §6 rule 4, the requirement that an offering peer already be a source
for the file, and spends a connect or callback per peer at peers that may already hold the part.

---

## 4. Server contract

The client depends on plain HTTP + JSON and nothing else, so any backend can serve it. The full
contract, with status codes and error shapes, is in the reference server's `README.md`. In summary:

```
GET    {base}/v1/info            → {"service":"emule-http-cache","version":1,
                                    "maxChunkSize":…, "defaultTtl":…, "rangeSupported":true}
POST   {base}/v1/chunks          Authorization: Bearer <apiKey>
                                 Content-Type: application/octet-stream
                                 Content-Length: <n>          (required)
                                 X-Chunk-TTL: <seconds>       (optional)
                                 body = ciphertext
                                 → 201 {"id","url","size","expires"}
                                 → 400 empty or short body · 401 · 411 no Content-Length
                                 → 413 too large · 429 quota spent · 500 · 507 no storage
GET    {base}/v1/chunks/{id}     no auth; Range-capable → 200 / 206 / 404 / 416
DELETE {base}/v1/chunks/{id}     Authorization: Bearer <apiKey>, uploader only
                                 → 204 gone · 401 · 404 unknown/expired/already gone
                                 → 500 the server could not remove it
```

Every one of those requests — the probe, the upload, the download, the delete — carries
`User-Agent: eMuleQt/<version>`. It is not decoration: an operator behind a WAF has no other way to
allow the client past a default bot rule, and an anonymous `GET` of a 9.28 MB blob is precisely what
such a rule challenges. The download is the one that had to be argued for, since it is assembled as
raw bytes over an EMSocket rather than through QNetworkAccessManager; `eMule::Http` (`net/HttpDefaults.h`)
holds both forms so they cannot drift apart. MFC eMule sends no agent from `CUrlClient`, so this is a
deliberate divergence, and it does tell an arbitrary HTTP source the exact client version.

A `500` from `DELETE` is not fatal to anything: the blob simply stays downloadable until its TTL
lapses. eMuleQt logs it as a warning rather than swallowing it, because the alternative — treating a
failed delete as a success — would leave an uploader believing a chunk it wanted gone is gone. `404`
counts as success: the caller asked for the chunk to be absent, and it is.

### 4.1 A refused upload

A failed `POST` is not one outcome. eMuleQt classifies it, because the answers differ (the policy is
`HttpCacheManager::backoffFor()`, pinned by `tst_HttpCachePublish`):

| Failure | Scope | Pause before the next attempt |
|---|---|---|
| local: unreadable part, bad base url, RNG | that part only | 10 min |
| `400` — the body arrived short | that part only | 10 min |
| transport: unreachable, TLS, stalled mid-body | whole server | 1 → 5 → 15 → 30 min |
| `5xx`, including `500` | whole server | 1 → 5 → 15 → 30 min |
| a `2xx` whose body is unusable | whole server | 1 → 5 → 15 → 30 min |
| `401` · `403` · `411` · `413` · `429` | whole server | 30 min, flat |

The distinction that matters is **scope**. A `500` will greet the next chunk exactly the same way, so
nothing may be published until the server has had time to recover; an unreadable part file says
nothing about the server and must not silence the feature. Without this, a broken server collects a
fresh ~9.28 MB `POST` every `kTickIntervalMs`, forever.

The `401`/`413`/`429` group does not escalate: those need somebody to change a key, a limit or a
quota, and a fourth-attempt backoff would only be theatre. Editing the base url or the API key clears
the pause immediately, so the user is never left waiting out a timer after fixing the cause.

`Retry-After` (delta-seconds form only) is honoured when it is *longer* than the pause we chose, never
when it is shorter — a server under load must not be able to ask to be hammered.

Publishing is what pauses. Chunks already on that server keep being offered to peers throughout: a
`POST` endpoint that is refusing says nothing about the blobs the server is already serving.

> A `500` may leave an orphan. If the backend fails *after* committing the blob but before the client
> reads the `201`, the chunk exists, holds storage and quota, and its id is in a response nobody
> received — so it can never be `DELETE`d. Only its TTL reclaims it. This is why a chunk TTL is
> mandatory rather than advisory (§4, `X-Chunk-TTL`).

A conforming backend MUST:

- return an **absolute** `url`; the client uses it verbatim and never reconstructs it from `id`, so a
  backend is free to serve blobs from another host or a signed CDN link;
- support single-range `Range` requests with a correct `206` and `Content-Range` (§7.2 depends on it);
- generate `id` from a CSPRNG with at least 128 bits of entropy — the id *is* the capability;
- reject a body whose length disagrees with `Content-Length`, rather than storing a truncated chunk.

A backend MUST NOT be given the eD2K file hash, the part index, or the encryption key. eMuleQt sends
none of them.

---

## 5. Result codes

`HCTAG_RESULT`, `HttpCacheResult` in `src/core/httpcache/HttpCacheTypes.h`:

| Value | Name | Meaning | Counts against the chunk? |
|---|---|---|---|
| 0 | `Ok` | fetched, decrypted, hash-verified | — |
| 1 | `Disabled` | downloads switched off locally | no |
| 2 | `Busy` | at the concurrent-fetch limit, or already fetching this part | no |
| 3 | `NotWanted` | part not needed (any more) | no |
| 4 | `BadOffer` | failed validation: URL, sizes, unknown file, expired | no |
| 5 | `HttpFailed` | transport failure or non-2xx status | yes |
| 6 | `SizeMismatch` | server delivered a different length than promised | yes |
| 7 | `Corrupt` | SHA-256 or AES padding check failed | yes |

A receiver MUST clamp an unknown value onto a defined enumerator.

Only the last three say anything about the blob. After three such reports an uploader stops offering
that entry. It does **not** delete it: a failure is as likely to be the downloader's or the network's
fault, and the chunk may still be serving other peers. Entries lapse at their TTL. `DELETE` exists
for explicit cleanup only.

---

## 6. Downloader validation

An offer is an instruction to fetch from a peer-chosen URL, so it is validated before a byte is
requested. An offer is accepted only when all of the following hold:

1. `httpCache.enabled` and `httpCache.allowDownload`.
2. Below `maxConcurrentFetches`, and no other fetch is in flight for this `(file, part)` — once a
   chunk is shared, several uploaders offering the same part is the normal case.
3. The file hash matches a `PartFile` in the download queue.
4. **The sender is already a source for that file** (`sender->reqFile() == file`). Without this, any
   client that can reach us could point us at an arbitrary URL.
5. The part index is in range and not already complete.
6. `plainLength ∈ (0, PARTSIZE]` and `cipherLength == cipherLengthFor(plainLength)` exactly — the
   padding rule is fixed, so a mismatch is either a bug or an attempt at over-allocation.
7. Key is 32 bytes, IV is 16, digest is 32.
8. The URL is ≤ 1024 chars, syntactically valid, `http`/`https`, with a host, and the host is not
   somewhere we should not be sent. A literal host of **either** family is checked against
   `isGoodIP()` and the IP filter before a connection is spent on it — `::ffff:a.b.c.d` is folded
   back to IPv4 first, since it is otherwise reported as IPv6 and slips an IPv4-only test — and the
   unspecified address (`0.0.0.0`, `::`) is refused outright. A hostname is vetted the same way plus
   the ban list once resolved, in `URLClient`, because the resolver's answer is the sender's to
   choose: a name pointing at `127.0.0.1` asks for exactly what a literal one is screened for.
9. `expiresAt`, if non-zero, leaves at least 120 seconds.

During the fetch the downloader additionally:

- refuses to accept more ciphertext than `cipherLength`;
- rejects a `Content-Length` that disagrees with `cipherLength`;
- rejects a `206` whose `Content-Range` does not start at the requested offset.

Nothing above is trusted as proof the data is *right*. The plaintext goes through the ordinary
`PartFile::writeToBuffer()` path, so the existing MD4 part hash (and AICH recovery) remains the final
arbiter, and `CorruptionBlackBox` attributes a bad part the same way it would for an eD2K source.

### 6.1 Who gets blamed

The sender recorded for those bytes is the **peer that offered the chunk**, never the cache server.
`HttpCacheClient` passes the peer's connect address to `writeToBuffer()` rather than its own, which is
the server's — so the server is not merely exempted from blame, it never enters the attribution at
all.

That is sound because the server is transparent to the content. It holds an AES-256-CBC blob it has
no key for, and `verifyComplete()` checks the ciphertext against the SHA-256 the *peer* pinned in the
offer before any plaintext is trusted. Bytes that reach the part file are therefore provably what the
peer published; a part that then fails MD4 can only be the peer's doing. Banning the server instead
would take a working cache away from every download over one peer's bad data.

MFC only ever assigns blame from AICH recovery, which needs a second source to supply the reference
hash and so may never arrive. eMuleQt adds one case that needs no AICH: when a single sender supplied
the *entire* part (`CorruptionBlackBox::soleSenderOfWholePart()`), nobody else could have contributed
the bad bytes, so the part is condemned and the sender banned on the spot. A cache chunk is always one
whole part from one peer, so this is the normal path for it.

Whole-part coverage, not merely "one sender", because the records do not survive a restart: a part
left half-finished last session has no sender for its first half, and whoever fills the remaining gap
would otherwise be blamed for bytes it never sent. With two or more contributors, or a part only
partly attributed, nothing is banned on a guess and AICH recovery decides — exactly as in MFC.

The threshold still applies on top of all this. `evaluateData()` weighs a sender's corrupt bytes
against everything it has ever verified for this file, so a peer that has delivered several good
parts is not banned over one bad one; a peer whose only contribution was the bad part is.

A failure *before* the plaintext — a ciphertext digest mismatch, bad PKCS#7 padding, a wrong length —
does **not** ban anybody. A peer that pinned a wrong hash and a broken cache server look identical
from there. Those failures keep their `Corrupt` report, and the three-strike entry retirement in §5 is
the proportionate answer.

When a part filled from a chunk later fails MD4, the downloader sends a **late** `HCOP_RESULT` with
`Corrupt` to the offering peer. The fetch itself reported `Ok` — the digest did match — so without
this the uploader would keep handing the bad chunk to everyone else. The report needs an attribution
that outlives the fetch, because MD4 does not run until the part file next flushes its buffer; see
`HttpCacheManager::m_fetchedFrom`.

---

## 7. Encryption

### 7.1 Scheme

AES-256-CBC with PKCS#7 padding. The uploader generates a **fresh 32-byte key and 16-byte IV per
published chunk** from `RAND_bytes`. Both travel only inside `HCTAG_KEYIV` on the eD2K link.

Reusing a key across chunks would let the server correlate two uploads of the same part, which is
exactly what this design exists to prevent.

`PARTSIZE` = 9,728,000 is an exact multiple of the 16-byte block size, so PKCS#7 appends a whole
extra block: the ciphertext of a full part is always **9,728,016 bytes**. `HCTAG_CIPHERLEN` states it
explicitly rather than leaving the receiver to derive it.

### 7.2 Range resume

CBC does not prevent resuming. The ciphertext block at offset *O* (a multiple of 16) decrypts using
the block at *O − 16* as its chaining value, so a downloader that lost its connection re-issues
`Range: bytes=O-<cipherLength−1>` and restarts the decryptor with `AesCbcDecryptor::beginAt()`. This
is why `Range` support is mandatory for a backend.

The chaining block is **kept in memory**, not refetched. Asking for `bytes=(O−16)-` instead would
work for the cipher but break the digest: `HCTAG_CIPHERSHA` covers the whole ciphertext, and
`QCryptographicHash` cannot rewind, so every byte must reach it exactly once across all attempts.

That constraint drives the whole intake path. A downloader MUST:

- stage incoming bytes and advance only in whole `kAesBlockSize` blocks, feeding the digest and the
  decryptor the identical block-aligned stream — the staged remainder of a dead connection is
  discarded and refetched;
- run the decryptor with **padding disabled** for the entire transfer, so decrypted output equals
  ciphertext input byte for byte and no block is ever held back inside the cipher context;
- treat the bytes past `HCTAG_PLAINLEN` as the PKCS#7 tail, verify it by hand
  (`cipherLength − plainLength` bytes, each equal to that count), and never write it to the part;
- keep **one request outstanding per connection**. A second GET issued while a response is still
  arriving is pipelined behind it, and its reply is then read as a continuation of that body — the
  transfer stalls at whatever offset the extra request went out at, while looking from the outside
  like a slow server. `HttpCacheClient` guards this explicitly because its base class asks for the
  next block whenever one completes, which is right for a URL source fetching 180 KB at a time and
  wrong here.

On a resumed request the response MUST be a `206` whose `Content-Range` is exactly
`bytes O-<cipherLength−1>/<cipherLength>`; anything else is refused as `SizeMismatch`. A `200` to a
resumed request means the backend ignored `Range` — the downloader restarts from offset 0 rather
than failing, which costs the bytes already fetched but still completes.

Attempts are bounded twice: `kMaxResumeAttempts` retries since the stream last moved forward
(2 s / 6 s / 15 s backoff), and `kMaxTotalAttempts` for the fetch as a whole. Both are needed — a
counter that resets on progress alone would let a backend dribbling one block per connection
reconnect forever, and a fixed cap alone would abandon a flaky link that is in fact getting there.

`HCTAG_CIPHERSHA` is computed over the ciphertext, not the plaintext, so it distinguishes *"the
server or the network mangled the blob"* from *"the uploader published something wrong"* — a
distinction the MD4 part hash alone cannot make.

### 7.3 Threat model

**What this protects against:** a *passive* cache operator, anyone with access to its disks or
backups, and anyone who obtains a chunk URL but not the key. All of them see uniform ~9.7 MB
blobs with no file hash, no part number and no filename attached.

It has never protected against an operator willing to run client nodes. Becoming a source for a
popular file and declaring parts missing is enough to be sent offers, and every offer carries the
URL, the key and the IV. Key harvesting at scale is therefore already available to a determined
operator; §11 changes what it costs, not whether it is possible.

**What it does not protect against:** anyone who can read the eD2K link between the two peers, since
the key travels there. eD2K obfuscation is RC4 with a key derived from the user hash and is not
authenticated encryption. The stated goal is that *the server* never holds decryptable data, and the
design meets that; it is not end-to-end secrecy against a network attacker who is already positioned
to read the eD2K stream — and such an attacker could read the file data off that stream anyway.

**Relaying (§3.4)** widens the set of peers holding the key for a given blob. This is the same
class of exposure the multi-peer offer already creates — `minClients` is 2 by default, so a chunk
is normally handed to several peers the moment it is published — and it is bounded by the same
per-entry "already told" set. It is a real widening all the same, and it is why relay is a
separate switch.

**Publishing to Kad (§11)** is on by default, as an accepted trade-off. It puts the URL, the key
and the IV into a public DHT record, so the cost of harvesting drops from "solicit one offer per
chunk" to "one lookup per file". What that buys an attacker is bounded, and bounded in three
directions at once: the key unlocks exactly one part of one file that is *already* sitting at a
public URL anyone may GET; the chunk lapses at `chunkTtlSeconds` (6 h by default) and the record
with it, since a publisher republishes on the `KADEMLIAREPUBLISHTIMES` clock and a storing node
expires on the same one; and only chunks we chose to publish are ever described, never a relayed
one (§11.3). Set against that, a peer who wanted the same bytes could simply ask us for the offer
over eD2K and get the identical key — the DHT changes the *effort*, not the *reach*.

The honest claim is therefore *the server is never **given** the key* — not *the key is secret*.
That is the whole of what §7's design buys, and it holds with `publishToKad` on or off. A
deployment that needs the stronger reading — the key stays between us and the peers we hand it to
— turns `publishToKad` off and keeps everything else.

**Bandwidth trust:** a malicious uploader can waste a downloader's bandwidth by publishing garbage.
The cost is bounded at one part, the digest catches a mangled blob before the MD4 check, and a blob
that is internally consistent but decrypts to the wrong bytes gets the peer banned through the
existing corruption machinery — see §6.1. The cache server is never blamed for either.

---

## 8. Configuration

Two ways in: an `ed2k://|httpcache|` link (§8.1), which is how a server hands out its own address and
key, or the YAML below by hand. There is deliberately no Options-dialog page yet. Edit
`$HOME/eMuleQt/Config/preferences.yml`:

```yaml
httpCache:
  enabled: false                        # master switch, both directions
  allowDownload: true
  allowUpload: true                     # also needs baseUrl and apiKey
  allowRelay: true                      # pass verified chunks on (§3.4); needs no account
  publishToKad: true                    # put chunk descriptors in the Kad source record (§11)
  fetchFromKad: true                    # fetch chunks found in Kad, vouched for by nobody
  baseUrl: "http://localhost/emule-http-cache-php"
  apiKeyEnc: "<AES-encrypted>"          # plaintext `apiKey:` is accepted once, then rewritten
  minClients: 2                         # 1 to exercise the path with a single peer
  chunkTtlSeconds: 21600
  maxPublishBytesPerDay: 2147483648
  publishRateKBs: 0                     # 0 = a quarter of the upload limit
  maxConcurrentPublishes: 1
  maxConcurrentFetches: 2
```

The API key is stored AES-256-CBC encrypted under the same at-rest key as the SMTP password, so
`preferences.yml` never contains a usable upload credential in the clear. A plaintext `apiKey:` may
be hand-written for first setup; it is rewritten as `apiKeyEnc` on the next save and never written
back in the clear.

Publishing is rate limited to `publishRateKBs`, or a quarter of the upload limit when that is 0.
Note eMuleQt's sentinel: an upload limit of `0` means *unlimited* (MFC used `UNLIMITED`), so a node
with no limit configured falls back to a fixed modest rate rather than to zero.

### 8.1 Configuration by link

A server's install page prints one of these; pasting or clicking it is the whole setup.

```
ed2k://|httpcache|HTTP%20Cache%20upload%20config|https://cache.example.com|1f4b9c02d7e35a68|k=default|/
```

Three positional fields — name, base URL, secret — then optional `key=value` fields, of which only
`k=<keyId>` is defined (display only). The format is specified by the server side, in
`emule-http-cache-php/docs/ed2k-httpcache-link.md`; that document is normative and this section only
records what eMuleQt does with it.

**The link is the credential.** Anyone holding it can upload under that key. eMuleQt therefore never
logs one, never echoes one back into an error message — `redactLinkSecret()` (`ED2KLink.h`) replaces
the secret wherever link text is reported — and never applies one without asking.

Parsing is `parseED2KLink()`, like every other link type, and it **splits on `|` before decoding
anything**: decoding first would let a `%7C` inside a name re-split the link into the wrong number of
fields. A broken `%` escape, a decoded control octet, a base URL that is relative, not `http(s)`, or
carries `user:pass@` / a query / a fragment, an empty or whitespace-bearing secret, a tail field
without `=`, a malformed `k=`, or a link over 4096 octets — each refuses the whole link. An unknown
`key=value` is skipped, which is the format's extension point.

Applying one is three steps, in this order:

1. **Handshake.** `GET <baseUrl>/v1/info` must answer `"service": "emule-http-cache"` with a version
   this client understands. This is what stops a link being used to point a client at an arbitrary
   host, so it happens before anything is shown or stored — and **the request carries no
   credential**, because at that moment the host is only what the link claimed it was.
2. **Ask.** A dialog naming the server, the key id, and — over plain `http` — that the key and every
   chunk URL will cross the network in the clear. A chunk URL is a bearer token.
3. **Store** `baseUrl`, `apiKey`, `enabled: true`, `allowUpload: true`, and save.

The daemon does both the handshake and the storing (`ProbeHttpCacheServer` / `ApplyHttpCacheConfig`
over IPC): preferences live there, it may be on another machine, and the only reachability that
matters is that of the node which will use the cache. `ApplyHttpCacheConfig` handshakes again before
writing, because the caller having done so is not a reason to skip it — and `emulecored --add-link`,
which also accepts these links, never probes separately.

`uploadRequiresAuth: false` in the handshake means that server takes uploads with no credential. It
is **not** a reason to drop the key: a key is still what authorises `DELETE`, and an upload made
without one belongs to the server's reserved `anonymous` id that nobody can authenticate as — those
chunks can only lapse at their TTL. `HttpCachePublisher::deleteChunk()` therefore returns without
sending anything when it has no key, and that is the right answer rather than a missing case.

One GUI rule falls out of this flow and applies to any IPC reply handler, not just these: **never
open a modal dialog from inside the call stack that delivered the reply.** A modal box spins a nested
event loop, and a quit arriving during it — a signal, Cmd-Q, a logout — unwinds every loop at once;
`main()` then destroys the `IpcClient` and its socket while the socket-read notification is still
below on the stack. Both link importers hand the dialog to the next turn of the event loop
(`QTimer::singleShot(0, …)`) for exactly this reason.

---

## 9. Reserved but unimplemented

These exist as identifiers or design allowances and MUST NOT be relied upon:

- **`HCOP_CANCEL`** is parsed and cancels a matching in-flight fetch, but eMuleQt does not currently
  originate one — entries lapse at their TTL instead.
- **Resumable uploads.** The POST is all-or-nothing; there is no offset-aware upload in the REST
  contract, so a publish that dies part-way is simply retried from the start. Adding one would be a
  server-protocol change, not a client one.
- **Multi-range responses.** A backend MAY return the whole entity for a multi-range request
  (RFC 9110 §14.2); eMuleQt never sends one.
- **`OP_PEERCACHE_QUERY/ANSWER/ACK` (0x94–0x96)**, `PCOP_*`, `PCTAG_*` — MFC PeerCache leftovers,
  never dispatched. New opcodes MUST NOT reuse them, because clients from 0.43a to 0.60d still
  contain live handlers for them.

---

## 10. Test coverage

| Test | Covers |
|---|---|
| `tests/tst_AesCbc.cpp` | cipher round trips, PKCS#7 edges, wrong-key/truncation/bit-flip detection, resume-from-offset, and the padding-disabled staged pipeline `HttpCacheClient` actually uses |
| `tests/tst_ED2KLink.cpp` | the `ed2k://\|httpcache\|` grammar — the spec's whole reference table, accepted and refused, plus `toLink()` round trips, a literal `\|` surviving as `%7C`, a UTF-8 name, control-octet and oversize refusals, and that `redactLinkSecret()` hides the secret in a malformed link as well as a valid one |
| `tests/tst_HttpCacheConfigLink.cpp` | the `/v1/info` handshake: a real cache accepted, and refused for another service, an HTML page, a 404, a version from the future, a body far larger than an info document, and a host that hangs up. Plus the two that are only visible from the server side — that the probe sends **no** `Authorization` header, and that a caller dying mid-probe neither leaks nor calls back |
| `tests/tst_HttpCacheOffer.cpp` | packet round trips, every malformed-field case, truncation at every length, unknown-tag skipping, unknown sub-opcode rejection |
| `tests/tst_HttpCacheResume.cpp` | a real `HttpCacheClient` against a deliberately unreliable origin: mid-transfer drop, block-boundary restart, digest continuity across the seam, a backend that ignores `Range`, a lying `Content-Range`, the give-up bound, and — with the body delivered a slice at a time — that asking the client for more blocks mid-response never puts a second GET on the socket. Also that the request reaching the socket carries `User-Agent: eMuleQt/<version>`, on the retry as well as the first attempt |
| `tests/tst_HttpCachePublish.cpp` | a real `HttpCachePublisher` against an origin that refuses the upload — `500` with the server's own message, `401`, `Retry-After`, an unusable `2xx`, an unreadable part, an aborted exchange — plus the whole §4.1 retry-policy table |
| `tests/tst_HttpCacheMultiPeer.cpp` | the uploader's decisions, with three mock ed2k peers on a real `ListenSocket`/`UploadQueue`: one publish serving three peers, the `minClients` threshold, the `MODMISC_HTTPCACHE` and part-availability filters, the whole-part guard, two parts serialised by `maxConcurrentPublishes`, the re-offer dedup, slot release, entry retirement after three bad reports, and §4.1's "publishing pauses, offers continue". A peer also fetches the offered URL and decrypts it back to the part on disk |
| `tests/tst_HttpCacheCorruptBan.cpp` | who gets blocked when a part fails MD4: a sole whole-part sender banned without AICH, a connected client banned through its own `ban()` path rather than by bare address, and four cases where nobody is blamed — a good part, two contributors, an unattributed write, and a part only half attributed (the resumed-download trap). Plus a full fetch of an internally consistent chunk that decrypts to the wrong bytes: the offering peer is banned, the cache server is not |
| `tests/tst_HttpCacheRelay.cpp` | §3.4 end to end against a real `HttpCacheClient` and `PartFile`: a verified chunk is promoted and the origin excluded from its offer list; ten flushes still produce one entry; a part that fails MD4, and a chunk with no stated expiry, are never relayed; relaying works with no base url, no API key and `allowUpload` off; and `allowRelay: false` stops it |
| `tests/tst_PartFileSharing.cpp` | part files are shared files — the MD4-hashset and complete-part gate, sharing on a verified part and on ICH recovery, idempotence across flushes, survival of a `SharedFileList::reload()`, and that leaving the download queue unshares without marking the hash unshared forever |
| `tests/tst_KadSearch.cpp` | §11: chunk tags absent with nothing to advertise, a full round trip through the real Kad tag codec (BSOB included), the chunk cap and the whole-part / URL-length / expiry screens, the record staying under a stock node's 2 KB serve buffer, and that the source record a legacy client reads is untouched |
| `tests/tst_HttpCacheLive.cpp` | the real `/v1/info` handshake; publisher → real server → GET → decrypt → byte-exact compare; ranged resume against the live server; a full `HttpCacheClient` fetch of a 9.28 MB part into a real `PartFile`. Every case runs once per backend that is reachable — see §10.1 |
| `emule-http-cache-php/tests/smoke.php` | the server contract standalone — valid against any backend, PHP or not |

### 10.1 Running the live test against a backend

There is more than one implementation of §4, and two implementations drift. `tst_HttpCacheLive`
therefore runs every case once per cache server it can reach, and skips the ones it cannot, so the
same binary is honest on a machine with two backends, one, or none.

| Variable | Backend | Absent means |
|---|---|---|
| `EMULE_HTTPCACHE_URL` + `EMULE_HTTPCACHE_KEY` | a server somebody else runs — the PHP reference server under Apache, in practice | unset, or nothing answers `/v1/info` there: that row is skipped |
| `HTTPCACHE_GO_CMD` | a binary the test starts itself, over a throwaway config and storage directory on a free loopback port | unset, or the executable is missing: that row is skipped |

Both can live in `.env`, which is where `SERVER_TEST_CMD` points `tst_ServerLocalTest` at eNode. A
binary that is *present* and will not serve is a failure rather than a skip — the prerequisite was
met, so something is broken and saying so is more use than staying quiet.

Each row asserts that `implementation` from `/v1/info` matches the backend it names. Without that,
an environment pointing both rows at one server would run everything twice against the same code and
report it as coverage of two.

Both backends together run in about a second against localhost. A row that instead takes tens of
seconds, in a round number close to the server's keep-alive timeout, is the client failing to drain
its own socket rather than the server being slow — `EMSocket::onReadyRead()` reads at most 2 MB per
pass and Qt only re-emits `readyRead` on *new* data, so a pass that leaves bytes behind has to
re-arm itself. That is fixed and covered by `tst_EMSocket::drainsBufferWhenPeerGoesQuiet()`, but the
shape of the symptom is worth recognising: it points at us, not at the backend.

---

## 11. Kad chunk records

On by default, and separately switchable (`httpCache.publishToKad` to publish,
`httpCache.fetchFromKad` to act on what is found). §7.3 sets out the trade-off being accepted:
this publishes the decryption key to a public DHT.

With it on, a lookup on the file hash yields everything needed to fetch a part over HTTP without
contacting any peer at all — which is the point, since otherwise a chunk can only be found by
first finding, connecting to and queueing at the peer holding it.

### 11.1 Why the descriptors ride the source record

Two constraints leave no alternative.

**A new Kad opcode cannot work.** Publishing means storing on the nodes closest to the target, and
those are ordinary eMule clients. An unrecognised opcode is dropped at their dispatch table and
nothing is ever stored. The record must travel in `KADEMLIA2_PUBLISH_SOURCE_REQ`, which stock
nodes accept, keep verbatim — unknown tags included — and serve back on
`KADEMLIA2_SEARCH_SOURCE_REQ`.

**A publisher gets one stored record per file hash.** The store dedups on publisher IP plus ports,
not on the source ID, so a second record from the same node overwrites the first. Minting a
distinct source ID per chunk achieves nothing; only fabricating distinct TCP *and* UDP ports would,
which is abusive and burns the per-IP publish token bucket.

So the descriptors are **extra tags on the publisher's real source record**, not a record of their
own. `FT_SOURCETYPE` keeps its true value, so a client that knows nothing about these tags still
reads the source it came for and ignores the rest.

### 11.2 Tags

Indexed families; the name is the prefix with the chunk number appended (`hcp0`, `hcu0`, `hcp1`,
…), for at most `KADHC_MAX_CHUNKS` = 3 chunks. A missing index ends the run.

| Tag | Type | Meaning |
|---|---|---|
| `hcp<i>` | uint32 | part index within the file |
| `hcu<i>` | string | absolute chunk URL, ≤ 256 chars |
| `hck<i>` | bsob(48) | 32-byte AES-256 key ‖ 16-byte IV |
| `hcs<i>` | bsob(32) | SHA-256 of the ciphertext |
| `hce<i>` | uint32 | unix expiry; MUST be non-zero |

Plaintext and ciphertext lengths are **not** published. Only whole `PARTSIZE` parts are ever
published (§3.1), so a reader derives `PARTSIZE` and `PARTSIZE + 16` and MUST refuse a record
whose part index would imply a file's short tail part. That removes two tags per chunk and one
thing a publisher could lie about.

String names rather than numeric `FT_` ids: the numeric space is nearly full, and two ids in it
are silently stripped by a storing node, so a string name is both cheaper and safer.

### 11.3 Limits a publisher MUST respect

- **At most 3 chunks, and a URL of at most 256 characters.** A storing node serialises one record
  into a 2 KB buffer and throws on overflow, which aborts the serve for *every* result in that
  packet, not only ours. A chunk that would breach either limit is skipped, and is still offered
  over eD2K as normal.
- **Only chunks the publisher published itself.** A relayed chunk (§3.4) is a borrowed URL on
  somebody else's TTL, and a DHT record outlives the chunk, so relayed entries are never
  advertised.
- A node that cannot publish a source record at all — firewalled with neither a direct UDP
  callback nor a buddy — publishes no chunks either, even though its chunks would be perfectly
  fetchable. Two gates enforce this: `KnownFile::publishSrc()`
  (`src/core/files/KnownFile.cpp:724`) refuses to start the search, and the tag builder returns
  early at `src/core/kademlia/KadSearch.cpp:232`, ahead of where the chunk tags are appended.
  Both are reachable, because they disagree — `publishSrc()` tests only `isFirewalledUDP(true)`
  while the builder additionally demands `UDPFirewallTester::isVerified()`, so an
  unverified-but-open UDP node starts a search and is stopped at the second gate.

  Fixing it would mean publishing something that is not a real source, and there is no benign
  way to do that. Three separate walls, checked against the reference implementation:

  1. **The source IP cannot be chosen.** A storing node discards whatever address the publisher
     names and synthesises `FT_SOURCEIP` from the observed UDP sender
     (`src/core/kademlia/KadUDPListener.cpp:1436`; reference
     `srchybrid/kademlia/net/KademliaUDPListener.cpp:1317`). There is no dummy IP to publish —
     the record always carries the publisher's real NAT-side address.
  2. **A portless record cannot be stored.** The clean substitute would be `FT_SOURCETYPE = 1`
     with `FT_SOURCEPORT = 0`: a record that carries chunks but names no reachable source, which
     both this port and stock eMule already discard on the reading side. No storing node will
     hold it — `src/core/kademlia/KadIndexed.cpp:159` and reference
     `srchybrid/kademlia/kademlia/Indexed.cpp:456` both require a non-zero TCP port.
  3. **The remaining opening is not filtered out downstream.** That leaves claiming
     `FT_SOURCETYPE = 1` with the real TCP port while firewalled. The chunks would arrive —
     `Search::processResultFile` applies no port check for types 1/4 and parses the `hc*` tags
     unconditionally, and `addKadChunks` runs on a path independent of the source
     (`src/core/app/CoreSession.cpp:1127`). But nothing rejects the bogus source:
     `DownloadQueue::addKadSourceResult` passes it on a non-zero port, and `checkAndAddSource`
     only rejects a High ID whose address fails `isGoodIP` (`DownloadQueue.cpp:340`) — a real
     NAT-side public IP passes cleanly. Stock eMule behaves the same
     (`srchybrid/DownloadQueue.cpp:1535` drops only a missing port). Every peer that looked the
     file up would pay a dead TCP connect and a dead-source entry in exchange for the chunks.

The file size is published, as it always was for a source. That is safe *because* these are
annotations on a genuine source record: the size is the file's own, so a searcher looking for that
file asks with the same number and the storing node's size filter matches.

### 11.4 Acting on a record

A record is a URL chosen by a stranger. §6 rule 4 — the offering peer must already be a source for
the file — cannot apply, because there is no sender. What replaces it is that *we* chose the file
hash that was looked up, plus the whole of §6's structural validation: the URL check and IP
filter, the expiry margin, the derived lengths, and the key, IV and digest sizes.

**Nobody is blamed.** Bytes from a Kad-discovered chunk are written with no sender attached, so the
corruption black box records them as unattributed and §6.1's sole-sender rule cannot fire. This is
not optional: attributing them to anything would mean attributing them to the cache server, which
did not choose them, and getting it banned. A part filled this way that later fails MD4 therefore
condemns nobody, and AICH decides as it would for any unattributed part.

There is likewise no peer to send `HCOP_RESULT` to and no three-strike counter to run, so a client
SHOULD remember the URLs it failed on and cap how often it will chase Kad-discovered chunks for
one file. The cost of a malicious record stays bounded at one part: a mangled blob fails the
published SHA-256 before any plaintext is trusted, and a self-consistent blob that decrypts to the
wrong bytes fails the MD4 part hash.
