# Kad buddy search: why it rarely succeeds, and what eMuleQt does differently

A firewalled Kad node needs a *buddy*: an open node that relays callback requests to it.
On today's network a buddy is very hard to find. This note records the measurements behind
that statement and the two changes eMuleQt makes to the search.

## How the search works

1. The firewalled node runs a Kad lookup (`SearchType::FindBuddy`) and sends
   `KADEMLIA_FINDBUDDY_REQ` (0x51) to the nodes that answer the lookup. The packet carries
   a BuddyID (`~KadID`), the client hash and the TCP port.
2. A node offers itself with `KADEMLIA_FINDBUDDY_RES` (0x5A) only if all of these hold
   (`KademliaUDPListener.cpp` in stock eMule, `process_KADEMLIA_FINDBUDDY_REQ` here):
   - it is not TCP-firewalled;
   - its UDP firewall test finished and came out open;
   - it has no connected buddy. A node serves one buddy at a time;
   - it does not already know the asking client.
   Otherwise it stays silent. There is no "no" answer.
3. The asker checks that the reply echoes its BuddyID (`echo ^ ~0 == KadID`) and opens a
   TCP connection to the offering node.

Stock eMule always walks to the target `~KadID`, asks about ten nodes there, and repeats
every 20 minutes. The neighbourhood of `~KadID` hardly changes between two searches, so
every retry asks nearly the same nodes.

## Measurements

Both runs: 2026-10-10, live Kad network, a separate eMuleQt test instance on random ports
(firewalled), FindBuddy searches to random targets. Each node counts once.

### Run 1 — offer rate

| | |
|---|---|
| Searches | 60 |
| Distinct nodes asked | 299 (v7: 2, v8: 152, v9: 86, v10: 59) |
| Buddy offers received | 2 (0.7 %) |

The BuddyID was random in this run, so the offers were only counted, not followed up.

### Run 2 — offer rate and firewalled state of the nodes asked

Every node that got a buddy request also got a `KADEMLIA2_HELLO_REQ`. Its answer says
whether the node considers itself firewalled (`TAG_KADMISCOPTIONS`: bit 0 UDP, bit 1 TCP;
a v8+ node omits the tag when both are clear).

| Kad version | asked | answered HELLO | TCP-firewalled | UDP-firewalled | open | offers |
|---|---|---|---|---|---|---|
| v7 | 2 | 2 | 0 | 0 | 2 | 0 |
| v8 | 546 | 539 | 43 | 0 | 496 | 0 |
| v9 | 281 | 277 | 40 | 2 | 235 | 0 |
| v10 | 171 | 169 | 13 | 0 | 156 | 1 |
| **all** | **1,000** | **987** | **96** | **2** | **889** | **1** |

121 searches in 17 minutes, about 8 requests per search (each search was stopped after 8 s).
The one offer came from a node that reported itself open. Whether the TCP connection to it
would have succeeded was not measured.

### Both runs together

| | asked | offers | rate |
|---|---|---|---|
| Run 1 | 299 | 2 | 0.67 % |
| Run 2 | 1,000 | 1 | 0.10 % |
| Total | 1,299 | 3 | 0.23 % |

With three events the true rate is uncertain: roughly 0.05 % to 0.7 % (95 % interval).

## What the numbers show

- **A free buddy slot is rare.** About 1 request in 400 is answered.
- **Being firewalled does not explain the silence.** 889 of the 987 nodes that answered
  (90 %) report themselves open, and 888 of those made no offer.
- **Why an open node stays silent** cannot be read off the wire. Three causes remain:
  1. it already has a buddy;
  2. its UDP firewall test has not produced a verified "open" result. Stock eMule then
     refuses to act as a buddy although it reports "not firewalled" in the HELLO;
  3. the request or the reply was lost, or the client does not implement the buddy role.

  The probe cannot separate 1 from 2. So the statement the data supports is "over 99 % of
  open nodes have no buddy slot to give". "They all have a buddy already" is the likely main
  cause, but it is not proven by these runs.
- **Firewalled nodes are undercounted.** A lookup only reaches nodes that accept unsolicited
  UDP. Nodes that are firewalled on both TCP and UDP — the ones that need a buddy — never
  appear in a walk. Their number, and so the real demand for buddy slots, is unknown.

## Measuring the firewalled share on your own node

The probe above cannot see nodes that are firewalled on TCP and UDP. A different sample
does: every `KADEMLIA2_PUBLISH_SOURCE_REQ` a node stores says how the publisher can be
reached. eMuleQt counts them, for the session and cumulative, under
Statistics → Kademlia → Nodes → "Source Publishes Received":

| Row | Source type | Publisher |
|---|---|---|
| Open | 1, 4 | open |
| Firewalled, with Buddy | 3, 5 with a buddy address | firewalled, found a buddy |
| Firewalled, Direct Callback | 6 | TCP-firewalled, UDP open; needs no buddy |
| Firewalled, without Buddy | 3, 5 without a buddy address | eMuleQt node publishing over IPv6 |

The same method gave 44 % firewalled publishers with a buddy in 2006 (R. Brunner,
["A performance evaluation of the Kad-protocol"](https://pages.di.unipi.it/ricci/A-performance-evaluation-of-the-Kad-protocol.pdf),
Eurecom, 100,000 received sources; type 6 did not exist yet).

How to read the rows:

- They count publishes, not nodes. A node sharing several files near this node's ID counts
  once per file, and again at each republish (every 5 hours).
- A firewalled node with no buddy, no open UDP port and no public IPv6 address publishes
  nothing. It is missing, so the firewalled share is a lower bound.
- Only an open node is sent publishes. On a firewalled node the rows read "n/a".
- Only publishes that were stored count; one with a filtered or banned buddy address does not.

### First sample, October 2026

One open eMuleQt v0.6.4 node, one hour, 276 stored publishes (2026-10-10):

| | [Brunner 2006](https://pages.di.unipi.it/ricci/A-performance-evaluation-of-the-Kad-protocol.pdf) | 2026 sample |
|---|---|---|
| Open | 56 % | 63.0 % (174) |
| Firewalled, with Buddy | 44 % | 29.3 % (81) |
| Firewalled, Direct Callback | did not exist yet | 7.6 % (21) |
| Firewalled, without Buddy | — | 0 % (0) |
| Firewalled, total | 44 % | 36.9 % (102) |

- Compare the totals. A direct-callback node would have needed a buddy in 2006.
- The sample shows no degradation since 2006. It is too small to show an improvement: the
  margin is about ±6 points for 276 independent publishes, and wider here because one node
  can count several times.
- "Without Buddy" is 0 because only eMuleQt nodes publishing over IPv6 send that type.
- The 29 % with a buddy does not contradict the low reply rate of a single buddy search
  above. That rate is per attempt; a node that keeps retrying gets a buddy in the end and
  then holds it for a long time.

A few thousand publishes (a day or two, cumulative scope) bring the margin to about ±2 points.

Code: `process_KADEMLIA2_PUBLISH_SOURCE_REQ` (`KadUDPListener.cpp`), fields `sources*` of
`KadCounters` (`src/core/stats/NetworkCounters.h`). They are also in `GET /api/v1/kad/stats`.

## What this means for a search

Chance that one search gets at least one offer, at 0.23 % per request:

| Requests per search | Chance per search |
|---|---|
| 10 (stock eMule) | 2.3 % |
| 20 (eMuleQt) | 4.5 % |

Expected time to the first offer, if every search reaches fresh nodes:

| Requests | Interval | Expected wait |
|---|---|---|
| 10 | 20 min | about 14 h |
| 20 | 20 min | about 7 h |
| 20 | 10 min | about 3.5 h |

These are rough: the rate could be three times higher or four times lower, and an offer
still has to survive the TCP connect.

The stock search does not reach fresh nodes. It asks the neighbours of `~KadID` again and
again; if none of them has a slot, the wait lasts until one of them loses its buddy or the
neighbourhood changes. That is the case the random target removes.

## What eMuleQt does

1. **First search as in stock eMule**, target `~KadID`.
2. **Every further search walks to a random target** as long as there is no buddy, so each
   one asks different nodes. The BuddyID in the request stays `~KadID`: the responder only
   echoes it, and nobody checks where the lookup went. The counter resets once a buddy is
   connected or the node is no longer firewalled.
3. **Retry every 10 minutes** instead of 20.
4. **Up to 20 requests per search** instead of about 10 (`kSearchFindBuddyRequests`).

Code: `Kademlia::buddySearchTarget()` and step 6 of `Kademlia::process()`
(`src/core/kademlia/Kademlia.cpp`), FindBuddy case of `Search::storePacket()`
(`src/core/kademlia/KadSearch.cpp`).

All four are deliberate departures from stock eMule. The extra load on the network is small:
at most 20 small UDP packets every 10 minutes, and only while the node is firewalled and
without a buddy.

Even so, a buddy remains a matter of hours. A firewalled node with a public IPv6 address
therefore publishes to Kad without a buddy (see `ipv6-spec.md`).

## An open eMuleQt node serves several buddies

The search changes above only help a firewalled node look harder. They do not add buddy
slots. An open eMuleQt node adds slots: it serves up to 8 firewalled nodes instead of one.
This is a deliberate departure from stock eMule.

### Why no protocol change is needed

"One buddy per node" is a rule of the serving node's own bookkeeping (`m_pBuddy`,
`m_nBuddyStatus` in `ClientList`). It is not visible on the wire. Every packet a serving
node has to route already names the firewalled node it is meant for:

| Packet | Direction | Selector already in the packet | Stock handling (`srchybrid/`) |
|---|---|---|---|
| `KADEMLIA_FINDBUDDY_REQ` | firewalled → open | BuddyID, client hash | `KademliaUDPListener.cpp:1693` returns when a buddy is connected. Local decision |
| `KADEMLIA_CALLBACK_REQ` | third party → open | BuddyID (first 16 bytes) | `KademliaUDPListener.cpp:1773-1795` reads the ID, does not compare it (`JOHNTODO`), and forwards to the one buddy |
| `OP_REASKCALLBACKUDP` | third party → open | BuddyID (first 16 bytes) | `ClientUDPSocket.cpp:210` compares it with the one buddy's ID |
| `OP_BUDDYPING` / `OP_BUDDYPONG` | firewalled ↔ open, TCP | the TCP connection | `ListenSocket.cpp:1409` accepts it only from the one buddy |
| `OP_CALLBACK`, `OP_REASKCALLBACKTCP` | open → firewalled, TCP | the TCP connection | sent on the buddy's socket |

A node that serves several buddies looks the target up by BuddyID where stock code takes
`m_pBuddy`. Packet formats, opcodes and tags stay as they are.

The serving node learns the BuddyID from `KADEMLIA_FINDBUDDY_REQ` and stores it with the
client (`ClientList.cpp:742-743`). The copy in `KADEMLIA_FINDBUDDY_RES` is only an echo: the
asker uses it to check that the reply answers its own request
(`KademliaUDPListener.cpp:1743-1744`) and identifies the buddy by IP, TCP port and client
hash. The BuddyID is declared by the firewalled node and never verified, so it selects a
served node but does not authenticate it.

The other two parties see no difference:

- **The firewalled node** has one buddy as before. It publishes that buddy's IP and port with
  its own BuddyID (`Search.cpp:664-669`), pings it over TCP, and accepts `OP_CALLBACK` only
  when the ID in it is its own (`ListenSocket.cpp:1377`). It cannot tell how many others the
  buddy serves. So this works for stock firewalled clients too.
- **The calling client** sends the BuddyID it got from the source entry to the buddy's
  address (`BaseClient.cpp:1435-1446`). Unchanged.

Two points differ from stock, both on the serving node only:

- Stock forwards a `KADEMLIA_CALLBACK_REQ` without checking the BuddyID, and the firewalled
  node drops a callback that is not for it. eMuleQt matches the ID and drops a request for a
  node it does not serve.
- Stock allows one `KADEMLIA_CALLBACK_REQ` per minute from one IP
  (`PacketTracking.cpp:144-146`). A fifth request in one burst bans that IP
  (`PacketTracking.cpp:187-192`). With one buddy per node a caller never needs more. With
  several, a caller that wants two nodes behind the same buddy would lose its second request,
  and a caller that wants five would be banned. eMuleQt allows one request per minute for
  each buddy slot (`maxServedBuddies`). With the limit set to 1 this is the stock rule.

### How eMuleQt does it

The two buddy roles are kept apart:

- `ClientList::getBuddy()` / `buddyStatus()` is only the buddy this node uses while it is
  firewalled. Unchanged.
- A *served buddy* is a firewalled node this node relays for. The client carries the mark
  (`UpDownClient::isServedBuddy()`), set when the node's `KADEMLIA_FINDBUDDY_REQ` is
  accepted. A served buddy never becomes `getBuddy()`.

| Step | Code | Behaviour |
|---|---|---|
| Offer a slot | `process_KADEMLIA_FINDBUDDY_REQ`, `ClientList::incomingBuddy()` | answers while the connected served buddies are below the limit (`canServeAnotherBuddy()`); a pending claim takes no slot, as in stock code |
| Callback relay | `process_KADEMLIA_CALLBACK_REQ` | picks the served buddy by BuddyID (`findServedBuddy()`) |
| Reask relay | `OP_REASKCALLBACKUDP` handler in `CoreSession.cpp` | same lookup |
| Keep-alive | `UpDownClient::processBuddyPing()` | answers the ping of any served buddy |
| Housekeeping | `ClientList::processKadList()` | see below |

A served buddy is dropped when:

- this node becomes firewalled on TCP and UDP, or Kad loses its contacts (all are dropped);
- the served node is no longer LowID, so it opened its port (as stock does for its one buddy);
- the limit was lowered, or more claimants connected than slots exist: the newest ones go
  first.

### Limits

| | Value | Where |
|---|---|---|
| Served buddies | 8, range 1–32 | preference `maxServedBuddies`, Options → Extended |
| Pending claim (answered, not yet connected) | reserves nothing, released after 5 minutes | `kIncomingBuddyTimeoutSecs` |
| Pending claims per IP | 1 | `ClientList::incomingBuddy()` |
| Pending claims in total | 64, a further one releases the oldest | `ClientList::kMaxPendingBuddyClaims` |
| Relayed packets per served buddy | 60 per minute | `UpDownClient::allowBuddyRelay()` |
| `KADEMLIA_CALLBACK_REQ` per caller IP | `maxServedBuddies` per minute (stock: 1) | `PacketTracking::inTrackListIsAllowedPacket()` |

Each served buddy holds one idle TCP connection, kept open by its ping every 10 minutes, and
counts against the connection limit.

The callback limit keeps the stock bucket and ban threshold; only the cost of one request
changes. At the default of 8 a burst of 8 requests from one IP passes, the 9th is dropped,
and the IP is banned at the 33rd. The limit for `KADEMLIA_FINDBUDDY_REQ` stays as in stock.

The number of served buddies is kept small on purpose:

- every served node depends on this one node. When it goes offline, all of them are
  unreachable until each finds a new buddy, which takes hours;
- other nodes drop a source whose buddy IP they have banned or filtered
  (`KademliaUDPListener.cpp:1345-1356`, `DownloadQueue.cpp:1560-1565`). One ban of the buddy
  hides every node behind it;
- every slot raises the callback limit above, so the node accepts more unverified UDP
  requests from one IP before it drops or bans;
- every slot costs a TCP connection and relay traffic.

A node that uses a buddy itself offers no slot.

`KADEMLIA_FINDBUDDY_REQ` is one unverified UDP packet, so its sender address can be forged.
That is why a claim reserves nothing and the total is bounded by release, not by refusal:
forged requests can neither fill the slots nor lock real nodes out of claiming.

The Network Info dialog shows "Buddies served: n / limit" while the node is open.

### How much it helps

- It adds slots only on nodes that run eMuleQt. Stock nodes keep one slot. The effect on
  the network grows with the share of open nodes that run eMuleQt.
- It does not help a firewalled eMuleQt node directly. That node gains only when some open
  node elsewhere offers more slots.
- Each open eMuleQt node serves up to 8 firewalled nodes of any client instead of one. The
  firewalled node and the calling client need no change, so stock clients gain as well.
- The gain grows in a straight line with deployment. If a share *p* of the open nodes runs
  eMuleQt, the number of slots rises by the factor 1 + 7*p*: 7 % more at a share of 1 %,
  35 % more at 5 %. A taken slot stays taken, so new slots fill once.
- Only a node that is firewalled on TCP and UDP needs a buddy. A node with open UDP uses
  direct callback and never searches for one (`ClientList.cpp:594`, `Search.cpp:647-661`).
- A buddy does not connect two firewalled nodes. A firewalled caller discards such a source
  (`DownloadQueue.cpp:1556`).
- If the silent open nodes in the probe are silent because their UDP test is unverified
  (cause 2 above) and not because their slot is taken, more slots on stock nodes would not
  have changed the result. The change rests on cause 1 being the main one, which the probe
  makes likely but does not prove.

Tests: `tst_ClientList` (`servedBuddies_*`), `tst_CallbackAndQueueRank`
(`kadCallbackRelay_relaysToBuddy`). Not yet observed on the live network.
