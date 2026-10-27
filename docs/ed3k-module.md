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
| `tests/tst_Ed3kShared.cpp` | the `ed2k` alias bytes, a file of a fake share list fetched by its root from a second node, the hash cache across a restart, move/remove/pref off |
| `tests/tst_Ed3kTorrentJoin.cpp` | real `BtSession`s and ed3k nodes on loopback: a completed torrent gives its set, ed3k alone fills a torrent, the join saved and loaded, a magnet with `xs` fetches its set (§4.1) |

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
| `udpPort` | `0` | 0 = a random free port, picked once at load (never the eD2K UDP or BT port) and saved; random listen ports pick it too |
| `creditRpc` | `[]` | IPC/REST `ed3kCreditRpc` (URLs separated by spaces): JSON-RPC endpoints of the credit ledger; empty = calls answered failed |
| `chaintubeRpc` | `[]` | IPC/REST `ed3kChaintubeRpc`: the same for chaintube's chain reads and writes |
| `shareFiles` | `true` | IPC/REST `ed3kShareFiles`: serve the shared files over ed3k (`Ed3kShared`); only while the node runs |

The port is mapped as `PortMapPurpose::Ed3kUdp` while `ed3kEnabled`.

## Files (`Config/ed3k/`)

| File | Slot | Written |
|---|---|---|
| `node.key` | 32-byte seed | once; 0600 on Unix (ToDo: owner-only DACL on Windows); never logged |
| `kad.dat` | `ED3K_STATE_KAD` | on `StateChanged` and at stop (only with contacts) |
| `bad.dat` | `ED3K_STATE_BAD` | at most once a minute after `StateChanged`, and at stop |
| `sets.dat` | `ED3K_STATE_SETS` | on `StateChanged` and at stop |
| `friend.dat` | friend state | on `FriendChanged` and at stop |
| `usenet/<item>.join` | a usenet join | debounced, and at stop |
| `torrent/<id>.join` | a torrent join (`:` of the id as `_`) | debounced, and at stop |
| `usenet/<item>/`, `torrent/<id>/` | kept for a restart (owner percent-encoded): `<root>.blob` per manifest, article list, info dictionary; `kept.tsv` names them, the verified members and the set published (`s` line); `records.bin` the `ALIAS` records (u32-LE length + `Alias` each) | when fetched, made or verified; dropped with the item or torrent |
| `shared.dat` | hash cache of the shared files (`Ed3kShared`) | 5 s after a change, and at stop |

Every blob is put back before the first datagram. `contactsLoaded()` is called when there is
no `kad.dat` or it does not parse. A blob that does not parse is dropped with a warning; it is
never a reason not to start. All blobs are written with `QSaveFile` and owner-only
permissions.

## Threading and the pump

The session follows `libs/packmule/docs/ffi.md` "The loop" and mode A of `socket-modes.md`.

- **One thread.** Everything runs on the daemon thread; the engine is not thread-safe.
- **The socket.** One `QUdpSocket` on its own port, dual stack (`QHostAddress::Any`, `local4 = 0.0.0.0:p`, `local6 = [::]:p`). It is not the eD2K socket. Datagrams that are not ed3k are counted and dropped.
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
  | `BootstrapAsk` | no |
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

Logging uses the category `emule.ed3k` (`logEd3k`, `logEd3kWarning`, `logEd3kError`,
`logEd3kDebug`). Its lines go to the main log, or to the verbose log for debug lines; it has
no file of its own yet.

## Status and bandwidth

- **IPC.** `GetEd3kStatus` (1000) → `Ed3kBridge::statusToCbor()`: `available`, `running`,
  `udpPort`, `nodeId`, `contacts`, `class4` / `class6` (0 unknown, 1 open, 2 cone,
  3 symmetric), `router`, `wireSent` / `wireRecv`, `rateUp` / `rateDown` (B/s, from
  `BandwidthCoordinator`), `limitUpKb` / `limitDownKb`, and `stats` (the rest of
  `ed3k_stats`). `PushEd3kStatus` (1050, a state push: a slow reader keeps the last) carries the same map on
  `Ed3kSession::statusChanged`. The block 1000-1049 is the ed3k one; without the module it
  answers `available: false`.
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
    on the daemon thread, one 256 KiB block at a time. A failed read is reported with `readFailed`.
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
  - `WantBlock`/`VerifyBlock` of a shared member are answered from the file. `FileRequested`
    gets a slot at once (ToDo: an upload queue).
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
- `articleDecoded` gives the part number, not mᵢ: a number listed twice with two IDs is
  reported under the first ID.
- A preference for holding posts back (§6), and an ed3k upload queue with slots.

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
  block values}`, `publish(Source)`, and an `ed2k` ALIAS record (alias.md §2: id = MD4 ‖ size
  as 8 B big endian, relation `SAME`; `Ed3kShared::ed2kAlias` encodes the `Alias` message, a
  name over 255 B is left out) through `publishAlias`, not while holding back.
- **Serving:** the base answers `WantBlock`/`VerifyBlock` (`readShared` reads the file, only
  while size and mtime are what was hashed) and gives every `FileRequested` a slot (the base's
  upload-queue ToDo). A file that changed or went away is closed by the base (`sharedGone`),
  then dropped and taken again from the list (hashed again).
- **Removal:** `unpublishAlias`, `unpublish(Source)`, `unshare`; the cache entry goes too. A
  move with the same size and mtime keeps the root (reads follow the new path); else the file
  is hashed again. At stop every file is unpublished and unshared, the cache saved.

ToDo (shared files):
- Keywords (`Publish::Both`) for files the user wants found over ed3k search.
- Hash again a file that could not be read.

## Out of scope (ToDo)

- GUI: an ed3k branch in the Statistics window and the graphs (the status bar label exists, see "Status and bandwidth").
- Downloads other than the joins (`Ed3kUsenetJoin`, `Ed3kTorrentJoin`).
- Chain and credit configuration from preferences (`chainConfigure`, `creditConfigure`); the RPC endpoints exist (`ed3k.creditRpc`, `ed3k.chaintubeRpc`).
- Node list sources for the first bootstrap (`bootstrapConfigure`, bootstrap.md §3) and keeping `BootstrapStarted`.
- LAN discovery sockets of mode A (socket-modes.md "Implementer note (LAN discovery)"): LAN is off for now.
- Bandwidth: a share of its own for ed3k (it uses eD2K's); upload slots of the throttler (`ed3k_grant_upload`) instead of `setLimits`.
- The stable IPv6 source address (`IPv6SourcePin`) as `local6`; the `bindAddress` pref for the socket.
- One preference per optional extension (`Config::extOff`); nick; filter and hops state.
- An owner-only DACL for `node.key` on Windows.
- A `tests/vs` project for `tst_Ed3kSession` and `tst_Ed3kBridge` (and the join tests); the generator was not run, since it would also drop `specialdlp` and `emuleqt-mcp` from the `.sln`.
