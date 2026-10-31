# BitTorrent module (`src/bt`, eMule::Bt)

Torrent downloads through libtorrent-rasterbar 2.1, in the daemon. Design and phases:
`docs/BitTorrentModule-Research.local.md`. Joining torrents with ed3k under BLAKE3
(`docs/ed3k-spec/extensions/torrent.md` §4): `libs/packmule/docs/torrent-join.md`.

## Layout

| File | What |
|---|---|
| `src/bt/BtSession.{h,cpp}` | façade the daemon owns: libtorrent session, alert drain on the Qt thread, resume data and session state, seed limits |
| `src/bt/BtTypes.h`, `BtIds.{h,cpp}` | plain Qt rows, settings, outcomes, IP ranges; the id `bt:v1:<40 hex>` / `bt:v2:<64 hex>` |
| `src/bt/BtJoin.h` | what an ed3k join sees: `BtLayout` (pieces, files), `BtJoinSink` (piece checks) |
| `src/bt/session/BtSettingsMap.*` | BtSettings → `settings_pack` (UPnP/NAT-PMP off) |
| `src/bt/session/BtJoinDiskIo.*` | `disk_interface` around `pread_disk_io`: the door an ed3k join uses to veto pieces |
| `src/bt/add/BtAddSource.*` | `.torrent` bytes / magnet → `add_torrent_params` |
| `src/bt/torrent/BtTorrentView.*` | `torrent_status`, files, peers, trackers → rows |
| `src/daemon/BtBridge.*` | IPC 800-899 and the CBOR rows; 503 when the module is not built or off; the IP filter handed over |
| `src/ed3k/Ed3kTorrentJoin.*` | the ed3k join of torrents (Part C), see "ed3k join" below and `docs/ed3k-module.md` |

Only `BtSession.h`, `BtTypes.h`, `BtJoin.h` and `BtIds.h` are free of libtorrent types.

## Daemon

- `DaemonApp` constructs `BtSession` always; `btEnabled()` (default on) gates the start. Options
  save → `IpcServer::btConfigChanged` → `applyBtConfig()` starts, stops or applies settings. Off,
  the session stops: nothing downloads or seeds and the port closes; torrents stay in the list
  (resume data) and continue when it is switched on. Prefs migration v3 switches it on once,
  because older configs stored the old default `enabled: false`.
- Pushes: `PushTorrentAdded/Removed/Finished`, `PushBtStatus` uncoalesced,
  `PushTorrentUpdate` coalesced per id (250 ms).
- State: `<config>/BT/session.dat` (DHT state), `<config>/BT/resume/<id>.fastresume`,
  written atomically; resume data every 5 min for changed torrents and at stop.
- CLI: `emulecored --add-torrent <file|magnet>`.
- Port: mapped by the app's PortMapper (`PortMapPurpose::BtTcp/BtUdp`, needs UPnP on and
  `btPortMapping`); libtorrent's own UPnP/NAT-PMP stay off. A second instance picks a free
  BT port on its first start, like the eD2K ports.
- Build without it: `-DEMULE_BITTORRENT=OFF`; the daemon then answers 503.

## Bandwidth

BitTorrent shares the global limits (`maxDownload`, `maxUpload`) with eD2K and Usenet.
Priority eD2K > Usenet > BitTorrent. The daemon's `BandwidthCoordinator` recomputes every
500 ms with the pure `planDownload()` / `planUpload()` (`core/transfer/BandwidthSplit`,
`tst_BandwidthSplit`), both built on the same two-sided split Usenet always used
(`computePairSplit`; see usenet-module.md, Bandwidth):

- Download: BitTorrent against eD2K and Usenet *together*, by `btDownloadSharePercent`
  (default 25). What the two get is then split between them by `usenetDownloadSharePercent`.
- Upload: eD2K against BitTorrent by `btUploadSharePercent` (default 25). The line is
  `UploadQueue::lineCapKB()`: USS's live limit when on, else `maxUpload`. Unlimited = no split.
- A share is a floor while the others are busy, never a cap: an idle engine lends its whole
  share, and one pressing its cap is back at its share within ~2 s.
- BitTorrent's demand: active = a torrent downloading (or fetching metadata) / a torrent with
  peers that may upload; rates from libtorrent's `net.recv_bytes` / `net.sent_bytes`
  counters (`post_session_stats`), so protocol overhead counts, as Usenet's wire bytes do.
- Applied through `BtSession::setRateLimits()` (session caps, only on change).
  `btMaxDownloadKb` / `btMaxUploadKb` are optional hard caps on top (0 = none).
- eD2K's upload slice is `Preferences::setEd2kUploadBudget()` (runtime only). **Both** upload
  gates read it: `uploadCapKB()` (slot count) and `UploadBandwidthThrottler`. Reading only
  the throttler would keep opening slots for bandwidth eD2K no longer has.
- Per torrent: every peer is in the session's class and its torrent's, so a per-torrent limit
  never exceeds the session cap. The daemon also clamps it to the global limit (IPC and
  REST); the Options tab of the inspector caps its spin boxes the same way.
- libtorrent does not rate-limit peers on the local network (its default, kept).
- Stats (`GetStats`, `PushStatsUpdate`): `maxDownloadKb`, `ed2kBudgetKb`, `usenetLimitKb`,
  `btDownLimitKb`, `uploadLineKb`, `ed2kUploadBudgetKb`, `btUpLimitKb`; KB/s, 0 = unlimited.
- Connections: `btMaxConnections` (session) and `btMaxConnectionsPerTorrent` (the default of
  every torrent; a torrent's own "max peers" 0 means this default).

Options: the BitTorrent section has two pages, BitTorrent (switch, folder, queue, seeding) and
Connection (port, mapping, encryption, DHT/LSD/PEX, bandwidth shares and hard caps, connection
limits). `--options bittorrent|btconnection` opens them (`btbandwidth` is an alias of
`btconnection`).

## IP filter (§7.6)

The app's `IPFilter` stays the source of truth. `BtBridge::applyIpFilter()` hands libtorrent
the ranges it blocks for eD2K (level below `ipFilterLevel`, the rule of `isFiltered`; the
table is merged already), IPv4 and IPv6, as one `lt::ip_filter`
(`BtSession::setIpFilter`). The session keeps it across stop/start (session state does not
save it). Applied at start, on `IPFilter::filterLoaded`, on the new `filterCleared` (every
reload, and a reload that finds no file, which emits no `filterLoaded`), and when an options
save changes `ipFilterLevel` (that key also raises `btConfigChanged`). Each blocked peer
(`peer_blocked_alert`, category `ip_block`) is counted (`blockedPeers()`) and logged at debug
level. Torrents keep libtorrent's default `apply_ip_filter`.

## ed3k join (torrent.md §4, Part C)

`Ed3kTorrentJoin` (src/ed3k, `docs/ed3k-module.md` "Torrent join") joins every torrent with
metadata while the ed3k node runs. What BtSession gives it, without libtorrent types:

- `attachJoin(id, sink)` / `detachJoin(id)`: the piece checks of a torrent go through the
  sink; kept across stop/start and attached again to each new disk I/O. The join door now
  reads the piece through the inner disk I/O (16 KiB requests) after libtorrent's hash and
  hands the bytes to the hook (`BtJoinHook::pieceHashed(piece, data, done)`).
- `infoSection(id)`, `layout(id)`: the info dictionary and the files on disk.
- `addPiece(id, piece, bytes)` (`add_piece`, overwrite) and `raisePieces` (`set_piece_deadline`).
- Signals `metadataReceived(id)` and `pieceResult(id, piece, passed)` (only for torrents with
  a join; needs `alert_category::piece_progress`).
- `havePieces(id)` (`status(query_pieces).pieces`) and signal `torrentChecked(id)`
  (`torrent_checked_alert`, joined torrents only): the join asks ed3k only for blocks of
  pieces libtorrent lacks.
- `ed3kLink(id)`: an `ed3k:file?…&bt=` link added as a magnet of that infohash, or a magnet's
  `xs` that is an `ed3k:file` link; kept in `<config>/BT/ed3k-links.json`. The GUI routes such
  links like BitTorrent magnets (`gui::isBitTorrentMagnet`).

## GUI (Torrents tab)

Transmission's layout, split between the tab and one inspector window:

| Where | What |
|---|---|
| filter bar | All / Downloading / Seeding / Paused / Active / Error with counts (`controls/TorrentFilterProxy`); the choice is in `uistate.yml` |
| list | `controls/TorrentListModel`, one row per torrent |
| bottom pane | `controls/TorrentListsPane`: Peers (Transmission's status letters, tooltip per letter), Trackers (status, next announce, scrape counts; Add/Remove/Reannounce, backup tiers toggle), Files (folder tree with sums, Download check box, priority menu). Follows the current row; polls `GetTorrentDetails` every 2 s while shown |
| inspector | `dialogs/TorrentDetailsDialog` (double click / Details): Info (size, location, hashes, privacy, origin, magnet, comment), Activity (transfer, piece grid with Progress/Available, dates, times), Options (speed limits, seed ratio and seed time, max peers). Only what the tab does not list. Prev/Next walk the list |

Context menu "Paste Magnet Links" (and Ctrl+V) adds every magnet in the clipboard to the default
folder, started, without the Add dialog, like "Paste eD2K Links".

Desktop: Options → BitTorrent → "Open .torrent files and magnet links with eMule Qt" (on by
default, `UiState::associateTorrentFiles`, re-applied at every start like `.nzb`). The ed3k:
scheme goes with ed2k on Options → General ("Handle eD2K Links", on macOS a check box); the only
ed3k link the GUI opens so far is `ed3k:file?…bt=` (a torrent as a file set → Torrents tab),
other ed3k links are logged and dropped (ToDo in `ExternalLinkHandler`).
`FileAssociation::applyFileTypes` writes one desktop entry on Linux for every type
(`application/x-bittorrent`, `x-scheme-handler/magnet`, `.nzb`, `ed2k`, `ed3k`), so claiming one
no longer drops another, and makes it the default (`xdg-mime default`). Windows: ProgId
`eMuleQt.torrent` and the `magnet` URL protocol under HKCU; given up only while still ours. macOS declares all of them in `Info.plist.in` (rank Alternate) and the check box sets
the default app through LaunchServices — see the macOS note in `usenet-module.md`.
`--register-file-types` / `--unregister-file-types` cover all of them; the installer's uninstall
step also removes a `magnet` key of the app.

`--tab torrents --subtab N` selects the first torrent: 0-2 pane tab, 3-5 inspector tab (screenshots).

Peer letters: `T` µTP, `O` optimistic unchoke, `D`/`d` downloading / interested but choked, `U`/`u`
uploading / they are interested but choked, `K` they unchoked us, not interested, `?` we unchoked them,
they are not interested, `E` encrypted, `h` holepunched, `H` from DHT, `X` from PEX, `I` incoming.
Half-open connection attempts are not listed.

Per-torrent options (IPC 810): speed limits and max peers are libtorrent's (in the resume data,
speed limits clamped to the global limits, max peers 0 = `btMaxConnectionsPerTorrent`);
seed ratio and seed time override the global ones from `<config>/BT/torrent-options.json`. libtorrent
does not rate-limit peers on the local network. Trackers: 811 add (last tier, no duplicates),
812 remove.

## Logging

`BtSession::drainAlerts()` forwards libtorrent's own alert lines (`what: message`) to `emule.torrent`
(`logTorrent*`): the GUI's **Torrent** log tab after Usenet, shown while `btEnabled`, and the `_Torrent.log`
file. Severity by alert category: error / performance_warning → warning, dht / ip_block → debug (follows
`verbose`), rest → info. Skipped as a firehose: `piece_progress` alerts, `session_stats`, `state_update`,
`dht_stats`, incoming `dht_get_peers` / `dht_announce`. eMule's own summaries ("BitTorrent: added …",
errors) stay in Log. Tab hidden: info/errors to Log, warnings to Verbose, debug dropped.

## Statistics

`BtCounters` (`core/stats/NetworkCounters.h`) follow the Kad pattern: the session half lives in
`Statistics::btSession()`, the banked half in `Preferences::cumBt` (YAML `statistics.cumBt`), so
reset, backup and flush need nothing BitTorrent-specific. `BtSession` folds the deltas of
libtorrent's session counters (`net.*_bytes`, `net.*_payload_bytes`, failed/redundant bytes,
`peer.num_peers_connected`) in at each `session_stats_alert`, i.e. at the coordinator's 500 ms
`demand()`; max rates use the 2 s window. Added counts user adds only, Completed only a torrent
that was downloading (libtorrent also posts finished for a seed loaded at start).

IPC `GetBtStats` (819) answers with the engine off too; the GUI's Statistics tree shows a
BitTorrent root (last) with Session, Cumulative and Torrents, hidden when the core has no BT.

Rates (§7.8): the daemon hands `StatsHistory::setBtRateSource` a reader of the payload rates
(`BtBridge::ratesKb`, cleared before the session goes). Each graph sample appends `btDown`/`btUp`
after `usenetDown` (`GetStatsHistory` fields 15/16): a BitTorrent line in the Download and the
Upload graph, colours 16/17 in Options → Statistics. The toolbar graph's samples and `GetStats`'
`rateDownBt`/`rateUpBt` make status bar, title, tray (tooltip and meter) and MiniMule show the
totals of eD2K, Usenet and BitTorrent. Overhead in the status bar stays eD2K's; the web graphs stay
eD2K-only.

## REST API (`/api/v1/torrents`)

Only a BitTorrent build has these routes; without `EMULE_HAVE_BITTORRENT` they are absent
(404, not in `/capabilities` or the OpenAPI document). Core declares `webserver/TorrentWebBackend`;
the daemon's `DaemonTorrentWebBackend` sends every call through `BtBridge::handleRequest`, so the
GUI and REST share one implementation. `docs/openapi.json` is the default build's contract;
`tst_WebServer` drops the torrent paths before comparing in a BitTorrent build.

| Route | What | Answers |
|---|---|---|
| `GET /api/v1/torrents/status` | engine: running, port, DHT nodes, rates | `200` |
| `GET /api/v1/torrents[?filter=all\|downloading\|seeding\|paused\|active\|error]` | paged rows, state by name, no piece map; empty while switched off | `200 {items, total, offset, limit}` |
| `POST /api/v1/torrents` | `{magnet, paused}` as JSON, or the `.torrent` as the body (`?paused=`); saved to the BT download folder | `200 {id, outcome}` · `400` · `409` duplicate · `503` |
| `GET /api/v1/torrents/<id>` | row + `options`, `pieceMap`, `availabilityMap` (base64url; empty while seeding) | `200` · `404` |
| `PATCH /api/v1/torrents/<id>` | options, only the fields sent: `downloadLimitKb`, `uploadLimitKb`, `maxPeers`, `ratioMode`/`seedTimeMode` (`global`\|`unlimited`\|`custom`), `ratio`, `seedTimeMinutes` | `200 options` |
| `DELETE /api/v1/torrents/<id>?confirm=true[&deleteFiles=true]` | remove | `200` · `400` |
| `GET /api/v1/torrents/<id>/files` · `peers` · `trackers` | the bottom pane's lists | `200 {items}` |
| `PATCH /api/v1/torrents/<id>/files` | `{files: [index…], priority: 0-7}` (0 = skip) | `200 {items}` |
| `PATCH /api/v1/torrents/<id>/trackers` | `{add: [url…], remove: [url…]}`; http(s)/udp only | `200 {items}` |
| `POST /api/v1/torrents/<id>/pause` · `resume` · `recheck` · `reannounce` | action; the list shows it within ~1 s | `200` · `409` refused |

Per-torrent routes answer `503` while BitTorrent is switched off and `404` for an unknown id.
MCP tools: `torrents_list`, `torrents_get`, `torrents_add_magnet`, `torrents_set_options`,
`torrents_remove`, `torrents_files`, `torrents_set_file_priority`, `torrents_peers`,
`torrents_trackers`, `torrents_edit_trackers`, `torrents_action`.

## Build

- On in every build (`EMULE_BITTORRENT` defaults to ON; CI, Docker and the scripts pass it
  explicitly). `-DEMULE_BITTORRENT=OFF` builds without it.
- `find_package(LibtorrentRasterbar 2.1 CONFIG)` first (vcpkg `libtorrent` ≥ 2.1.1, Homebrew),
  else FetchContent v2.1.1 **static**, ABI 100, without WebTorrent. Headers are system
  includes. Link the exported target: it carries `TORRENT_ABI_VERSION`.
- FetchContent needs Boost ≥ 1.74 headers (a CMake config, or a bare tree under `BOOST_ROOT`
  / the prefix path). Missing → configure stops (`FATAL_ERROR`); it no longer drops to OFF
  quietly. CI: pinned Boost 1.92 headers from `scripts/build-linux-deps.sh` /
  `build-macos-deps.sh` (windows-3rd: zip + `BOOST_ROOT`), Docker: apt `libboost-dev`.
- libtorrent uses the same OpenSSL as the rest (3.5, which QUIC NAT-T needs).
- ABI 100 has no `torrent_info::comment()/creator()/creation_date()`: BtSession keeps them
  from the add parameters (`Impl::meta`).
- Homebrew's target adds `TORRENT_USE_ASSERTS` in Debug although the library is a release
  build; that changes class layouts and crashed `tst_BtLocalSwarm`. CMake strips it from an
  installed target.
- qmake: `src/bt/bt.pro` (a subdir of `eMuleQt.pro`, needs an installed libtorrent); the daemon
  and GUI `.pro` files define `EMULE_HAVE_BITTORRENT`, `ed3k.pro` too (the torrent join). The
  qmake `tests/tests.pro` is a single-binary listing that does not link; the tests are CMake's.
- Visual Studio: `src/bt/emulebt.vcxproj`; the tests under the `Tests\BitTorrent` folder of
  `src/eMuleQt.sln`: `tst_BtSmoke`, `tst_BtLocalSwarm`, `tst_Ed3kTorrentJoin` (needs
  `EmuleEd3k`), `tst_TorrentLinks`, `tst_TorrentListModel`, `tst_TorrentDetailsGui`. They come
  from `TEST_EXTRAS` in `scripts/generate_test_vcxprojs.py`; after a change there run
  `python scripts/generate_test_vcxprojs.py --only <tests>`, which adds them to the `.sln`
  in place. The libtorrent defines (`EmuleBtDefines` in `QtVersion.props`) are a guess of
  what the vcpkg port builds with and are not checked on Windows yet.

## The join door (BtJoinDiskIo)

- Hooks are attached by info hash (`info_hash_t::get_best()`, what libtorrent puts into
  `storage_params`), before or after the torrent is added.
- A verdict given at once is handed back in place; a later one waits for the pump plugin,
  which libtorrent ticks on its network thread (`tick_interval`, 500 ms). No Boost.Asio call
  is made from our code: posting to libtorrent's `io_context` from one of our units crashed
  in the scheduler, with the Homebrew dylib and with a static build alike. Cause not found;
  layout and defines matched.
- A spoiled piece fails in libtorrent, which blames its peers: a peer that alone sent a
  failed piece is banned at once. v2 block hashes are spoiled too, so a v2 torrent blames by
  block.

## Tests

- `tst_BtSmoke`: ids, magnet detection (also `ed3k:file` links with `bt`, `xs`), inputs,
  start/stop, rate limits and demand.
- `tst_BandwidthSplit`: the eD2K/Usenet/BitTorrent plans for download and upload.
- `tst_BtLocalSwarm`: two sessions on loopback (no network): magnet with metadata exchange,
  duplicate add, a hook that passes every piece (at once and later), a spoiled piece counted
  as failed, resume after a restart; peer letters, availability, tracker add/remove, and
  per-torrent options across a restart.
- `tst_WebServer` (`restTorrents*`): the REST routes against a fake backend; without
  BitTorrent, that they are absent.
- `tst_StatisticsPanel` (`bitTorrentBranch*`), `tst_Preferences` (`bittorrent_v3SwitchesOn`).
- `tst_TorrentDetailsGui`: filter buckets, Files tree and check boxes, peer letters, backup
  trackers, the inspector's labels, grid and Options round trip.

## Not done yet

- ToDo: Visual Studio projects for `tst_TorrentDetailsGui` (and the other BitTorrent GUI tests)
- ToDo: a Torrents page in the web UI (the REST routes exist)
