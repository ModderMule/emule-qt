# ExtSX LowID routes and buddy offer

Status 2026-11-12: built. Route tags sent + read (`KnownFile::buildSrcInfoPacket`,
`KnownFile::writeExtendedSourceExchangeData`, `PartFile::addClientSources`); buddy offer taken
by a TCP claim over the open link (`OP_BUDDYCLAIM`, below). Context: aMule issue #2456.

The route tags stand alone: a client may send and read them without the offer or the claim.
Offer + claim are optional, for a firewalled LowID that has no buddy yet.

## Why the buddy ID is ~KadID

A firewalled node F looks for its buddy with a FINDBUDDY lookup whose target is
`~KadID(F)` (eMuleAI `ClientList.cpp:1401`, MFC same). Its `KADEMLIA_FINDBUDDY_REQ` carries
that target as `<buddy id 16>`. The same 16 bytes are then:

- the Kad address the buddy sits near. Nodes close to `~KadID(F)` are asked, so the buddy is
  one of them. A downloader without the buddy's endpoint runs a `FINDSOURCE` lookup on the
  buddy ID and lands next to it (`UpDownClient::tryToConnect`, eMuleAI `BaseClient.cpp:3323`).
- the key the buddy files F under. `KADEMLIA_CALLBACK_REQ <buddy id><file><tcp>` is matched
  against it (`ClientList::findServedBuddy`, eMuleAI `FindServedBuddyByKadID`).
- F's proof the traffic is for it. F checks `~id == KadID(F)` on `FINDBUDDY_RES` and on
  `OP_CALLBACK` (eMuleAI `KademliaUDPListener.cpp:1790`, `ListenSocket.cpp:2464`).

Why the complement, not KadID itself: nodes near KadID(F) are F's own routing neighbourhood.
Every firewalled node near F would ask the same few nodes, and those keep F's contacts anyway.
The complement sends each firewalled node to an unrelated part of the keyspace, which spreads
buddies evenly. It is cheap to verify (one XOR) and needs no extra field.

eMuleAI also sends its **plain** KadID in the hello as `CT_EMULE_SERVINGBUDDYID` 0xBF,
written from raw `CUInt128` memory (`BaseClient.cpp:2010`, `myKadID.GetData()`): plain *and*
in word order, not `ToByteArray` order. It stores it in the same field as the Kad-learned
~KadID and relays it in ExtSX. A relayed eMuleAI buddy ID can be either form. We don't
convert it: such IDs are used for our own callback only, never relayed (relay rule).

## Wire

Per-source ExtSX record, extra tags for a **LowID** source. All tags eMuleAI reads itself
(`PartFile.cpp:5288`), so they go to every ExtSX peer, no skip-tags gate.

| tag | id | type | when |
|---|---|---|---|
| `CT_EMULE_ADDRESS` | 0xB0 | u32 net order | we know its public IPv4 (connect address, `isGoodIP`) |
| `CT_EMULE_UDPPORTS` | 0xf9 | u32 `kad<<16 \| udp` | with ADDRESS, a port known |
| `CT_EMULE_BUDDYIP` | 0xfc | u32 net order | relayable buddy route, buddy IPv4 |
| `CT_EMULE_SERVINGBUDDYIPV6` | 0xA0 | hash16 | relayable buddy route, buddy public IPv6 |
| `CT_EMULE_BUDDYUDP` | 0xfd | u32 port | relayable buddy route |
| `CT_EMULE_SERVINGBUDDYID` | 0xBF | hash16 | relayable buddy route; ~KadID, `ToByteArray` order (as `TAG_BUDDYHASH` decoded) |

`CT_EMULE_CONOPTS` (bit-5 peers only) now sets bit 3 (direct UDP callback) when the record
carries ADDRESS and a Kad port and the source supports it.

Which LowIDs go out on ExtSX (besides the old IPv6 / NAT-T server cases):

- every LowID we completed a hello with (`UpDownClient::hasConnectedOnce`), with or without
  a buddy or a shared server;
- every LowID with a relayable buddy route (`hasRelayableBuddyRoute`: relayable ID + port +
  good buddy IPv4 or public IPv6).

Classic SX (no tag block) still sends no such LowID.

### Relay rule

We hand on only buddy IDs known to be ~KadID: from Kad (`TAG_BUDDYHASH`), from a
`FINDBUDDY_REQ`, or from an ExtSX peer with `MODMISC_EXTXS_SKIPTAGS` (bit 5), which follows
this rule too. IDs from other ExtSX peers are kept for our own callback, not relayed. We
ignore 0xBF in hellos.

### Buddy offer, `CT_MOD_BUDDY_OFFER` 0xBD

u32: bits 0-15 our Kad UDP port, 16-31 reserved 0. Free in eMuleAI, MorphXT, Xtreme, aMule,
stock.

Carried on one extra record at the end of the answer that describes the answerer itself:
`<our ip, htonl(hybrid) 4><our tcp port 2><tagcount 1 = 1><0xBD>`. Counted in the source
count. Sent when all hold:

- requester: ExtSX + bit 5, LowID, Kad port in hello != 0, no buddy in its hello
  (0xfc/0xA0 absent), not our served buddy already;
- us: Kad connected, TCP and UDP open, UDP verified, a free served-buddy slot
  (`ClientList::buddyOfferPort`), public IPv4 known.

A receiver that knows the tag takes it as the offer and does not add the record as a source.
An older one adds it as a source, which dedups into the sender.

### Receive

- ADDRESS / buddy IPv4 dropped when not `isGoodIP`, IP-filtered or banned; buddy IPv6 when
  not public or banned. A buddy endpoint without a port is dropped whole. Route tags on a
  HighID record are ignored.
- LowID gets ADDRESS as connect address only (TCP still by callback), Kad/UDP ports,
  buddy address/IPv6/port, buddy ID (relayable only from a bit-5 sender).
- Firewalled receiver: the NAT keep rule now also gets the Kad endpoint and the buddy
  (`natt::PeerInfo`).
- Duplicate of a known source: `UpDownClient::adoptExchangedRoute` fills only empty route
  fields (ports/address only while unconnected).
- Callbacks via a relayed buddy go through the existing per-buddy budget
  (`ClientList::allowRelayRequest`).

## Using the offer: claim over the open TCP link (built)

Optional. Not needed to send or use the route tags above.

Goal: a firewalled LowID R without a buddy turns the offer from S into a buddy at once,
over the TCP connection that just carried the SX answer. No Kad search, which mostly fails
(`kad-buddy-search.md`: low offer rate), no UDP round trip (alternative: footnote ¹).

### Wire

`OP_EMULEPROT` `OP_BUDDYCLAIM` 0xBD `<subop 1>[payload]`. Unclaimed as `OP_` in stock,
MorphXT, eMuleAI (0xB3-0xBB there); 0xBC = HTTP cache. Same number as the 0xBD tag, but tags
and opcodes never meet.

| subop | dir | payload |
|---|---|---|
| 0 CLAIM | R→S | `<buddy id 16>` = ~KadID(R), `ToByteArray` order (as in `FINDBUDDY_REQ`) |
| 1 ACCEPT | S→R | — |
| 2 REFUSE | S→R | `<reason 1>`: 1 busy (no slot / uses a buddy), 2 not eligible, 3 IP or buddy ID already served |

No capability bit: R sends CLAIM only to a peer that sent it the offer. R's Kad port comes
from its hello (`CT_EMULE_UDPPORTS`).

### R (firewalled, gets the offer)

- `addClientSources` → `ClientList::takeBuddyOffer(sender, kadPort)` on the sender's own
  offer record. Gates: Kad running, firewalled, UDP not open+verified, no buddy, no claim
  pending; sender HighID IPv4, Kad state None, link connected, not failed within 1 h, not
  banned. Sends CLAIM, sender → `KadState::ClaimingBuddy`, buddy status Connecting.
- ACCEPT → `ConnectedBuddy` and `setBuddy(…, Connected)` at once; BUDDYPING keep-alive and
  hello 0xfc/0xfd follow as for any buddy; Kad source publish takes it on the next cycle.
- REFUSE, no answer in 30 s (`kBuddyClaimTimeoutSecs`), or link closed → failed candidate
  (1 h), state None; `processKadList` resets the status and the Kad buddy search re-arms.
- Hold: LowID sources added by the same answer are held (`heldForBuddyClaim`);
  `PartFile::process` skips them. Whenever the claim ends (accept or fail) they are released
  and asked at once (`askForDownload`). LowID->LowID does not use *our* buddy, but they learn
  it in the first hello, so a failed dial can come back to us through S later.

### S (open, sent the offer)

`processBuddyClaim` → `ClientList::acceptBuddyClaim`. After a completed hello only. Gates
shared with `incomingBuddy` (`buddyServeRefusal`: slot free, not ourselves, no firewall check
with it, one served buddy per IP), plus the offer's gates again (`buddyOfferPort`), Kad state
None, Kad version known, buddy ID not served yet. Accepted: served, buddy ID set (relayable),
straight to `ConnectedBuddy`, ACCEPT.

### Why `incomingBuddy` refuses known clients

From MFC (`ClientList.cpp:721`, "It could cause conflicts"), still right for the UDP path:

1. `FINDBUDDY_REQ` is one unverified UDP packet. Taking it for a known client would let anyone
   who knows an IP:port overwrite that client's user hash and make it a served buddy.
2. IncomingBuddy only turns ConnectedBuddy in `connectionEstablished()`, which an open link
   never runs again; it would expire after 5 min.
3. One Kad state per client: it may be in a firewall check or our own buddy candidate.

The TCP claim avoids all three: TCP proved the IP, the hello the user hash, and the state is
switched in place on the live link. So `incomingBuddy` stays as it is.

### Caveat

S is not near ~KadID(R) (got3nks, #1809), so a `FINDSOURCE` on R's buddy ID never finds S.
Fine while the buddy endpoint travels with the ID (Kad record, hello, ExtSX); only the
endpoint-less fallback misses.

### Also open

- Our own hello tag with R's ~KadID (`ToByteArray` order), so peers that meet R can relay
  R's buddy route; 0xfc/0xfd alone carry no ID. Not 0xBF.
- Filter-bit request (LowID with buddy only, SX2 options word, new `CT_MOD_MISCOPTIONS`
  bit; bits 6-9 stay free for eMuleAI, 10 = HTTP cache, propose 11).
- Bloom filter of known sources with a per-request seed, later.

---

¹ Alternative, not built: R sends the stock `KADEMLIA_FINDBUDDY_REQ` to S's offered Kad port;
S answers via `incomingBuddy`, R dials or reuses the link through `requestBuddy`. No new
opcode, but needs `incomingBuddy` to adopt a known client (refused today, see above) and a
UDP round trip that a blocked Kad UDP loses.
