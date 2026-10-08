# Clients seen, by country

The Statistics window's **Clients** branch keeps MFC's live rows (Known Clients, Client Software,
Low ID, Banned, Filtered) and adds two scopes below them:

```
Clients
├─ …                       MFC's rows, the client list as it stands
├─ Session
│  ├─ Clients Seen: ≈n      distinct user hashes that said hello
│  ├─ Identified: ≈n (%)    of those, proved by SecureIdent
│  └─ By Country            flag, name, count, share
└─ Cumulative               the same, banked across sessions
```

## What is counted

A client is its **user hash**. It is counted when a hello (`OP_HELLO` / `OP_HELLOANSWER`) has been
parsed (`UpDownClient::processHelloTypePacket`), with the country of the address the TCP
connection came from. Sources we only heard of (server, Kad, source exchange) are not counted
until they say hello.

| Candidate | Why not |
|---|---|
| IP address | A dynamic IP makes one user many over weeks: the cumulative count would grow without bound. CGNAT merges users the other way. |
| IP + port, hash + IP | Inherit the IP problem. |
| SecureIdent-verified hash only | Sound, but leaves out every client without SecureIdent (MLDonkey, Shareaza, old eDonkey). |
| **User hash from a hello** | chosen: the only identity that survives an IP change. |

The hash is self-chosen, so:

- The sketches get a salted hash of it (see [kad-statistics.md](kad-statistics.md), "Salted hash");
  crafted hashes cannot inflate the estimate.
- The address is real: the hello arrives on an established TCP connection. Hence one tier, and
  it is the one split by country.
- Mods that share one hash undercount, mods that change it every session overcount. **Identified**
  is the answer to that: hashes whose owner signed our challenge (`verifyIdent` in
  `UpDownClient::processSignaturePacket`). SecureIdent runs on every connection once both info
  packets are in (`onInfoPacketsReceived`), so for eMule clients the two numbers should be close.
  A share well below 100 % means clients without SecureIdent, hash thieves, or a bug.
- The null hash is not counted.

A client seen from two countries is in both rows but once in "Clients Seen"; the country shares
are taken of the sum of the rows.

## Storage

`ClientCensus` (`src/core/client/`) and `kad::KadNodeCensus` are both a `CountryCensus`
(`src/core/stats/`): one HyperLogLog sketch per country plus one secondary sketch (here:
identified; for Kad: listed). File `clientcensus.dat` next to `kadcensus.dat`, same layout with
magic `CLC1`, same flush clock, and the same Reset / Restore behaviour (`clientcensus.bak`).

## Access

- IPC `GetClientStats = 278`.
- REST `GET /api/v1/clients/stats`, MCP tool `client_stats` — the same `ops::clientStats()` payload:
  `{ seen: { session | cumulative: { seen, identified, countries: [[cc, clients]] } } }`.

## Tests

`tst_ClientCensus`, `tst_UpDownClient::hello_countsTheClientOnceInTheCensus`,
`tst_StatisticsPanel::clientsSeenByCountryInBothScopes`, `tst_KadNodeCensus` (the shared base).
