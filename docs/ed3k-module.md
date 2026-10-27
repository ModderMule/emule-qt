# ed3k module (packmule in the daemon)

First step of hosting the Rust ed3k library packmule (`libs/packmule`, a separate repo,
gitignored here) in `emulecored`. The node runs, keeps its key and state, and finds other
nodes over Kad. It shares nothing and downloads nothing yet.

## What exists

| File | What |
|---|---|
| `src/ed3k/Ed3kSession.{h,cpp}` | `eMule::ed3k::Ed3kSession` (QObject): the engine, its UDP socket, the clock and the one timer, the pump, key and state files, default answers |
| `src/ed3k/Ed3kHttp.{h,cpp}` | host IO (`libs/packmule/docs/host-io.md`): HTTP/1.1 on `QTcpSocket`/`QSslSocket` for `event::HttpRequest` |
| `src/ed3k/Ed3kStore.{h,cpp}` | `Config/ed3k/node.key` (create once, 0600, never replaced), atomic blobs, wiping |
| `tests/tst_Ed3kSession.cpp` | key file, two nodes on loopback, state across a restart, HTTP client |
| `src/ed3k/Ed3kUsenetJoin.{h,cpp}` | `eMule::ed3k::Ed3kUsenetJoin`: a Usenet download joined with ed3k (see "Usenet join" below) |
| `tests/tst_Ed3kUsenetJoin.cpp` | NZB buffer, server tag, a real queue + fake news server verified under a claim, blocks from a second node fill holes, a false claim |

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
- **MSBuild:** `EmuleEd3k` in `QtVersion.props`.
  - It is on when `libs\packmule\Cargo.toml` exists. Turn it off with `/p:EmuleEd3k=false`.
  - `src/ed3k/emuleed3k.vcxproj` runs `cargo build --release -p packmule-ffi` as a pre-build step, with `CARGO_TARGET_DIR=libs\packmule\target\emuleqt-msbuild`.
  - `emulecored.vcxproj` gets `$(EmuleEd3kLibs)`; `emulecore`/`emulecored` get `$(EmuleEd3kDefines)`.
  - The project is in `eMuleQt.sln` and in `scripts/generate_test_vcxprojs.py`. With the switch off, unload the project, as with `emulebt`.
- **qmake:** `src/ed3k/ed3k.pro` is a subdir of `eMuleQt.pro` when `libs/packmule` exists. Build `packmule-ffi` with cargo first (`target/release`).

## Preferences

The `ed3k:` block in `preferences.yml`, with IPC/REST keys `ed3kEnabled` and `ed3kUdpPort` (`PreferenceSchema`):

| Key | Default | |
|---|---|---|
| `enabled` | `false` | opt-in |
| `udpPort` | `0` | 0 = a random free port, picked once at load (never the eD2K UDP or BT port) and saved; random listen ports pick it too |

The port is mapped as `PortMapPurpose::Ed3kUdp` while `ed3kEnabled`.

## Files (`Config/ed3k/`)

| File | Slot | Written |
|---|---|---|
| `node.key` | 32-byte seed | once; 0600 on Unix (ToDo: owner-only DACL on Windows); never logged |
| `kad.dat` | `ED3K_STATE_KAD` | on `StateChanged` and at stop (only with contacts) |
| `bad.dat` | `ED3K_STATE_BAD` | at most once a minute after `StateChanged`, and at stop |
| `sets.dat` | `ED3K_STATE_SETS` | on `StateChanged` and at stop |
| `friend.dat` | friend state | on `FriendChanged` and at stop |

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
  | `WantBlock` / `VerifyBlock` | `blockFail` |
  | `RpcRequest` | `rpcFailed` |
  | `BootstrapAsk` | no |
  | unpack | failed / unsupported |

- **HTTP.** `HttpRequest` and `IoCancel` go to `Ed3kHttp`.
  - With `addr`, it connects exactly there; `Host`, SNI and the certificate check use the URL's name (`QSslSocket::connectToHostEncrypted(ip, port, name)`).
  - Without `addr`, the name is resolved here and each address is offered with `httpResolved`. `Denied` ends the request, and no other address is tried. A failed connect gives `httpUnreachable`, then the next address.
  - It follows no redirect (a 3xx is handed in as it came), decodes no `Content-Encoding` (chunked transfer is decoded), sends no cookies, reads at most `maxLen` bytes and stops after `timeoutS`.
  - It uses the user's proxy with the literal address.
- **A dead engine.** `ED3K_ERR_DEAD` frees the engine and logs it; ed3k stays off until the next start.

Logging uses the category `emule.ed3k` (`logEd3k`, `logEd3kWarning`, `logEd3kError`,
`logEd3kDebug`). Its lines go to the main log, or to the verbose log for debug lines; it has
no file of its own yet.

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
  | `POST_COMPLETE` | `complete`: manifest and article list shared (in memory) with `SOURCE`, `setPublish`, `publishAlias` of the `ALIAS` rows |

- **Sealing:** `UsenetQueue::setSealHold`. The queue asks before sealing a file whose
  articles all resolved. The join holds it while the member has a root, an ed3k download is
  open, and `missing` is not empty, for at most 10 minutes. `releaseSealHold` re-asks. It is
  called after `ACCEPT_RANGE`, after each article, after hashing, and when the hold times out.
  Without a hook, nothing changes.
- **PAR2:** `UsenetQueue::filesRepaired(itemId, files)` is emitted after a successful repair,
  before consumed volumes are deleted. It lists the files of which no article could be
  fetched. Each one gets `member_repaired`, and its reads are served.
- **ed3k side:**
  - A member with a root and missing bytes gets `downloadOpen` and a Kad `searchSources`.
    Found sources get `fileRequest` (at most 4 streams).
  - With a slot and block values (`canVerify`), the 256 KiB blocks that overlap `missing` are
    asked for (`rangesHint`, at most 16).
  - `event::Block` is written into `tempPath` (never into a sealed file) → `ed3k_block`.
  - `event::Hashes` → `pending_reads`.
  - `WantBlock`/`VerifyBlock` of a shared member are answered from the file. `FileRequested`
    gets a slot at once (ToDo: an upload queue).
- **Claims (§4.2):** `nzb-file` alias searches (`aliasSearchStart`, at most 4 per post) of
  the members' file ids. Candidates with relation `SAME` → `fileClaim`. A refused claim is
  blamed (`aliasSearchBlame`).

ToDo (usenet join):
- The `nzb` post-id search and §4.1 (`<meta type="ed3k">`): fetch the post set (manifest and
  article list) over ed3k, then `setManifest`. With a link, the core is only the BLAKE3 judge
  for now.
- Search sources again when every stream closed.
- Re-share verified members and the post's manifest and list after a restart (they need a
  hash again).
- Archive volumes are deleted right after `filesRepaired`, so their `MEMBER_COMPLETE` hash fails.
- `articleDecoded` gives the part number, not mᵢ: a number listed twice with two IDs is
  reported under the first ID.
- A preference for holding posts back (§6), and an ed3k upload queue with slots.

## Out of scope (ToDo)

- GUI and IPC status panel: running, port, node ID, contacts (the getters exist: `udpPort()`, `nodeIdHex()`, `contactCount()`, `statusChanged`).
- Serving eMule shared files over ed3k (`WantBlock` is refused).
- Downloads other than the usenet join (E2: `src/ed3k/Ed3kUsenetJoin`).
- Chain RPC (credit, chaintube): `RpcRequest` is answered failed, since no chain is configured.
- Node list sources for the first bootstrap (`bootstrapConfigure`, bootstrap.md §3) and keeping `BootstrapStarted`.
- LAN discovery sockets of mode A (socket-modes.md "Implementer note (LAN discovery)"): LAN is off for now.
- Bandwidth: ed3k traffic is outside `BandwidthCoordinator` and the throttler's statistics (`setLimits`/grants, `noteBytesSentOutsideTheLoop`).
- The stable IPv6 source address (`IPv6SourcePin`) as `local6`; the `bindAddress` pref for the socket.
- One preference per optional extension (`Config::extOff`); nick; filter and hops state.
- An owner-only DACL for `node.key` on Windows.
- A `tests/vs` project for `tst_Ed3kSession`; the generator was not run, since it would also drop `specialdlp` and `emuleqt-mcp` from the `.sln`.
