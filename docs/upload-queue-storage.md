# Upload Queue Storage (UQS)

Remembers the clients waiting in our upload queue and puts them back — with the places they
had earned — when eMule restarts.

The upload-side counterpart to [Save/Load Sources](save-load-sources.local.md). Unlike SLS
there is nothing to stay compatible with: neither official eMule nor MorphXT has ever
persisted the upload queue (`waitinglist` appears in three files in `srchybrid/` and none of
them writes it to disk), so the format below is ours alone.

Controlled by the `rememberUploadQueue` preference (default on) — *Options → Extended →
"Remember the upload queue between restarts"*.

## Why it needs its own file

A peer's queue position is `curTick - waitStartTime`, and `waitStartTime` lives in
`ClientCredits::m_secureWaitTime`. `clients.met` stores only hash/up/down/lastSeen/pubkey, so
the wait time is **memory-only**: without UQS a peer that has waited two hours restarts from
zero even if it reconnects one second after we do.

## Files

| Path | Contents |
| --- | --- |
| `src/core/transfer/UploadQueueStore.h/.cpp` | `QueuedClientRecord`, the `UploadQueueFile` codec, the `UploadQueueStore` driver |
| `src/core/net/PeerVetting.h/.cpp` | `vetPeerAddress()`, shared with `DownloadQueue` |
| `tests/tst_UploadQueueStore.cpp` | 23 tests |

One global file at `<ConfigDir>/uploadqueue.met`. `UploadQueueStore` is held by value in
`UploadQueue`, the way `SourceSaver` is held by value in `PartFile`.

## Format

Binary, little-endian, written through `SafeFile`. Atomic: tmp → rotate old to `.bak` →
rename, the same dance `KnownFileList::save()` does.

```
Header:  uint8  version (1) | uint32 savedAtUnix | uint16 recordCount
Record:  hash16 userHash | hash16 reqUpFileId
         uint32 userIDHybrid | uint32 userIPv4 (net order) | 16 bytes userIPv6
         uint16 userPort | uint32 serverIP | uint16 serverPort
         uint16 kadPort  | uint16 udpPort  | uint8  connectOptions
         uint8  kadVersion | uint8 udpVer
         uint32 waitedSeconds | uint32 sinceLastRequestSeconds | uint32 askedCount
         string userName | uint32 clientVersion | uint8 emuleVersion | uint8 compatibleClient
```

The version byte is the entire forward-compatibility story — there is no other reader to
satisfy. An unknown version, a truncated file or a corrupt record yields an **empty** read,
never a partial queue.

### Times are durations, never ticks

`waitStartTime` and `lastUpRequest` are `steady_clock` milliseconds truncated to 32 bits:
process-relative values that mean nothing after a restart. Both are therefore stored as
**elapsed seconds at save time** and rebased onto the live clock on load.
`ClientCredits::restoreWaitStartTime()` exists solely for that rebase — every other caller
must keep using `setSecWaitStartTime()`, which always starts the clock now.

## Policy

| Setting | Value |
| --- | --- |
| max records | 100, highest `score()` first |
| expiry | 1 h |
| resave interval | 10 min |
| load | exactly once, only after ED2K **or** Kad is up |

`theApp.isConnected()` is already "an ED2K server or Kad", so the gate is that call and
nothing else — no new signal wiring.

### Expiry reuses the purge rule

`findBestClientInQueue()` already drops any waiter whose `lastUpRequest()` is older than
`MAX_PURGEQUEUETIME` (1 h). UQS restores `lastUpRequest` to its true age, so expiry is
enforced by machinery that already exists: a record too old is skipped at load, and one that
goes stale afterwards is purged at the next slot decision. The header stamp is a cheap
whole-file early-out.

The offline gap counts towards `lastUpRequest` — that really is "time since we last heard
from them" — but **not** towards the wait time, since a peer was not queued while we were
down. Relative order among restored clients is preserved either way.

### What gets saved

A waiter is eligible when it has a valid user hash, its requested file is still shared, its
port is non-zero, it is not banned, and it has some endpoint to match or dial (a direct
address, or a LowID with a server or Kad callback route). The hash is not optional: without
it there is no credits key, no dedup key and no obfuscation key.

## Lifecycle

| Hook | Where |
| --- | --- |
| tick | `CoreSession::onTimer()`'s 1 s branch → `UploadQueue::processStore()`, after `sharedFileList->process()` |
| one-shot load | first tick where `theApp.isConnected()` is true |
| autosave | 10 min after the load, then every 10 min |
| forced save | `CoreSession::~CoreSession()`, **immediately after `stop()`** |

### The shutdown ordering trap

The natural home for the forced save looks like `shutdownUploadPipeline()`, where SLS's twin
sits. It is wrong. `shutdownClientInfra()` runs *earlier* in the destructor list and does
`m_clientCredits.reset()`, destroying every `ClientCredits` object the waiting clients read
their wait times from. Saving there is a use-after-free. The save therefore runs before any
`shutdownXxx()` at all.

### Saving is refused before the load

Until the one-shot load has run, both the autosave and the forced save refuse outright.
Otherwise a daemon started offline — or killed before it ever connected — would write its
empty queue over a perfectly good file and destroy exactly the data it was about to restore.
A missing file still counts as loaded, so a fresh install starts saving normally.

## Restored clients are not dialled

`addRestoredClient()` appends to the waiting list and stops there. It deliberately avoids
`addClientToQueue()`'s tail, which promotes straight to an upload slot when the waiting list
is empty and a slot is free — on a bulk restore that would dial one peer per free slot the
instant we come online. It also skips `incAskedCount()`/`addRequestCount()` (a restore is not
a request and must not feed the flood counter) and `sendRankingInfo()` (no socket yet).

Both entry points share `checkWaitingListAdmission()` — the ban, duplicate, per-IP and IPv6
gates — so a persisted record cannot smuggle a banned or duplicate peer past rules a live
request would have failed.

Two ordering requirements on the load path:

- `setUploadFileID()` is **mandatory**. Without it `score()` returns 0 *and* the queue's
  `noFile` purge fires on the first slot decision.
- The client must be registered with `ClientList` **and** put on the queue in the same tick,
  or `ClientList::process()` reaps it a second later.

Credits and friend links are re-derived from the user hash, never persisted.

## Protocol obfuscation

Restored peers dial out obfuscated exactly as they would have before the restart. The
decision is one expression in `tryToConnect()`:

```cpp
if (hasValidHash() && supportsCryptLayer() && thePrefs.cryptLayerSupported()
    && (requestsCryptLayer() || thePrefs.cryptLayerRequested()))
    reqSocket->setConnectionEncryption(true, userHash(), false);
```

and the RC4 keys are `MD5(peerUserHash ‖ magic ‖ nonce)`, one per direction. The peer's user
hash *is* the key seed, which is why a hashless record is never stored.

Two traps the implementation has to honour, both pinned by
`obfuscation_cryptFlagsAndKadVersionSurvive`:

- **`setConnectOptions(byte, encryption, callback)` ANDs each stored bit with its bool
  arguments.** The restore passes `(byte, true, true)`; anything else silently discards the
  crypt flags and the direct-UDP-callback bit.
- **`kadVersion` must be restored.** `shouldReceiveCryptUDPPackets()` is
  `supportsCryptLayer && kadVersion >= KADEMLIA_VERSION8_49b`, and it gates obfuscation on
  both the direct-UDP-callback packet and the `OP_REASKACK` reply. Left at 0 we would send
  plaintext UDP to a peer expecting obfuscation — silently costing us exactly the firewalled
  peers the callback path exists for.

Inbound needs nothing: obfuscated UDP from a peer is decrypted with **our** hash, and an
inbound TCP connection is merged onto the restored object by
`ClientList::attachToAlreadyKnown()` before crypt state matters. That merge is also what lets
a returning peer keep its restored position instead of being rejected as a duplicate.

There is no "obfuscation failed, retry plaintext" fallback for peers anywhere in the codebase
(only for servers). A peer that disables obfuscation within the ≤1 h a record is valid fails
one connect and is then purged by the normal stale rule.

## Notes

- **Privacy**: peer IPs, hashes and usernames are stored in plaintext in the config dir, the
  same exposure `clients.met` already has. Turning the preference off never deletes the file.
- **Untrusted input**: every address is re-vetted on load through `vetPeerAddress()` — routable,
  not IP-filtered, not banned. The file may predate an IP filter update.
- **Still-hashing shares**: a record whose file has not finished hashing when the load fires is
  skipped. In practice the connectivity gate costs enough seconds for this to be rare, and the
  next run picks the peer up again.

## Known limitations

**Clients holding an upload slot are not stored.** `collectRecords()` walks the *waiting* list
only, matching the feature's scope. A peer that is mid-upload when we shut down is therefore
not persisted and returns to the back of the queue. In practice that is at most a slot's worth
of peers against a queue of hundreds, and they re-ask immediately anyway — but it is a real
gap, and including `forEachUploading()` would close it.

Note this also makes a short test run look empty: restore a handful of peers onto an idle
queue and the slot logic promotes all of them within a second, so the next shutdown save
writes zero records.

## Out of scope

MFC's soft/hard queue cap (`srchybrid/UploadQueue.cpp:638-655`) was never ported —
`thePrefs.queueSize()` exists but `UploadQueue` never reads it, so the waiting list is
unbounded. Restoring at most 100 entries does not make that worse, but it remains a real gap.
