# ed3k module (packmule in the daemon)

First step of hosting the Rust ed3k library packmule (`libs/packmule`, a separate repo,
gitignored here) in `emulecored`. The node runs, keeps its key and state, and finds other
nodes over Kad. It serves eMule's shared files (see "Shared files"); it downloads only for the
joins.

## What exists

| File | What |
|---|---|
| `src/ed3k/Ed3kSession.{h,cpp}` | `eMule::ed3k::Ed3kSession` (QObject): the engine, its UDP socket, the clock and the one timer, the pump, key and state files, default answers |
| `src/ed3k/Ed3kHttp.{h,cpp}` | host IO (`libs/packmule/docs/host-io.md`): HTTP/1.1 on `QTcpSocket`/`QSslSocket` for `event::HttpRequest`, and the JSON-RPC POST of `event::RpcRequest` |
| `src/ed3k/Ed3kStore.{h,cpp}` | `Config/ed3k/node.key` (create once, 0600, never replaced), atomic blobs, wiping |
| `tests/tst_Ed3kSession.cpp` | key file, two nodes on loopback, state across a restart, HTTP client, JSON-RPC (URL rule, cancel, a chain read answered by a local server, no endpoint), traffic and limits |
| `src/daemon/Ed3kBridge.{h,cpp}` | IPC `GetEd3kStatus` / `PushEd3kStatus` (see "Status and bandwidth" below) |
| `tests/tst_Ed3kBridge.cpp` | the status map without and with a node, the 1000-1049 block, `BandwidthCoordinator` with the node (limits, rates, eD2K's budget) |
| `src/ed3k/Ed3kUsenetJoin.{h,cpp}` | `eMule::ed3k::Ed3kUsenetJoin`: a Usenet download joined with ed3k (see "Usenet join" below) |
| `tests/tst_Ed3kUsenetJoin.cpp` | NZB buffer, server tag, a real queue + fake news server verified under a claim, blocks from a second node fill holes, a false claim |
| `src/ed3k/Ed3kJoinBase.{h,cpp}` | what both joins share on the ed3k side: member downloads (sources, streams, block requests), manifests fetched into memory (`fetchBlob`), serving shared roots, sharing blobs, hashing on a worker, debounced saving |
| `src/ed3k/Ed3kTorrentJoin.{h,cpp}` | `eMule::ed3k::Ed3kTorrentJoin`: a torrent joined with ed3k (see "Torrent join" below); only with `EMULE_HAVE_BITTORRENT`, links `eMule::Bt` |
| `src/ed3k/Ed3kShared.{h,cpp}` | `eMule::ed3k::Ed3kShared`: eMule's shared files served over ed3k (see "Shared files" below), derived from `Ed3kJoinBase` |
| `src/ed3k/Ed3kEd2kJoin.{h,cpp}` | `eMule::ed3k::Ed3kEd2kJoin`: eD2K part files fed from ed3k too (root by `ed2k` ALIAS), hinted eD2K sources over ed3k first, pair trust (Protocol.md §14.2, §14.4; `docs/ed2k-compat.md`) |
| `tests/tst_Ed3kEd2kJoin.cpp` | three loopback nodes: an eD2K part file filled over ed3k and trusted, false pairs (part / FileStatus), hinted sources preferred or falling back |
| `tests/tst_Ed3kShared.cpp` | the `ed2k` alias bytes, a file of a fake share list fetched by its root from a second node, the hash cache across a restart, move/remove/pref off, keywords published (not while holding back), an unreadable file hashed again |
| `src/ed3k/Ed3kPlumbing.{h,cpp}` | pure helpers: extension names → `ED3K_EXT_*` (`extOffBits`), `ed3k:nodes` links (`parseNodesLink`, `nodesLink`, base32), the advertised IPv6 source, address conversions |
| `src/ed3k/Ed3kLan.{h,cpp}` | LAN discovery of mode A (see "LAN discovery" below); owned by the session |
| `src/ed3k/Ed3kBootstrap.{h,cpp}` | node list sources and the asks of `ed3k:nodes` links (see "Node lists" below) |
| `tests/tst_Ed3kPlumbing.cpp` | extension names and `extOff` in the engine, base32 and `ed3k:nodes` links, the IPv6 source following the pin, filter and names state across a restart, two nodes finding each other over LAN (sockets by hand), node list links refused without a GUI, asked and answered |
| `src/ed3k/Ed3kFriends.{h,cpp}` | `eMule::ed3k::Ed3kFriends`: ed3k friends and chat (see "Friends and chat" below) |
| `src/daemon/Ed3kFriendsBridge.{h,cpp}` | IPC 1001-1007 / pushes 1051-1056 of friends and chat, reached through `Ed3kBridge` |
| `tests/tst_Ed3kFriends.cpp` | three nodes on loopback: a friend by link and request, accepted, found by its record, chat both ways, the message filter; the IPC messages through CBOR frames |
| `src/ed3k/Ed3kRooms.{h,cpp}` | `eMule::ed3k::Ed3kRooms`: ed3k rooms (see "Rooms" below) |
| `src/daemon/Ed3kRoomsBridge.{h,cpp}` | IPC 1120-1129, 1044 / pushes 1160-1164 of rooms, reached through `Ed3kBridge` |
| `tests/tst_Ed3kRooms.cpp` | two nodes on loopback: an open room joined by link with posts both ways, a closed room with the admit round trip, a ban, the rooms across a restart; the IPC through CBOR frames; batch 13: a reply round trip and its quoted line in `Ed3kRoomTab`, a mute kept across a restart, `ROOM_MEMBER` showing the member key |
| `src/ed3k/Ed3kNames.{h,cpp}` | `eMule::ed3k::Ed3kNames`: readable names (name.md): the DNS duty, name list files, resolving and opening, pins, petnames, followed lists; the sets and members of websites (see "Names and websites" below), derived from `Ed3kJoinBase` |
| `src/ed3k/Ed3kSiteServer.{h,cpp}` | `eMule::ed3k::Ed3kSiteServer`: the local website server (name.md §6.2), its own `QHttpServer` on 127.0.0.1 and ::1 |
| `src/ed3k/Ed3kSiteCache.{h,cpp}` | `eMule::ed3k::Ed3kSiteCache`: website manifests and members on disk across restarts (name.md §6.4), 2 GiB, LRU |
| `src/daemon/Ed3kNamesBridge.{h,cpp}` | IPC 1010-1019 / pushes 1060-1063 of names and websites, reached through `Ed3kBridge` |
| `src/gui/app/Ed3kNameOpener.{h,cpp}` | GUI: an `ed3k:name` link resolved, shown, confirmed and opened (`ExternalLinkHandler`) |
| `tests/tst_Ed3kNames.cpp` | set links, the DNS answer through a fake resolver, a domain-verified web name to a set whose page a second node serves over the website server (302, 200, headers), the gate (421, 405, 403), the IPC |
| `src/ed3k/Ed3kCredit.{h,cpp}` | `eMule::ed3k::Ed3kCredit`: credits (credit.md): the key file, the ledger from the prefs, bids, the status (see "Credits" below) |
| `src/daemon/Ed3kCreditBridge.{h,cpp}` | IPC 1020-1022 (the key; local GUI only) and the `credit` map of `GetEd3kStatus`, reached through `Ed3kBridge` |
| `src/gui/dialogs/Ed3kOptionsPages.{h,cpp}` | GUI: the "ed3k" group of the Options dialog (General, Connection, Credits, Names; Filters: `Ed3kFilterPage`) |
| `src/gui/dialogs/Ed3kStakeGroup.{h,cpp}` | GUI: the stake group of the Credits page (burn for stake, the claims, "Claim"; see "Stake") |
| `src/gui/dialogs/Ed3kFilterPage.{h,cpp}` | GUI: Options → ed3k → Filters (followed filter lists, their mode, own verdicts; see "Filter lists") |
| `tests/tst_Ed3kStake.cpp` | a burn through a fake ledger answering the session's `RpcRequest`s, the claim kept in `stake-claims.json`, loaded and read again after a restart (paid meanwhile), a claim paid, the IPC; a typed id read then claimed; the tx hashes kept and a claim pending at a stop sent again |
| `src/ed3k/Ed3kUploads.{h,cpp}` | `eMule::ed3k::Ed3kUploads`: the engine's upload queue as rows for the GUI (observes the queue events only; see "Upload queue") |
| `src/daemon/Ed3kUploadsBridge.{h,cpp}` | the ed3k rows of `GetUploads` (`network: "ed3k"`) |
| `tests/tst_Ed3kUploads.cpp` | queue events by hand become rows and stay unhandled, the `GetUploads` rows; four nodes on loopback: holders, a waiting node, named, the rows end |
| `src/ed3k/Ed3kFilters.{h,cpp}` | `eMule::ed3k::Ed3kFilters`: filter lists (filter-list.md): `FilterFetch` → `fetchBlob` → `filterFile`, follow/unfollow/mode, own verdicts, `judge()`; derived from `Ed3kJoinBase` (see "Filter lists") |
| `src/daemon/Ed3kFiltersBridge.{h,cpp}` | IPC 1130-1136, 1042-1043 / push 1170 of the filter lists, reached through `Ed3kBridge` |
| `tests/tst_Ed3kFilters.cpp` | follow by link and unfollow, a `FilterFetch` fetched from a second node and handed in, a list file opened by hand marking then hiding an entry, own verdicts across a restart, the IPC; batch 13: the own list published (and again on a change, across a restart), show anyway, a mark judging search rows again, a set author judging a row |
| `src/ed3k/Ed3kChain.{h,cpp}` | `eMule::ed3k::Ed3kChain`: `chainConfigure` from the `ed3k.chain` prefs (chaintube.md §4.2) |
| `tests/tst_Ed3kCredit.cpp` | off by default, the key file (owner-only, stable over a restart, import/export, a new key), the key over IPC, `creditConfigure` reaching a fake JSON-RPC ledger (chain ID, balance, no second configure), an endpoint on another chain |
| `tests/tst_Ed3kTorrentJoin.cpp` | real `BtSession`s and ed3k nodes on loopback: a completed torrent gives its set, ed3k alone fills a torrent, the join saved and loaded, a magnet with `xs` fetches its set (§4.1) |
| `src/ed3k/Ed3kDownloads.{h,cpp}` | `eMule::ed3k::Ed3kDownloads`: plain ed3k file downloads (search rows, `ed3k:file` links without `bt=`; set members later), derived from `Ed3kJoinBase` (see "Downloads and search" below) |
| `src/ed3k/Ed3kSearch.{h,cpp}` | `eMule::ed3k::Ed3kSearch`: ed3k keyword search (`SearchType::Ed3k`) as a `MetaSearchRunner` |
| `src/daemon/Ed3kDownloadsBridge.{h,cpp}` | IPC 1140-1143 / push 1180 of the downloads, reached through `Ed3kBridge` |
| `src/core/utils/BlockRuns.{h,cpp}` | a block bitmap as run lengths (LEB128, set/clear alternating) for IPC, and as byte ranges |
| `src/gui/controls/Ed3kDownloadRows.{h,cpp}` | GUI: the rows of `GetEd3kDownloads` as `DownloadRow`s (the bar from the blocks) |
| `tests/tst_Ed3kDownloadRows.cpp` | GUI: block runs round trip and damaged, a row's gaps/pending/sources per piece, the bar painted by piece |
| `tests/tst_Ed3kSearch.cpp` | a found file as a row, the filter, three nodes on loopback: a shared file's keywords found by another node's search, cancel, no node |
| `tests/tst_Ed3kDownloads.cpp` | two nodes on loopback: a download verified, moved and shared; pause/resume; a part file taken up after a restart; the IPC (with the block runs); turns at one source; a higher priority gets more blocks, a raised one takes the source |
| `src/ed3k/Ed3kSets.{h,cpp}` | `eMule::ed3k::Ed3kSets`: followed sets (file-set.md §5.3, hidden and pinned links), their members through `Ed3kDownloads` (see "Sets and live" below) |
| `src/ed3k/Ed3kSetManifest.{h,cpp}` | a manifest read for showing and choosing members (title, id, seq, paths), local names (§2.2) |
| `src/ed3k/Ed3kLive.{h,cpp}` | `eMule::ed3k::Ed3kLive`: live streams watched (live.md), served as a progressive HTTP stream on the website server's port; restarts on a gap, new init or broadcast (#15) |
| `src/ed3k/Ed3kLiveBroadcast.{h,cpp}` | `eMule::ed3k::Ed3kLiveBroadcast`: broadcasting from an HLS fMP4 folder (live.md §4), the channel keys; `Ed3kHlsPlaylist` reads a media playlist |
| `src/ed3k/Ed3kPaid.{h,cpp}` | `eMule::ed3k::Ed3kPaid`: paid subscriptions, buying passes (subscription.md §5, §6, §9): offers, the holder keys, pass polls and renewals, the anonymous warning, share reports; `Ed3kPaidOffer`/`Ed3kPaidPass` read them |
| `src/ed3k/Ed3kProto.h` | protobuf wire reader/writer of the host's reads (lifted from `Ed3kSetManifest.cpp`) |
| `src/daemon/Ed3kSetsBridge.{h,cpp}` | IPC 1110-1119, 1144, 1039 / pushes 1155-1158 of sets, live streams and broadcasts, reached through `Ed3kBridge` |
| `src/daemon/Ed3kPaidBridge.{h,cpp}` | IPC 1035-1038 / pushes 1064-1066 of paid subscriptions, reached through `Ed3kBridge` |
| `src/gui/dialogs/Ed3kSetsDialog.{h,cpp}` | GUI: "ed3k Sets" (followed sets, the member tree with check boxes, the policy; live streams) |
| `tests/tst_Ed3kSets.cpp` | four nodes on loopback: a signed set published (`setPublish` with a seed) and followed by its link, every member downloaded into the set's folder; a chosen part; new versions by policy across restarts; links, the state file owner-only; the IPC; the website server's 503/504/wait and a set that is no website offered |
| `tests/tst_Ed3kSetsDialog.cpp` | GUI: "ed3k Sets" filled with hand-built CBOR (list, member tree and boxes, ticks kept over a refresh, the live list); `EMULE_SCREENSHOT_DIR` saves a picture of each tab |
| `tests/tst_Ed3kLive.cpp` | a broadcast in the test (`liveBroadcast`, `livePushSegment`) watched by a second node and read over loopback HTTP (init data first, chunked, ended with the broadcast and a `free` box, 410 after); other hosts and bad links; a gap and a new init end the responses, the next begins with the new init (#15), the player page; HLS playlists read; a broadcast from an HLS folder (`Ed3kLiveBroadcast`) watched, its link kept per title |
| `tests/tst_Ed3kPaid.cpp` | a publisher node with a season and its own pass (signed in the test: OpenSSL Ed25519, a one-block BLAKE3 `derive_key` checked against the node ID); the buyer follows the scope with its offer, buys (pay link), the core polls a fake seller (HTTPS on loopback: 404, then the pass), the pass is set, keys from the publisher (`PAID_KEYS`); the holder key file owner-only; the IPC; kept across a restart |

`DaemonApp` owns the session (`#ifdef EMULE_HAVE_ED3K`). It starts after the Usenet session
when `ed3kEnabled`, and it stops before Usenet. `applyEd3kConfig` runs on
`IpcServer::ed3kConfigChanged`: it starts, stops or restarts the node (a port change means a
restart) and updates the port mappings.

## Build switches

- **CMake:** `-DEMULE_ED3K=ON` (the default).
  - The top-level `CMakeLists.txt` adds `libs/packmule/crates/packmule-ffi` (its cargo fallback builds `libpackmule_ffi.a`) and sets `EMULE_HAVE_ED3K=1` for every target.
  - The cargo profile follows `CMAKE_BUILD_TYPE`: `Debug` uses debug, anything else uses release.
  - The cargo target dir is `<build>/packmule-ffi/cargo`.
  - Without `libs/packmule` or `cargo`, the option turns itself off and the build goes on without ed3k.
  - `emuleed3k` links `eMule::Bt` when that target exists (the torrent join); without it `Ed3kTorrentJoin` compiles to nothing.
- **MSBuild:** `EmuleEd3k` in `QtVersion.props`.
  - It is on when `libs\packmule\Cargo.toml` exists. Turn it off with `/p:EmuleEd3k=false`.
  - `src/ed3k/emuleed3k.vcxproj` runs `cargo build --release -p packmule-ffi` as a pre-build step, with `CARGO_TARGET_DIR=libs\packmule\target\emuleqt-msbuild`.
  - `emulecored.vcxproj` gets `$(EmuleEd3kLibs)`; `emulecore`/`emulecored` get `$(EmuleEd3kDefines)`.
  - The project is in `eMuleQt.sln` and in `scripts/generate_test_vcxprojs.py`. With the switch off, unload the project, as with `emulebt`.
  - `emuleed3k.vcxproj` also takes `$(EmuleBtDefines)` and `..\bt` for the torrent join.
- **qmake:** `src/ed3k/ed3k.pro` is a subdir of `eMuleQt.pro` when `libs/packmule` exists (after `bt`, whose headers the torrent join uses). Build `packmule-ffi` with cargo first (`target/release`).

## Preferences

The `ed3k:` block in `preferences.yml`, with IPC/REST keys `ed3kEnabled` and `ed3kUdpPort` (`PreferenceSchema`):

| Key | Default | |
|---|---|---|
| `enabled` | `false` | opt-in |
| `udpPort` | `0` | 0 = not picked yet: at load the eD2K TCP port number (§14.1) when free, else a random one (never the eD2K UDP or BT port); saved. Equal to the eD2K UDP port: one shared socket (see "The socket"). A new config has TCP = UDP = ed3k; random listen ports keep them together |
| `creditRpc` | `[]` | IPC/REST `ed3kCreditRpc` (URLs separated by spaces): JSON-RPC endpoints of the credit ledger; empty = calls answered failed |
| `chaintubeRpc` | `[]` | IPC/REST `ed3kChaintubeRpc`: the same for chaintube's chain reads and writes |
| `chain:` | | chaintube.md §4.2, no defaults: `chainId` (0 = none), `content`, `comment`, `vote` (0x + 40 hex; empty = none), `forwarder` (empty = none); IPC/REST `ed3kChainId` (decimal text), `ed3kChainContent`, `ed3kChainComment`, `ed3kChainVote`, `ed3kChainForwarder` (`Preferences::applyEd3kChainKey`). `Ed3kChain` calls `chainConfigure` at each node start and when they or `chaintubeRpc` change (no restart): only with a chain ID and the three contracts; providers per read 0 without endpoints, 1 when every endpoint is a local node, else 2 (held to the endpoints there are). One remote endpoint only (`Ed3kChain::singleRemote`): the §4.3 SHOULD of two agreeing providers can't be met, said once in the log and as a note under the endpoints (batch 12 #8). GUI: Options → ed3k → Connection → Chaintube |
| `jointDownloads` | `true` | IPC `ed3kJointDownloads`: eD2K downloads also fetch from ed3k, dual peers over ed3k (`Ed3kEd2kJoin`) |
| `shareFiles` | `true` | IPC/REST `ed3kShareFiles`: serve the shared files over ed3k (`Ed3kShared`); only while the node runs |
| `sitePort` | `0` | IPC/REST `ed3kSitePort`: TCP port of the website server (name.md §6.2); 0 = a random free port, picked once at load and saved (it is part of every site's origin) |
| `extOff` | `[]` | IPC/REST `ed3kExtOff` (names separated by spaces): optional extensions turned off (`Preferences::ed3kExtensionNames()`: the names of `extensions/*.md`, `lan` = `lan-discovery`) → `Config::extOff`; unknown names are logged and ignored. A change restarts the node |
| `lan` | `true` | IPC/REST `ed3kLan`: LAN discovery (`Ed3kLan`). A change restarts the node |
| `nodeLists` | `[]` | IPC/REST `ed3kNodeLists` (links separated by spaces): node list sources as `ed3k:nodes?url=…&key=…` links (`Ed3kBootstrap`); a source the user said yes to is added. No restart |
| `credit:` | | `enabled` (`false`, §9), `chainId` (0 = none), `contract` (empty = none; no defaults, §7.1), `quorum` (2, held to 1..endpoints), `bidMaxPrice` (µ/MiB per download, 0 = receipts only), `sell` (`false`), `sellFloor` (µ/MiB), `sellSlots` (0 = the queue's); IPC/REST `ed3kCreditEnabled`, `ed3kCreditChainId` (decimal text: a u64), `ed3kCreditContract`, `ed3kCreditQuorum`, `ed3kCreditBidMaxPrice`, `ed3kCreditSell`, `ed3kCreditSellFloor`, `ed3kCreditSellSlots` (`Preferences::applyEd3kCreditKey`). No restart |
| `shareUsenetPosts` | `true` | IPC/REST `ed3kShareUsenetPosts`, Options "Share Usenet posts over ed3k too". Off holds Usenet posts back (usenet.md §6): no article lists, post sets or `nzb`/`nzb-file` records; the members are shared all the same. A change restarts the usenet join; switching off withdraws the kept `ALIAS` records (`unpublishAlias`) |

The port is mapped as `PortMapPurpose::Ed3kUdp` while `ed3kEnabled`.

## Files (`Config/ed3k/`)

| File | Slot | Written |
|---|---|---|
| `node.key` | 32-byte seed | once; 0600 on Unix (ToDo: owner-only DACL on Windows); never logged |
| `credit.key` | 32-byte secp256k1 key (credit.md §2) | when credits are first configured, or by a new key / an import (replaced atomically, 0600); never logged |
| `kad.dat` | `ED3K_STATE_KAD` | on `StateChanged` and at stop (only with contacts) |
| `bad.dat` | `ED3K_STATE_BAD` | at most once a minute after `StateChanged`, and at stop |
| `sets.dat` | `ED3K_STATE_SETS` | on `StateChanged` and at stop |
| `friend.dat` | friend state | on `FriendChanged` and at stop |
| `rooms.dat`, `names.dat`, `paid.dat` | `ED3K_STATE_ROOMS` (secret: generation keys), `_NAMES`, `_PAID` | on `StateChanged` and at stop |
| `filter.dat` | `filterExportState` | on `FilterChanged` and at stop |
| `hops.dat` | `hopsExportState` (the entry hops) | at stop |
| `bootstrap.dat` | the start of the last first contact from node lists (u64 LE, `BootstrapStarted`) | on `BootstrapStarted` |
| `friend-names.dat` | `keyhex<TAB>name` lines: `dn` of the added links (`Ed3kFriends`); not a credential | when a named link is added or a friend removed |
| `rooms/joined.cbor` | the joined rooms: room, link (a credential with `s`), nick, last info, muted member IDs (`Ed3kRooms`); 0600 | on create, join, leave, nick, mute |
| `rooms/<room>.member`, `rooms/owner-<room key>.key` | 32-byte seeds: the member key of a room, the room key of an owned room (kept on leave); 0600, never logged | on create or join |
| `usenet/<item>.join` | a usenet join | debounced, and at stop |
| `torrent/<id>.join` | a torrent join (`:` of the id as `_`) | debounced, and at stop |
| `usenet/<item>/`, `torrent/<id>/` | kept for a restart (owner percent-encoded): `<root>.blob` per manifest, article list, info dictionary; `kept.tsv` names them, the verified members and the set published (`s` line); `records.bin` the `ALIAS` records (u32-LE length + `Alias` each) | when fetched, made or verified; dropped with the item or torrent |
| `names.dat` | `ED3K_STATE_NAMES` (petnames, pins, own zones, followed lists) | on `StateChanged` and at stop |
| `name-lists.dat` | `id<TAB>label<TAB>link` of the followed name lists in the user's order (`Ed3kNames`); not a credential | on follow, unfollow, move |
| `name-petnames.dat` | `text<TAB>zone hex<TAB>shareable` of the petnames set here (the core lists none); owner-only | on set, remove |
| `shared.dat` | hash cache of the shared files (`Ed3kShared`) | 5 s after a change, and at stop |
| `stake-claims.json` | the claims of own burns and of ids claimed (credit.md §6.7): `[{id, to, amount (uint256 hex), readyAt, state (0 waiting, 1 claiming, 2 paid), burnTx, claimTx (hash hex, "" unknown)}]`, u64 as text; public on the chain, not a secret | on a burn, a claim, a read of a claim |
| `site-cache/` | websites kept across restarts (name.md §6.4, `Ed3kSiteCache`): `s-…` manifests, `m-…` members, 2 GiB, LRU; not secrets | on a manifest or a member in |
| `sets.json` | the followed sets (`Ed3kSets`): id, link (a hidden set's is a credential), policy, title, folder, members left out, the version applied and the one waiting (root, seq), inner manifests and mounts; owner-only, never logged | on follow, unfollow, policy, selection, a version |
| `sets/<root hex>.manifest` | the manifests of those versions, inner sets and mounts (the root the core named; a hidden set's: the sealed file's) | when one arrives |
| `paid.json` | the paid subscriptions (`Ed3kPaid`): scope, publisher, kind, title, anonymous key or not, offer, pass, holder key (public), until, the purchase begun, state; owner-only | on follow, an offer, a purchase, a pass, a poll's end |
| `paid/<scope hex>.holder` | 32-byte seed of the holder key of a subscription (subscription.md §5.1: one per subscription, kept for renewals); 0600, never logged | on the first purchase |
| `live/channels.json`, `live/<id hex>.key`, `.secret` | the broadcast channels by title (`Ed3kLiveBroadcast`: channel `id`, private); the seed of each channel key and `s` of a private one (0600, never logged; the link of a private channel is a credential) | on the first broadcast of a title |
| `filter-own.dat` | the own filter verdicts mirrored (`subject<TAB>id hex<TAB>verdict<TAB>reason`, a `mode` line, a `publish<TAB>set id hex<TAB>title` line when published; the core lists none, it keeps them in `filter.dat`); not a credential | on a change |
| `filter-own.key`, `filter-own.ed3kflt`, `filter-own.ed3kset` | the key the own list's set is signed with (32-byte seed, 0600, never logged); the list file and manifest published last (shared again at start) | on publish |

Every blob is put back before the first datagram. `contactsLoaded()` is called when there is
no `kad.dat` or it does not parse. A blob that does not parse is dropped with a warning; it is
never a reason not to start. All blobs are written with `QSaveFile` and owner-only
permissions.

## Threading and the pump

The session follows `libs/packmule/docs/ffi.md` "The loop" and mode A of `socket-modes.md`.

- **One thread.** Everything runs on the daemon thread; the engine is not thread-safe.
  File reads of the joins and of the blocks served go to `Ed3kSession::readPool()` (2
  threads): `Ed3kJoinBase::readInBackground`, one batch per owner at a time, results back on
  the daemon thread; dropped at `closeAll()`. `m_readsNow` reads inline (usenet
  `filesRepaired`: the volumes are deleted right after; a worker would read them too late).
  A usenet read that failed because the member moved or was sealed meanwhile is read again
  under the path now on the pool too (`readBatch`, once), in order.
- **The socket.** One `QUdpSocket` on its own port, dual stack (`QHostAddress::Any`, `local4 = 0.0.0.0:p`, `local6 = [::]:p`). Datagrams that are not ed3k are counted and dropped.
  - **Shared with eD2K (§14.1):** `ed3k.udpPort` = eD2K `udpPort` → the node binds nothing
    (`Ed3kSettings::sharedSocket` = `ClientUDPSocket::socket()`). `ClientUDPSocket` keeps plain
    eD2K first bytes (`E3 E4 E5 C5 D4 A3 B2`) and offers every other datagram to
    `Ed3kSession::offerDatagram` (its foreign handler) before trial decryption; `ED3K_NOT_OURS`
    goes on to eD2K. One pump per burst (queued). At start the node waits for the eD2K socket
    (`DaemonApp::onCoreLoaded` applies again); `theApp.yieldEd3kUdpPort` stops a node on its own
    socket when eD2K UDP moves onto its port. One port mapping (`Ed3kUdp` is skipped).
  - **Bind address:** `BindAddress::listenAddress()` (the `bindAddress` pref); a selection that
    does not resolve does not start the node; a selected interface pins the socket
    (`InterfacePin::pin`). A change restarts the node.
  - **IPv6 source (§8.3):** with a wildcard bind, `local6` is the stable address of
    `IPv6SourcePin` (none with the privacy address or a proxy: `[::]`), and datagrams to global
    IPv6 peers are sent from it (`QNetworkDatagram::setSender`; loopback, link-local, ULA get
    the system's). Looked at every 5 s and on the timer; a move → `setLocalAddr(6)`.
  - **Nick:** `thePrefs.nick()` → `Config::nick`.
- **The pump.**
  - `readyRead` drains every datagram into `datagramIn`, then pumps once.
  - The pump writes out `datagramOut` at once and in order, drains `pollEvent`, and loops until both are empty.
  - It then re-arms one single-shot timer to `nextTimeout()`.
- **The clock.** `Now` comes from `QElapsedTimer` (µs) and `QDateTime::currentSecsSinceEpoch`.
- **Events.**
  - `eventPolled(const ed3k::Event&, bool* handled)` is emitted synchronously inside the pump.
  - A slot may call engine commands directly; they only queue work, and the running pump picks it up.
  - `pump()` called from inside the pump only makes it loop once more. A slot must not block.
  - From outside the pump (a worker result, a socket slot), use `post(cmd)`, or call a command and then `pump()`.
- **Unanswered events.** An event that needs an answer and that no slot marked handled gets a failure, so the core does not wait:

  | Event | Answer |
  |---|---|
  | `WantBlock` / `VerifyBlock` | `blockFail` (a root that no join and no shared file serves) |
  | `RpcRequest` | `rpcFailed` (no endpoint for its class and operator) |
  | `BootstrapAsk` | no (`Ed3kBootstrap` asks the user first) |
  | unpack | failed / unsupported |

- **HTTP.** `HttpRequest` and `IoCancel` go to `Ed3kHttp`.
  - With `addr`, it connects exactly there; `Host`, SNI and the certificate check use the URL's name (`QSslSocket::connectToHostEncrypted(ip, port, name)`).
  - Without `addr`, the name is resolved here and each address is offered with `httpResolved`. `Denied` ends the request, and no other address is tried. A failed connect gives `httpUnreachable`, then the next address.
  - It follows no redirect (a 3xx is handed in as it came), decodes no `Content-Encoding` (chunked transfer is decoded), sends no cookies, reads at most `maxLen` bytes and stops after `timeoutS`.
  - It uses the user's proxy with the literal address.
- **JSON-RPC.** `RpcRequest` goes to the endpoint list of its class (`ed3k.creditRpc` for
  `ED3K_RPC_CREDIT`, `ed3k.chaintubeRpc` for `ED3K_RPC_CHAINTUBE`): operator 0 is the first
  URL, operator n the n-th, as mode B does (`packmule_udp` `HttpConfig::endpoint`).
  `ed3k_rpc_endpoints` is refused for an `ed3k_new` engine, so the host picks the URL itself.
  - Only `https`, or `http` to `localhost` / a loopback address (a local node); no user info.
  - `Ed3kHttp::startRpc` POSTs the body with `Content-Type: application/json`,
    `Accept: application/json` and `Accept-Encoding: identity`, reads at most 16 MiB and
    stops after 30 s (mode B's `RPC_BODY_MAX`, `RPC_TIMEOUT_S`).
  - The user's URL: its name is resolved here and the addresses are tried in turn, without
    `httpResolved`.
  - A 2xx body → `rpcAnswer`; another status, no connection, TLS failure, too long →
    `rpcFailed`. `IoCancel` drops it like an HTTP request (one id space).
  - The lists take effect without a restart (`DaemonApp::applyEd3kConfig`).
- **A dead engine.** `ED3K_ERR_DEAD` frees the engine and logs it; ed3k stays off until the next start.

Logging uses the categories `emule.ed3k` (`logEd3k`, `logEd3kWarning`, `logEd3kError`,
`logEd3kDebug`) and `emule.ed3k.kad` (`logEd3kKad`, `logEd3kKadWarning`, `logEd3kKadDebug`).
The GUI shows them in three tabs of the log pane while `ed3kEnabled`: **ed3k** (emule.ed3k
info, warnings, errors), **ed3k Verbose** (its debug lines) and **ed3k Kad**; hidden, they
fall back to Log/Verbose and debug lines are dropped (`LogWidget::routeEd3k`). Files:
`<base>_Ed3k.log` and `<base>_Ed3kKad.log`. Filter rules: `emule.ed3k.debug` follows
`verbose`, `emule.ed3k.kad.debug` follows `verbose` or `kadVerboseLog`.

## Status and bandwidth

- **IPC.** `GetEd3kStatus` (1000) → `Ed3kBridge::statusToCbor()`: `available`, `running`,
  `udpPort`, `nodeId`, `contacts`, `class4` / `class6` (0 unknown, 1 open, 2 cone,
  3 symmetric), `router`, `wireSent` / `wireRecv`, `rateUp` / `rateDown` (B/s, from
  `BandwidthCoordinator`), `limitUpKb` / `limitDownKb`, and `stats` (the rest of
  `ed3k_stats`). `PushEd3kStatus` (1050, a state push: a slow reader keeps the last) carries the same map on
  `Ed3kSession::statusChanged`. The block 1000-1049 is the ed3k one; without the module it
  answers `available: false`.
- `queue` (`slotCount`, `holders`, `waiting` of the engine's upload queue policy) and
  `credit` (`Ed3kCreditBridge::statusToCbor`, see "Credits") are in the same map;
  `Ed3kCredit::statusChanged` pushes it too (counters at most once a second).
- **Statistics.** `StatisticsPanel` has an ed3k branch (after Hoster) from
  `GetEd3kStatus`: state, node ID, address class, router; Session (rates, connections,
  contacts, bans, traffic with file/other shares, Kad lookups/records, the upload queue);
  Cumulative (`Ed3kCounters`: traffic, avg/max rates, up/down time, peaks; sampled each
  second by `Ed3kBridge::sampleCounters`, banked in prefs `cumEd3k`); Credits (state, address, balance, account, receipts, priority turns, stake). Hidden for a
  core without ed3k.
- **GUI.** `MainWindow` shows an `ed3k:` label next to Kad's in the status bar, in Kad's
  style: "Connected", "Connected (Firewalled)" (no family open, no router duty) or
  "Connecting..." (no contacts). The tooltip has the node ID (8 hex digits), the port, the
  contacts, the class per family and the router duty. Hidden while the node is off; it has no
  MFC original.
- **Bandwidth** (socket-modes.md "Mode A in eMuleQt"). ed3k rides on eD2K's share of
  `BandwidthCoordinator`:
  - `Ed3kSession::setLimits` (→ `ed3k_set_limits`, raised to its 16 KiB/s floor) gets the
    whole eD2K share each tick (0 = none); it is kept across restarts. At `stop()` the
    coordinator hands it the global line.
  - Every 500 ms the coordinator reads `traffic()` (`wire_sent` / `wire_recv` of
    `ed3k_stats`, summed over restarts). The sent bytes go to
    `UploadBandwidthThrottler::noteBytesSentOutsideTheLoop` (in the upload rate, off eD2K's
    budget). The received rate joins eD2K's download demand, and eD2K's download budget drops
    by it.

## Usenet join

The host side of `libs/packmule/docs/usenet-join.md` and `usenet-join-host.md`. `DaemonApp`
creates `Ed3kUsenetJoin` with the ed3k session and a `UsenetJoinQueue` adapter of the usenet
queue (`Ed3kUsenetJoin::queueAdapter`), before the node starts. It attaches on
`Ed3kSession::statusChanged` (running) and detaches before the node stops.

- **One join per queue item** that is not complete. Members are the files of `NzbInfo::files`,
  so member index = file index.
  - NZB buffer: per `<segment>` in NZB order (`nzbOrderSegments()`), `numberText` (or
    `QString::number(number)` for an old sidecar) and `messageId` as mᵢ, a voided ID as empty.
  - The root is `<meta type="ed3k">` parsed as an `ed3k:file` link, else none (§4.2).
  - The join is saved in `Config/ed3k/usenet/<itemId>.join`: 5 s after a change, and at stop.
    It is dropped when the item finishes or is removed. A blob that does not load is replaced
    by a new join.
- **Articles** (`UsenetQueue::articleDecoded`):
  - mᵢ is the first segment of the part number in NZB order.
  - `ybegin` is called when the CRC is ok; then `article`.
  - Reads are served from the file's current path (`tempPath`, or `finalPath` once published),
    on the session's read pool (`readInBackground`), one 256 KiB block at a time. A failed read
    is reported with `readFailed` (torrent join too: a member file gone fails the pieces waiting
    on that block).
  - The server is a stable tag: the first 8 bytes of SHA-256 of the account key. It never
    leaves the host.
- **Join events:**

  | Event | Host |
  |---|---|
  | `HAVE_CHANGED` | `fileHave`, or `fileOpen` with those pieces (served only where `servable`) |
  | `ACCEPT_RANGE` | `acceptExternalRange` with the union of the accepted ranges of that file (they come per block; an article across two blocks is only wholly inside their union) |
  | `REFETCH_RANGE` | `refetchRange`, excluding the account of the tag |
  | `MEMBER_COMPLETE` | hash the file on `QThreadPool` and post the result back; `member_hashed`. If it moved while hashed, it is hashed again |
  | `MEMBER_VERIFIED` | `fileOpen` (complete, own block values), `publish` (`Both`, or `Source` when holding back), `member_alias` → `publishAlias` |
  | `MEMBER_REJECTED`, `LINK_MISMATCH` | `logEd3kWarning`, and a localized line in the Usenet log (`memberProblem`) |
  | `CLAIM_FALSE` | `UsenetSearch` ban + blame; `aliasBan`; close that download and share; `refetchRange` of each `drop` range with no exclusion |
  | `POST_COMPLETE` | `complete`: manifest and article list shared (`keepBlob`: from memory, kept for a restart) with `SOURCE`, `setPublish`, `publishAlias` of the `ALIAS` rows |

- **Sealing:** `UsenetQueue::setSealHold`. The queue asks before sealing a file whose
  articles all resolved. The join holds it while the member has a root, an ed3k download is
  open, and `missing` is not empty, for at most 10 minutes. `releaseSealHold` re-asks. It is
  called after `ACCEPT_RANGE`, after each article, after hashing, and when the hold times out.
  Without a hook, nothing changes.
- **PAR2:** `UsenetQueue::filesRepaired(itemId, files)` is emitted after a successful repair,
  before consumed volumes are deleted. It lists the files of which no article could be
  fetched. Each one gets `member_repaired`, and its reads are served. A `MEMBER_COMPLETE`
  then, and every hash still running on a worker, is hashed on the daemon thread in the slot
  (`hashNow`): the volumes are gone after it.
- **ed3k side:**
  - A member with a root and missing bytes gets `downloadOpen` and a Kad `searchSources`.
    Found sources get `fileRequest` (at most 4 streams).
  - With a slot and block values (`canVerify`), the 256 KiB blocks that overlap `missing` are
    asked for (`rangesHint`, at most 16).
  - `event::Block` is written into `tempPath` (never into a sealed file) → `ed3k_block`.
  - `event::Hashes` → `pending_reads`.
  - `WantBlock`/`VerifyBlock` of a shared member are answered from the file, read on a
    worker. `FileRequested`: see "Upload queue".
- **Post set (§4.1, §4.2 step 2):** the manifest is fetched into memory (`fetchBlob`), then
  the article list it names (`ed3k::usenetSetList`: `usenet.list`, `list_size`; above 32 MiB
  not fetched), then `setManifest(manifest, list, publishers)`. §4.1: the root and size of
  `<meta type="ed3k">`, fetched when the join is made; a set that does not do is told
  (`memberProblem`), nobody blamed. §4.2: the base's set search, `aliasSearchStart("nzb",
  postId, ED3K_REL_SET)`; its candidates (at most 3) are tried in turn, a refused one blamed
  (`aliasSearchBlame`) and the next tried. Fetched sets are kept and shared (not while
  holding back, §6). `trySet` and `addBlobSource` do it by hand (tests).
- **Claims (§4.2):** only when `nzb` gave no set: `nzb-file` alias searches
  (`aliasSearchStart(..., ED3K_REL_SAME)`, at most 4 per post) of the members' file ids →
  `fileClaim`. A refused claim is blamed (`aliasSearchBlame`).
- **Restart:** verified members, the post set made (`POST_COMPLETE`) or fetched are kept in
  `Config/ed3k/usenet/<item>/` and shared again at `start()` while the item is in the queue;
  members are hashed again first (`hashInBackground`), an unreadable one again after 10 min.
  A member complete but not hashed (unreadable then) is hashed again when its join loads and
  after 10 min. The set (`setPublish`) and the `ALIAS` records kept at completion
  (`keepRecords`) are published again once every kept member had its first hash (see below).

ToDo (usenet join):
- was: the `nzb` post-id search and §4.1 (`<meta type="ed3k">`) (done: post set above).
- was: search sources again when every stream closed (done: `Ed3kJoinBase`, both joins).
- was: re-share verified members and the post's manifest and list after a restart (done:
  kept, see Restart).
- was: archive volumes deleted right after `filesRepaired` (done: hashed in the slot).
- was: the `ALIAS` (`nzb`, `nzb-file`) and `MEMBER` records of a kept post are not published
  again after a restart (done: `keepRecords`, see Records again).
- was: `articleDecoded` gives the part number, not mᵢ: a number listed twice with two IDs is
  reported under the first ID (done: the queue hands the message ID; an article fetched under
  a second ID of its number is not reported).
- was: a preference for holding posts back (§6) (done: `shareUsenetPosts`), and an ed3k upload
  queue with slots (done: see "Upload queue").
- was: block reads on the daemon thread (done: `readInBackground`, both joins and serving).

## Torrent join

Contract: `libs/packmule/docs/torrent-join.md` (Part C, the host loop); spec
`docs/ed3k-spec/extensions/torrent.md` §4. `DaemonApp` creates `Ed3kTorrentJoin` after both
the ed3k and the BitTorrent session, starts it at once when the node runs already, and stops
it before either session.

- **One join per torrent with metadata** (`torrentAdded`, `metadataReceived`). Members are
  the files without padding, in order. No join for a private torrent (`DENIED`), with the
  extension off, or for a torrent complete before ed3k saw it.
- **Piece checks:** the join attaches a `BtJoinSink` (`BtSession::attachJoin`). libtorrent's
  hash job hands over the piece's bytes (the door reads them through the inner disk I/O) on
  its network thread; the sink hops to the daemon thread through a gate the join closes in
  its destructor. `pieceHashed` → reads served from the member files → `PASS`/`UNKNOWN`
  pass, `FAIL` spoils, `PENDING` and held pieces wait for `PIECE_VERDICT`. A held piece
  raises the pieces it waits for (`set_piece_deadline`) and fails after 60 s
  (`pieceRelease`). libtorrent's verdicts (`piece_finished_alert`, `hash_failed_alert`) go
  to `pieceResult`.
- **§4.1:** `ed3k:file?…&bt=` links and magnets with `xs` are added as magnets of the
  infohash; `BtSession` keeps the link (`BT/ed3k-links.json`). The join fetches the link's
  manifest into memory, then `setManifest`.
- **§4.2:** `btih`/`btmh` ALIAS searches with relation SET (`Ed3kJoinBase::startSetSearch`,
  shared with the usenet join; `aliasSearchStart`); the engine ranks the claims. Each
  candidate's manifest is fetched, refused ones and `CLAIM_FALSE` are blamed
  (`refuseSet` → `aliasSearchBlame`, `aliasBan`), at most 3 tried; a second search with roots
  (hybrid) is tried after the first. packmule's `SetSearch` is not used: the engine's alias
  search does the same and does the Kad lookups.
- **ed3k data:** members with a root get ed3k downloads; blocks are written into the member
  files, `ed3kBlock` names the pieces they complete, those are read (`extents`, zeros
  between) and handed to libtorrent (`add_piece`), which hashes them as from a peer.
  Blocks whose pieces libtorrent has already (`havePieces` at the start of the downloads and
  after its resume check, `torrentChecked`; then its verdicts) are not asked for, and a member
  libtorrent has whole gets no ed3k download.
- **Completion:** `TORRENT_COMPLETE` waits until every member is hashed (`complete()` needs
  it), then shares the manifest and the info dictionary from memory, `setPublish` and
  `publishAlias` (not while holding back). Verified members are shared and published at once.
- **Restart:** the manifests fetched or made, the info dictionary and the verified members are
  kept in `Config/ed3k/torrent/<id>/` and shared again at `start()` while the torrent is in
  the session (members hashed again first). A member whose hash failed is hashed again after
  10 min and when its join loads. The set and its `ALIAS` records (btih, btmh) are published
  again (see Records again).
- Tests disable Kad searches (`setSearchEnabled(false)`) and add sources by hand
  (`addSource`, `addManifestSource`).

ToDo (torrent join):
- was: skip the blocks libtorrent has already when asking ed3k for a member (done: `have`).
- was: re-share the manifest and info dictionary after a restart (done: kept).
- was: hash a member again when its file was unreadable (done: after 10 min, and at load).
- was: search sources again when every stream closed (done: both joins, below).
- was: the `ALIAS` and `MEMBER` records of a kept torrent are not published again after a
  restart (done: `keepRecords`).

**Sources again (both joins, `Ed3kJoinBase`):** a download whose last stream closed searches
its Kad sources again (at most once a minute), and every 10 min while no stream holds a slot;
nodes asked before are asked again once none streams (`setSourceResearch` for tests).

**Records again (both joins, `Ed3kJoinBase`):** the engine stores `ALIAS` records again every
22 h only while it runs, so completion keeps them (`keepRecords`: the set's manifest root and
name, the `Alias` messages as `complete()` gave them; a Usenet member's `nzb-file` record is
added when it verifies). At `start()`, `shareKept` shares the kept blobs, hashes the kept
members again and, once each had its first hash (shared, changed or unreadable), calls
`setPublish` with the kept manifest and `publishAlias` (not while holding back; the roots
must be shared first, else `ED3K_ERR_STATE`). Signal `keptRecordsPublished(owner, aliases)`.

## Shared files

`Ed3kShared` (derived from `Ed3kJoinBase`, owner `shared`, member = a stable index per file)
serves the complete files of eMule's `SharedFileList` over ed3k. `DaemonApp` creates it with
the session and `Ed3kShared::listAdapter` (a `SharedFileSource`; tests pass a fake), before the
node starts; it starts on `statusChanged` (running) when `ed3kShareFiles`, and stops before the
node stops. `applyEd3kConfig` switches it with the pref.

- **Files:** every complete file of the list (no part files). `fileAdded`, `fileRemoved`
  (direct), `fileRelocated`, and `fileChanged` / `DownloadQueue::fileCompleted` (a completed
  download stays the same object, so no `fileAdded`).
- **Hash cache** `Config/ed3k/shared.dat` (versioned `QDataStream`, `writeBlob`: `QSaveFile`,
  owner-only): per MD4 the path, size, mtime (ms), root and block values. A file whose path,
  size and mtime match is shared at once. Otherwise it is hashed on a worker
  (`hashInBackground`), **one file at a time**, and dropped when it changed while hashed. An
  entry of a file not listed is kept while its path exists (the list may load later).
- **Sharing:** `shareMember` with `SharedFile{root, size, name, ed2k = MD4, no note, complete,
  block values}`, `publish(Both)` (keywords too, so ed3k search finds it; `Source` while
  holding back), and an `ed2k` ALIAS record (alias.md §2: id = MD4 ‖ size
  as 8 B big endian, relation `SAME`; `Ed3kShared::ed2kAlias` encodes the `Alias` message, a
  name over 255 B is left out) through `publishAlias`, not while holding back.
- **Serving:** the base answers `WantBlock`/`VerifyBlock` (`sharedRead` names the file; the
  worker reads it only while size and mtime are what was hashed). `FileRequested`: see "Upload
  queue"; each file's upload priority (`KnownFile::upPriority`, read again once a minute: no
  signal, auto priority moves) goes to the engine. A file that changed or went away is closed by the base (`sharedGone`),
  then dropped and taken again from the list (hashed again).
- **Removal:** `unpublishAlias`, `unpublish` (as published), `unshare`; the cache entry goes too. A
  move with the same size and mtime keeps the root (reads follow the new path); else the file
  is hashed again. At stop every file is unpublished and unshared, the cache saved.

- **Unreadable:** a file whose hash failed is hashed again after 10 min (`kRehashMs`, at most
  `kRehashTries` times; `setRehashDelay` for tests), and at the next start.

ToDo (shared files):
- was: keywords (`Publish::Both`) for files the user wants found over ed3k search (done: every
  shared file, not while holding back).
- was: hash again a file that could not be read (done: after 10 min).

## LAN discovery

`Ed3kLan` (socket-modes.md "Implementer note (LAN discovery)"), made by `Ed3kSession::start`
while `ed3k.lan` and the extension are on (else `lanEnable(false)`, which ends LAN's first
round at once, bootstrap.md §1). `Ed3kSettings::lan` is off by default: the daemon sets it.

- Two `QUdpSocket`s on port 6533 (`AnyIPv4` / `AnyIPv6`, `ShareAddress | ReuseAddressHint`),
  TTL 1, loopback on; joined to `239.255.101.51` / `ff02::6533:6b` on every interface that is
  up, running, multicast, not loopback or point-to-point, not `awdl*`/`llw*`, with a private,
  link-local or unique-local address, and (with `bindAddress`) only the bound one.
- `lanInterface(index, port, nets)` per interface, looked at again every minute
  (`lanInterfaceDown`, groups left). Arrivals → `lanDatagramIn` with
  `QNetworkDatagram::interfaceIndex()`; after each pump `drain()` sends `lanDatagramOut` on the
  interface named, hop limit 1 (IPv4: `setMulticastInterface` first).
- No socket bindable: LAN off (logged). Tests pass `Ed3kLan::Hooks` (interfaces and sends by
  hand, no sockets): the core leaves loopback out, and macOS needs the Local Network permission
  for a real interface.
- ToDo: §4 per network (Windows Network List Manager category); the pref is on/off for all.
  `LanSources` (open file streams to a LAN peer) is not used by the joins yet.

## Node lists

`Ed3kBootstrap` (bootstrap.md §3, host-io.md "Node lists"), made by `DaemonApp`.

- At each start of the node: `bootstrapConfigure(sources of ed3k.nodeLists, autoFetch on,
  last start from bootstrap.dat)`. The fetches are the engine's HTTP requests (`Ed3kHttp`).
- An `ed3k:nodes` link the user opens (GUI `ExternalLinkHandler` → IPC `OpenEd3kNodesLink`
  1030 → `bootstrapLink`) is never used before a yes: `BootstrapAsk` → push
  `PushEd3kBootstrapAsk` 1080 `[ask, host, url]` to the GUI, which asks (localized
  `QMessageBox`, the host shown) and answers `AnswerEd3kBootstrapAsk` 1031 `[ask, yes]` →
  `bootstrapAnswer`. No GUI connected (`IpcServer::hasReadyClient`): refused at once; not
  answered in 5 min: refused.
- `BootstrapAdded` → the link is added to `ed3k.nodeLists` (saved).

## Upload queue

packmule's own queue policy (`ed3k_queue_auto`, `libs/packmule/docs/ffi.md`), switched on by
`Ed3kSession::start` for the main node. The engine answers `FileRequested` itself
(`autoAnswered`; the base answers only without it), ranks by eMule's score (waiting time ×
priority VeryLow 2 / Low 6 / Normal 7 / High 9 / Release 18, +50 000 while less than half the
file went out), takes due turns (16 MiB / 1 h) when someone waits, offers free slots to
unconnected nodes, and acts on the credit extension's slot hints itself. LAN peers get a slot
outside the count.

- **Priorities:** `Ed3kJoinBase::uploadPriority(Shared)`, Normal for join members;
  `Ed3kShared` takes the file's (`SharedEntry::priority`); `updateUploadPriority`.
- **Slots, eD2K and ed3k together:** `BandwidthCoordinator::applyEd3kSlots` each tick:
  ed3k's slots = ⌈ed3k's upload rate / 25 KiB/s⌉, 2 to 20 (`Ed3kSession::setUploadSlots`), and
  the slots it holds (`uploadQueueStats().holders`) go to `UploadQueue::setForeignSlots`, which
  counts them in `acceptNewClient` and the slot ladder (not in eD2K's own minimum).
- was: No GUI yet (ToDo below) (done: `Ed3kUploads`, below); the policy's grants and takes are in the ed3k Verbose log.
- **GUI** (`Ed3kUploads`, `Ed3kUploadsBridge`): the queue events are observed, never handled
  (`FileRequested` stays the joins' and the policy's): a request makes a waiting row (node,
  root, the file's name and size from `Ed3kShared::entryOfRoot`, since, LAN), `SlotUsed` or an
  accepted `SlotOfferResult` a holder (the bytes of the turn, due), `SlotEnded` /
  `QueueEntryEnded` drop it. `GetUploads` appends them to `uploading` / `waiting` with
  `network: "ed3k"`; Transfers → Uploading / On Queue show them with "(ed3k)" after the name
  and the place by waiting time in the Score column; the eD2K actions (details, friends,
  message, shared files, unban) are not offered, only "Copy ed3k Node ID" and Find.
  was: ToDo: the engine's rank and the node's address (no FFI call lists the queue) (done: the
  rows come from `Engine::queueList` while the node runs: the engine's rank as the place, the
  endpoint (`addr`, `port`; empty with relays only), since when, the turn's bytes, `isConnected`;
  the events add the name, LAN and due).

## Credits

`Ed3kCredit` (credit.md), owned by `DaemonApp`, configured on `applyEd3kConfig` and at each
node start:

- **Off by default** (§9) and no default chain or contract (§7.1): `creditConfigure` only with
  `enabled`, a chain ID and a contract. Without ledger endpoints (`ed3k.creditRpc`) the node
  signs and keeps receipts and sells nothing (§7.3); each endpoint URL is one operator class,
  `quorum` of them must agree.
- `creditConfigure` drops the receipts held, so it runs again only when the key, the chain, the
  contract, the endpoint count or the quorum change; `creditSell` / `creditBid` follow the
  prefs. A bid (`bidMaxPrice`) goes to every root of a download seen in a `FileStatus`.
- **The key** (§2): `credit.key`, 32 bytes from OpenSSL's CSPRNG that `ed3k_credit_address`
  takes (else drawn again), made owner-only through `Ed3kStore` (`loadOrCreateSecret`,
  `replaceSecret`), a group/other-readable file is made owner-only again; wiped after each
  use, never logged. IPC `NewEd3kCreditKey` (1020), `ImportEd3kCreditKey` (1021, 64 hex
  digits), `ExportEd3kCreditKey` (1022) only for a local GUI (`IpcClientHandler`: 403 for a
  remote one).
- **Slot hints:** the engine's queue policy grants a `CreditSlotHint` itself (ffi.md "The queue
  policy"); `Ed3kCredit` only counts them (no `creditSlotGrant`).
- **Status:** balance (`CreditBalance`), mature / overdrawn, the last transaction, receipts
  signed and taken, priority turns, endpoints on another chain, the stake rate
  (`creditStakeParams` after configuring) and the claims of burns.
- **Stake (§6.7):** `burnForStake` (`creditBurnForStake`, the main key only: anon false, §9.1;
  no anonymous credit address exists here), `claimStake` (`creditClaimStake`), `refreshClaims`
  (`creditStakeClaim` of every claim not paid, also at each configure). `CreditStakeBurned`
  adds a claim, `CreditStakeClaimed` and a read with `to` = 0 mark it paid, a failed or
  reverted claim transaction sends it back to waiting; kept in `stake-claims.json`, loaded at
  start, kept over a new key (anyone may claim). IPC `BurnEd3kStake` (1023, µ and `to`),
  `ClaimEd3kStake` (1024), `GetEd3kStakeClaims` (1025), `RefreshEd3kStakeClaims` (1026);
  pushes `PushEd3kStakeClaims` (1076, a state push) and `PushEd3kStakeTx` (1077).
- **Typed ids and restarts** (batch 12 #9, #10, §9.1): another claim id only after
  `stakeClaims(id)`: `readClaim` (`ReadEd3kStakeClaim` 1027) → `stakeLookup` →
  `PushEd3kStakeLookup` (1078: found, failed, to, amount, readyAt, kept) → shown by "Claim id…"
  → `ClaimEd3kStake`, then kept like an own one. A `to` that is the anonymous address is
  refused by the core. The burn's hash (its `CreditTx` before `CreditStakeBurned`) and the
  claim's (the next `CreditTx` `SENT` of a claim, in the order sent) are kept as `burnTx` /
  `claimTx` (also from `CreditStakeBurned::tx` / `CreditStakeClaimed::tx`). The core claims
  only an own burn of this run or an id read since (else `ED3K_ERR_STATE`): a kept claim not
  read in this run is read first and claimed on the answer. A claim still sent (state 1) at a
  stop is read again at the next configure and claimed again while unpaid and ready; a paid one
  reads `to` = 0 (`claimed`).
- **Chain refusals** (batch 12 #6): `ChainFailed` / `ChainTxFailed` with `ED3K_CHAIN_REFUSED`
  (15, the same JSON-RPC error at every provider, the node's text in `message`) or
  `_BAD_REQUEST` (16, the forwarder's `verify` false) are logged by `Ed3kChain` and shown as
  plain text under the chaintube endpoints (GetEd3kStatus `chain`: `configured`,
  `singleRemote`, `refused`; a `PushEd3kStatus` follows).

ToDo (chain): eMuleQt hands no site-sent forwarder request to a site (no `ChainTxWanted`
handling here; chaintube writes are the separate web project's). Whoever does must call
`chainTxVerify` with the signed request first and hand it over only on `ChainTxVerified`
(chaintube.md §4.4, §10).
- **GUI:** Options → ed3k → Credits: the switch (ticking it shows the notes of §9, §9.1, §7.3
  and §10 and asks), the ledger, bid and sale, the key (new, import, export; disabled for a
  remote core), the account and the stake group (`Ed3kStakeGroup`: the rate, "Burn for stake…"
  with the amount, `to` = the own credit address or one entered, and the §6.7/§9 notice that a
  burn cannot be undone and is public, asked twice; the claims with "Claim" once ready, the tx hashes as tooltip; "Claim id…" for any id, read and shown first). Options → ed3k → General / Connection hold the other ed3k
  prefs, Names the pins, petnames and followed lists (EN's IPC). A friend's name list
  (`PushEd3kNameFriendList`) is offered by `Ed3kNameOpener` in a non-modal box.

ToDo (credits):
- was: Burning for stake and claiming (`creditBurnForStake`, `creditClaimStake`) from the GUI, and
  the claims kept across restarts (only counted in memory now) (done: "Stake" above,
  `stake-claims.json`, `Ed3kStakeGroup`).
- The anonymous credit key (`creditAnonKey`, §9.1) once the app hides (`anonStart`).
- An owner-only DACL for `credit.key` on Windows (as `node.key`).

## Downloads and search

**Search** (Protocol.md §10.3): the Search panel's method "ed3k" is `SearchType::Ed3k` (9).
`SearchStarter` sends it like a Meta search (`awaitsMeta`, no wait reason) to
`theApp.metaSearch`; `MetaSearchService` hands it to its ed3k runner (`setEd3kRunner`,
`Ed3kSearch`, made by `DaemonApp`). `Ed3kSearch` calls `Engine::search(expression, Filter{ext,
minSize, maxSize, minSources = availability})`; the `KadFound` of that lookup (others' lookups
are left alone) becomes `SearchFile` rows (`addMetaSearchResult`): name, size, sources,
complete = sources − partial, length/bitrate tags, hash = the root's first 16 bytes, and
`SearchFile::setEd3k(root, ed2k)`. `GetSearchResults` shows them as `metaKind` 7 with
`ed3kRoot` (`ed3kEd2k` when the publisher named one). One answer ends the search; none after
90 s ends it too. The GUI marks the rows with the Kad icon (no ed3k icon, no MFC original) and
downloads them with `AddEd3kDownload`.

**Downloads:** `Ed3kDownloads` (owner `dl`, member = a stable id per download; made by
`DaemonApp` after `Ed3kShared`):

- `<temp>/<root hex>.ed3k.part` (first temp dir) while it runs: the plain file, each verified
  block written at its offset (sparse). Progress beside it in `<root hex>.ed3k.part.met`
  (`QDataStream` Qt_6_0, magic `E3PM`, v1: root, size, name, owner, category, priority, state,
  target kind + target, block bitmap, added, error), 5 s after a change and at stop. No absolute
  paths: the part file is the one beside the `.met`; a target under the incoming dir is stored
  relative. Every temp dir is scanned (`setDirs`, `start`): the two files copied into another
  machine's temp dir are that download there. A target folder that cannot be made → incoming dir;
  a set member of a set not followed there → plain download (`Ed3kSets::releaseOrphan`).
  `Config/ed3k/downloads.dat` (v2) keeps only the complete rows; a v1 file (every row) is
  migrated to `.met` files at the first `setDirs`. Pieces (4 MiB) held are shared from the part
  file once the block values came. Hoster downloads do the same with
  `<key>.hoster.part(.met)` (JSON; `docs/hoster.md` "Files").
- Complete: hashed on a worker (root and size must agree, else downloaded again), moved to the
  target (the category's incoming dir, else `incomingDir`; a free name when taken), shared
  (`Publish::Both`, `Source` while holding back) and kept (`keepMember`). `DaemonApp`'s hook
  adds it to `SharedFileList` (`addFileInSharedLocation`); once `Ed3kShared` serves the root
  (`fileShared`) the own share is forgotten without a close (`releaseShare`), so nothing is
  shared twice.
- Pause (streams and the download closed, part file kept), resume, cancel (part file deleted; a
  complete row only leaves the list), priority (was: kept and shown only, ToDo (done: see
  Priority below)), category, clear
  completed. Restart: complete rows are loaded at construction, unfinished ones from the temp
  dirs; at start the downloads run again from their part files and the complete ones are shared
  again.
- IPC (1140-1143, push 1180; 1100-1109 / 1150-1154 were taken by the hoster links):
  `GetEd3kDownloads`, `AddEd3kDownload` (root, size, name, category, paused),
  `AddEd3kDownloadLink` (`ed3k:file` without `bt=`, `parseFileLink`), `Ed3kDownloadControl`;
  `PushEd3kDownloads` (a state push, at most once a second). A row's bar: `blocks` (the bitmap
  of held 256 KiB blocks as `BlockRuns`: LEB128 run lengths, set first), `blockSize`,
  `pending` (blocks asked for, while running), `avail` + `pieceSize` (a byte per 4 MiB piece:
  open sources holding it, from their `FileStatus.have`; only once one told it).
- **Priority** (`Ed3kJoinBase`, every join; weights in eD2K's order, `realPriority`: very low
  1 … very high 5): at a source shared by several downloads (one file stream per uploader,
  §12.5) the highest waiter goes first, and a waiter above the holder takes the node at once
  (the holder's stream is replaced, its queue position stays; it waits again). While queued (no
  slot) that takes the node at most once per 30 min (`queuedSwitch`): §12.5 counts a switch in
  the uploader's 30 min budget, and a 2nd one is `BUSY` with the old stream kept. Outstanding
  ranges per stream: the engine's `rangesHint` for every granted stream of the session, shared
  by weight (one download alone keeps the hint). Streams opened per download: 1, 2, 4
  (normal), 5, 6. `setPriority` re-orders the waits and the depth at once (`priorityChanged`).
- GUI: `TransferPanel` merges the ed3k rows (`DownloadRow::network` 1, hash = root hex) after the
  eD2K ones (`Ed3kDownloadRows`), marked "(ed3k)" in the name column; the progress cell is
  eD2K's bar (`paintDownloadBar`): held dark, missing red / blue by sources per 4 MiB piece
  (`DownloadBarData::freqUnit`), asked yellow, the green strip on top;
  pause/stop/resume/cancel/priority/category/clear
  go to `Ed3kDownloadControl`, open file/folder use the row's path (local GUI); the eD2K-only
  menu entries are off for ed3k rows. `ExternalLinkHandler` sends `ed3k:file` links without
  `bt=` to `AddEd3kDownloadLink`.

## Sets and live

**Followed sets** (`Ed3kSets`, DaemonApp after `Ed3kDownloads`; file-set.md §2.3, §5.3):

- **Follow** an `ed3k:set` link: with `id` → `setFollow` (the newest version held handed in
  after a restart, so only newer heads are fetched), with `s` or `paid=1` → `setFollowHidden`
  (with `s` the version held too)
  (the link is a credential: kept in `sets.json`, owner-only, never logged, not shown in the
  list), with `b3`+`sz` → `setFollowPinned` (chaintube.md §3.2). Followed again at each start.
- **Members are chosen here.** The engine follows with `SetPolicy::Ask` (manifest events
  only); `Ed3kSetManifest` reads the manifest (title, files, paths). Policy per set: *download*
  takes every new version at once, *ask* keeps it pending (`PushEd3kSetVersion`; the GUI shows
  a notice) until "Download Selected" (`ApplyEd3kSetVersion`). The selection is the members
  left out (paths or folders); new members are taken. Members go through `Ed3kDownloads`
  (owner `set:<id>`) to `<incoming>/<title>/<path>` (names made safe, never out of that
  folder, §2.2); size 0 is created; a file there with its size is not downloaded again; nothing
  is deleted or overwritten (§5.3). Members left out later stop their unfinished downloads.
  One member downloads at a time per set, in the manifest's order (§4.2; the core keeps one
  file stream per uploader, a second request to the same node replaces the first): members
  are added paused and resumed in turn (`queued` in `sets.json`); one the user resumes or
  cancels leaves the queue.
- **Inner sets and mounts** (`SetInner`; `SetMount` then the mounted set's `SetManifest`) are
  shown and downloaded as subfolders. **Open parts** (`SetPart`) are listed; "Download Parts"
  → `setPartDownload` → `SetMember` → a download at `local`; `SetMemberSources` are added.
- Only the `Set*` events of its own topics (and mounted ones) are taken: `Ed3kNames` follows
  website sets, the cores of name and filter lists follow theirs.

- **Mounts' new versions** under *ask* wait like a version (`pendingMounts` in `sets.json`,
  `PushEd3kSetVersion`; the dialog shows "a new version of the mounted folder(s) … is
  waiting") until "Download Selected" (`ApplyEd3kSetVersion`); the first version of a mount
  and every one under *download* are taken at once.
- **Paid sets** (subscription.md §8): a `paid=1` link (scope kind 3, the set key) and a
  catalogue whose manifest carries `FileSet.offer` (kind 2, author key and `FileSet.id`) are
  followed in `Ed3kPaid` (`Ed3kSets::setPaid`); the set's `paidScope` joins the two in the
  GUI. Keys arriving (`PAID_KEYS`) follow a hidden paid set without a version again so its
  record opens.

**Paid subscriptions** (`Ed3kPaid`, DaemonApp before `Ed3kSets`; subscription.md):

- `follow` → `paidFollow` (or `paidAnonFollow`); the offer (`paidSetOffer`) from a catalogue's
  manifest or the pass answer. Without an offer `paidWantKeys` asks pass holders (§6.2
  `KEY_NO_PASS` gives it to the core); on `PAID_OFFER` the host reads it (`paidOffer`).
- **Buying** (§9.1): a holder key per subscription (`paid/<scope>.holder`, never the node key),
  the pay link `<pay_url>?scope=…&holder=…` opened in the browser by the GUI, `pass_url`
  polled by the core (`ed3k_pass_poll`: 15 s for 30 min, then at each start until 7 days; the
  host resumes it with `resume` 1). `PASS` → `paidSetPass` (+ the offer of the answer),
  `paidWantKeys`, and the renewal (§9.2, `renew_until` = `until`: the core polls in the last
  48 h). `PASS_ENDED`: refused (410) or given up; for a renewal the pass runs to `until`.
- **The anonymous key** (§5.1): "Buy for Anonymous Key" → `ed3k_pass_poll_anon`; `DENIED` (and
  `PAID_ANON_WARN`) → `PushEd3kPaidAnonWarn` → the GUI tells the user the seller sees an
  address → `ConfirmEd3kPaidAnon` → `paidAnonConfirm`, `ED3K_STATE_PAID` saved
  (`Ed3kSession::savePaidState`), the poll goes ahead and the pay link opens. A resumed poll
  or a renewal of such a key asks the same way.
- **Share reports** (§5.3): on by default, the checkbox "Share reports" (`SetEd3kPassReports`
  → `ed3k_pass_set_reports`), kept in `paid.json`.
- At each start: follow, offer, pass set again; purchases polled again, renewals scheduled.

**Live broadcasting** (`Ed3kLiveBroadcast`; live.md §1, §4): "Broadcast..." in the Live Streams
tab (`BroadcastEd3kLive`: folder of the core's machine, title, private, codecs). The channel
key of a title is made once and kept (`live/`), `liveBroadcast`; the link (`ed3k:live?key=…
&id=…[&s=…]&dn=…`) is copied to the clipboard. The newest `.m3u8` of the folder (as OBS or
`ffmpeg -f hls -hls_segment_type fmp4 -hls_time 2 live.m3u8` write it) is read every 500 ms:
each new segment (`EXT-X-MEDIA-SEQUENCE` + index) → `livePushSegment` with its EXTINF in ms,
`gap` after `EXT-X-DISCONTINUITY`, a changed `EXT-X-MAP` or a playlist that started over, its
init data (≤ 256 KiB; segments ≤ 4 MiB, larger ones skipped). It begins at the newest segment.
"Stop" (`LeaveEd3kLive` of a broadcast) → `liveEnd`. The live list shows broadcasts with
`broadcast: true` ("On air", Copy Link).

**Live streams** (`Ed3kLive`, DaemonApp after `Ed3kSiteServer`; live.md): `WatchEd3kLive` →
`liveWatchLink`; the website server hands requests of `live-<stream hex>.localhost:<sitePort>`
to it (`Ed3kSiteServer::setHostHandler`, loopback only like the sites). A response is chunked:
`Content-Type` from `LiveChannel` (`video/mp4` before it), the init data, the last 4 segments,
then each new segment; it ends with `LiveEnded`/`LiveLeft` (410 after). States: joining,
playing, interrupted (`LiveInterrupted`), no peers (`LiveGone` took the last neighbour), ended.
`LeaveEd3kLive` → `liveLeave`. The GUI opens the URL with `QDesktopServices` (a local core only).
**Restarts** (live.md §2.3, batch 12 ruling #15): one fMP4 response can't carry a second `moov`.
A segment with `gap`, another `init` or a new broadcast (`start`) ends every begun response and
empties the window; the next request gets that segment's init data and the window. A browser
(`Accept: text/html` at `/`) gets a small player page whose `<video>` reconnects to `/stream`
when a response ends, until the stream answers 410; players get the stream at any path. The
last chunk is a box the container skips (`free`, a null TS packet, an EBML Void), never empty.

**IPC** (1110-1119, 1144, 1039; pushes 1155-1158; `Ed3kSetsBridge`): `GetEd3kSets` (a set also
has `pendingMounts`, `paidScope`), `FollowEd3kSet`,
`UnfollowEd3kSet`, `SetEd3kSetPolicy`, `SetEd3kSetSelection`, `ApplyEd3kSetVersion`,
`DownloadEd3kSetParts`, `WatchEd3kLive`, `LeaveEd3kLive`, `GetEd3kLive`, `GetEd3kSetSite` (a
link's website origin); `PushEd3kSetsChanged`, `PushEd3kLiveChanged` (state pushes),
`PushEd3kSetVersion`, `PushEd3kSetOffer` (a set opened as a website is none, name.md §6.1),
`BroadcastEd3kLive` (batch 13). Paid (1035-1038, pushes 1064-1066; `Ed3kPaidBridge`):
`GetEd3kPaid`, `BuyEd3kPass`, `ConfirmEd3kPaidAnon`, `SetEd3kPassReports`;
`PushEd3kPaidChanged`, `PushEd3kPaidAnonWarn`, `PushEd3kPassArrived`.

**GUI:** "ed3k Sets" (`Ed3kSetsDialog`, Transfers → download list menu "ed3k Sets..."):
the sets (name, author's short key, version, files, size, policy, state), the member tree with
check boxes, Follow Link…, Unfollow, the policy, Download Selected, Download Parts; for a
paid set the price, terms, the pass (until, renewal) in the info, "Buy Pass..." (the price,
terms, seller's host and devices asked first; Buy / Buy for Anonymous Key) and "Share reports";
a "Live Streams" tab (Watch Link…, Open, Stop, Broadcast…, Copy Link). `ExternalLinkHandler`: `ed3k:set` asks (Follow and
Download / Choose Files… / Open as Website / Cancel), `ed3k:live` is watched and opened.

ToDo (sets and live):
- was: Paid subscriptions: buying passes (subscription.md, `ed3k_paid_*`, `ed3k_pass_*`) is out
  of scope (done: `Ed3kPaid`, batch 13).
- Selling paid channels and sets (offers, seasons, horizon keys, signing passes:
  `ed3k_paid_publish*`, `_new_season`) is not offered; nor `ed3k:pass` links (§9.3 by hand,
  vouchers), nor a paid live channel (§7).
- The sealed members of a paid catalogue (§8.1 `epoch`) download sealed; they are not opened
  with the epoch key.
- was: Live broadcasting (`liveBroadcast`, `livePushSegment`, the channel key) is out of scope
  (done: `Ed3kLiveBroadcast`, batch 13). Not resumed after a restart; no channel file (§1.2).
- was: A hidden set's version held is not handed in after a restart (done: the plain manifest
  with its sealed root is handed to `setFollowHidden`, packmule batch 13; a paid hidden set
  still takes none: its newest is fetched again, members there are not downloaded again).
- was: A mount's new version under *ask* is downloaded on the next "Download Selected" (done:
  it waits in `pendingMounts` and is offered, batch 13).
- §2.3 of live.md: a viewer that cannot play `media`/`codecs` should say so; the player decides.
- was: Qt warns "Chunk must have length > 0" when a live response ends (done: the last chunk is
  a `free` box / null TS packet / EBML Void, batch 13).

## Friends and chat

`Ed3kFriends` (DaemonApp, after `Ed3kShared`) takes the `Friend*` and `Chat*` events
(friend.md, chat.md) and answers the IPC of `Ed3kFriendsBridge`:

- **Requests** (1001-1007): `GetEd3kFriends` (friends with key, node, name, mutual, asking,
  heldLink, addr, lastSeen; and the requests waiting), `AddEd3kFriend` (link, request text),
  `RemoveEd3kFriend`, `AnswerEd3kFriendRequest` (accept/decline), `GetEd3kMyFriendLink`
  (with `r`, `dn` = nick), `SendEd3kChat` (node, text), `EndEd3kChat`. A failure is
  `[false, {error}]`: 0 no node, else `ed3k::Error`.
- **Pushes** (1051-1056): friends changed (a state push), chat message, sent, failed, a friend
  request, chat state (`ChatConnectProgress`: searching / found / failed).
- **Chat** is keyed by node ID. A send goes to the newest address known (`FriendFound`,
  `FriendSeen`), else to the node alone (an open connection is used). A friend's send that
  fails `Unreachable`/`Lost` is retried once after `friendFind`. Presence looks are on while the
  node runs.
- **Options** from eD2K's "Messages and Comments" (applied after every `SetPreferences`):
  "Accept from friends only" → `ChatPolicy::FriendsOnly` (also checked on arrival),
  "Advanced spam filter" → the 20 a minute rate limit (off: none), "Filter messages
  containing" → such messages dropped. Captchas and "secure" do not apply (the sender is an
  authenticated key, chat.md §2).
- **GUI**: the Messages panel shows both networks (`ChatNetwork`: `Ed2kChatNetwork` /
  `Ed3kChatNetwork`, one chat-tab code). ed3k rows are marked "(ed3k)" and keyed
  `ed3k:<node>`; "Add..." takes an `ed3k:friend` link; a request asks Accept / Decline / Later
  once per key; the friend list menu has "Copy my ed3k friend link".

## Rooms

`Ed3kRooms` (DaemonApp, after `Ed3kFriends`) takes the `Room*` events (room.md) and answers
the IPC of `Ed3kRoomsBridge`:

- **Requests** (1120-1129): `GetEd3kRooms` (rooms with link, title, about, nick, role, kind,
  state, own member key, mods, members), `GetEd3kRoomPosts` (the last 200 of a room),
  `CreateEd3kRoom` (a new room key and member key; the owner's member key is a moderator),
  `JoinEd3kRoom` (a link), `LeaveEd3kRoom`, `PostEd3kRoom`, `SetEd3kRoomNick`,
  `SetEd3kRoomInfo` (owner), `ModerateEd3kRoom` (admit → the admit text, ban (≤ 30 days),
  lift, new generation), `ImportEd3kRoomAdmit`. Rooms and post ids as hex, member keys as
  base32 (§1.1). A failure is `[false, {error}]`: 0 no node, else `ed3k::Error`.
- **Pushes** (1160-1164): rooms changed (a state push), a post, posts removed, a presence, a
  notice (joined, banned, generation, key missing, wrap bad, refused, fault).
- **Secrets**: the member key per room and the room key of an owned room are files of their own
  (see "Files"), handed in at every join; the joined rooms are joined again at start. A link
  with `s` is never logged.
- **Closed room** (batch 10 reading 1): the newcomer joins by the link and copies its member
  key; a moderator admits it (the admit text `ed3k-admit:<room base32>:<RoomItem base32>`, an
  app format); the newcomer imports it. Batch 13 (#17): a `RoomRefused` `ROOM_MEMBER` sets
  `notAdmitted` on the row (cleared by `RoomJoined`); the room tab then shows a bar with the
  own member key, "Copy" ("send this to a moderator") and "Import admit". A peer holding the
  admit takes the newcomer (§4.2), so importing is an extra: offered only for a room whose
  info says closed or moderated, or after `ROOM_MEMBER` (not for every room with `s`).
- **Reply** (batch 13): "Reply" on a post (right click in the log; each post's blocks carry its
  id) or "Reply to last post" in the nick menu; the next post goes with `replyTo`. A reply
  shows a quoted line of its post (60 characters), or "answer to a post not held".
- **Mute** (§6, batch 13): `MuteEd3kRoomMember` (1044) keeps the member IDs in
  `rooms/joined.cbor` (`muted`), listed in the row; the GUI draws none of their posts (the
  history keeps them; unmute draws them again). Nothing goes to the core.
- **Ban**: the history drops the member's posts on `RoomBanned` too (the core's own ban gives
  no `RoomPostsRemoved`).
- **GUI**: the IRC panel. `ChatChannelView` (log + topic pane) is the tab of an IRC channel,
  a private chat and, as `Ed3kRoomTab`, of a room: "title (ed3k)", the room info as the
  topic, members as `nick#abcd` (`@` a moderator) in the nick list, posts in the order of
  `sent`. A "Rooms (ed3k)" tab next to "Channels" (`Ed3kRoomList`): join by link, create,
  leave, copy link, copy my member key, import admit, admit a member key, new generation,
  room info, nick. The nick menu: copy member key, admit, ban, lift, make/remove moderator.
  `ed3k:room` links open the IRC panel and ask before joining.
- was: ToDo: replying to a post from the GUI (the IPC takes `replyTo`); muting a member locally (§6)
  (done: batch 13, "Reply" and "Mute" above).

## Names and websites

`Ed3kNames` (DaemonApp, after `Ed3kBootstrap`) and `Ed3kSiteServer` (libs/packmule/docs/ffi.md
"Names"; spec `extensions/name.md`):

- **DNS (§5.1):** `NameDns` → `QDnsLookup` TXT (the user's resolver), 10 s → `nameDnsAnswer`
  (each record's strings joined; NXDOMAIN: no record) or `nameDnsFailed`. `secure` is always
  false: Qt's AD bit comes from a resolver nobody here validates. Tests inject a resolver
  (`setDnsResolver`). `ed3k.json` (§5.2) is an ordinary `HttpRequest` (`Ed3kHttp`).
- **Name lists (§4.5):** `NameListFetch` (also list 0, a friend's list) → `fetchBlob` (at most
  4 MiB) → `nameListFile`. `NameListUpdated`/`_Refused` update the list rows; a
  `NameFriendList` is pushed to the GUI (offer to follow).
- **Resolving:** only what the user opened. `NameResolved` is kept until `open` (`nameOpen`,
  the pin); both go to the GUI (`PushEd3kNameResolved`/`_Failed`).
- **IPC** (1010-1019): `ResolveEd3kName`, `OpenEd3kName` (links, and `url` = the website of the
  first `ed3k:set` link), `GetEd3kNames` (pins, petnames, lists, `sitePort`), `SetEd3kPetname`,
  `RemoveEd3kPetname`, `UnpinEd3kName`, `VouchEd3kName`, `FollowEd3kNameList`,
  `UnfollowEd3kNameList`, `MoveEd3kNameList`; pushes 1060-1063 (`PushEd3kNamesChanged` a state
  push). The GUI's `IpcClient` has a method per request (`resolveEd3kName`, …).
- **GUI:** `ExternalLinkHandler` hands `ed3k:name` links to `Ed3kNameOpener`: the resolution is
  shown (how, warnings with both zones, ties; §4.4), asked first when `confirm` or DNS
  unverified or not pinned yet, then opened: a website in the browser, an `ed3k:friend` link
  added as a friend, others (`ed3k:file?…&bt=`) through `ExternalLinkHandler::open`.
- **Website server (§6.2, §6.3):** listens on 127.0.0.1 and ::1 at `ed3k.sitePort` while the
  node runs; nothing else is reachable on it, no CORS header. Per request: `siteHost`; `served`
  = a known kind on our port; `siteGate` (421, 405, 403). A readable host
  (`<name>.ed3k.localhost`, `<domain>.web.localhost`) is resolved for the user only and
  answered with a 302 to the set's origin; one that needs the user's yes is pushed to the GUI
  (`viaSite`) and answered 403. A set's origin: the newest manifest held (`setFollow` for the
  user; others wait only when it is followed) → `sitePage` → the member from memory, else
  fetched first (`fetchBlob`, at most 64 MiB, 256 MiB kept, the oldest go) and sent when in;
  504 after 60 s. `If-None-Match` of the `ETag` → 304. Members kept are shared from memory
  (§6.4).
- **A set not held** (§6.3, batch 11 ruling #9): `siteSetStatus(held, fetchingSince, now)`:
  a set fetched for the user waits for its manifest (asked again, at most `ED3K_SITE_SET_WAIT_S`
  = 60 s from the fetch's start, and no longer than the server's own wait), then 504; a set
  neither held nor fetched is 503 (was 404 for others' requests). A set that is no website
  (§6.1, `sitePage` NotFound) is answered 404 and, for the user's own request, offered for
  download (`setOffered` → `PushEd3kSetOffer` → the GUI asks, `Ed3kSets` follows).
- **Range requests** (§6.3, RFC 9110 §14): members say `Accept-Ranges: bytes`. One range of a
  GET (`bytes=a-b`, `a-`, `-n`) → 206 with `Content-Range`, from the member held or the blocks
  of its fetch in memory verified so far (`Ed3kNames::siteMemberRange`); not there yet: it waits
  (checked every 500 ms) within the same 60 s, then 504. Beyond the end → 416 (`bytes */size`).
  Several ranges, a malformed one, an `If-Range` other than the `ETag`, HEAD, a 404 page →
  the whole member (a server may ignore Range; no multipart here).
- **Kept across restarts** (§6.4): `Ed3kSiteCache` keeps each manifest (`s-<key hex>.<id
  hex>`) and member (`m-<root hex>`) in `Config/ed3k/site-cache/`, up to 2 GiB
  (`Ed3kNames::kSiteDiskMax`); the files used longest ago go first (LRU by modification time,
  touched on each use). A member read back is checked against its root (else removed) and put
  in memory, shared from there again. A manifest kept is served at once, followed for the
  user's request and named as held to `setFollow` (only newer heads are fetched).

ToDo (names and websites):
- was: Range requests from verified blocks, and sending a member as its blocks arrive (§6.3) (done: ranges, see
  "Range requests" above; still ToDo: a whole member is sent once all its blocks are in, not as they arrive).
- was: Keep website manifests and members across restarts, 2 GiB, longest-unviewed first (§6.4) (done:
  `Ed3kSiteCache`, see "Kept across restarts" above).
- was: A set that is no website: offer it for download in the GUI (§6.1) (done: `setOffered`, "Sets and live").
- was: the Names options page (pins, petnames, lists) uses the IpcClient calls above (done: Options → ed3k → Names, `Ed3kOptionsPages`).

## Filter lists

`Ed3kFilters` (filter-list.md; DaemonApp, derived from `Ed3kJoinBase`, owner `filter`):

- **Following (§2, §3):** only lists the user chose: `filterFollow` of an `ed3k:set` link
  (Options → ed3k → Filters, "Follow…"), `filterUnfollow`; a list file opened by hand
  (`filterOpen`, "Open list file…": no author, no updates, said in the row). The core keeps
  them in `filter.dat`.
- **List files:** `FilterFetch` → fetched into memory (`fetchBlob`, at most 4 MiB, §2; above:
  not fetched) → `filterFile`. `FilterUpdated` / `TooLarge` (the version before stays) /
  `Ignored` / `Changed` are logged and push `PushEd3kFiltersChanged`.
- **Mode (§3):** mark (the default), hide, or hide and refuse, per list and for the own list
  (`filterSetMode`, `ED3K_FILTER_OWN`).
- **Own verdicts:** `filterSetOwn` (None takes it back), mirrored in `filter-own.dat` for the
  list on the page.
- **`judge(root, ed2k, author)` → `Judgement`** (each optional; a FILE entry is the root's first
  16 bytes): `hide` / `refuse` as the core decided over every subject asked (modes, the own
  verdict, a "show anyway" choice), the verdict shown (the own one first, else the first list's,
  with its title or label and reason), `disagree` when lists disagree; nullopt when nothing
  judges it. For search rows (Ed3kSearch) and downloads: mark, hide only when the mode says so;
  nothing is deleted, no download stopped, no connection refused (§3).
- **IPC** (1130-1136): `GetEd3kFilters`, `FollowEd3kFilter`, `UnfollowEd3kFilter`,
  `SetEd3kFilterMode`, `SetEd3kFilterOwn`, `JudgeEd3kFilter`, `OpenEd3kFilterFile`; push
  `PushEd3kFiltersChanged` (1170, a state push). Batch 13: `PublishEd3kFilterOwn` (1042),
  `ShowEd3kFilterAnyway` (1043); `GetEd3kFilters` has `published` {link, title}.
- **Set author (#18, §1):** `judge(root, ed2k, publisher, setAuthor)`: DaemonApp passes the
  author key of a followed set whose applied or pending manifest has the row's root
  (`Ed3kSets::authorOfManifest`); the core judges it with the row's first entry
  (`filterJudge(subject, id, setAuthor)`, batch 13); either one marks the row.
- **Publishing (§2):** Options → ed3k → Filters, "Publish my list…" (a title):
  `filterSetOwnTitle` → `filterOwnWrite` → shared (`shareBlob`, `<title>.ed3kflt`) →
  `filterOwnManifest` (set id: 16 random bytes, kept) → shared → `setPublish` signed with a key
  of its own (`filter-own.key`, a 32-byte secret, owner-only; not the node key, so the list does
  not name the node). The link (`ed3k:set?key=…&id=…&dn=<title>`) is shown, copied to the
  clipboard. A new version goes out 3 s after a change of the own verdicts; the last one
  (`filter-own.ed3kflt`, `filter-own.ed3kset`) is shared and published again at start. "Stop
  publishing" forgets it (what is out expires).
- **Search rows (§3):** each ed3k row carries the judgement (`ed3kVerdict`, `ed3kHidden`,
  `ed3kOwn`, `ed3kShown`, `ed3kDisagree`, `ed3kBy`, `ed3kReason`, `ed3kAuthor` = the publisher)
  shown in the Confidence column and as a tooltip. A row a list hides is kept, hidden by the
  proxy; "ed3k Filter" in the result menu: "Mark File As" Fake/Spam/Harmful/Good/Clear and
  "Mark Publisher As" Spam/Clear (`SetEd3kFilterOwn`), "Show Anyway" / "Hide Again"
  (`ShowEd3kFilterAnyway`, the file's entry; over every list and the own verdict), "Show Rows
  Hidden by Filter Lists" (per tab). A change of verdicts, modes or a set's manifest judges the
  rows again (`Ed3kSearch::rejudge` → `SearchList::updateResults`, stamped for the GUI).

was: ToDo (filter lists): publishing the own list as a set (`filterOwnWrite`, `filterOwnManifest`,
`setPublish`), "show anyway" (`filterShowAnyway`) and a "mark as fake/spam/harmful/good" action
on ed3k search rows (done: batch 13, "Publishing", "Search rows" above).

## Out of scope (ToDo)

- was: GUI: the ed3k upload queue (slot holders, waiting) in the upload lists or Statistics (done: the
  Uploading / On Queue lists, "Upload queue"; Statistics keeps its counts).

- was: GUI: the ed3k graphs in the Statistics window (was: an ed3k branch in the tree; done: "Status and bandwidth")
  (done: `StatsGraphSample::ed3kDown`/`ed3kUp`, fields 17/18 of `GetStatsHistory` from
  `BandwidthCoordinator`'s wire rates, "ed3k" lines in the download and upload graphs, colours 18/19;
  a 17-field sample of an older core still reads; the toolbar adds ed3k's download).
- was: downloads other than the joins (done: plain files, "Downloads and search"; was: sets and live: ToDo (done: "Sets and live")).
- Paid subscriptions (buying passes, subscription.md) and live broadcasting: see "Sets and live".
- was: Chain configuration from preferences (`chainConfigure`); the RPC endpoints exist (`ed3k.chaintubeRpc`). (Credits: done, see "Credits".)
  (done: `ed3k.chain`, `Ed3kChain`, see "Preferences"). was: A chaintube GUI (videos, comments, votes) stays out of scope.
  (now: the chaintube GUI is a separate web project, not part of eMuleQt.)
- was: node list sources for the first bootstrap (`bootstrapConfigure`, bootstrap.md §3) and keeping `BootstrapStarted` (done: "Node lists").
- was: LAN discovery sockets of mode A (done: "LAN discovery").
- Bandwidth: a share of its own for ed3k (it uses eD2K's); upload slots of the throttler (`ed3k_grant_upload`) instead of `setLimits`.
- was: the stable IPv6 source address (`IPv6SourcePin`) as `local6`; the `bindAddress` pref for the socket (done: "The socket").
- was: one preference per optional extension (`Config::extOff`); nick; filter and hops state (done: `ed3k.extOff`, nick, `filter.dat`/`hops.dat`). (done: the options pages, Options → ed3k.)
- An owner-only DACL for `node.key` on Windows.
- `tests/vs` projects: every ed3k test has one (`Tests\Ed3k`, `tst_Ed3kTorrentJoin` in `Tests\BitTorrent`). A new test: add a `TEST_EXTRAS` entry in `scripts/generate_test_vcxprojs.py` if it needs more than the core libs and run it with `--only`, which leaves `specialdlp` and `emuleqt-mcp` in the `.sln` (a run without `--only` drops them).
