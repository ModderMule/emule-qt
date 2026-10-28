# Hoster downloads (ed3k `hoster` extension)

Downloads from links at file hosters, verified under ed3k roots. Spec:
[`ed3k-spec/extensions/hoster.md`](ed3k-spec/extensions/hoster.md). Design notes:
[`ed3k-spec/docs/hoster-resolver.local.md`](ed3k-spec/docs/hoster-resolver.local.md).

## Pieces

| Piece | Where | Does |
|---|---|---|
| packmule hoster service | `libs/packmule/src/ext/hoster*.rs` | canonical links, `ALIAS` scheme `url`, link fields, block verification, result codes |
| `ed3k_link_id`, `ed3k_link_alias_same` | `libs/packmule/crates/packmule-ffi/src/pure.rs` | the id of a link (for the §4 lookup) and its `SAME` record (published when hoster links are published) |
| `ed3k_link_share` | `libs/packmule/crates/packmule-ffi/src/links.rs` | §2/§4.1: the links of a shared root go with its `SOURCE` record and to downloaders (`FileStatus.links`); empty stops |
| `Ed3kHosterJoin` | `src/ed3k/` | the host side: items, §4 lookup, answers to `LinkResolve`, feeds the resolver's part file in, writes verified blocks, moves the file to incoming |
| `Ed3kResolver` | `src/ed3k/Ed3kResolver.h` | the interface: match, check, open, expand, accountCheck, sites, puzzles |
| `Ed3kJdResolver` | `src/ed3k/` | the JDownloader sidecar over stdin/stdout JSON lines; installs `resolver.zip` |
| `Ed3kJavaRuntime` | `src/ed3k/` | finds Java 17+, else downloads Temurin 21 into `<eMuleQt>/runtime/jre` |
| sidecar driver | `tools/hoster-resolver/` | Java, GPL v3+: JD's plugins unmodified, `SingleDownloadController` subclass, puzzle solver |
| IPC | `src/daemon/Ed3kHosterBridge.*` | requests 1100-1108 (1107 GetHosterStats, Statistics branch; 1108 CheckHosterLinks), pushes 1150-1154 and 1159 (PushHosterLinkCheck), `hoster` map in `GetEd3kStatus` |
| GUI | `panels/HosterPanel`, `dialogs/HosterPuzzleDialog`, Options › ed3k › Hoster | list, add links (dialog, or Paste links from the clipboard), image puzzles, settings, accounts; web search rows' Download (`MetaResultActions`, `WebLinksDialog`) adds one item per file with its mirrors, only while `enabled` (else Show Links); the links dialog checks its links (Status column); its Download says "Download (all)" / "Download (n)" for the selection, and a context menu has Download and Deselect All |

## Flow of a pasted link

1. **match**: a JD plugin that takes the link (crawler plugins first, as JD does). None: *No resolver*.
2. **Off by default** (§3): with "Allow downloads from file hosters" off the item waits as *Needs you*.
3. **§4 lookup**: `ALIAS` records of the link's `url` id. A root found: an ed3k download of it with
   Kad sources, and the link as one more source; the first block from the link tests the claim.
   Skipped while the node has no contacts; given up after 45 s.
4. **§4.1 rootless**: no root: check (name, size), then the file is downloaded whole and its
   root computed at the end.
5. The core asks the resolver (`LinkResolve`); the sidecar writes `<temp>/ed3k-hoster/<id>/jd-<link>/data.part`;
   the join reads it as it grows and hands it in with `linkData`, in order, then `linkEnd`.
6. Complete: the file moves to the incoming folder; eMule's shared files take it from there.

## Mirrors

A web search row's file can have several links (`AddHosterLinks` field 1: `[[url, mirror…]]`).
One item per file; `Item::mirrors` holds the links not tried yet (saved in `items.json`).
The tick moves to the next one (`mirrorDue`) when the current link is:

- hard failed: *Gone*, *No resolver* (no plugin), *Failed* (not a disk error)
- stuck: *Needs you* for a login, a browser puzzle or a block; a hoster wait over 5 min

Never for a cause no mirror changes (hosters off, no/unready resolver, no node), never Paused
or Queued. Verified blocks under a root stay, so the next mirror is checked against the same
root. The last mirror keeps its state; Resume retries it. Host column tooltip: *Mirror n of m*.

**Order** (`rankMirrors`, in `addMirrored`; stable): offline links last, then the account tier
(2 premium with traffic left and not expired, 1 any other account, 0 none; `accountTier`), then
links checked online before unchecked ones, else the page's order. Pasted links have no mirrors.

**Account tier**: the join checks each stored login (`accountCheck`) on start, when the accounts
change and with a link check, at most every 6 h; the manual check in Options fills the same cache.
In memory only. A login that failed to check counts as an account (1), one found not valid as none.

**Hoster domains**: a login matches its domain, subdomains and "www.", and every other domain of
the same hoster. The resolver names them from JD's plugin (`LazyHostPlugin.getSitesSupported`):
`sites` for each account host (on start / account change, once), and `match` adds `sites` for a
hoster plugin. E.g. a login for `rg.to` counts for `rapidgator.net` and `rapidgator.asia` links,
checked or not (`siteOf`). An older resolver without `sites`: own domain and subdomains only.

## Packages

Items added together can form a **package**: one row in the Hoster tab with the items as
children, and one folder under incoming (`<incoming>/<package>/`) their files move to.

- A web search row's Download (`MetaResultActions`, `WebLinksDialog`): always a package named
  after the row, even with one file.
- Add links dialog: the optional *Package Name* field. Empty: plain items, as before.
- A folder link's files (`PushHosterLinks`): a new package named after the folder. A folder
  item inside a package asks: add to that package, or a new package. Packages never nest.

`AddHosterLinks` field 2 `package` (name) makes a new package; field 3 `packageId` joins an
existing one and wins. The same name twice is two packages that share a folder. Items carry
`packageId`/`package` (`Item`, `items.json` keys `pkg`/`pkgName`, the IPC item map); the GUI
builds the package rows from them and sums size, progress, speed and states. The name is a
folder name: `/` and `\` become `_`. Removing an item with its file removes the package folder
once it is empty. Actions on a package row act on all its items.

## Link checks of a links dialog

`WebLinksDialog` sends `CheckHosterLinks` with every link when it opens, while hosters are `enabled`
and `checkSearchLinks` is on (else no Status/Account column). Status: green dot online, red offline, grey unknown; Account:
"Premium account" with a gold star, or "Account" (shown once one link has a login). Download never waits: unchecked links count as
online and are checked by the item as before.

The check goes where an item's own goes, through the core: `match` (the plugin host, for the
account), `linkAddRootless`, `linkAsk(CHECK)`, answered in `onResolve` with the hoster's account.
`LinkChecked` = online; `LinkEnded` `GONE`/`SIZE_MISMATCH`/`CORRUPT`/`BAD_SOURCE` = offline; else, a
wait, no plugin or 60 s without an answer = unknown. Puzzles are skipped. At most `checkMaxParallel` (30) at once and
`checkMaxPerHost` (5) at one hoster, its domains counted as one; a link that waits for a slot or a
busy hoster over 2 min is unknown.

- The result and the core's link are kept 30 min: Download soon after starts from the link the
  core already judged. A dead one is refused at once (*Gone*, then the next mirror), no second check.
- An item's current link is never checked beside it: its state answers (downloading or complete
  online, gone offline, else unknown).
- Download while a check runs: the item takes the link over and goes on from that check.

## Resume

A stream that breaks goes on from where it stood instead of starting over:

- **Rootless** (§4.1): the core keeps its hash and asks to open at the bytes it took
  (`LinkResolve.offset`). **Rooted**: the join asks from the first block not held from 0, also
  after a restart (`items.json` keeps the verified blocks as `have` runs).
- The join copies the first `offset` bytes of the item's file into the task's `data.part` (a
  clone where the file system can), and the sidecar opens with `have`. JD resumes from its part
  file (`chunksProgress`); the real bytes are there because JD checks the file's hash at the end.
- The sidecar tells where the new bytes start once JD decided (`progress`/`done` field `from`):
  `have` when the hoster resumed, `0` when it could not; the join answers `OPENED from` and feeds
  from there. Below 1 MiB it starts over (cheaper than the copy).
- The sidecar runs JD from loose classes, which AppWorkUtils takes for an IDE: it sets
  `DebugMode.TRUE_IN_IDE_ELSE_FALSE = false` first, else JD picks `HTTPDownloader`, which ignores
  `chunksProgress`.
- A rootless item that is paused, or the daemon restarted, starts over: the hash lives in the core.

## Links of peers and sharing links

- Links peers name (`FileStatus.links`) are kept for roots the join downloads when "Use hoster
  links that other ed3k nodes name" is on. When the item's own link cannot deliver (gone, needs
  the user, no plugin, its hoster busy with another stream, a wait over 5 minutes) a peer's link
  is opened instead, verified block by block under the root; one that fails is not tried again
  for the item. The core reports each result to the peer that named the link.
- **Vouched links** (`LinkVouched`): the item's own link (rooted: first block verified under the
  root; rootless: whole file gave the root) and a peer's link a block from verified. Kept per
  item in `items.json` `links` [{url, checkedAt, offline}], own first. Any downloader does this,
  wherever the link came from (search row, paste): its own node key signs the record, so a link
  stays findable when its first publisher leaves (alias.md: ≤3 publishers per root and name).
- **Published** (`publishShared`) while "Publish hoster links" is on, for live links (not offline,
  checked within `recheckHours`, or unknown at their recheck): `ALIAS` record each (first 4),
  `linkShare(root, urls)` → `SOURCE` record (first 3) and downloaders that set `LINKS` (first 8).
  `publishAlias` refused as `State` (root not shared yet: hashing after completion or a start) is
  tried each tick for 2 h. A start publishes again what was out before.
- **Recheck** (`recheckHours`, default 72): a complete item's link past it is checked through the
  same probe queue and limits as a links dialog's (`checkMaxParallel`, `checkMaxPerHost`), before
  it goes out again; at a start stale links wait for it. Online: published, `checkedAt` moves.
  Offline: withdrawn, the item says *link offline* (`Reason::LinkOffline`), asked again after the
  next interval. Unknown (hosters off, no resolver, puzzle, timeout): kept, asked again in
  min(6 h, interval).
- **Dead-link reports** (`reportRecheckHours`, default 6): downloaders' `HttpResult`s for a link
  handed out (`event::HttpResults`, by `url_id` = `ed3k_link_url_id`) that say `GONE`,
  `SIZE_MISMATCH` or `CORRUPT`, from downloaders in 3 sites (`HttpResults.site`: IPv4 /24, IPv6 /48, as the core counts;
  none on a virtual connection: not counted), check it at once, at most once
  per `reportRecheckHours` since its last check (`max(checkedAt, reportCheckAt)`, saved). The core
  stops handing it out after 3 sites by itself; the check decides: offline withdraws as above,
  online re-shares it (the core's report count starts over). Reports never withdraw by themselves.
- **Withdraw** (offline, held back, item removed): Kad has no delete. `unpublishAlias` stops the
  22 h refresh; stored copies run out within 24 h. `linkShare(root, {})` stops handing it out at
  once, the next `SOURCE` store (≤4 h) goes without it, other copies run out within 5 h. Other
  publishers' records are theirs; each rechecks its own.

## Settings (`preferences.yml`, `ed3k: hoster:`)

| Key | IPC | Default | |
|---|---|---|---|
| `enabled` | `ed3kHosterEnabled` | off | the user's switch (§3); the GUI shows what it means first |
| `java` | `ed3kHosterJava` | empty | path of `java`; empty = find one |
| `autoJava` | `ed3kHosterAutoJava` | on | download Temurin when none is found |
| `maxStreams` | `ed3kHosterMaxStreams` | 2 | 1-16; one per hoster and account in any case |
| `puzzles` | `ed3kHosterPuzzles` | on | show image puzzles |
| `publishLinks` | `ed3kHosterPublishLinks` | **on** | §4.1: vouched links get an `ALIAS` record and are handed out; switching on asks first. Off = holding links back; on is the `hoster.md` §4.1 default |
| `fromPeers` | `ed3kHosterFromPeers` | on | use links peers name |
| `checkSearchLinks` | `ed3kHosterCheckSearchLinks` | on | a web search row's links dialog checks its links |
| `checkMaxParallel` | — (YAML only) | 30 | 1-256: those checks at once |
| `checkMaxPerHost` | — (YAML only) | 5 | 1-64: of them at one hoster |
| `recheckHours` | — (YAML only) | 72 | 0-8760: a published link is checked again after this; 0 = never |
| `reportRecheckHours` | — (YAML only) | 6 | 0-8760: 3 dead-link reports check a published link at once, at most this often; 0 = reports never check |
| `accounts` | `ed3kHosterAccounts` | empty | `{host, user, passwordEncrypted}`; IPC never carries a password out |

## Java

Probe order: the pref, `<eMuleQt>/runtime/jre`, `$JAVA_HOME`, macOS `java_home -v 17+`, `java` on
`PATH`, the usual install folders. A candidate must be Java 17+ and of the daemon's CPU
architecture. None found and `autoJava` on: Eclipse Temurin 21 JRE from `api.adoptium.net`,
through the Proxy page's proxy, SHA-256 checked, unpacked with the system `tar`.

Only while hoster downloads are switched on (`Ed3kJavaRuntime::setAllowed`): nothing is fetched
before, the "Download Java" button is off and the IPC request refused; switching off stops a
running download and removes its files.

## Building the resolver

```sh
scripts/build-hoster-resolver.sh [<dir with JDownloader, AppWorkUtils, JDBrowser, MyJDownloaderClient>]
# Windows: scripts\build-hoster-resolver.ps1 -JdDir <dir>
```

Default source dir: `$JD_DIR` or `/Volumes/MySSD/Coding/Java` (SVN checkouts). Needs a JDK 17+.
Output: `build/hoster-resolver/resolver.zip` (about 38 MB: the plugins as loose classes, JD's core,
the jars a headless JD loads). The bundle scripts copy it next to the daemon (macOS:
`Contents/Resources`); `emulecored` unpacks it into `Config/ed3k/resolver/plugins` when it changed,
because JD writes caches next to the plugin classes. `EMULE_RESOLVER_ZIP` points at another zip.

Compile-time switch: CMake `-DEMULE_HOSTER_JD=OFF`, qmake `CONFIG+=no_emule_hoster_jd`, MSBuild
`/p:EmuleHosterJd=false`. Off: no resolver, no Java lookup, no Hoster tab; ed3k still stores, passes
on and looks up links and answers `NO_RESOLVER`.

The Hoster tab shows only while ed3k is on (`ed3k: enabled`); the Torrents tab likewise follows
`bittorrent: enabled`.

### Release builds

- CI has no JD sources: `scripts/publish-release.sh` runs `build-hoster-resolver.sh`, rsyncs the zip to
  `https://emule-qt.org/pub/resolver.zip` and puts `Hoster-Resolver-SHA256:` in the tag message, next
  to the GeoLite2 line. `release.yml`'s geoip job checks it and hands `resolver-zip` to every platform
  and Docker. A tag without the line ships no resolver (warning only).
- ed3k in CI: the workflows check out the private `ModderMule/packmule` at the commit in
  `scripts/packmule.ref` into `libs/packmule` (still gitignored), with the `PACKMULE_TOKEN` secret
  (fine-grained PAT, Contents: read). No secret (forks, `windows-3rd.yml`): no checkout, ed3k off.
  `publish-release.sh` refuses an unpushed packmule HEAD and commits the new pin with the release.

## Tests

- `tst_Ed3kHoster`: the join on a real engine with a fake resolver (mirrors, link checks `probe_*`,
  order `rank_*`); the JD resolver's event parser; the Java probe and Adoptium parsing.
- Live, opt-in: `EMULE_HOSTER_LIVE=1` downloads a small archive.org file through the real sidecar;
  `EMULE_HOSTER_LIVE_JAVA=1` fetches Temurin into a temp folder.

## Open (ToDo in code)

- reCAPTCHA, hCaptcha, Turnstile need a browser: such links end as *Needs you*.
- Resume of a rootless item after a pause or a restart (the core's hash state is not saved).
- Peers' links for plain ed3k downloads (`Ed3kDownloads`) and the links of Kad answers; only
  hoster items use them now.
- The address check of `http-source.md` §5 for the sidecar's own requests (needs a hook in JD's `Browser`).
- A signed online refresh of `resolver.zip`; an OS keychain for account passwords.
