# Kad: echo the KADEMLIA2_RES sender key on publishes

Status: eMuleQt sends it (2026-11-02). Receivers only log the key for now; refusing
unkeyed `KADEMLIA2_PUBLISH_SOURCE_REQ` comes later, once most publishers send it.
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
       && pFromContact->GetUDPKey().GetKeyValue(senderKey.GetIP()) == 0)
       pFromContact->SetUDPKey(senderKey);
   ```

3. Nothing else changes. `CSearch::StorePacket` already sends every action packet
   (`PUBLISH_SOURCE/KEY/NOTES_REQ`, `SEARCH_*_REQ`) with `pFromContact->GetUDPKey()`
   (`Search.cpp:509-750`).

eMuleQt: `KadUDPListener::process_KADEMLIA2_RES` → `SearchManager::processResponse`
→ `Search::processResponse` (`src/core/kademlia/KadSearch.cpp`).

Rules:
- **Only fill a missing key.** Some search contacts are the routing table's own objects
  (MFC `GetClosestTo`, shared and pinned with `IncUse`). A key from HELLO is the routing
  key; don't overwrite it. The `GetKeyValue(ip) == 0` test also replaces a stale key
  from before our IP changed.
- **Don't trust it for anything else.** The key only gets echoed back; it never marks
  the contact verified on our side. A forged or wrong key just reads as invalid at the
  receiver, the same as key 0 today.
- Unencrypted RES (Kad < 6, or the responder didn't encrypt) carries no key. The
  contact stays at key 0, the same as before.
- `senderKey.GetIP()` is our public IP when the RES arrived (`CKadUDPKey(key, GetPublicIP())`).
  If our IP is still unknown (0), `IsEmpty()` is true and nothing is stored.

## Receiver side (now: measure; later: refuse)

The dispatcher already computes `validReceiverKey`. Pass it to
`Process_KADEMLIA2_PUBLISH_SOURCE_REQ`.

Now: log it. eMuleQt writes to the Kad log:
```
Kad: PUBLISH_SOURCE_REQ from <ip>:<port> — receiver key valid|missing
```

Later (ToDo in eMuleQt, `process_KADEMLIA2_PUBLISH_SOURCE_REQ`): when `!validReceiverKey`
and not LAN mode, drop the packet without a `PUBLISH_RES`. The publisher then counts the
node as unanswered and publishes to the next one. Only flip this once the "valid" share
is high on the live network. Before that it drops honest publishes from stock clients.

Keyword and notes publishes carry no IP claim, so they stay as they are.

## Compatibility

| Peer | Effect |
|---|---|
| Stock / aMule / mods without the change | publish with key 0, as today; accepted while the receiver only logs |
| Receiver without the change | ignores the receiver key on publishes, as today |
| Kad < 6 (no encryption) | no keys either way; stays unkeyed |
| Our IP changed since the lookup | stored key reads 0 → unkeyed publish; the next lookup (publishes re-run on a timer anyway) learns a fresh key |

## Testing

- Unit (eMuleQt `tests/tst_KadSearch.cpp`):
  - `processResponse_keepsResponderKeyForActionPackets`: the RES key reaches the
    packets sent to that responder.
  - `processResponse_doesNotReplaceAKnownKey`: a HELLO key survives a RES key.
- Docker rig (`docker/kad/kadnet.py --nat-test`, 20 nodes, 2026-11-02): 1006 source
  publishes received, all `receiver key valid`, 0 `missing`.
- Live: grep the Kad log for `receiver key` and count valid vs missing. That ratio
  tells you when the refusal can be switched on.

Related: `ipv6-spec.md` (Kad `ip6` tag on the same publish), `kad-buddy-search.md`.
