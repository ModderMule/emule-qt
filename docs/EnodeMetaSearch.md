# eNode meta search: torrent and Usenet results

eNode-go servers can mix torrent and Usenet releases into ordinary eD2K search
answers. eMuleQt recognises these rows, shows a network icon in front of each
one, and downloads through the server's **Meta API**.

The contract is the shared `enodemeta` repo, a git submodule at
`external/enodemeta`. It holds the protos, the test vectors and the docs. The
server side is documented in `eNode-go/docs/meta-api.md` and
`meta-search-torrent-usenet-plan.local.md` §8.

## Recognition: by hash, never by name

A meta row is an `OP_SEARCHRESULT` entry whose 16-byte hash is a pseudo-hash:

```
ED 2B | version 01 | kind<<4 | flags | file index (u16 LE) | fold10(identity)
kind: 1 = bt-v1/hybrid, 2 = bt-v2, 3 = nzb
```

- **Hash parsing.** `SearchFile::resolveMeta()` parses the hash with
  `enodemeta::parse()`.
- **Tag cross-check.** If the row carries `FT_META_*` tags (0x60–0x6C), kind,
  version and the low 16 bits of the index must agree with the hash. The row is
  dropped (§8.1) when they disagree, or when the scheme version is unknown.
- **Name prefix.** The server adds a prefix such as `[torrent] ` or
  `[usenet example.org] ` for legacy clients. Only a bracket that names the
  network is stripped; `[Group] Title` is left alone.
- **Capability bits.** The client announces `SRVCAP_META_SEARCH` (0x2000) at
  login, and puts `SRVCAP_UDP_META_SEARCH` (0x02) in the `GLOBSEARCHREQ3` flags.
- **Never an eD2K file.** A meta hash is never queued as an eD2K download: the
  daemon refuses it in `DownloadSearchFile`.

## Discovery

`OP_SERVERIDENT` carries these tags:

| Tag | Content |
|---|---|
| `ST_META_API` (0x9E) | base URL |
| `ST_META_API_VER` (0x9F) | contract version; only 1 is accepted |
| `ST_META_API_FP` (0x9C) | optional SPKI pin, `sha256/<base64>`, for self-signed TLS |

These are runtime `Server` fields and are not persisted in `server.met`.

A row's API is the one of the server that answered it. When that server is
unknown, the daemon falls back to the server it is connected to.

## Transport

- **Messages** are generated from `meta.proto` and `api.proto` with QtProtobuf
  (`qt_add_protobuf`).
  - CMake first copies the protos flat into the build tree, dropping the
    `enode/meta/v1/` import prefix.
  - Target `emulemetaproto`. Only `Qt6::Protobuf` is linked, not QtGrpc.
- **Calls** are unary gRPC-Web over `QNetworkAccessManager`, written by hand in
  `enodemeta/GrpcWeb.cpp`. Both eNode listeners accept gRPC-Web.
  - Frame format: `flag(1) len(4 BE) payload`.
  - Flag `0x80` marks the trailer frame (`grpc-status`, `grpc-message`,
    `grpc-status-details-bin`).
  - A trailers-only error carries the same fields in the HTTP headers instead.
  - `grpc-status-details-bin` holds a `google.rpc.Status`. It is read by hand, and
    the `enode.meta.v1.ErrorInfo` inside it (msg code, registration/account URLs,
    pending steps) is decoded with the generated class.
- **RPCs in use:** `GetCaps` (cached 10 min), `GetMetaFile`, `GetAuthStatus`,
  `Login`, `Logout`. `Search` is phase 7 on the server.
- **Verification.** Every metafile is checked before use with
  `fold10(identity) == hash[6..16)`. The identity is the torrent's infohash
  (v1, or v2-only) or the canonical NZB digest.

## Actions

| Row | Double-click / bold default | Other menu entries |
|---|---|---|
| eD2K | Download (eD2K queue) | Copy eD2K Links, **Copy Magnet Links** (`urn:ed2k`) |
| Usenet | Download: the daemon fetches the NZB and queues it in eMuleQt's Usenet downloader | **Download NZB File...** |
| Torrent | **Download Torrent**: save the `.torrent`, until BitTorrent downloads exist | **Copy Magnet Links** (the server's magnet) |

- Meta rows get no eD2K link, no Details/Comments, no Search Related and no Web
  Services, because all of these key on the eD2K hash.
- Usenet rows have no magnet, so the magnet entry skips them.
- Saving asks for a file name (one row) or a folder (several rows). The last
  folder is remembered in `uistate.yml` (`metaFileSaveDir`).
- A metafile saved through the GUI travels over IPC, so it is capped just below
  `MaxPayloadSize` (16 MiB). Queuing a Usenet row has no such cap, because the
  NZB never leaves the daemon.

## Accounts

A server may require an account (`Caps.auth_mode = ACCOUNT_REQUIRED`).

1. **Login needed.** A download answers `MetaStatus::AuthRequired`. The GUI opens
   `MetaAccountDialog` (one per server), with user and password fields and a
   **Register an account** link that opens the server's website in the default
   browser.
2. **Login.** `MetaLogin` makes the daemon call `AccountApi.Login` and store the
   bearer token in `<configDir>/metaaccounts.yml` (mode 0600, token only, never
   the password). Every download that hit the wall is then retried.
3. **Account not active.** An account can still have registration steps open
   (payment, e-mail check...). The server then answers `permission_denied`, the
   GUI receives `AccountInactive`, and the dialog lists the steps as links next to
   **Check Again**.
4. **Management.** The server list's **eNode Account...** entry opens the same
   dialog to show the account status and to **Log Out**.

Credentials and tokens are sent only over `https`, or over plain `http` to a
loopback host. Links coming from the server open in the browser only when they
are `http(s)`.

## IPC (750–754)

| Opcode | Name | Purpose |
|---|---|---|
| 750 | `FetchMetaFile` | fetch the metafile for the GUI to save |
| 751 | `DownloadMetaResult` | queue a Usenet row |
| 752 | `GetMetaAuthStatus` | account status |
| 753 | `MetaLogin` | log in |
| 754 | `MetaLogout` | log out |

Every failure carries a `MetaStatus` map as its last field. See `IpcProtocol.h`
for the field layouts.

## Tests

- **`tst_EnodeMeta`:** the cross-repository vectors (fold, mint, reject, NZB
  digest), torrent v1/v2/hybrid identity, `verifyMetaFile`, gRPC-Web parsing
  including `ErrorInfo` details, the token store, and the rule that credentials
  go only over TLS.
- **`tst_SearchFile`:** a meta row is recognised by its hash, a row with
  contradicting tags or an unknown version is dropped, and the prefix is
  stripped.
- **`tst_MetaSearchGui`:** network icons, and the dialog's login, pending and
  accept states. Set `EMULE_TEST_SHOTS=<dir>` to get PNGs.
- **Live check** (manual, 2026-09-26), against a local eNode-go with torrent meta
  search on:
  - 50 of 50 rows were recognised.
  - The `.torrent` was fetched over gRPC-Web on `:4671` and verified; its
    infohash matches the magnet.
  - A meta row was refused as an eD2K download.
  - With accounts on: AuthRequired with the registration URL, then register on
    the website, a wrong password rejected, login, fetch, logout, and
    AuthRequired again.

## ToDo

- BitTorrent downloads. Then **Download** replaces **Download Torrent** as the
  torrent default, and the toolbar gets a `Torrent.ico` button.
- The web UI and the REST API don't show meta rows yet.
