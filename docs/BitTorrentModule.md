# BitTorrent module (`src/bt`, eMule::Bt)

Torrent downloads through libtorrent-rasterbar 2.1, in the daemon. Design and phases:
`docs/BitTorrentModule-Research.local.md`. Joining torrents with ed3k under BLAKE3
(`docs/ed3k-spec/extensions/torrent.md` §4): `libs/packmule/docs/torrent-join.md`.

## Layout

| File | What |
|---|---|
| `src/bt/BtSession.{h,cpp}` | façade the daemon owns: libtorrent session, alert drain on the Qt thread, resume data and session state, seed limits |
| `src/bt/BtTypes.h`, `BtIds.{h,cpp}` | plain Qt rows, settings, outcomes; the id `bt:v1:<40 hex>` / `bt:v2:<64 hex>` |
| `src/bt/session/BtSettingsMap.*` | BtSettings → `settings_pack` (UPnP/NAT-PMP off) |
| `src/bt/session/BtJoinDiskIo.*` | `disk_interface` around `pread_disk_io`: the door an ed3k join uses to veto pieces |
| `src/bt/add/BtAddSource.*` | `.torrent` bytes / magnet → `add_torrent_params` |
| `src/bt/torrent/BtTorrentView.*` | `torrent_status`, files, peers, trackers → rows |
| `src/daemon/BtBridge.*` | IPC 800-899 and the CBOR rows; 503 when the module is not built or off |

Only `BtSession.h`, `BtTypes.h` and `BtIds.h` are free of libtorrent types.

## Daemon

- `DaemonApp` constructs `BtSession` always; `btEnabled()` gates the start. Options save →
  `IpcServer::btConfigChanged` → `applyBtConfig()` starts, stops or applies settings.
- Pushes: `PushTorrentAdded/Removed/Finished`, `PushBtStatus` uncoalesced,
  `PushTorrentUpdate` coalesced per id (250 ms).
- State: `<config>/BT/session.dat` (DHT state), `<config>/BT/resume/<id>.fastresume`,
  written atomically; resume data every 5 min for changed torrents and at stop.
- CLI: `emulecored --add-torrent <file|magnet>`.
- Build without it: `-DEMULE_BITTORRENT=OFF`; the daemon then answers 503.

## GUI (Torrents tab)

Transmission's layout, split between the tab and one inspector window:

| Where | What |
|---|---|
| filter bar | All / Downloading / Seeding / Paused / Active / Error with counts (`controls/TorrentFilterProxy`); the choice is in `uistate.yml` |
| list | `controls/TorrentListModel`, one row per torrent |
| bottom pane | `controls/TorrentListsPane`: Peers (Transmission's status letters, tooltip per letter), Trackers (status, next announce, scrape counts; Add/Remove/Reannounce, backup tiers toggle), Files (folder tree with sums, Download check box, priority menu). Follows the current row; polls `GetTorrentDetails` every 2 s while shown |
| inspector | `dialogs/TorrentDetailsDialog` (double click / Details): Info (size, location, hashes, privacy, origin, magnet, comment), Activity (transfer, piece grid with Progress/Available, dates, times), Options (speed limits, seed ratio and seed time, max peers). Only what the tab does not list. Prev/Next walk the list |

`--tab torrents --subtab N` selects the first torrent: 0-2 pane tab, 3-5 inspector tab (screenshots).

Peer letters: `T` µTP, `O` optimistic unchoke, `D`/`d` downloading / interested but choked, `U`/`u`
uploading / they are interested but choked, `K` they unchoked us, not interested, `?` we unchoked them,
they are not interested, `E` encrypted, `h` holepunched, `H` from DHT, `X` from PEX, `I` incoming.
Half-open connection attempts are not listed.

Per-torrent options (IPC 810): speed limits and max peers are libtorrent's (in the resume data);
seed ratio and seed time override the global ones from `<config>/BT/torrent-options.json`. libtorrent
does not rate-limit peers on the local network. Trackers: 811 add (last tier, no duplicates),
812 remove.

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

- `find_package(LibtorrentRasterbar 2.1 CONFIG)` first (vcpkg `libtorrent`, Homebrew),
  else FetchContent v2.1.1 static, ABI 100, without WebTorrent. Headers are system
  includes. Link the exported target: it carries `TORRENT_ABI_VERSION`.
- Visual Studio: `src/bt/emulebt.vcxproj`, `tests/vs/tst_BtSmoke.vcxproj`,
  `tests/vs/tst_BtLocalSwarm.vcxproj`; the libtorrent defines there are a guess of what the
  vcpkg port builds with and are not checked on Windows yet.

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

- `tst_BtSmoke`: ids, magnet detection, inputs, start/stop.
- `tst_BtLocalSwarm`: two sessions on loopback (no network): magnet with metadata exchange,
  duplicate add, a hook that passes every piece (at once and later), a spoiled piece counted
  as failed, resume after a restart; peer letters, availability, tracker add/remove, and
  per-torrent options across a restart.
- `tst_WebServer` (`restTorrents*`): the REST routes against a fake backend; without
  BitTorrent, that they are absent.
- `tst_TorrentDetailsGui`: filter buckets, Files tree and check boxes, peer letters, backup
  trackers, the inspector's labels, grid and Options round trip.

## Not done yet

- ToDo: map `btListenPort()` with the app's PortMapper (research doc §7.5)
- ToDo: one download budget for ED2K, Usenet and BitTorrent (research doc §7.7); today
  BitTorrent has its own limits
- ToDo: IP filter into `lt::ip_filter` (§7.6), statistics (§7.8)
- ToDo: the ed3k join wiring in the daemon (`libs/packmule/docs/torrent-join.md`, Part C)
- ToDo: qmake files (`*.pro`) for src/bt
- ToDo: Visual Studio projects for `tst_TorrentDetailsGui` (and the other BitTorrent GUI tests)
- ToDo: a Torrents page in the web UI (the REST routes exist)
