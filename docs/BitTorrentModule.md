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
  as failed, resume after a restart.

## Not done yet

- ToDo: map `btListenPort()` with the app's PortMapper (research doc §7.5)
- ToDo: one download budget for ED2K, Usenet and BitTorrent (research doc §7.7); today
  BitTorrent has its own limits
- ToDo: IP filter into `lt::ip_filter` (§7.6), statistics (§7.8)
- ToDo: the ed3k join wiring in the daemon (`libs/packmule/docs/torrent-join.md`, Part C)
- ToDo: qmake files (`*.pro`) for src/bt
