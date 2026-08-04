# eD2K / Kademlia IPv6 Extensions — Interoperability Specification

**Status:** Implemented and shipping in eMuleQt.
**Document version:** 1.1 — 2026-07-30.
**Audience:** implementers of eD2K clients, eD2K servers, and eD2K link generators who want to
interoperate with eMuleQt over IPv6.

This document specifies **only what is actually implemented**. Everything described here is
readable in the eMuleQt source tree; section headings carry the authoritative file for each area.
Elements that exist as reserved identifiers but have no implementation are collected in
§9 and MUST NOT be relied upon.

The design goal throughout is **additive, opt-in, legacy-safe**: an eD2K client or server that
knows nothing about IPv6 sees byte-identical classic traffic, and every IPv6 element is either an
unknown tag it skips, an optional trailing block it never reads, or a link token its tokenizer
never reaches. No packet layout was changed in a way that a conforming legacy parser can observe.

---

## 0. Conventions

### 0.1 Requirement levels

The key words MUST, MUST NOT, REQUIRED, SHOULD, SHOULD NOT, RECOMMENDED, MAY and OPTIONAL are to be
interpreted as in RFC 2119.

### 0.2 Byte order

eD2K is a **little-endian** protocol. All integer fields in this document are little-endian unless
explicitly stated otherwise. Two long-standing exceptions from classic eD2K are preserved:

- **IPv4 addresses** carried in `uint32` fields are stored in **network byte order** inside the
  integer, so that the four bytes appear on the wire in `a.b.c.d` order.
- **IPv6 addresses** are always carried as **16 raw bytes in network byte order** — never as
  integers, never byte-swapped, never text (except in Kad, §5, and in links, §6).

An IPv4-mapped address (`::ffff:a.b.c.d`) MUST be normalised to a plain IPv4 address on ingest from
a socket, so a legacy IPv4 peer arriving on a dual-stack listener is never mistaken for an IPv6
peer.

### 0.3 Tag encodings

Two eD2K tag serialisations are used. Both are classic eD2K; neither is new.

**Old format** (`Tag::writeTagToFile`) — used in `OP_HELLO`/`OP_HELLOANSWER` and in
`OP_LOGINREQUEST`, because at those points the peer/server has not yet learned that we support
new-format tags:

```
uint8   type              e.g. 0x01 = TAGTYPE_HASH
uint16  nameLength = 1
uint8   nameId
...     value             (16 raw bytes for TAGTYPE_HASH)
```

**New / optimised format** (`Tag::writeNewEd2kTag`) — used in ExtSX records (§3.3), in Kad, and in
`server.met`:

```
uint8   type | 0x80       e.g. 0x81 = TAGTYPE_HASH with a numeric name
uint8   nameId
...     value
```

Tag type values used by this specification:

| Type | Value |
| --- | --- |
| `TAGTYPE_HASH` | `0x01` |
| `TAGTYPE_STRING` | `0x02` |
| `TAGTYPE_UINT32` | `0x03` |
| `TAGTYPE_UINT16` | `0x08` |
| `TAGTYPE_UINT8` | `0x09` |

> **Normative width rule.** The new/optimised writer **size-optimises integers**: a value ≤ `0xFF`
> is emitted as `TAGTYPE_UINT8`, ≤ `0xFFFF` as `TAGTYPE_UINT16`, otherwise `TAGTYPE_UINT32`.
> A receiver MUST therefore accept `TAGTYPE_UINT8`, `TAGTYPE_UINT16` and `TAGTYPE_UINT32`
> interchangeably for every integer tag defined here and zero-extend to 32 bits. This is not
> theoretical: a server IPv4 such as `4.0.0.0` yields the integer `0x00000004` and is emitted as a
> single byte.

`TAGTYPE_HASH` is never size-optimised; a 16-byte IPv6 value is always exactly 16 bytes.

### 0.4 Forward compatibility

Every tag list defined here is self-describing. A receiver MUST skip unknown tags by type rather
than aborting, and MUST tolerate tags appearing in any order. New tags MAY be added to any of these
lists in future revisions; that is the intended extension mechanism, and no version number needs to
change for it.

---

## 1. Constant registry

All values are as defined in `src/core/utils/Opcodes.h` and `src/core/server/Server.h`.

The `0xA0`–`0xAF` client-tag block was chosen to match the MOD-tag family already used by an
existing IPv6-capable eMule fork, so that the two implementations agree on the wire.

### 1.1 Client tags (`CT_*` / `ST_*`)

| Name | ID | Type | Direction | Where |
| --- | --- | --- | --- | --- |
| `CT_EMULE_SERVINGBUDDYIPV6` | `0xA0` | HASH (16 B) | c↔c | Hello (§3.1) |
| `CT_MOD_MISCOPTIONS` | `0xAA` | UINT32 bitfield | c↔c | Hello (§3.1) |
| `ST_IPV6_STATUS` | `0xAB` | UINT8 bitfield | server→client | `OP_SERVERIDENT` (§4.3) |
| `CT_MOD_YOUR_IP` | `0xAD` | UINT32 (v4) or HASH (v6) | c↔c, server→client | Hello (§3.1), `OP_SERVERIDENT` (§4.3) |
| `CT_MOD_IP_V6` | `0xAE` | HASH (16 B) | c↔c, client→server | Hello (§3.1), login (§4.1), ExtSX (§3.3) |
| `CT_MOD_SVR_IP_V6` | `0xAF` | HASH (16 B) | server→client | `OP_SERVERIDENT` (§4.3) |
| `CT_EMULE_SERVERIP` | `0xBA` | UINT32 (v4, network order) | c↔c | ExtSX record (§3.3) |
| `CT_EMULE_SERVERTCP` | `0xBB` | UINT16 | c↔c | ExtSX record (§3.3) |
| `CT_EMULE_USERHASH` | `0xBC` | HASH (16 B) | c↔c | ExtSX record, gated (§3.3.6) |
| `CT_EMULE_CONOPTS` | `0xBE` | UINT8 | c↔c | ExtSX record, gated (§3.3.6) |
| `ST_IPV6` | `0x99` | HASH (16 B) | **on disk only** | `server.met` (§4.8) |

### 1.2 Opcodes

| Name | Value | Protocol byte | Direction | Status |
| --- | --- | --- | --- | --- |
| `OP_CHANGE_CLIENT_IP` | `0xAC` | `0xE3` (`OP_EDONKEYPROT`) | c↔c, TCP | Implemented (§3.2) |
| `OP_CALLBACKREQUESTED_IPV6` | `0x26` | `0xE3` (`OP_EDONKEYPROT`) | server→client, TCP | Implemented (§4.5) |

**No new opcode carries IPv6 sources.** IPv6 sources travel inside the existing
`OP_ANSWERSOURCES2` (§3.3), `OP_FOUNDSOURCES` / `OP_FOUNDSOURCES_OBFU` / `OP_GLOBFOUNDSOURCES`
(§4.4) and `KADEMLIA2_SEARCH_RES` (§5) packets. See §9 for reserved-but-unimplemented opcodes.

### 1.3 Bitfields

**`CT_MOD_MISCOPTIONS` (`0xAA`), UINT32 — client capability bits:**

| Bit | Mask | Meaning |
| --- | --- | --- |
| 0 | `0x00000001` | `MODMISC_EXTXS` — supports Extended Source Exchange (§3.3) |
| 1 | `0x00000002` | uTP NAT traversal (not implemented here; **do not reuse**) |
| 2 | `0x00000004` | `MODMISC_IPV6` — supports the IPv6 extension |
| 3 | `0x00000008` | serving-buddy pull (not implemented here; **do not reuse**) |
| 4 | `0x00000010` | QUIC NAT traversal (not implemented here; **do not reuse**) |
| 5 | `0x00000020` | `MODMISC_EXTXS_SKIPTAGS` — my ExtSX reader skips unknown tags (§3.3.6) |

**`ST_IPV6_STATUS` (`0xAB`), UINT8 — the server's verdict on the client's advertised IPv6:**

| Bit | Mask | Meaning |
| --- | --- | --- |
| 0 | `0x01` | `IPV6ST_HAVE` — the server holds a public IPv6 for this session |
| 1 | `0x02` | `IPV6ST_REACHABLE` — that address is treated as reachable; the client is published as a v6 source |
| 2 | `0x04` | `IPV6ST_PROBED` — the verdict came from a real dial-back probe, not a trust default |

An absent tag, or a value of `0`, means "no verdict". Unset bits mean "no".

**`CT_SERVER_FLAGS` (`0x20`), UINT32 — client→server login capabilities:**

| Mask | Meaning |
| --- | --- |
| `0x1000` | `SRVCAP_IPV6` — "I speak the IPv6 server extension" |

**Server→client capability flags** (`OP_IDCHANGE` TCP flags word; `OP_GLOBSERVSTATRES` UDP flags word):

| Mask | Meaning |
| --- | --- |
| `0x00004000` | Server speaks the IPv6 extension (TCP and UDP respectively) |
| `0x00008000` | `NatRendezvous` — parsed and ignored by this implementation |

> **The two directions deliberately use different values** — `0x1000` client→server,
> `0x4000` server→client. This asymmetry is easy to get wrong.
>
> A note on `0x1000`: at least one server implementation uses `0x1000`/`0x2000` unofficially in its
> **server→client** flag words (ChaCha20 / AES256 capability). That is a different word from the
> login `CT_SERVER_FLAGS`, so there is no confirmed collision in the client→server direction — but
> the reuse is a reason not to treat `0x1000` as authoritative on its own. In practice it is not:
> see the gating rule in §4.1.

### 1.4 Sentinel

| Name | Value | Meaning |
| --- | --- | --- |
| `IPV6_SOURCE_SENTINEL` | `0xFFFFFFFF` | ClientID marking an inline IPv6 source inside a classic source block (§4.4) |

### 1.5 Kademlia tag names

| Name | String | Type | Meaning |
| --- | --- | --- | --- |
| `TAG_IPV6` | `"ip6"` | STRING, 32 hex chars | Source's public IPv6 |
| `TAG_SERVINGBUDDYIPV6` | `"bi6"` | STRING, 32 hex chars | Serving buddy's public IPv6 |

---

## 2. Determining and advertising your own public IPv6

*(`src/core/app/AppContext.cpp`)*

Every place this specification says "advertise your public IPv6" refers to a single gate. An
implementation SHOULD reproduce it, because advertising an address you cannot actually be reached
on creates dead sources for every peer that believes you.

### 2.1 Confidence tiers

`publicIPv6()` selects, in descending order of trust:

1. **Server-observed egress** — the address a server reflected back in `CT_MOD_YOUR_IP` during an
   IPv6-connected session (§4.3).
2. **Operator override** — a manually configured literal.
3. **Peer-corroborated** — an address that *N* distinct peers independently reflected back in
   `CT_MOD_YOUR_IP` within a time window (defaults: N = 3, window = 300 s).
4. **Local interface** — the first global-unicast address found on a local interface.

Tiers 1 and 3 are accepted only if the reflected address is **also assigned to a local interface**.
This prevents a hostile or broken peer from convincing a client to advertise someone else's address.

### 2.2 The advertise gate

```
shouldAdvertisePublicIPv6()  =  hasConfidentPublicIPv6()  &&  !publicIPv6ProbedUnreachable()

hasConfidentPublicIPv6()     =  publicIPv6() is IPv6  &&  publicIPv6() is global unicast (§7)
publicIPv6ProbedUnreachable()=  (ST_IPV6_STATUS & IPV6ST_PROBED) && !(… & IPV6ST_REACHABLE)
```

A client MUST NOT advertise a public IPv6 in the hello (§3.1), the server login (§4.1), a Kad
source publish (§5) or an eD2K link (§6) unless this gate is open. The gate is checked freshly at
each advertisement, not cached.

The probe verdict is cleared on server disconnect, so a fresh login always re-advertises and gives
the server another chance to probe.

---

## 3. Client ↔ client

*(`src/core/client/UpDownClient.cpp`, `src/core/files/KnownFile.cpp`, `src/core/files/PartFile.cpp`)*

### 3.1 Hello handshake

IPv6 negotiation happens entirely in the **eDonkey hello** (`OP_HELLO` = `0x01` and
`OP_HELLOANSWER` = `0x4C` under protocol byte `0xE3`). `OP_EMULEINFO` / `OP_EMULEINFOANSWER` carry
**no** IPv6 content.

Hello body (unchanged): `userHash[16]`, `clientID uint32`, `port uint16`, `tagCount uint32`,
*tags*, `serverIP uint32`, `serverPort uint16`. (`OP_HELLO` additionally prefixes a single
`0x10` byte giving the user-hash size, as in classic eD2K.)

Four tags are added to the tag list. All use the **old** tag format (§0.3). All are optional to a
receiver — a legacy peer skips them by type and is unaffected. The `tagCount` field MUST of course
count them.

| Tag | Sent when | Value |
| --- | --- | --- |
| `CT_MOD_MISCOPTIONS` `0xAA` | **always** | UINT32 capability bits (§1.3) |
| `CT_MOD_YOUR_IP` `0xAD` | the peer's address is known | HASH16 of the peer's IPv6 if the connection is IPv6; otherwise UINT32 IPv4 in network order |
| `CT_MOD_IP_V6` `0xAE` | the advertise gate is open (§2.2) | HASH16 — our public IPv6 |
| `CT_EMULE_SERVINGBUDDYIPV6` `0xA0` | we are firewalled and our buddy has a public IPv6 | HASH16 — the buddy's IPv6 |

Wire bytes for `CT_MOD_MISCOPTIONS` advertising both capabilities:

```
03 01 00 AA 05 00 00 00
│  │     │  └── value = MODMISC_IPV6 | MODMISC_EXTXS = 0x00000005
│  │     └───── nameId = 0xAA
│  └─────────── nameLength = 1
└────────────── TAGTYPE_UINT32
```

Wire bytes for `CT_MOD_IP_V6` carrying `2a01:4f8::1`:

```
01 01 00 AE 2a 01 04 f8 00 00 00 00 00 00 00 00 00 00 00 01
```

**Receiver rules:**

- `CT_MOD_MISCOPTIONS` sets the peer's `supportsIPv6` / `supportsExtendedXS` capability flags.
- `CT_MOD_IP_V6` MUST be accepted only when the tag type is `TAGTYPE_HASH`. Accepting it marks the
  peer as directly reachable over IPv6.
- `CT_MOD_YOUR_IP` MUST NOT be adopted directly as one's own address. It is a *vote*: feed it to
  the corroboration logic of §2.1 keyed on the peer's observed socket address, and require the
  candidate to be an address the host actually holds.

> **`MODMISC_IPV6` is a static capability claim, not a statement of fact.** This implementation
> always sends `0x00000005` regardless of whether it currently has a public IPv6. Peers MUST treat
> *the presence of `CT_MOD_IP_V6`*, not the bit, as "this client has a reachable IPv6".

**No per-family ports.** The peer's TCP and UDP ports from the hello apply to both families. This
protocol has no way to express different ports per family; a client using distinct ports for IPv4
and IPv6 cannot be described.

### 3.2 IP change notification — `OP_CHANGE_CLIENT_IP` (`0xAC`, `OP_EDONKEYPROT`, TCP)

Payload is **16 raw IPv6 bytes with no tag wrapper**:

```
uint8[16]  new public IPv6, network order
```

> **The protocol byte is `0xE3` (`OP_EDONKEYPROT`), not `0xC5`.** This is the single most
> easily-mistaken detail in this document, and version 1.0 of it was wrong. The opcode is the IPv6
> counterpart of `OP_CHANGE_CLIENT_ID` (`0x4D`) and travels beside it on the eDonkey protocol byte;
> `0xAC` is unassigned in the stock eDonkey client-to-client opcode space, so there is no clash.
> An implementation that listens for it only under `OP_EMULEPROT` will never see one.
> eMuleQt accepts it under either protocol byte and sends it under `0xE3`.

**Receiver rules.** The address MUST be validated as global unicast (§7) before use; a sender can
put any 16 bytes here. On acceptance, update the peer's stored IPv6, mark it IPv6-reachable, and
re-point the connect address **only if it was already IPv6** — an IPv4 connection in progress must
not be redirected mid-flight.

**Sender rules.** Send it to a connected peer that advertised `MODMISC_IPV6` when your own public
IPv6 changes, subject to the advertise gate of §2.2. Senders SHOULD NOT broadcast on the instant of
the change: mark the affected peers and emit the packet the next time you are writing to each one
anyway. Privacy addresses (RFC 4941) rotate on a timer, and a fan-out to every open socket per
rotation is a burst of writes that buys nothing.

> **Note for a future revision — this notification cannot reach the peers that need it most.**
> `0xAC` is TCP-only, so it reaches exactly the peers holding an open connection. The peers left
> with a stale address are by definition the ones with no connection: a client sitting in a remote
> upload queue waits hours between connections, and that stored address is precisely what the remote
> dials when the slot comes up. UDP traffic between the two continues throughout (`OP_REASKFILEPING`,
> §3.5), but those packets are fixed-layout and positional with no tag section, so there is nowhere
> to put an address without desynchronising every legacy reader.
>
> A future revision SHOULD therefore be able to announce an IPv6 change **over UDP** as well —
> either a UDP counterpart of `0xAC` addressed to peers already being reasked, or, better, a tagged
> reask variant negotiated by a capability bit so the address rides on traffic that is already
> flowing. Either needs a new wire identifier and is deliberately not attempted unilaterally here;
> see §10.6.
>
> What an implementation MUST NOT do in the meantime is re-point a stored peer address from the
> source address of a received datagram. An unauthenticated UDP source address is not proof of
> identity, and treating it as one turns any observer into a redirector.

### 3.3 Extended Source Exchange (ExtSX)

ExtSX replaces the fixed-layout per-source record of classic SX2 with a self-describing tag block,
which is what lets a source carry an IPv6 address.

#### 3.3.1 Negotiation

ExtSX is gated **exclusively** on `MODMISC_EXTXS` (bit 0 of `CT_MOD_MISCOPTIONS`) from the hello.
There is no separate version negotiation, and the source-exchange version byte deliberately stays
at **1**:

```
SOURCEEXCHANGE2_VERSION   = 4      (classic)
SOURCEEXCHANGEEXT_VERSION = 1      (ExtSX)
```

Version 1 is used because at v1 the classic format's optional trailing fields (userHash at v ≥ 2,
crypt options at v ≥ 4) are switched off, so a record is `ID | port | tagCount | tags` —
self-describing and impossible to desync a tag-skipping reader.

> **This value MUST NOT be increased.** A version > 1 re-enables the legacy tail and the two
> formats become ambiguous.

#### 3.3.2 Request — `OP_REQUESTSOURCES2` (`0x83`, `OP_EMULEPROT`)

```
uint8   version        = 1 if the peer advertised MODMISC_EXTXS, else 4
uint16  options        = 0
uint8[16] fileHash
```

Sent standalone or inside a multipacket. The legacy `OP_REQUESTSOURCES` (`0x81`) carries only the
16-byte hash and no version, and never yields ExtSX.

#### 3.3.3 Answer — `OP_ANSWERSOURCES2` (`0x84`, `OP_EMULEPROT`)

```
uint8      version      = 1  (ExtSX)
uint8[16]  fileHash
uint16     sourceCount
sourceCount × record
```

The packet is zlib-packed when it exceeds 354 bytes, exactly as classic SX2.

**ExtSX record:**

```
uint32  clientID   = htonl(userIDHybrid)      // see below
uint16  tcpPort
uint8   tagCount
tagCount × new-format tag (§0.3)
```

**Classic SX2 record, for contrast** (still sent to peers without `MODMISC_EXTXS`, byte-identical
to before):

```
uint32  clientID
uint16  tcpPort
uint32  serverIP    (network order)
uint16  serverPort
uint8[16] userHash        if version >= 2
uint8   cryptOptions      if version >= 4
```

**Tags defined inside an ExtSX record:**

| Tag | Emitted when | Value |
| --- | --- | --- |
| `CT_EMULE_SERVERIP` `0xBA` | the source has a server, **always paired with `0xBB`** | IPv4 in network order, as an integer (subject to the §0.3 width rule) |
| `CT_EMULE_SERVERTCP` `0xBB` | as above | server TCP port |
| `CT_MOD_IP_V6` `0xAE` | the source is IPv6-reachable **and** its address is global unicast (§7) | HASH16 |
| `CT_EMULE_USERHASH` `0xBC` | **only** to a peer advertising `MODMISC_EXTXS_SKIPTAGS` (§3.3.6) | HASH16 |
| `CT_EMULE_CONOPTS` `0xBE` | as above, and the source has any crypt bit set | UINT8, same bit layout as the classic v4 tail |

Worked example — a source at hybrid ID `0x0100000A`, TCP port 4662, server `1.2.3.4:4661`,
IPv6 `2a01:4f8::1`:

```
0a 00 00 01     clientID  (htonl of the hybrid ID)
36 12           tcpPort   = 4662
03              tagCount  = 3
83 ba 01 02 03 04                     CT_EMULE_SERVERIP,  UINT32
88 bb 35 12                           CT_EMULE_SERVERTCP, UINT16 (size-optimised)
81 ae 2a 01 04 f8 00 …00 01           CT_MOD_IP_V6,       HASH16
```

#### 3.3.4 Sender rules

- The source cap is **500** on the ExtSX path (and on classic v ≥ 4), otherwise 50.
- The `clientID` field is `htonl(userIDHybrid)` — the *hybrid* ID, network-ordered — not the raw
  address. For a LowID or IPv6-only source the two differ, and the receiver normalises the hybrid
  form back out.
- A **LowID source is normally skipped**, but on the ExtSX path a LowID source that has a reachable
  public IPv6 MUST be kept: the receiver can reach it directly over IPv6 and no callback is needed.
- Likewise a source with no usable IPv4 at all is kept if it has an IPv6.
- `CT_MOD_IP_V6` MUST be validated as global unicast before emission.

#### 3.3.5 Receiver rules

- Treat the payload as ExtSX **iff** the packet is SX2, the sending peer advertised
  `MODMISC_EXTXS`, and the version byte is 1.
- ExtSX records have **no fixed size**, so the classic `count × recordSize == dataSize`
  validation does not apply and MUST be replaced by bounds-checked reads with a
  truncation guard around the whole loop.
- Unknown tags MUST be skipped by type.
- `CT_MOD_IP_V6` MUST be rejected unless it is `TAGTYPE_HASH` *and* global unicast (§7).
- ID normalisation is unchanged from classic SX: at version < 3 the ID is the raw address; at
  version ≥ 3 it is the hybrid form. ExtSX runs at version 1 but writes `htonl(hybrid)`, so a
  receiver reads it through the version-1 path and recovers the hybrid ID correctly.
- A LowID source MUST NOT be dropped for firewall reasons if it carries a usable IPv6.
- Judge the two families **independently**: filter/ban each, keep whichever survives, and discard
  the record only when both are unusable.
- An IPv6-only source SHOULD be constructed with a LowID-range placeholder ClientID so that no
  garbage IPv4 is ever dialled.

#### 3.3.6 The user hash and crypt options — and why they are gated

The classic record carries the source's user hash at version >= 2 and its crypt options at
version >= 4. ExtSX pins the version at 1, so both fields are structurally unreachable there. A
source learned over ExtSX therefore arrives with **no user hash** — breaking credit and
secure-identification matching, and obfuscated-UDP keying to that peer — and with **unknown crypt
capability**.

The fix is the obvious one: carry them as tags, `CT_EMULE_USERHASH` (`0xBC`, HASH16) and
`CT_EMULE_CONOPTS` (`0xBE`, UINT8, the same bit layout as the classic v4 tail with bit 3 never set).

They MUST NOT be sent to a peer that has not advertised `MODMISC_EXTXS_SKIPTAGS` (bit 5).

> **Why a capability bit for something the format already mandates.** §0.4 requires a receiver to
> skip unknown tags. At least one deployed IPv6-capable implementation does not: its ExtSX reader
> appends every unrecognised tag to an error string and then returns out of the *entire*
> source-exchange parse. One unrecognised tag in the first record therefore costs it every source
> in the packet, including the ones it had already accepted. Bit 5 is a peer stating that its reader
> genuinely conforms. It is ugly, and it exists only because the conservative alternative — never
> extending the record again — is worse.
>
> `0xBE` is not a new assignment: the same fork already declares it `CT_EMULE_CONOPTS`, commented
> "MOD SX", and never uses it. `0xBC` was verified unused in that fork, in MorphXT and in stock.
>
> A receiver SHOULD accept both tags from any peer. Only *emission* is gated.

### 3.4 UDP obfuscation with IPv6 peers

*(`src/core/net/EncryptedDatagramSocket.cpp`)*

Client UDP obfuscation derives its key from the receiver's user hash plus the **sender's** IP. The
IPv6 case needs a 35-byte key layout instead of the classic 23-byte one:

**IPv4 — 23 bytes:**

```
[0..15]  receiver userHash (16 bytes)
[16..19] uint32 IPv4, host order, written in native byte order
[20]     0x5B   magic
[21..22] uint16 randomKeyPart
```

**IPv6 — 35 bytes:**

```
[0..15]  receiver userHash (16 bytes)
[16..31] 16 IPv6 bytes, network order
[32]     0x5B   magic
[33..34] uint16 randomKeyPart
```

Key = `RC4(MD5(keyData))` with the usual initial-discard behaviour. The rest of the obfuscation
header (clear marker byte, clear `randomKeyPart`, then RC4 over magic `0x395F2EC1`, padding length,
padding, payload) is unchanged and family-independent.

**A receiver MUST select the layout from the address family of the datagram's actual source**, not
by trial: both layouts are valid key material and trying both would succeed on garbage.

> **Critical implementation notes.**
> 1. The magic byte sits at **offset 32** in the IPv6 layout. Placing it at offset 20 — correct only
>    for the 23-byte IPv4 layout — leaves byte 32 uninitialised and produces a key neither side can
>    reproduce. At least one other implementation has this bug; obfuscated IPv6 client-UDP cannot
>    interoperate with it.
> 2. The IPv4 field is the address as a **host-order** `uint32` written in native byte order. This
>    is a preserved historical quirk; changing it to network order breaks every existing IPv4 peer.
>    Cross-check against a reference client before implementing.
>
> Kad UDP obfuscation and TCP stream obfuscation do not involve any IP in their key material and are
> therefore family-agnostic and unchanged.

### 3.5 Connecting, reasking, and callbacks

- **Dial preference:** when a peer is known to be IPv6-reachable and the local host has a confident
  public IPv6 (§2.2), dial IPv6; otherwise dial IPv4 derived from the hybrid ID.
- **A LowID peer with an IPv6 MUST be dialled directly.** The classic "LowID ⇒ must use a callback"
  rule applies to the IPv4 identity only; the IPv6 address stands on its own.
- `OP_DIRECTCALLBACKREQ` (`0x95`) is sent to the peer's Kad UDP port at whichever family is in use;
  the payload (`tcpPort uint16`, `userHash[16]`, `connectOptions uint8`) is unchanged.
- `OP_REASKFILEPING` (`0x90`) and `OP_REASKACK` (`0x91`) are **byte-identical over IPv6**. There is
  no IPv6-specific reask opcode or field. Upload-side sender attribution MUST compare the full
  128-bit address, not a 32-bit projection.
- The server-mediated `OP_CALLBACKREQUEST` (`0x1C`) remains IPv4/LowID only; the IPv6 equivalent is
  the server→client `OP_CALLBACKREQUESTED_IPV6` of §4.5.

#### 3.5.1 Buddy-relayed reask — `OP_REASKCALLBACKTCP` (`0x9A`) with an IPv6 requester

A Kad buddy relaying `OP_REASKCALLBACKUDP` (`0x94`) strips the 16-byte buddy ID and prepends the
requester's address. The classic body is:

```
uint32     requester IPv4, ED2K network order
uint16     requester UDP port
uint8[16]  file hash
...        part bitmap / source count, unchanged
```

For an IPv6 requester the same `0xFFFFFFFF` sentinel used for inline server sources marks an
extended form:

```
uint32     0xFFFFFFFF
uint8[16]  requester IPv6, network order
uint16     requester UDP port
uint8[16]  file hash
...        unchanged
```

Header length is 6 bytes for IPv4 and 22 for IPv6; minimum body size is 22 and 38 respectively.
`255.255.255.255` is never a real requester, so the sentinel is unambiguous.

> **Reading this form is not optional for a v6-capable client.** Without the sentinel branch, an
> IPv6 relay parses as `destIP = 255.255.255.255` with the first 16 address bytes consumed as the
> file hash — silent corruption, not a clean miss. Implement the reader even if you never relay.
>
> The IPv4 form is byte-identical to what stock eMule produces, so legacy buddies on either side of
> the relay are unaffected.

---

## 4. Client ↔ server

*(`src/core/server/ServerConnect.cpp`, `src/core/net/ServerSocket.cpp`,
`src/core/transfer/DownloadQueue.cpp`, `src/core/server/ServerList.cpp`)*

### 4.1 Login — `OP_LOGINREQUEST` (`0x01`)

Body (unchanged prefix): `userHash[16]`, `clientID uint32`, `listenPort uint16`,
`tagCount uint32`, *tags*.

Classic clients send 4 tags (`CT_NAME`, `CT_VERSION`, `CT_SERVER_FLAGS`, `CT_EMULE_VERSION`). When
advertising IPv6, send **5**, adding:

```
CT_MOD_IP_V6 (0xAE), TAGTYPE_HASH, 16 bytes — our public IPv6, network order

wire:  01 01 00 AE <16 bytes>
```

All login tags MUST use the **old** tag format — the server has not yet parsed `SRVCAP_NEWTAGS`
when it begins reading them, and new-format tags cause a parse failure and immediate disconnect on
real servers.

Simultaneously set `SRVCAP_IPV6` (`0x1000`) in the `CT_SERVER_FLAGS` (`0x20`) bitfield.

> **`SRVCAP_IPV6` and `CT_MOD_IP_V6` are strictly coupled**: send both or neither.
>
> **Which of the two actually gates anything is server-dependent, and it is not the bit.** At least
> one server ignores `CT_SERVER_FLAGS` entirely except for the crypt bits, and decides
> sentinel-safety from *the presence of the `CT_MOD_IP_V6` tag* (any value) or from the session
> having arrived over IPv6. Over UDP there is no login state at all, so the gate there is simply the
> query's own source family. A client MUST therefore be sentinel-safe (§4.4) whenever it sends
> either signal — and, in practice, should just be sentinel-safe unconditionally, as eMuleQt is.
>
> A consequence worth knowing: because both signals are gated on having a confident public IPv6 of
> your own (§2.2), a client with a working sentinel parser but no IPv6 address of its own currently
> has **no way to tell such a server so**, and receives no IPv6 sources. There is no channel for
> "I can parse them, I just cannot be one".

Both are sent only when the advertise gate of §2.2 is open. `ST_IPV6_STATUS` is **never sent by a
client**; it is receive-only.

`OP_OFFERFILES` carries no IPv6 tag.

### 4.2 Server capability flags

`OP_IDCHANGE` (`0x40`) is unchanged: `clientID uint32` [, `tcpFlags uint32`] [, `word2 uint32`]
[, `serverReportedIP uint32`]. Bit `0x00004000` of `tcpFlags` means the server speaks the IPv6
extension; the same bit in the UDP flags word of `OP_GLOBSERVSTATRES` means the same for UDP.

The third word is not an auxiliary port in any implementation checked — at least one server puts
its own primary TCP listening port there. Treat it as reserved and ignore it.

**`OP_SERVERIDENT` carries no flags word.** Its body is `serverHash[16]`, `serverIP uint32`,
`serverPort uint16`, `tagCount uint32`, tags — nothing else.

> This implementation parses those bits but **does not gate any behaviour on them**. The inline
> sentinel parser (§4.4) is always active and the login tag is gated only on the client's own
> advertise state. A server therefore does **not** need to echo `0x4000` for interoperability —
> though it SHOULD, for clients that do gate on it.

`serverReportedIP` at offset 12 is IPv4-only and unchanged.

### 4.3 `OP_SERVERIDENT` (`0x41`)

Body (unchanged): `serverHash[16]`, `serverIP uint32`, `serverPort uint16`, `tagCount uint32`,
*tags*. Tags here MAY use either format — and at least one server always uses the **old** one
regardless of `SRVCAP_NEWTAGS`, so a receiver MUST handle both.

**A client must not depend on this packet arriving at all.** At least one server aborts building it
when it cannot determine its own routable IPv4 — which is the default configuration on a v6-only or
air-gapped host — and swallows the error. A client that has no other source of its public IPv6 must
degrade to peer corroboration (§2.1, tier 3), as eMuleQt does.

Servers typically send a *second* ident in reply to `OP_GETSERVERLIST`, which many clients request
immediately after connecting. Handling more than one per session is REQUIRED.

Three IPv6 tags are defined:

| Tag | Type | Meaning |
| --- | --- | --- |
| `CT_MOD_YOUR_IP` `0xAD` | HASH16 | The client's public IPv6 as observed by the server (egress reflection) |
| `ST_IPV6_STATUS` `0xAB` | UINT8 bitfield | The server's verdict on the client's advertised IPv6 (§1.3) |
| `CT_MOD_SVR_IP_V6` `0xAF` | HASH16 | The server's own public IPv6 — informational |

Observed emission conditions (server-dependent, but a useful baseline):

- `CT_MOD_SVR_IP_V6` — whenever the server is configured with one, including to legacy IPv4-only
  clients. Not per-session.
- `CT_MOD_YOUR_IP` — only on a session that arrived over IPv6, and only when the peer address is
  global unicast. Never the UINT32/IPv4 form on this packet.
- `ST_IPV6_STATUS` — only after login, only when the server publishes IPv6 sources at all, and only
  when it holds an address for this client. "Tag absent" is the normal case on the pre-login ident
  drawn by `OP_GETSERVERLIST`.

**Client rules:**

- `CT_MOD_YOUR_IP` is accepted here **only in the HASH16 form**, and **only when the session itself
  runs over IPv6** — a reflection received over an IPv4 session says nothing about the client's v6
  connectivity. It is further rejected unless the address is global unicast *and* assigned to a
  local interface. On acceptance it becomes tier-1 of §2.1, i.e. what the client advertises from
  the next hello/login onwards. It MUST be cleared on disconnect.
- `ST_IPV6_STATUS` SHOULD be sent by servers as `TAGTYPE_UINT8`; receivers MUST accept any integer
  width (§0.3). Only the combination `IPV6ST_PROBED` set **and** `IPV6ST_REACHABLE` clear suppresses
  further advertisement — an absent tag or an unprobed verdict never suppresses.
- `CT_MOD_SVR_IP_V6` is informational; this implementation logs it and keeps using the address it
  dialled.

### 4.4 Inline IPv6 sources — the `0xFFFFFFFF` sentinel

This is the single most important element for server interoperability.

Applies to **`OP_FOUNDSOURCES` (`0x42`)**, **`OP_FOUNDSOURCES_OBFU` (`0x44`)** over TCP, and
**`OP_GLOBFOUNDSOURCES` (`0x9B`)** over UDP. **Requests are entirely unchanged** — no IPv6 opcode
is ever sent; `OP_GETSOURCES` / `OP_GETSOURCES_OBFU` / `OP_GLOBGETSOURCES` / `OP_GLOBGETSOURCES2`
keep their classic payloads.

Block framing is unchanged: `fileHash[16]`, then `sourceCount uint8`, then that many records. (The
UDP form may chain several `hash + block` pairs separated by the two-byte marker `0xE3 0x9B`.)

> **A server MUST NOT put a sentinel record inside a chained UDP datagram.** A legacy client walks a
> chained datagram with the fixed `count × 6` stride to find the next block, so a record carrying 16
> extra bytes desynchronises everything after it — and unlike the TCP case there is no per-session
> capability state to gate on, only the query's source family. Servers that emit sentinels should
> send one file block per datagram; at least one does exactly that, for exactly this reason.

**Per-source record:**

```
uint32   clientID                          // 0xFFFFFFFF = IPv6 sentinel
uint16   port                              // little-endian
uint8    cryptOptions                      // only in the _OBFU variants
uint8[16] userHash                         // only in _OBFU and only if (cryptOptions & 0x80)
uint8[16] ipv6                             // only if clientID == 0xFFFFFFFF — ALWAYS LAST
```

A legacy IPv4 record is therefore exactly the classic 6 bytes, plus the two optional obfuscation
fields. An IPv6 record differs only by the sentinel ClientID and 16 trailing bytes appended **after**
everything else.

> **The 16 bytes are mandatory to consume.** A client that recognises the sentinel but skips the
> record without consuming them desynchronises the remainder of the source list — and with the UDP
> form, the remainder of the datagram. This is precisely why a server MUST only emit sentinel
> records to a session that advertised `SRVCAP_IPV6` (§4.1). Any short read MUST abort the rest of
> the block rather than attempting recovery.

**Client acceptance rules for an IPv6 source:**

- The IPv4 ipfilter and ban list do not apply (they hold no IPv6 ranges); apply IPv6 filtering if
  available.
- An IPv6 source MUST NOT be dropped because the local client is IPv4-firewalled — the exact
  opposite of the classic LowID rule. It is directly dialable regardless of the local IPv4 identity.
- Construct the source with a LowID-range placeholder eD2K ID and the IPv6 as its connect address.
- Obfuscation flags and the user hash, when present, apply identically to both families.
- No server IP/port is stamped on a v6 source; it is reached directly.

### 4.5 `OP_CALLBACKREQUESTED_IPV6` (`0x26`, server→client, TCP)

The IPv6 counterpart of `OP_CALLBACKREQUESTED` (`0x35`), used when a firewalled IPv6 peer asks the
server to have us call it back.

```
uint8[16]  requester's public IPv6, network order
uint16     requester's TCP port, LITTLE-endian
```

Minimum payload 18 bytes. On receipt, create a client with a LowID-range placeholder eD2K ID, set
its address and IPv6 to the given address, mark it IPv6-reachable, and connect. The ban list is
checked; the IPv4 ipfilter is a no-op here. The classic `0x35` path is unchanged.

### 4.6 `OP_SERVERLIST` (`0x32`) — optional trailing IPv6 block

```
uint8                       v4count
v4count × ( ipv4[4] , port[2 LE] )
--- optional, present only from an IPv6-aware server ---
uint8                       v6count
v6count × ( ipv6[16] , port[2 LE] )
```

The IPv6 block is appended after the self-terminating IPv4 array. A legacy server sends nothing
after the v4 array — **not even a zero byte** — so a parser MUST treat "no bytes remain" as "no IPv6
block", and MUST bounds-check `offset + v6count × 18 ≤ size` before reading. A v4-only parser simply
stops after the v4 array and is unaffected.

### 4.7 HighID / LowID and the server's address family

**A HighID *is* the client's packed public IPv4.** A client that reaches a server over IPv6 with no
routable IPv4 is therefore assigned a **LowID unconditionally** — this is structural, not a policy
choice, and no implementation can avoid it.

The RECOMMENDED pattern is consequently:

> Connect to the server **over IPv4** (preserving the HighID) and advertise the IPv6 via
> `CT_MOD_IP_V6`. Peer-to-peer transfers then prefer IPv6. Connect to a server over IPv6 only when
> it has no reachable IPv4.

This is why hostname resolution for servers defaults to **A first, with a single fallback to
AAAA** rather than the usual "prefer IPv6": preferring AAAA on a dual-stack server costs a HighID
for no gain. eMuleQt exposes a `serverPreferIPv6` preference (default `false`) that swaps the order.

No IPv6 branch exists anywhere in ID handling; a LowID obtained over an IPv6 session is accepted
normally, and remains usable because IPv6 peers are dialled directly regardless of eD2K ID.

**A second, unrelated forced-LowID rule to be aware of:** an IPv4 address ending in `.0` packs to a
value below `0x01000000` and is therefore indistinguishable from a LowID. Servers force such a
client to LowID unconditionally, skip the IPv4 dial-back, and report `serverReportedIP = 0`. Treat a
zero there as "unknown", never as an address.

### 4.8 `server.met` — the `ST_IPV6` tag (`0x99`)

**On-disk only; never sent on the wire.** Documented here because `server.met` files are exchanged
between clients and published by server-list maintainers.

The `ServerMet_Struct` header has a 4-byte IP field that cannot hold an IPv6 address. For an IPv6
server entry the header IP is written as **0** and the address is carried in a tag:

```
ST_IPV6 (0x99), TAGTYPE_HASH, new-format tag:

81 99 <16 bytes of IPv6, network order>
```

**This is a deliberate compatibility trick.** Stock eMule drops unknown `server.met` tags and then
rejects the resulting `ip == 0` entry when adding the server. It therefore *ignores* IPv6 servers
rather than mis-dialling them. An IPv6-aware reader recognises `header ip == 0` plus an `ST_IPV6`
tag as a valid IPv6 server. A dynIP (hostname) entry takes precedence over `ST_IPV6`, and a second
`ST_IPV6` tag MUST NOT overwrite the first.

Validation of such an entry MUST test routability of the actual 128-bit address, not of the
`uint32` projection (which is 0 and would reject every IPv6 server).

> **This is not a universal convention.** At least one server implementation persists its IPv6 peers
> in a separate sidecar file instead and neither writes nor reads `ST_IPV6`. The two on-disk forms do
> not round-trip through each other; do not assume a `server.met` from another implementation carries
> its IPv6 entries.

---

## 5. Kademlia

*(`src/core/kademlia/KadSearch.cpp`, `src/core/kademlia/KadUDPListener.cpp`)*

> **Kad itself is IPv4-only end to end** in this implementation: the transport, contact records,
> routing table, `nodes.dat`, firewall checks and verify keys are all IPv4. The only IPv6 on the Kad
> wire is two additive string tags carried inside the source-publish tag list and re-served verbatim
> inside source search results. **No new Kad opcode, no Kad version bump, and no Kad-level IPv6
> capability signal exists.**

### 5.1 The `ip6` and `bi6` tags

Unlike every other source tag (which uses a single-byte numeric name), these are genuine
**multi-character string-named tags** whose value is a **32-character hex string**, not a binary
blob.

```
0x02              TAGTYPE_STRING
0x03 0x00         name length = 3
'i' 'p' '6'       name
0x20 0x00         value length = 32
<32 ASCII hex characters>
```

Total 40 bytes per tag. The hex is the 16 address bytes in network (textual) order, so
`2001:db8::1` encodes as `20010db8000000000000000000000001`.

- Emitters produce **lowercase**; at least one other implementation produces uppercase.
  **Parsers MUST be case-insensitive.**
- A value that is not exactly 32 hex characters MUST be ignored silently.
- Kad tag encoding has no compact-name form and **no `TAGTYPE_BLOB` and no compact string types** —
  sending `ip6` as a binary blob or a compact string causes the entire packet to be dropped, not
  just the tag. This is why a hex string is used.

| Tag | Meaning |
| --- | --- |
| `"ip6"` | The source's own public IPv6 |
| `"bi6"` | The source's serving buddy's public IPv6 |

### 5.2 Publishing — `KADEMLIA2_PUBLISH_SOURCE_REQ` (`0x44`)

Body: `UInt128 fileID`, `UInt128 sourceID`, `uint8 tagCount`, *tags*. The classic tags
(`FT_SOURCETYPE` `0xFF`, `FT_SOURCEPORT` `0xFD`, `FT_SOURCEUPORT` `0xFC`, `FT_FILESIZE` `0x02`,
`FT_ENCRYPTION` `0xF3`, and for a buddy-relayed source `FT_SERVERIP` `0xFB` / `FT_SERVERPORT` `0xFA`
/ `FT_BUDDYHASH` `0xF8`) are unchanged.

Append, **last**:

- `"ip6"` — only when the advertise gate of §2.2 is open. It MAY accompany **any** source type
  (1, 3, 4, 5 or 6).
- `"bi6"` — only in the firewalled-with-buddy branch, when the buddy has a known IPv6.

### 5.3 Storing and serving

An indexing node stores unrecognised tags verbatim and re-serves them inside
`KADEMLIA2_SEARCH_RES` (`0x3B`) source results. No change to the indexing node is required for
`ip6`/`bi6` to propagate — they ride through the existing "unknown tag" path.

> **`ip6` is an unauthenticated claim.** Unlike `FT_SOURCEIP`, which an indexing node overwrites
> with the observed packet source address, `ip6` is stored exactly as the publisher wrote it. A
> receiving client MUST treat it as a hint and validate it (§7) before dialling, and SHOULD NOT
> treat it as evidence of anything about the publisher.

### 5.4 Consuming a search result

`KADEMLIA2_SEARCH_RES` (`0x3B`) body: `UInt128 senderKadID`, `UInt128 target`, `uint16 count`,
then `count ×` (`UInt128 sourceID`, `uint8 tagCount`, *tags*).

Parse order matters: evaluate the classic numeric tags and the source-type gate **first**, then read
`ip6`/`bi6` — an `ip6` on a rejected source type is meaningless. If multiple `ip6` tags appear, the
last valid one wins and a trailing malformed one clears the value.

An accepted `ip6` marks the source IPv6-reachable, which allows a LowID (type 3/5) source to be
dialled directly over IPv6 instead of via the buddy callback.

### 5.5 Kad DNS

Kad bootstrap hostname resolution requests **A records only**, deliberately. Kad contacts are keyed
by a 32-bit IPv4 throughout; taking the first address of either family turns an AAAA-first answer
into address `0.0.0.0` and silently bootstraps against nothing.

---

## 6. eD2K link format

*(`src/core/protocol/ED2KLink.cpp`)*

### 6.1 File links

Grammar as emitted:

```
ed2k://|file|<name>|<size>|<hash>|[p=<md4>[:<md4>]*|][h=<base32>|]/[|s6=<v6list>][|sources,<v4list>][|/]
```

- `<name>` is percent-encoded; the emitter first replaces each of `" * < > ? | \ / :` with `_`.
- `<hash>` is uppercase base16 MD4. Parsers MUST accept either case.
- `p=` part hashes are colon-separated uppercase base16.
- `h=` is the 20-byte AICH root hash in unpadded base32.
- The lone `/` **terminates the parameter section and is emitted unconditionally, before any source
  block.** This is load-bearing: stock eMule's tokenizer stops at the first empty token, so
  `…|HASH||sources,…` is rejected outright.
- Parsers MUST accept parameters in any order, ignore unknown ones, and accept a link with no
  trailing `|/`.
- Both `s=<http-url>` (single HTTP source) and the legacy `sources@YYMMDD,` spelling are accepted on
  ingest. This implementation caps a link at **64** sources across all source tokens combined.

### 6.2 The `s6=` IPv6 source token

```
s6=<entry>[,<entry>]*        entry := "[" <ipv6-literal> "]" [":" <port>]
```

- **Emission always brackets the literal and always includes an explicit port.**
- **Ingest is more tolerant**: `[v6]:port`, `[v6]` (port defaults to **4662**), and a bare
  unbracketed IPv6 literal are all accepted. A bare token containing two or more colons MUST parse
  as a *whole* IPv6 address — it MUST NOT be split at the last colon, because `2001:db8::1:4662` is
  itself a valid address.
- `s6=` is IPv6-only: an IPv4 literal or a DNS name inside it is a malformed entry and MUST be
  dropped.
- A malformed entry MUST be skipped without discarding the rest of the list.
- The classic "drop `*.*.*.0` as a LowID" rule applies to IPv4 only. Running an IPv6 literal through
  a 32-bit projection yields 0 and silently drops every IPv6 source.

#### Placement — the legacy-parser constraint

Two placement rules are **mandatory**, and both exist to protect stock eMule:

1. **`s6=` MUST appear after the `/` that closes the parameter section**, not among the `p=`/`h=`
   parameters. Stock eMule's parameter switch asserts on an unknown parameter in debug builds.
2. **`s6=` MUST appear before the `|sources,` token.** Stock eMule's source scan begins at the
   *first* occurrence of the substring `sources` and tokenises everything from there to the end of
   the link. Anything earlier is never reached, so the IPv6 hints stay invisible to it. Conversely
   the token name MUST NOT itself contain the substring `sources`.

**Why IPv6 hints are not simply placed inside `sources,`:** stock eMule splits each source token at
its **first** colon and never rejects a link over a bad token. `[2001:1234::1]:4662` would yield the
port `1234` and a first segment of `2001`, which `inet_addr()` on Windows accepts as a bare 32-bit
number. The result is **a live source pointed at the wrong address**, not a harmless skip.

Classification on emission: an IPv6 literal goes to `s6=`; IPv4 literals *and* DNS names (including
names with only AAAA records) go to `sources,`. If only IPv6 hints exist, no `sources` token is
emitted at all.

### 6.3 Examples

```
ed2k://|file|test.mp3|12345|0123456789ABCDEF0123456789ABCDEF|/

ed2k://|file|test.mp3|12345|0123456789ABCDEF0123456789ABCDEF|/|s6=[2a01:4f8::1]:4662|/

ed2k://|file|test.mp3|12345|0123456789ABCDEF0123456789ABCDEF|/|s6=[2a01:4f8::1]:4662,[2a01:4f8::2]:5662|/

ed2k://|file|My%20File.avi|10485760|0123456789ABCDEF0123456789ABCDEF|p=…|h=…|/|s6=[2a01:4f8::1]:4662|sources,host.example.com:4662|/
```

### 6.4 Server links

```
ed2k://|server|<address>|<port>|/
```

- An IPv6 literal is **always bracketed on emission**: `ed2k://|server|[2001:db8::1]|4661|/`.
  Rationale — a legacy client runs `inet_addr()` on it, fails, and degrades to treating the string
  as a dynIP hostname. The entry becomes unresolvable rather than mis-dialled.
- On ingest both bracketed and bare literals are accepted and the bracket-stripped form is stored.
  An unbracketed address containing a colon is rejected unless the whole string parses as an IPv6
  literal — this is what rejects `host:1234` pasted into an address field.

`ed2k://|serverlist|…` and `ed2k://|nodeslist|…` carry an opaque URL and have no IPv6 semantics of
their own.

### 6.5 Advertising your own address in a link

When a client adds itself as a source hint to a link it emits, it MUST apply the gate of §2.2 to the
IPv6 hint. eMuleQt additionally exposes a user preference (`ed2kLinkAdvertiseIPv6`, default on) and
supports a configured hostname or literal that is emitted alongside.

---

## 7. Address acceptance rules

Before advertising an address as your own, or accepting one from a peer, server or Kad result, an
implementation MUST verify it is **global unicast**. The following IPv6 ranges are rejected:

| Range | Why |
| --- | --- |
| `::/128`, `::1/128` | unspecified, loopback |
| `fe80::/10` | link-local |
| `fc00::/7` | unique local |
| `::ffff:0:0/96` | IPv4-mapped |
| `64:ff9b::/96` | NAT64 well-known prefix |
| `100::/64` | discard-only |
| `2001::/32` | Teredo |
| `2001:20::/28` | ORCHIDv2 |
| `2001:db8::/32` | documentation |
| `2002::/16` | 6to4 |
| `5f00::/16` | reserved |
| `ff00::/8` | multicast |

Note that `2001:db8::/32` is rejected — a documentation address parses cleanly but is refused as an
actual source. Test fixtures using it will parse but never connect.

> **Implementations disagree on how strict this list should be, and it matters for interop.** At
> least one server accepts anything that is global-unicast-and-not-private, which admits
> `2001:db8::/32`, `2002::/16`, `2001::/32` and the rest of the table above. It will publish, probe
> and reflect such addresses; a client applying the strict list drops every one of them. Since that
> server's own containerised interop fixtures run on the documentation prefix, the two are mutually
> invisible out of the box.
>
> eMuleQt resolves this with a **lab-network mode** rather than a permanent relaxation. It is not a
> new setting: clearing the long-standing `filterLANIPs` preference — already the "this is a private
> network, stop rejecting non-routable peers" switch for Kad and the server list — additionally
> widens IPv6 acceptance to every unicast address, leaving only multicast and the unspecified
> address rejected. IPv4 acceptance is deliberately untouched (it is relaxed for LAN through the
> existing `forceCheck`/`allowLan` paths), because the classic IPv4 rule is load-bearing for
> compatibility. Production defaults are unchanged.
>
> An implementation without such a mode should expect to be untestable against fixtures on reserved
> prefixes.

**Security considerations.**

- IPv4 ipfilter range lists do not cover IPv6. Until an implementation carries IPv6 ranges, IPv6
  peers pass unfiltered; the ban list, which is address-typed, does apply. Implementers should be
  explicit with users about this asymmetry.
- Kad `ip6` values are unauthenticated (§5.3) and `CT_MOD_YOUR_IP` reflections are attacker-
  controlled (§2.1). Both MUST be validated, and reflections MUST additionally be corroborated
  against locally-held addresses before being adopted.
- Per-address abuse limits should be applied per exact 128-bit address for IPv6. Prefix-based
  aggregation is not implemented here; note that a single IPv6 prefix delegation can supply an
  effectively unlimited number of distinct addresses, so an implementation that grants per-address
  upload slots without prefix awareness is trivially gameable. eMuleQt mitigates this by allowing
  only **one** queued or uploading client per exact IPv6 address (against three per IPv4 address).

---

## 8. Address family and identity — summary of rules

| Situation | Rule |
| --- | --- |
| Client reaches a server over IPv6 with no routable IPv4 | LowID, unconditionally (§4.7) |
| Peer is LowID but has a public IPv6 | Dial it directly; do not require a callback (§3.5) |
| Source has only an IPv6 | Represent with a LowID-range placeholder eD2K ID so no garbage IPv4 is dialled |
| Local client is IPv4-firewalled | Does **not** disqualify an inbound IPv6 source (§4.4) |
| Peer's ports | One TCP port and one UDP port, shared by both families |
| Dual-stack listener | Bind `::` with `IPV6_V6ONLY=0`; normalise `::ffff:` peers to IPv4 |

---

## 9. Reserved — defined but NOT implemented

The following identifiers are reserved in the eMuleQt source so that nobody reuses them, but **no
code sends, receives or handles them**. A client or server MUST NOT expect eMuleQt to understand
them, and MUST NOT list them as supported on the strength of this document.

| Name | Value | Intended purpose |
| --- | --- | --- |
| `OP_GETSOURCES_IPV6` | `0x24` | client→server: request tag-block IPv6 sources |
| `OP_FOUNDSOURCES_IPV6` | `0x25` | server→client: tag-block IPv6 sources reply |
| `OP_GLOBGETSOURCES_IPV6` | `0xA5` | as above, over UDP |
| `OP_GLOBFOUNDSOURCES_IPV6` | `0xA6` | as above, over UDP |

> **Reserved *here* — not unused everywhere.** At least one server implements all four fully: a
> tag-block source exchange whose per-source record is `id 4 | port 2 | tagCount 1 | tags`, with a
> `CT_MOD_IP_V6` tag per v6-reachable source, over TCP (`0x24`/`0x25`) and UDP (`0xA5`/`0xA6`). A
> client that never sends `0x24`/`0xA5` simply does not use the richer format.

This "S3b" tag-block source exchange was deliberately deferred on the client side: the inline
sentinel of §4.4 carries the same sources from the same servers with no new opcode and no new
negotiation, and a second ingest path would have to be kept in step with the first.

Also out of scope and deliberately inert: NAT rendezvous / `PR_NAT` (the `0x00008000` server flag
and any NAT-port tag are parsed and ignored), and µTP.

---

## 10. Known gaps in this implementation

Stated plainly so that another implementer does not spend time diagnosing them as their own bugs.

### Ours

1. **Kad is IPv4-only.** A Kad packet arriving from a native IPv6 address is attributed to
   `0.0.0.0` and cannot be routed or replied to. Only the `ip6`/`bi6` payload tags of §5 cross the
   boundary, and that is deliberate: a Kad node's identity is a 32-bit address throughout the
   routing table, `nodes.dat`, the per-subnet caps and the UDP verify keys, and no interoperable
   IPv6 contact format exists in any implementation surveyed. Adding one would fork the DHT.
2. **The tag-block source exchange (§9) is not implemented**, so IPv6 sources arrive only via the
   inline sentinel. Against servers that implement both, this loses nothing.
3. **Server search results carry no IPv6.** Search results are not a source of downloads — sources
   come from `OP_FOUNDSOURCES`, which does handle IPv6 — so the only casualty is spam
   fingerprinting of IPv6-only publishers. Closing it would need a server-side result tag that no
   server implements.
4. **A source known only by IPv6 gets UDP reasks only if it advertised a UDP port.** Neither the
   server sentinel record nor the ExtSX record carries one; a Kad publish and a completed hello do.
5. **`ST_NAT_PORT` and the `0x8000` rendezvous flag are skipped by type, not parsed.** `PR_NAT`
   and µTP are out of scope.
6. **An IPv6 change is announced over TCP only.** `OP_CHANGE_CLIENT_IP` (§3.2) reaches connected
   peers; a peer that holds us in its upload queue with no open socket keeps the stale address until
   our next hello, even while UDP reasks flow between us the whole time. This is a limit of the
   protocol rather than of the implementation — no existing opcode has room for the address — and
   the fix belongs in a negotiated revision, not in a unilateral extension. See the note in §3.2.

### Not ours — but you will hit them

7. **IPv6 UDP obfuscation is not interoperable with at least one other implementation.** Its
   35-byte key writes the magic byte at offset 20, where the 16-byte address copy immediately
   overwrites it, and leaves offset 32 uninitialised on both send and receive. The key is therefore
   nondeterministic even against itself. Ours (§3.4) is the only self-consistent layout; expect to
   fall back to plaintext against that implementation.
8. **At least one implementation's ExtSX reader discards the whole answer on an unknown tag** —
   see §3.3.6, which is the entire reason bit 5 exists.
9. **At least one implementation consumes the inline sentinel's 16 bytes but then discards the
   parsed address**, so it stays in sync but cannot use a server-supplied IPv6 source. Do not treat
   its behaviour as normative for §4.4.
10. **`ip6`/`bi6` values are stored unverified by indexing nodes** (§5.3), unlike `FT_SOURCEIP`.
   Treat them as hints and validate before dialling.

## 11. Conformance checklist

A client claiming interoperability with these extensions should implement, at minimum:

**Required for basic IPv6 peer connectivity**

- [ ] Dual-stack listener with `::ffff:` normalisation (§0.2)
- [ ] `CT_MOD_MISCOPTIONS` emit and parse (§3.1)
- [ ] `CT_MOD_IP_V6` emit (gated per §2.2) and parse in the hello (§3.1)
- [ ] Dial a LowID peer directly when it has an IPv6 (§3.5)
- [ ] Global-unicast validation per §7, on **every** ingest path including the hello
- [ ] `OP_CHANGE_CLIENT_IP` received on `OP_EDONKEYPROT` (§3.2)
- [ ] `OP_REASKCALLBACKTCP` sentinel form — **reader at minimum** (§3.5.1)

**Required to exchange IPv6 sources with peers**

- [ ] `MODMISC_EXTXS` negotiation and the version-1 rule (§3.3.1)
- [ ] ExtSX request/answer record format, including the integer width rule (§0.3, §3.3.3)
- [ ] Independent per-family filtering on ingest (§3.3.5)
- [ ] `MODMISC_EXTXS_SKIPTAGS` before emitting `CT_EMULE_USERHASH` / `CT_EMULE_CONOPTS` (§3.3.6)

**Required to exchange IPv6 sources with servers**

- [ ] `CT_MOD_IP_V6` login tag in **old** tag format, plus `SRVCAP_IPV6` (§4.1) — send both, but do not assume the server reads the bit
- [ ] Inline sentinel record parsing, **including consuming the 16 trailing bytes** (§4.4)
- [ ] `OP_CALLBACKREQUESTED_IPV6` (§4.5)
- [ ] `OP_SERVERLIST` trailing IPv6 block, bounds-checked (§4.6)
- [ ] A-record-first server DNS, for HighID preservation (§4.7)

**Required to publish and find IPv6 sources over Kad**

- [ ] `"ip6"` / `"bi6"` as 32-hex-char string tags, parsed case-insensitively (§5.1)
- [ ] Emit only after the source-type gate on ingest (§5.4)

**Required for link interoperability**

- [ ] `s6=` emission after `/` and before `|sources,` (§6.2)
- [ ] Tolerant `s6=` ingest, including the bare-literal rule (§6.2)
- [ ] Bracketed IPv6 in `|server|` links (§6.4)

**Recommended**

- [ ] `ST_IPV6_STATUS` handling and suppression on probed-unreachable (§4.3)
- [ ] `CT_MOD_YOUR_IP` corroboration rather than direct adoption (§2.1)
- [ ] IPv6 UDP obfuscation with the 35-byte key, magic at offset 32 (§3.4)
- [ ] `server.met` `ST_IPV6` read/write with header IP 0 (§4.8)
- [ ] Per-exact-address IPv6 upload-slot limiting (§7)

---

## Appendix A — source file map

| Area | File |
| --- | --- |
| Constants | `src/core/utils/Opcodes.h`, `src/core/server/Server.h` |
| Address type, validation, host:port parsing | `src/core/net/Address.{h,cpp}` |
| Tag encoding | `src/core/protocol/Tag.cpp` |
| Hello, dial, reask | `src/core/client/UpDownClient.cpp`, `src/core/client/DownloadClient.cpp` |
| ExtSX emit / parse | `src/core/files/KnownFile.cpp`, `src/core/files/PartFile.cpp` |
| UDP obfuscation | `src/core/net/EncryptedDatagramSocket.cpp` |
| Server login, DNS | `src/core/server/ServerConnect.cpp` |
| `OP_SERVERIDENT`, `OP_IDCHANGE`, callbacks | `src/core/net/ServerSocket.cpp` |
| Inline source sentinel | `src/core/transfer/DownloadQueue.cpp` |
| `server.met`, server list block | `src/core/server/ServerList.cpp`, `src/core/server/Server.cpp` |
| Public-IPv6 confidence gate | `src/core/app/AppContext.cpp` |
| Kad tags | `src/core/kademlia/KadSearch.cpp`, `src/core/kademlia/KadUDPListener.cpp` |
| eD2K links | `src/core/protocol/ED2KLink.cpp` |

## Appendix B — IPv6 test inventory

Every test file under `tests/` that exercises IPv6, with the IPv6-relevant coverage in each.
Deterministic unless marked **live**; the deterministic set runs with `ctest -LE live`.

*(`-LE` filters by label. `-E live` filters by test **name** and would silently include the live
tests, whose names are capitalised "Live".)*

| File | IPv6 coverage | Live |
| --- | --- | --- |
| `tst_LocalIPv6` | The whole file (34 cases). Stable-vs-temporary address ranking, deprecated/tentative flags, `/proc/net/if_inet6` parsing, Windows lifetime tie-break, `publicIPv6Override` resolution, tier-slot population. Pure — needs no interfaces | no |
| `tst_Address` | 42 cases. Family construction and round-trips, ordering and hashing across families, the `isPublicIP` reject table (Teredo, documentation, 6to4, multicast, SRv6), `toUint32`/`toNetworkUint32` returning 0 for v6, mapped-v4 conversion, `isGoodIP`, `Endpoint` and `parseHostPort`/`formatHostPort` bracketing, and `labNetworkMode_widensIPv6Only` (§7 lab mode, incl. IPv4 left untouched and the scoped guard restoring state) | no |
| `tst_SourceExchange` | 12 cases. ExtSX v1 tag-block layout, IPv6 round-trip, unknown-tag skip without desync, LowID-with-IPv6 kept where classic drops it, the 500 cap, hybrid-ID encoding, IPv6-only source survival, `CT_EMULE_USERHASH`/`CT_EMULE_CONOPTS` emitted **only** for a `MODMISC_EXTXS_SKIPTAGS` peer and recovered on parse, plus the §2 confidence gate (peer corroboration threshold, reflection not held locally, tier precedence) | no |
| `tst_ED2KLink` | 16 cases. `s6=` grammar (bracketed, bracketed-no-port, bare literal, bare-with-colon-is-an-address, malformed skipped), rejection of non-IPv6 in `s6=`, the v4-LowID drop applying to v4 only, and the legacy-safety invariants: `s6=` after `/` and before `\|sources,`, no IPv6 inside `sources,`, v6-only hints emit no `sources` token, and a re-implementation of the legacy scan recovering exactly the v4 entry. Plus IPv6 server links, bracketed and bare | no |
| `tst_ServerList` | 9 cases. Adding an IPv6 server, not colliding with a dynIP entry (both project to `uint32` 0), duplicate rejection, `ST_IPV6` `server.met` round-trip incl. mixed lists, static-server round-trip, text and `ed2k://` link import of bracketed literals, `findByIPUdp` by Address | no |
| `tst_Server` | 6 cases. Address-vs-hostname classification, `addressWithPort` bracketing, `ST_IPV6` tag add/read, dynIP precedence over `ST_IPV6`, tag write round-trip, and no `ST_IPV6` emitted for an IPv4 server | no |
| `tst_DownloadQueue` | 5 cases. The `0xFFFFFFFF` sentinel stride (the sentinel source is placed **first**, so a mis-stride corrupts the source after it), link-sourced v6 literals surviving a firewalled harness, v4+v6 collapsing to one client, banned-v6 drop, and no dedup against an address-less client | no |
| `tst_IPFilterMatch` | 8 IPv6 cases. Range match and boundaries across a byte carry, families isolated in both directions, CIDR prefix expansion, explicit `start - end`, `description:range` with colons in the description, longest-match parsing (so `2a01:4f8::/32` is never split into `4f8::/32`), overlap split across differing levels, save/load round-trip, and the Address-typed `ipBlocked` signal | no |
| `tst_UpDownClient` | 4 cases. Hello `CT_MOD_MISCOPTIONS`/`CT_MOD_IP_V6` parsing, rejection of a non-public advertised IPv6, `MODMISC_EXTXS_SKIPTAGS` parsed independently of the older bits, and `OP_CHANGE_CLIENT_IP` accept/short-packet/non-public-address handling | no |
| `tst_CallbackAndQueueRank` | 1 IPv6 case: `OP_REASKCALLBACKTCP` in the `0xFFFFFFFF` sentinel form, over a real IPv6 loopback socket, asserting the reply reaches the v6 endpoint (skips if the host has no IPv6 loopback) | no |
| `tst_UploadQueue` | 2 cases: one queued/uploading client per **exact** IPv6 address (no prefix aggregation), and slot promotion alternating between families | no |
| `tst_EncryptedDatagram` | 1 case: the 35-byte IPv6 client-UDP key round-trip, with the magic byte at offset 32 (§3.4) | no |
| `tst_HostResolver` | 3 cases. IPv6 literal short-circuit, family filtering (`IPv6Only` on a v4 literal returns empty with a reason), and `PreferIPv4`/`PreferIPv6`/`IPv4Only`/`IPv6Only` ordering and dedup | no |
| `tst_ClientList` | 1 case: `findByEndpoint_UDP` finding an IPv6 peer at all, and same-port-different-family not cross-matching | no |
| `tst_AbstractFile` | 1 case: an IPv6 literal in `ed2kHostname` accepted as a link source hint (the old "contains a dot" test dropped it) | no |
| `tst_Preferences` | 2 cases: `separateIPv6Queue` default and YAML round-trip of a non-default value | no |
| `tst_ServerLocalTest` | Round 7 against a local server over `::1` — start, request fixture sources over IPv6, stop. `QSKIP`s when the server lacks `ipv6.enabled` | **yes** |
| `tst_PortTestLive` | `testPortsQtIPv6` — `QSKIP`s when either this host or the test server has no IPv6 route, so an IPv4 "closed" verdict is never misreported as an IPv6 one | **yes** |
| `tst_FileDownloadLive` | No IPv6-specific assertions. The only v6 surface is the Kad source callback carrying `sourceIPv6`/`buddyIPv6`; given §10.1 an IPv6 Kad path cannot be exercised here | **yes** |

**Not covered by any test**, and known: the `OP_CHANGE_CLIENT_IP` *send* path (needs two connected
peers and a public-IPv6 change), `OP_DIRECTCALLBACKREQ` in either family, and IPv6 Kad transport
(§10.1, declined).
