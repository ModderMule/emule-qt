# Kad: echo the KADEMLIA2_RES sender key on publishes

Status: eMuleQt sends it (2026-11-02). Receivers only log the key for now, on every
request that trusts the source IP (2026-11-03, see below); refusing unkeyed
`KADEMLIA2_PUBLISH_SOURCE_REQ` comes later, once most publishers send it.
Applies to any Kad 2 client (eMule 0.49a+, aMule, mods). No new opcode, tag or packet
field. Older clients keep working.

## Why

A source publish says "file X is at IP Y". Plain Kad UDP has no source-IP proof, so a
spoofed `PUBLISH_SOURCE_REQ` can plant a victim's IP as a source (security audit 9c).
Kad already has the proof: the UDP verify key in the encrypted header. A receiver that
sees its own key for the sender's IP echoed back knows the sender got packets at that IP.

The receiver can't demand that key today: stock clients publish with key 0 to almost
every target. Publish targets come from `KADEMLIA2_RES` lookups, and stock eMule throws
away the key that each RES carries (`KademliaUDPListener.cpp:733`,
`const CKadUDPKey& /*senderUDPKey*/`). Contacts learned from a RES therefore start with
an empty key, and every publish to them goes out with receiver key 0.

## The keys (recap)

Encrypted Kad UDP (`EncryptedDatagramSocket`) carries two 4-byte keys after the padding:

```
<marker 1><randomKeyPart 2> | RC4: <magic 4><padLen 1><pad n><receiverKey 4><senderKey 4><payload>
```

- **senderKey**: our key for *this* destination IP:
  `VerifyKey(ip) = fold32(MD5(uint64(kadUDPSecret) << 32 | ip)) % 0xFFFFFFFE + 1`.
  Only someone who receives packets at `ip` learns it. Sent on every Kad packet.
- **receiverKey**: the peer's senderKey for us, echoed back. On receive,
  `valid = receiverKey != 0 && receiverKey == VerifyKey(senderIP)`.
- The stored peer key is bound to our public IP when we learned it (`CKadUDPKey{key, ip}`).
  `GetKeyValue(myIP)` returns 0 once our IP changes, so a stale key reads as "no key".
- RC4 keying uses the target's KadID when it is known, and only falls back to the
  receiver key without one. A wrong receiver key never breaks delivery.

## Sender side (the change to make)

Every encrypted `KADEMLIA2_RES` already brings the responder's senderKey. Keep it on the
search's contact for that responder:

1. Pass the RES `senderKey` from `Process_KADEMLIA2_RES` into
   `CSearchManager::ProcessResponse` → `CSearch::ProcessResponse`.
2. In `CSearch::ProcessResponse`, after the responder is found in the tried map
   (`pFromContact`):

   ```cpp
   // CKadUDPKey has no IP getter in MFC; add one (m_dwIP)
   if (pFromContact && !senderKey.IsEmpty()
       && pFromContact->GetUDPKey().GetKeyValue(senderKey.GetIP()) == 0
       && std::find(m_listDelete.begin(), m_listDelete.end(), pFromContact)
              != m_listDelete.end())
       pFromContact->SetUDPKey(senderKey);
   ```

3. Nothing else changes. `CSearch::StorePacket` already sends every action packet
   (`PUBLISH_SOURCE/KEY/NOTES_REQ`, `SEARCH_*_REQ`) with `pFromContact->GetUDPKey()`
   (`Search.cpp:509-750`).

eMuleQt: `KadUDPListener::process_KADEMLIA2_RES` → `SearchManager::processResponse`
→ `Search::processResponse` (`src/core/kademlia/KadSearch.cpp`).

Rules:
- **Only key contacts the search owns** (`m_listDelete`: the copies made from RES
  answers). A search starts from routing-table contacts it only borrows (MFC
  `GetClosestTo`, shared and pinned with `IncUse`). A routing contact with a key refuses
  any update without that key (`RoutingZone.cpp` Add), so a spoofed RES that set a
  wrong key there would lock out the real node. Borrowed contacts keep their HELLO key,
  or publish unkeyed without one. Costs little: publish targets are the closest nodes,
  which nearly always come from RES answers. (aMule PR #2317 found this.)
- **Only fill a missing key.** The first answer wins. The `GetKeyValue(ip) == 0` test
  also replaces a stale key from before our IP changed.
- **Don't trust it for anything else.** The key only gets echoed back; it never marks
  the contact verified on our side. A forged or wrong key just reads as invalid at the
  receiver, the same as key 0 today.
- Unencrypted RES (Kad < 6, or the responder didn't encrypt) carries no key. The
  contact stays at key 0, the same as before.
- `senderKey.GetIP()` is our public IP when the RES arrived (`CKadUDPKey(key, GetPublicIP())`).
  If our IP is still unknown (0), `IsEmpty()` is true and nothing is stored.

## Receiver side (now: measure; later: refuse)

The dispatcher already computes `validReceiverKey`. Requests that act on the source IP
log it. Nothing is refused yet.

### Which requests trust the source IP (checked 2026-11-03)

| Request | What a forged source IP does | Sender has a key? |
|---|---|---|
| `PUBLISH_SOURCE_REQ` | stores the IP as a source for the file (victim flooded by downloaders, real sources pushed out) | yes, RES contact (this change) |
| `PUBLISH_KEY_REQ` | the IP is the publisher's vote: each new IP raises the entry's trust value, file name popularity and AICH hash vote; per-IP flood limit (`shouldAcceptPublish`) is bypassed. A victim's IP refreshes that publisher's AICH vote | yes, RES contact |
| `PUBLISH_NOTES_REQ` | notes collapse per IP (`kNotePolicy`): a forged IP replaces that host's note; many IPs beat the one-note-per-host cap | yes, RES contact |
| `FIREWALLED_REQ`, `FIREWALLED2_REQ` | we dial TCP to ip:tcpPort (port in the packet) and send `FIREWALLED_RES` there: a TCP connect to any third party | yes: sent from the `HELLO_RES` handler with the key that packet just brought |
| `FINDBUDDY_REQ` | a pending buddy claim for the claimed IP; claims are capped, oldest goes first, so forged claims evict real ones; `FINDBUDDY_RES` goes to the victim | yes, RES contact |
| `CALLBACK_REQ` | the IP is relayed as `OP_CALLBACK` target: our firewalled buddy dials it (rate-limited per target, `vetPeerAddress`) | **only on the Kad search path.** The usual path (`UpDownClient::sendKadCallback`, MFC the same) sends unencrypted: the buddy IP came from a source answer, no Kad packet was ever exchanged with it |

Not logged:
- `HELLO_REQ`, `BOOTSTRAP_REQ`, first `KADEMLIA2_REQ` to a new contact: no key can exist yet.
- `SEARCH_KEY/SOURCE/NOTES_REQ`, `KADEMLIA2_REQ`, `PING`: no stored state, just an answer to the
  claimed IP (reflection; the answer can be larger than the request). The flood limiter caps it.
- Answers (`*_RES`, `PONG`, `FIREWALLED_ACK_RES`, `FIREWALLUDP`): gated by the out-track list
  or the port check, not by the key.

### Log

eMuleQt, Kad log (verbose Kad logging on):
```
Kad: <REQUEST> from <ip>:<port> — receiver key valid|missing|wrong|unencrypted
```
- `valid`: our key for that IP came back. The IP is proven.
- `missing`: encrypted, receiver key 0. The sender has no key for us: stock client
  publishing to a RES-learned node, or a spoofer.
- `wrong`: a non-zero key that isn't ours for that IP. The sender's IP changed since it
  learned the key, or it was guessed.
- `unencrypted`: no keys at all (Kad < 6, plain packet). Also shown in the rare case our
  own public IP is still unknown.

aMule (#2328) logs valid/not valid only, so it can't tell `missing` from `wrong`. The
socket passes `receiverKeySent` (receiver key != 0) along to make that split.

### Later: refuse

ToDo in eMuleQt, `process_KADEMLIA2_PUBLISH_SOURCE_REQ`: when `!validReceiverKey` and not
LAN mode, drop the packet without a `PUBLISH_RES`. The publisher then counts the node as
unanswered and publishes to the next one. Only flip this once the "valid" share is high
on the live network. Before that it drops honest publishes from stock clients.

Same rule can follow for keyword/notes publishes, `FIREWALLED*_REQ` and `FINDBUDDY_REQ`,
each by its own share. `CALLBACK_REQ` can't be refused this way: honest callbacks are
mostly unencrypted, so that would need the buddy's key in the source answer first.

## Compatibility

| Peer | Effect |
|---|---|
| Stock / aMule / mods without the change | publish with key 0, as today; accepted while the receiver only logs |
| Receiver without the change | ignores the receiver key on publishes, as today |
| Kad < 6 (no encryption) | no keys either way; stays unkeyed |
| Borrowed routing contact without a HELLO key | publishes unkeyed, as today |
| Our IP changed since the lookup | stored key reads 0 → unkeyed publish; the next lookup (publishes re-run on a timer anyway) learns a fresh key |

## Testing

- Unit (eMuleQt `tests/tst_KadSearch.cpp`):
  - `processResponse_keepsResponderKeyForActionPackets`: the RES key reaches the
    packets sent to that responder.
  - `processResponse_doesNotReplaceAKnownKey`: the first key stays; a later RES key
    doesn't replace it.
  - `processResponse_doesNotKeyARoutingTableContact`: a borrowed contact gets no key
    from a RES; packets to it stay unkeyed.
  - `ipTrustingRequest_logsReceiverKeyState` (`tst_KadUDPListener.cpp`): the four log
    states.
- Docker rig (`docker/kad/kadnet.py --nat-test`, 20 nodes, 2026-11-02): 1006 source
  publishes received, all `receiver key valid`, 0 `missing`.
  Re-run with the owned-contacts rule (6 min): 58 received, all valid, 0 missing.
- aMule (PR #2317, same rules) measured on live Kad: keyed source publishes went from
  6 of 237 to 234 of 234.
- Live: `grep 'receiver key' | sed 's/ from .*key / /' | sort | uniq -c` on the Kad log
  gives the share per request and state. That ratio tells you when the refusal can be
  switched on.

Related: `ipv6-spec.md` (Kad `ip6` tag on the same publish), `kad-buddy-search.md`.
