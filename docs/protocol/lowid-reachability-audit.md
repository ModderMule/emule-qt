# LowID reachability audit

Audit of how eMuleQt reaches a firewalled (LowID) peer, against the MFC reference in
`srchybrid/`. Written 2026-08-22, after the pass that ported MFC's `GetScore` guards, the
friend-slot link and the Kad state machine.

## The invariant

A LowID peer has no reachable TCP port. It can only be reached by asking it to connect to
*us*, over one of three routes:

1. **Server callback** — `OP_CALLBACKREQUEST` to the server *both* of us are connected to.
   Only works when we have a High ID there, and only for a peer on that same server.
2. **Kad callback** — `KADEMLIA_CALLBACK_REQ` to the peer's buddy, which relays `OP_CALLBACK`
   down its open TCP link to the peer.
3. **Direct UDP callback** — `OP_DIRECTCALLBACKREQ` straight to the peer's UDP port, for a
   peer whose UDP is open even though its TCP is not.

Everything else is a direct TCP dial, which a LowID peer cannot accept.

## Where the invariant is enforced

`UpDownClient::connect()` is the **only** thing in the codebase that creates an outbound
`ClientReqSocket`, and it has exactly one caller: the direct-TCP branch of
`UpDownClient::tryToConnect()`. That branch is guarded by

```cpp
!hasLowID() || m_connectAddress.isIPv6()
            || m_kadState == KadState::QueuedFwCheck
            || m_kadState == KadState::QueuedFwCheckUDP
```

so the only LowID clients that ever reach a direct dial are:

- **IPv6-reachable peers.** An IPv6-only source carries `kNoIPv4SourceId`
  (`transfer/DownloadQueue.h`), a placeholder chosen so `isLowID()` is true — but the peer is
  dialable over v6. A port extension, not in MFC, which has no IPv6.
- **Kad firewall-check probes.** These are built with the target's IP and are the test itself:
  we dial the port under test to find out whether it is open. MFC allows
  `KS_CONNECTING_FWCHECK` here for the same reason; the port also allows the UDP variant.

`isLowID()` (`utils/OtherFunctions.h`) is `id < 0x01000000` and is never open-coded anywhere —
every site goes through it or through `UpDownClient::hasLowID()`.

## Four spellings of "reachable", and why they differ

| site | test | asks |
|---|---|---|
| `UpDownClient::isReachableForSlot()` | High ID, or IPv6, or a live socket | can we hand this peer an upload slot *right now*? |
| `UpDownClient::tryToConnect()` | the guard above | can we dial it, or must we fall back to a callback? |
| `QueuedClientRecord::isRestorable()` | has a server or a Kad route | is this persisted queue entry worth reloading? |
| `AppContext::canDoCallback()` | High ID on ed2k, or Kad open, and not the same server | is a *server relay* usable at all? |

`isReachableForSlot()` is the one MFC uses in two places
(`srchybrid/UploadQueue.cpp:131`, `UploadClient.cpp:205`); the port now shares one
implementation between `UploadQueue::findBestClientInQueue()` and `score()`'s `sysValue`
guard. The other three answer genuinely different questions and stay separate.

## The failure mode this port had

The invariant was never *violated* — the port did not dial LowID peers it could not reach.
Every defect ran the other way: it refused to dial peers it could.

Five sites built Kad and callback clients as
`new UpDownClient(tcpPort, 0 /* userId */, 0, 0, nullptr)` and then set the address with
`setConnectAddress()`. `userId = 0` leaves `m_userIDHybrid` at 0, so `hasLowID()` reports true
for a peer with a perfectly routable IP. MFC passes the peer's IP in that slot at every one of
them. Consequences:

- **Inbound `OP_DIRECTCALLBACKREQ` was never answered over IPv4.** The requester read as
  LowID, so `tryToConnect()` skipped the direct-TCP branch, then found no callback route
  either — the one thing the request asks us to do was silently not done. IPv6 senders worked,
  via the `isIPv6()` bypass.
- **An outgoing Kad buddy could never be dialled.** `KadState::QueuedBuddy` is not one of the
  bypasses, so `ClientList::requestBuddy()` always failed and `m_buddyStatus` stuck at
  `Connecting`. Route 2 above was dead outbound.
- The two firewall-check paths survived only because their states are on the bypass list.

Fixed by passing the peer's IP, matching MFC. The bypasses stay (MFC has them too) but are no
longer load-bearing.

## Other divergences closed in the 2026-08-22 pass

- `theApp.canDoCallback()` (MFC `Emule.cpp:1187`) did not exist. Neither did the
  `DownloadState::LowToLowIP` state's only writer — the enum value was declared, stringified
  and sorted on in the GUI, and never assigned. Both are now wired, with MFC's revival path in
  `PartFile::process()`.
- `askForDownload()` gained MFC's "it is already on our upload queue, let it re-ask us"
  delay, its `TooManyConns` state, and the re-ask throttle that used to sit inside
  `tryToConnect()` — where it silently refused upload-slot dials, chat and every Kad path.
- `tryToConnect()` gained MFC's `bNoCallbacks` parameter and now honours `ignoreMaxCon`,
  which it previously accepted and `Q_UNUSED`'d.
- `UploadQueue::addUpNextClient()` opened a slot even when the dial failed. It now declines,
  and `addClientToQueue()` falls back to the waiting list rather than losing the peer.
- `ClientList::processKadList()` did not exist: every `KadState` was set by the Kad handlers
  and then never advanced. With it, `QueuedBuddy → ConnectingBuddy → ConnectedBuddy` runs,
  buddy loss is detected, an open (non-firewalled) buddy is dropped, and the `OP_BUDDYPING`
  keep-alive that `sendBuddyPingPong()` was written for is finally sent.

## Closed in the 2026-08-23 pass (the former "Still open" list)

### `checkAndAddSource()` did not vet a High-ID source's address

MFC applies `IsGoodIP` to a source *only* when it has a High ID
(`srchybrid/DownloadQueue.cpp:568-575`), because a Low ID's `userIDHybrid` is an ID and not
an address — testing it would reject every firewalled source, and this port's IPv6-only
marker `kNoIPv4SourceId` is deliberately a Low ID for the same reason. Three of the port's
ingresses did their own check (`addServerSourceClient`, `PartFile::addSources`,
`addLinkUrlSource`); the Kad path (`addKadSourceResult`) and the source-exchange revival did
not, so a peer could hand us `0.x`, multicast, broadcast, loopback or LAN addresses as
download sources. The guard now lives in `checkAndAddSource()` itself, where every ingress
passes through it. LAN stays admissible under the lab-rig switch, since `isGoodIP()` honours
`thePrefs.filterLANIPs()`.

### No LowID client counter in statistics

MFC counts it in the single `CClientList::GetStatistics` walk (`ClientList.cpp:78`,
`stats[14]`). The port now counts it in its one equivalent walk,
`collectClientSoftwareStats()`, and the Statistics panel shows `Low ID: N (x%)` in MFC's
`cligen[4]` slot — after the Client Software group, before Banned.

### One flag was doing the work of MFC's two

`UpDownClient` had a single `m_reaskPending` standing in for both `m_bUDPPending` (a UDP
re-ask datagram is in flight) and `m_bReaskPending` (we owe this peer a TCP re-ask that a
LowID rule made us delay). `m_udpPending` was declared and never written. The conflation was
live: `askForDownload()` sets the delay flag on its two LowID paths, and
`udpReaskForDownload()` then early-returned on that same flag — a LowID source with a buddy
stopped getting UDP re-asks until it happened to connect. The two are now separate, with
`udpPacketPending()` alongside `reaskPending()`, and the three UDP response handlers in
`CoreSession` gate on the flag their packets actually answer.

`DownloadQueue` gained MFC's `m_nUDPFileReasks` / `m_nFailedUDPFileReasks`
(`DownloadQueue.h:114-117`), counted at both send sites and charged at the head of
`askForDownload()` — falling back to TCP while a datagram is outstanding means that datagram
will never be answered (`DownloadClient.cpp:179-183`). Both readings are surfaced under
*Downloads → Session → Found Sources*, where MFC puts them.

### `askForDownload()` had no production caller

MFC calls it from `CPartFile::Process()` (`srchybrid/PartFile.cpp:2354`); the port's
`PartFile::process()` retry branch called `tryToConnect()` directly, which left the whole
re-ask preamble unreachable — the `TooManyConns` backoff, the LowID delays, the one-minute
throttle and the A4AF swap. It is now called from that branch, which also picked up
`TooManyConns` as a retryable state, since nothing else clears it.

Two things had to be fixed for that to work:

- `swapToAnotherFile()` → `doSwap()` removes the client from `m_reqFile`'s source list, so
  the index-based loop in `PartFile::process()` could step over the source that slid into
  the vacated slot. Both call sites now re-examine the slot instead of advancing. (The A4AF
  swap also means the order in which A4AF files complete is no longer the order they were
  registered in — it is MFC's best-file choice.)
- `tryToConnect()`'s already-connected branch only serviced `DownloadState::None`, while
  MFC runs the whole of `ConnectionEstablished()` there (`BaseClient.cpp:1274-1282`) and its
  download-state switch also covers `DS_CONNECTING` / `DS_WAITCALLBACK` /
  `DS_WAITCALLBACKKAD`. `askForDownload()` sets `Connecting` before calling, so a re-ask
  over a connection we already held sent nothing at all. Those three states are now handled
  inline.

### `udpReaskForDownload()` was missing most of MFC's preconditions

Only the peer's UDP port and version were checked (`supportsUDP()`). MFC also requires our
own UDP port to be configured, that we are not firewalled, and no proxy — and, the one that
mattered, **that we do not already hold a TCP socket to the peer**
(`srchybrid/DownloadClient.cpp:1350-1351`). A UDP re-ask exists to keep a queue position
warm on a peer we are *not* connected to; asking one we are drags its A4AF swap along, and
since nothing stamps the re-ask clock on the swap path, an actively downloading source was
bounced between two files once a second. All four are now enforced.

The in-flight flag is also only raised once the datagram is actually handed to the socket.
It used to be set before the destination was resolved, so a source with no usable address
latched it and the next TCP re-ask charged a failure for a packet that was never sent.

### The UDP and TCP re-ask windows overlapped

`DownloadQueue::process()` fired a UDP re-ask at `timeUntilReask(file) == 0` — the same
instant `PartFile::process()` uses for the TCP one. MFC keeps them disjoint
(`srchybrid/PartFile.cpp:2337-2339`): the UDP re-ask is for a `DS_ONQUEUE` source in the
last two minutes before the TCP re-ask falls due (never in the final second), and only when
we have not tried to dial it in the last 20 minutes — *"Allow up to 1 min for UDP to
respond. If we are within one min of TCP re-ask, do not try."*

Sharing the instant was harmless while nothing counted, but with the accounting in place it
is self-poisoning: the same pass sends a datagram and then charges it as failed at the head
of `askForDownload()`, which drives `m_failedUDPPackets` past `udpReaskForDownload()`'s own
30 % abort and disables UDP re-asks for that peer permanently. MFC's window is now ported.

## Deliberate divergences kept

- **The IPv6 bypass** in the direct-TCP guard and in `isReachableForSlot()`. MFC has no IPv6.
- **`canDoCallback()` gates only the server route.** MFC applies it to every LowID client
  (`BaseClient.cpp:1341`), which would disable direct-UDP callbacks whenever Kad happens to be
  down — the situation they exist for. Its two arms are both about the server relay.
- **`tryToConnect()`'s return value** means "did we start something". MFC's means "the client
  was not deleted"; MFC deletes on `Disconnected()` and this port never does, leaving that to
  the `ClientList` reaper.
- **`KadState::QueuedFwCheckUDP`** is on the direct-TCP bypass list; MFC lists only
  `KS_CONNECTING_FWCHECK`.
- MFC's `FindSource` fallback for a buddy whose IP we do not know
  (`BaseClient.cpp:1450-1465`) is not ported. It is download-only, and the port already
  guards the null-`m_reqFile` dereference MFC crashes on there.

## Still open

Nothing from this audit. MFC's `FindSource` fallback (listed under *Deliberate divergences
kept*) remains unported on purpose.
