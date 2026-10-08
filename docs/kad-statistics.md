# Kademlia statistics

The Statistics window has a **Kademlia** branch (after MFC's own branches, before Usenet). MFC
shows Kad only as overhead lines and a firewalled ratio under Clients; everything else here is
an addition.

```
Kademlia
├─ Session
│  ├─ Status
│  ├─ Routing Table   contacts now (verified, by type, by version), peak, added, expired, banned
│  ├─ Nodes           nodes seen / heard of, Firewalled (Kad), By Country
│  ├─ Network         estimated users and files, our index, active searches
│  └─ Activity        time connected, hellos, lookup answers, searches, publishes
└─ Cumulative         the same without the "now" rows
```

## Where the numbers come from

| Kind | Storage | Notes |
|---|---|---|
| Activity counters | `KadCounters` in `src/core/stats/NetworkCounters.h` | Session half in `Statistics::kadSession()`, banked as `statistics: cumKad:` in preferences.yml. Same mechanics as the Usenet block: reset, backup, restore and the periodic flush need no Kad code. |
| Routing table as it stands | read from `RoutingZone` per request | Session only; there is no cumulative form of "now". |
| Distinct nodes, by country | `kad::KadNodeCensus` → `kadcensus.dat` | See below. |

Kad stops and restarts inside one session and deletes its own `KadPrefs` when it does, so
nothing is kept there. The Kad code counts through `countKad()` / `raiseKad()`
(`src/core/kademlia/KadStats.h`), which are no-ops when no `Statistics` is installed (unit tests).

Things that are easy to get wrong:

- **Contacts Added** counts in `RoutingZone::addUnfiltered()`, the wire path. `readFile()` inserts
  below it, so the up to 200 contacts reloaded from nodes.dat on every start are not counted.
- **IP Verified** counts the transition, not every HELLO that verifies again.
- **Firewalled (Kad)** counts per HELLO_REQ from a 0.49b+ node, as MFC's
  `StatsIncUDPFirewalledNodes` does; it is a ratio of answers, not of distinct nodes.
- **Time Connected** adds up the 1 s ticks spent connected and drops a tick longer than 10 s
  (system sleep).

## Nodes seen

Two tiers, because a node's word is not evidence:

- **Nodes Seen** (`contacted`): the node sent us a HELLO_REQ or HELLO_RES, so its ID arrived with
  a real packet source address (counted before the "UDP firewalled, don't add" return: seen is
  not the same as added), or it answered a lookup the search had addressed to that ID
  (`Search::processResponse`). The second source matters: a firewalled client gets hardly any
  HELLO answers, and without it the tier and its countries stay empty. Only this tier is split
  by country.
- **Nodes Heard Of** (`listed`): the ID appeared in another node's KADEMLIA2_RES or
  BOOTSTRAP_RES. One hostile node can list any ID with any address, so this is one number and
  has no countries.

A node is attributed to the country its address resolved to when it was seen (`countryCodeOf`,
the bundled GeoLite2 database). No database, or a LAN address, is the *Unknown* row. A node seen
from two countries is in both rows but once in the total, so the country shares are taken of the
sum of the rows.

### Counting: HyperLogLog

Nodes are counted by node ID in `CardinalitySketch` (`src/core/stats/`), one sketch per country
plus one for the listed tier. Counting and persistence live in the base class `CountryCensus`,
which the client census shares ([client-census.md](client-census.md)).

| Option | Cost | Why not |
|---|---|---|
| Exact set (SQLite like `SeenFileIndex`, or a hash set) | 16 B per node, unbounded | A cap means eviction, so the "cumulative" would be lossy anyway. |
| Bitmap / linear counting | linear in the count | No gain over HLL. |
| KMV / theta sketch | 8 KB for ~3 % | Worse per byte; only wins when intersections are needed. |
| UltraLogLog, CPC | ~25 % smaller | More code for a saving that does not matter here. |
| **HyperLogLog, 2¹² registers** | **4 KB, 1.6 % standard error** | chosen |

What makes it fit:

- **A merge is a union, and idempotent.** Cumulative = banked ∪ session can be written out
  absolutely every `statsSaveInterval`, like every other cumulative statistic
  ([flush is idempotent](../src/core/stats/Statistics.h)). The grand total is the union of the
  country sketches.
- **Small sets are exact.** Up to 256 nodes a sketch keeps the hashes themselves, so most
  countries cost a few bytes and show true counts. ~250 countries stay well under 1 MB.
- **Salted hash.** Node IDs are chosen by the peer; IDs crafted to look like rare hashes would
  inflate an estimator fed raw IDs by orders of magnitude (`tst_CardinalitySketch` shows it).
  The registers get `keyedHash(id, salt)` with a random 128-bit per-install salt. The mixer is
  fixed arithmetic, not `qHash`, which changes between Qt versions.
- **Estimator.** Ertl's improved estimator (2017): no bias tables, no range switches.

The tree marks these rows with "≈".

### kadcensus.dat

In the directory of preferences.yml, written atomically on the statistics flush and at shutdown,
only when something changed. Little-endian:

```
u32 magic "KNC1"   u8 version = 1   u8 precision = 12   u64 salt0   u64 salt1
sketch listed
u16 countries, then per country:  u16 code ('D'<<8 | 'E', 0 = unknown)   sketch
sketch := u8 0, u16 n, n × u64 hash (sorted)      -- exact
        | u8 1, 4096 × u8 rank                    -- dense
```

The salt has to outlive the session: sketches hashed with another salt cannot be merged. A lost
or unreadable file therefore starts a new census with a new salt.

**Reset / Restore** (statistics context menu) treat it like the counters: reset writes the totals
to `kadcensus.bak` and forgets the banked half, the session keeps counting; restore swaps the
totals with the backup, so restoring twice is a no-op.

## Access

- IPC `GetKadStats = 277` (`src/ipc/IpcProtocol.h` documents the payload). A request of its own:
  `GetStats` is polled every second for the status bar.
- REST `GET /api/v1/kad/stats`, MCP tool `kad_stats` — the same `ops::kadStats()` payload.

## Tests

`tst_CardinalitySketch`, `tst_KadNodeCensus` (incl. the file layout, pinned byte by byte), `tst_Statistics::cumulativeKad_*`,
`tst_KadRoutingZone::counters_*`, `tst_KadUDPListener::helloReq_firewalledNodeIsSeenButNotAdded`,
`tst_StatisticsPanel::kademlia*`.
