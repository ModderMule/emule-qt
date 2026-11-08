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
| `Ed3kResolver` | `src/ed3k/Ed3kResolver.h` | the interface: match, check, open, expand (+ `expandDeep`), accountCheck, sites, puzzles |
| `Ed3kLinkGrabber` | `src/ed3k/` | the Link Grabber list: crawls through the resolver, checks and downloads through the join, `linkgrabber.json` |
| `Ed3kJdResolver` | `src/ed3k/` | the JDownloader sidecar: JSON lines over a loopback socket (token via stdin, then stdin closed); installs `resolver.zip` |
| `Ed3kResolverUpdater` | `src/ed3k/` | newer `resolver.zip` from the update manifest (`hosterResolver`), see "Plugin updates" |
| `Ed3kJavaRuntime` | `src/ed3k/` | finds Java 17+, else downloads Temurin 21 into `Config/ed3k/java/jre`, or `<sharedPluginDir>/java/jre` (an older `<eMuleQt>/runtime/jre` is moved there) |
| sidecar driver | `tools/hoster-resolver/` | Java, GPL v3+: JD's plugins unmodified, `SingleDownloadController` subclass, puzzle solver |
| IPC | `src/daemon/Ed3kHosterBridge.*` | requests 1100-1108 (1107 GetHosterStats, Statistics branch; 1108 CheckHosterLinks), 1210-1218 (Link Grabber), pushes 1150-1154, 1159 (PushHosterLinkCheck) and 1190 (PushLinkGrabber), `hoster` map in `GetEd3kStatus` |
| GUI | `panels/HosterPanel`, `dialogs/HosterPuzzleDialog`, Options › ed3k › Hoster | list, add links (dialog, or Paste links from the clipboard), image puzzles, settings, accounts; web search rows' Download (`MetaResultActions`, `WebLinksDialog`) adds one item per file with its mirrors, only while `enabled` (else the links window, as Details... opens it); the links dialog checks its links (Status column; with the ed3k node off the resolver checks them itself, `pumpDirectChecks`); check boxes pick what its Download takes ("Download (all)" / "Download (n)"), and a context menu has Download, Check Selected and Uncheck Selected |

## Flow of a pasted link

1. **match**: a JD plugin that takes the link (crawler plugins first, as JD does). None: *No resolver*.
2. **The user's switch** (§3), on by default, no confirm dialog: with "Allow downloads from file hosters"
   off the item waits as *Needs you*. Hosters run on the ed3k node: the Hoster tab shows, the resolver is
   unpacked and Java looked for only while both `ed3k.enabled` and `ed3k.hoster.enabled` are on
   (`Preferences::hosterDownloadsOn`). Off: `Config/ed3k/resolver/plugins` is removed (#88).
3. **§4 lookup**: `ALIAS` records of the link's `url` id. A root found: an ed3k download of it with
   Kad sources, and the link as one more source; the first block from the link tests the claim.
   Skipped while the node has no contacts; given up after 45 s.
4. **§4.1 rootless**: no root: check (name, size), then the file is downloaded whole and its
   root computed at the end.
5. The core asks the resolver (`LinkResolve`); the sidecar writes `<temp>/ed3k-hoster/<id>/jd-<link>/data.part`;
   the join reads it as it grows and hands it in with `linkData`, in order, then `linkEnd`.
   That folder is scratch of one run (removed at start).
6. Rootless and the hoster told a checksum (md5, sha1, sha224-512, crc32, crc32c): the core's file
   is hashed in the background first (*HashChecking*); the link's vouch waits for it. A mismatch,
   or JD's own check failing (error code `hash`), drops the bytes: *Failed* / `HashMismatch`, next
   mirror, never published.
7. Complete: the file moves to the incoming folder; eMule's shared files take it from there.

## Files

- Unfinished item: `<temp>/<key>.hoster.part` (bytes at their offsets) and
  `<temp>/<key>.hoster.part.met` (JSON: `format` `ed3k-hoster-met`, `v` 1, url, host, name,
  size, added, root, state, reason, mirrors, mirrorNo, pkgName, `have` runs, links). key = 16
  random hex, fixed for the item's life. No item id, no absolute path, no account data.
- New items go to the first temp dir; every temp dir is scanned for `*.hoster.part.met` at
  `setConfig` and `start()`. Copying both files into a temp dir of another node carries the
  download over (part file = the one beside the .met; package joined by name). A key or link
  already an item is skipped.
- Rooted items go on from `have`; without the part file they start over. Rootless ones always
  start over after a restart (the core's hash is not kept).
- Complete and removed items: the .met goes. `Config/ed3k/hoster/items.json` keeps only complete
  items; unfinished rows of older versions are not taken over.

## Mirrors

A web search row's file can have several links (`AddHosterLinks` field 1: `[[url, mirror…]]`).
One item per file; `Item::mirrors` holds the links not tried yet (saved with the item).
The tick moves to the next one (`mirrorDue`) when the current link is:

- hard failed: *Gone*, *No resolver* (no plugin), *Failed* (not a disk error)
- stuck: *Needs you* for a login, a puzzle (browser, skipped, wrong answer, puzzles off) or a
  block; a hoster wait over 5 min

Never for a cause no mirror changes (hosters off, no/unready resolver, no node), never Paused
or Queued. Verified blocks under a root stay, so the next mirror is checked against the same
root. The last mirror keeps its state; Resume retries it. `Item::tried` keeps the links already
left (saved). **Retry** (context menu, `HosterAction` op `retry`, `Ed3kHosterJoin::retry`) starts over
from the first link and drops the skip of its hosters, so their puzzles show again. Enabled for
Waiting, File removed, Needs you, No resolver, Failed. Host column tooltip: *Mirror n of m*.

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

## Link containers (DLC, RSDF, CCF)

A container file holds hoster links, often with package names and archive passwords. Ways in:
the Hoster tab's *Open DLC...* button and context menu entry, a drop on the Hoster tab or anywhere
on the main window, the desktop (Finder, double click, command line argument) once *Open .dlc files
with eMule Qt* is on (Options › ed3k › Hoster; `uistate.yml` `associateDlcFiles`, default off,
claimed only while hoster downloads are on; `FileTypes::dlc`, bit 8 of `appliedFileTypes`).

- GUI → `OpenHosterContainer` (1109) `[fileName, data]`, at most 4 MiB: the bytes travel, so a
  remote GUI works. The daemon writes `<temp>/ed3k-hoster/containers/<token>-<name>` and asks the
  sidecar's `container` op; the file is deleted after. Needs no ed3k node, only the resolver.
- The sidecar calls JD's container plugin (`org.jdownloader.container.D` for DLC). DLC asks
  `service.jdownloader.org/dlcrypt/service.php` for the key (`srcType=dlc&destType=jdtc6`, as JD
  does; one request per file). The service refuses for a few minutes after many files: code `limit`.
- Result → `PushHosterLinks [0, links, {token, fileName, code, text}]`. The GUI lists the links;
  one package: an editable *Package Name* (the container's, else the file name); several: each
  added as its own package. Passwords go with the items (`AddHosterLinks` field 4, `Item::passwords`,
  `.met`/`items.json` key `passwords`) and the unpack tries them after the empty one.
- Test files: `data/dlc/archive-*.dlc` (gitignored), made with JD's own `srcType=plain` call
  around archive.org links; `live_jdResolverOpensContainers` opens every file there.

## Click'n'Load

Link sites and browser add-ons hand links to JDownloader at `http://127.0.0.1:9666` (Click'n'Load 2,
JD `org/jdownloader/api/cnl2`). The **GUI** answers there, so the browser and the port are on the
same machine even when the core runs elsewhere. Switch: Options › ed3k › Hoster › Desktop *Accept
links from web pages (Click'n'Load)*, `uistate.yml` `clickNLoad`, **default on**. It listens only
while hoster downloads are on (core status), on `127.0.0.1` and `::1`. A port in use (JDownloader
runs) is logged once and tried again every minute. Port: `uistate.yml` `clickNLoadPort`, default
9666, no GUI (pages only post to 9666; other ports are for testing next to a running JD).

- `ClickNLoadServer` (src/gui/app): `/jdcheck.js` (`jdownloader=true;`, what pages look for),
  `/jdcheckjson`, `/crossdomain.xml`, `/flash` (`JDownloader`), `/flash/add` (`urls`),
  `/flash/addcrypted2` (`crypted` + `jk` or `k`), `/flash/addcrypted` (DLC as base64),
  `/flash/addcnl` (JD's `CnlQuery` JSON: `urls`, `crypted`, `jk`/`key`, `passwords`, `packageName`,
  `source`/`referrer`; as the first query/form value or an `application/json` body, raw, `[obj]`
  or `{"params":[obj]}`; `comment`/`dir`/`autostart` ignored; with `callback` an empty script, as
  JD never calls back). Fields
  `passwords`/`passwords[]`, `package` (JD's ` - site.tld` cleanup), `source` (else Referer/Origin).
  CORS `*` and the Private Network Access preflight, like JD. Answer `success` / `failed <why>`
  after the core answered. Bodies over 6 MiB, chunked bodies and other paths are refused.
- GUI → `AddClickNLoad` (1139) `[urls, crypted, jk, k, package, passwords, source]`; a DLC post goes
  as `OpenHosterContainer` `clicknload.dlc`. 1139 is inside the filter lists' block, so
  `Ed3kBridge` asks the hoster bridge before the filter one.
- Core: `Ed3kHosterJoin::openClickNLoad()`. The encrypted part becomes JD's
  `https://dummycnl.jdownloader.org/#<hex of {crypted, jk, k, source}>` and goes through the
  sidecar's `expand` op: JD's `DummyCNL` crawler runs `jk` in Rhino for the AES-128-CBC key (key =
  IV) and decrypts; LinkCrawler resolves the links. No sidecar change. Plain links skip the sidecar.
  At most 4 posts read at once (any page can post); the post's package and passwords go on each link.
- Result → `PushHosterLinks [0, links, {token, cnl: true, source, code, text}]`. Only the GUI that
  sent the post (its token) shows the container dialog (*Links of <site>*: pick links, *Package
  Name*); the main window comes forward on the Hoster tab. Nothing is added without that dialog, so
  a page can only ask. Failure: code `invalid` (nothing decrypted or no plugin takes the links).
- Tests: `tst_ClickNLoadServer` (endpoints on real sockets), `tst_Ed3kHoster` `clickNLoad_*`,
  live `live_jdResolverClickNLoad` (an archive.org link encrypted with a known key, via `jk` and `k`).

## Clipboard

Options › ed3k › Hoster › Desktop *Watch the clipboard for hoster links (http/https)*,
`clipboard` (`ed3kHosterClipboard`), **default off**. The GUI's watcher (`MainWindow::onClipboardChanged`,
beside the eD2K one) sends new clipboard text with an http(s) link as `GrabLinks [text, "Clipboard",
false, "", [], ask, clipboard: true]`. Skipped: the text that was there at start or when the switch
went on (JD's "skip on startup"), the same text again, the app's own copies (`Clipboard::setText`).
The core asks the resolver's `match` first (no network): links no plugin takes (any web page) are
dropped quietly. The others are crawled (`expand`).

- Link Grabber off (`ask`): `PushHosterLinks [0, links, {token, grab: true, source, code}]`; only the
  GUI that sent it (token) opens *Copied hoster links* (the container dialog). None found: nothing.
- Link Grabber on: into its list, the GUI does not come forward.

## Link Grabber

JDownloader's LinkGrabber: links are crawled, checked and edited before they become downloads.
Options › ed3k › Hoster › Link Grabber: *Collect links in the Link Grabber before downloading*
(`linkGrabber`, `ed3kHosterLinkGrabber`, **default off**) and *Open all links in the Link Grabber*
(`grabAll`, `ed3kHosterGrabAll`, **default off**, greyed out without the first). On, the Hoster tab
gets two tabs as the Kad window: **Downloads | Link Grabber (n)** (no tab bar while off).

| Source | Grabber on | + grabAll |
|---|---|---|
| clipboard | list | list |
| folder link found by a download (`linksExpanded`) | list (package: the folder item's name) | list |
| Click'n'Load, Open DLC / drop / desktop | dialog | list (`container.grabbed`, GUI shows the tab; CNL raises the window) |
| Add Links..., Ctrl+V, Paste links | add at once | list (the grabber's Add Links dialog) |
| search results (web rows' Download, mirrors) | add at once | add at once, always |

Core: `Ed3kLinkGrabber`, one per daemon, created with the join (DaemonApp), saved to
`Config/ed3k/hoster/linkgrabber.json` (2 s after a change and at exit; nothing written while empty
and no file). Running crawls are not saved; a check running at exit loads as unknown.

- `grab(text)`: `Ed3kHosterJoin::linksIn` URLs (≤1 MiB text, ≤1000 queued), at most 3 `expand` tasks
  at once; waits while the resolver is not available (asks for plugins). Links already in the list
  are not crawled again. `deep`: `expandDeep` (see below).
- `add(links)`: a URL in the list is skipped; a URL that is a hoster download already gets `dupe`
  (*Already in downloads*, never started). Then `Ed3kHosterJoin::checkLinks` (the links dialog's
  check, 30 min cache: a start soon after reuses it) fills status, name, size.
- Packages: the link's own (container, CNL), else the one given (Add Links, folder name), else from
  the file name: archive volumes of one set share one (`.partN.rar`, `.rNN`/`.zNN`, `.7z.001`,
  `.NNN`), else the name without extension. Same name (any case) = same package; passwords merge.
  A link without a name (crawler gave none) moves to its name's package once checked.
- Edits: rename package, set passwords (replace), enable/disable links (check box), merge into a new
  package, split by hoster (`<name> (<host>)`), remove links/packages, remove offline / disabled /
  dupes, check again, stop crawling, clear (`all`).
- Start (`confirm`): per package the enabled links that are not offline (unless asked) and not dupes
  → `addMirrored(files, package, 0, passwords)`; they leave the list.
- IPC (1210-1218, `Ed3kHosterBridge`, block grown in `Ed3kBridge::isEd3kRequest`): `GetLinkGrabber`,
  `GrabLinks`, `EditGrabberPackages`, `SetGrabberLinksEnabled`, `MoveGrabberLinks` (also split),
  `RemoveGrabberLinks` (ids or cleanup mode), `RecheckGrabberLinks`, `ConfirmGrabberLinks`,
  `AbortGrabberCrawls`; push `PushLinkGrabber` (1190, coalesced 200 ms: changed packages/links,
  removed ids, crawling count; `all` = whole list). Status map `hoster`: `clipboard`,
  `linkGrabber`, `grabAll`, `deepCrawl`; a change of the three switches pushes the status.
- GUI: `LinkGrabberPanel` (tab of `HosterPanel`): package rows *name (n)* with size, hosters and
  online/offline counts, link rows with check box, size, hoster, status dot, URL. Enter = start
  selection, Del, F2 = rename, Ctrl+C/V; *Start All*, *Clear*.

Deep crawl (*Add Links › Deep crawl*): the sidecar's `expand` with `deep: true` runs
`LinkCrawler.crawl(url, null, true)`: a page no plugin takes is loaded and every link on it is
distributed; only links with a (non-generic) host plugin come back. The sidecar's `hello` says
`deep: true`; protocol stays 2 (an older sidecar ignores the field and crawls plainly; its hello
lacks it, so the GUI hides the box: `Ed3kResolver::canDeepCrawl`).

## Extraction and sharing

- Done file not an archive (or extraction off): moves to `<incoming>[/<package>]/`.
- Archive (`UsenetUnpacker::isArchiveVolume`: rar, partNN.rar, r00, zip, 7z, tar, .001) with
  `extract` on: waits in `<temp>/hoster-archives/<package or _>/`, never in incoming. Reason
  `UnpackWaiting` until no item of the same package with the same set key (name without volume
  suffix) is still on its way; then one set at a time (`Ed3kHosterUnpack`, worker thread):
  extracted into `.unpack/` beside the archives, moved into `<incoming>[/<package>]/` (archive
  folders kept, taken names get ` (n)`) under the `.usenetpart` suffix the share scan skips.
  `Unpacking` (arg %), then `Unpacked` (arg = folder; *Open folder* opens it), `UnpackFailed`
  (headless set, broken archive) or `UnpackPassword`; *Retry* runs a failed one again.
- Passwords: the empty one, then the package's (`Item::passwords` of the set's items, typed ones
  first, then a container's). None opens it → `UnpackPassword`; the GUI asks at once (*Set
  Password…*, one dialog per package, else per folder; rows loaded that way are not asked, the
  menu covers them). `HosterAction [id, "password", pw]` → `setArchivePassword`: prepended to
  every item of the package (no package: complete items in the same folder), those waiting go
  back to `UnpackWaiting`. A wrong one ends as `UnpackPassword` again and asks again.
- `deleteArchives` on: the set's volumes go after a good unpack; off: they stay in Temp.
- Extraction switched off: waiting and failed archives move to incoming as they are.
- An interrupted unpack (`Unpacking` in items.json) runs again at the next start.
- Sharing: every file that lands in incoming goes through `setPublishedHook` → daemon
  `addFileInSharedLocation`, else `addSingleSharedFile`. With `shareIncomingSubdirs` (default on,
  #124) a package subfolder is shared with incoming; off, it is no shared dir: single shared
  files, sharedfiles.dat as MFC). Ed3kShared serves eD2K's list on ed3k.
- Kept archives in Temp are not shared, so a hoster root of an archive is not served to ed3k peers.

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
  after a restart (the .met keeps the verified blocks as `have` runs).
- The join copies the first `offset` bytes of the item's file into the task's `data.part` (a
  clone where the file system can), and the sidecar opens with `have`. JD resumes from its part
  file (`chunksProgress`); the real bytes are there because JD checks the file's hash at the end.
- The sidecar tells where the new bytes start once JD decided (`progress`/`done` field `from`):
  `have` when the hoster resumed, `0` when it could not; the join answers `OPENED from` and feeds
  from there. Below 1 MiB it starts over (cheaper than the copy).
- The sidecar runs JD outside its updater install, which AppWorkUtils takes for an IDE: it sets
  `DebugMode.TRUE_IN_IDE_ELSE_FALSE = false` first, else JD picks `HTTPDownloader`, which ignores
  `chunksProgress`. It also forces `Application.IS_JARED` false: in jar mode `PluginClassLoader`
  wants `bcprov` under `ROOT/libs` (the updater layout) and fails BouncyCastle classes.
- Puzzle images JD saved (`jd/captchas/`) are deleted when the sidecar starts; it stops after
  5 min idle, so they don't pile up.
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
| `enabled` | `ed3kHosterEnabled` | on | the user's switch (§3); needs `ed3k.enabled` (also on by default) |
| `java` | `ed3kHosterJava` | empty | path of `java`; empty = find one |
| `autoJava` | `ed3kHosterAutoJava` | on | download Temurin when none is found |
| `maxStreams` | `ed3kHosterMaxStreams` | 2 | 1-16; one per hoster and account in any case |
| `puzzles` | `ed3kHosterPuzzles` | on | show image puzzles |
| `publishLinks` | `ed3kHosterPublishLinks` | **on** | §4.1: vouched links get an `ALIAS` record and are handed out; switching on asks first. Off = holding links back; on is the `hoster.md` §4.1 default |
| `fromPeers` | `ed3kHosterFromPeers` | on | use links peers name |
| `checkSearchLinks` | `ed3kHosterCheckSearchLinks` | on | a web search row's links dialog checks its links |
| `extract` | `ed3kHosterExtract` | on | unpack archives into `<incoming>/<package>/`, archives stay in Temp |
| `deleteArchives` | `ed3kHosterDeleteArchives` | off | delete archives after a good unpack; greyed out while `extract` is off |
| `clipboard` | `ed3kHosterClipboard` | off | the GUI watches the clipboard for hoster links (see "Clipboard") |
| `linkGrabber` | `ed3kHosterLinkGrabber` | off | the Link Grabber tab; copied and folder links go there |
| `grabAll` | `ed3kHosterGrabAll` | off | Click'n'Load, Add Links and containers go there too; search results never |
| `checkMaxParallel` | — (YAML only) | 30 | 1-256: those checks at once |
| `checkMaxPerHost` | — (YAML only) | 5 | 1-64: of them at one hoster |
| `recheckHours` | — (YAML only) | 72 | 0-8760: a published link is checked again after this; 0 = never |
| `reportRecheckHours` | — (YAML only) | 6 | 0-8760: 3 dead-link reports check a published link at once, at most this often; 0 = reports never check |
| `autoUpdate` | `ed3kHosterAutoUpdate` | on | fetch newer resolver plugins (see "Plugin updates"); Options › ed3k › Hoster, Java runtime group |
| `autoUpdateHours` | — (YAML only) | 24 | 1-8760: manifest check interval |
| `sharedPluginDir` | — (YAML only) | empty | resolver plugins, their updates and the Java runtime once for several instances (see "Shared plugins" below); relative = to the config folder; empty = per instance |
| `accounts` | `ed3kHosterAccounts` | empty | `{host, user, passwordEncrypted}`; IPC never carries a password out |
| `solversEnabled` | `ed3kHosterSolversEnabled` | on | puzzles go to the solver services below first |
| `solvers` | `ed3kHosterSolvers` | empty | captcha solver services, `{host, user, passwordEncrypted}`; user empty for key-only ones (key = password). Same password rule as `accounts` |

## Captcha solvers

Options › ed3k › Hoster has two tabs, like Usenet: *General* and *Captcha solvers*. The solver tab
lists JD's paid solver plugins (`PluginForCaptchaSolver*`): 2Captcha, Anti-Captcha, CapMonster Cloud,
AZcaptcha, Captchas.io, 9kw (API key only), DeathByCaptcha and ImageTyperz (user + password, or the
password alone as API token), EndCaptcha and CheapCaptcha (user + password).

- The daemon puts the enabled accounts with a password into `JdResolverConfig::solvers`. The resolver
  sends them as `solverAccounts` after every sidecar start (the sidecar clears JD's saved ones; it also
  stops when idle) and at once when they change while it runs. Its answer stays in
  `Ed3kResolver::solverStates()` and goes to the GUI in the ed3k status (`hoster.solvers[{host, valid,
  balance, currency, code}]`, no passwords).
- JD hands a puzzle to its valid solver accounts before the user's puzzle window
  (`UseAvailableCaptchaSolverAccounts`).
- *Check* reuses `CheckHosterAccount`: `accountCheck` on a solver host answers `balance`/`currency` too,
  shown as *Credit: 9.84 USD* / *No credit (…)*. A key-only row is checked with the user `apikey`.
- Live tests: `tst_HosterSolvers` (`EMULE_HOSTER_LIVE=1`, keys `EMULE_CAPTCHA_*` in `.env`).

## Java

Probe order: the pref, `Config/ed3k/java/jre`, `$JAVA_HOME`, macOS `java_home -v 17+`, `java` on
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
Output: `build/hoster-resolver/resolver.zip` (about 33 MB). Layout of `resolver/`:

| Path | What |
|---|---|
| `Core.jar` | JD core, AppWork, the driver, translations/json defaults (~9.7k classes, 13 MB) |
| `jd/plugins/{hoster,decrypter}/` | plugins, loose: JD's scanners list the folder beside the jar holding `jd.SecondLevelLaunch` (2.3k files, 20 MB) |
| `libs/` | 15 jars a headless JD loads (15 MB) |
| `revision.txt`, `api.txt`, `LICENSE` | JD SVN revision, sidecar protocol (`ResolverMain.PROTOCOL`, from the source by the build script), GPL |

Classpath: `Core.jar`, the `resolver/` dir (for the loose plugins), `libs/*`. Left out of `libs/`
besides JD's GUI/desktop jars: `cling-*` + `seamless-http/xml` (UPnP reconnect, MyJD direct
server), `jsvg` (icons), `jna_platform.jar` (same file as `jna-platform.jar`). `seamless-util` stays
(archive.org plugin, image puzzles); `miglayout`, `image4j`, `Filters` stay because plugin and
puzzle classes link them. GUI classes stay in `Core.jar`: plugins link them. The bundle scripts copy it next to the daemon (macOS:
`Contents/Resources`); `emulecored` unpacks it into `Config/ed3k/resolver/plugins` when it changed and
hoster downloads are on. Switched off, the unpacked plugins are removed again (48 MB, 2.4k files;
89 MB and 12k files before Core.jar, #88). `EMULE_RESOLVER_ZIP` points at another zip.

Shared plugins (`sharedPluginDir`, YAML only; #88, many instances on one host): JD never writes
into the unpacked plugins (its cfg, tmp, logs, captchas go to `-DROOT` = `Config/ed3k/resolver/jd`,
still per instance), so one copy serves all. Layout `<sharedPluginDir>/<12 hex of zip SHA-1>/resolver/`
+ `.lastused`: one folder per zip content, so mixed app versions don't unpack over each other.
Install under `.install.lock` (QLockFile, wait ≤3 min): the first instance unpacks, the others take
it; an installed version is used without the lock (read-only share works). Pruned on install:
`*.tmp` left-overs and versions whose `.lastused` (touched at each sidecar start) is >30 days old;
an instance whose version went is unpacked again at its next start. Switching off leaves the shared
copy; switching to shared removes the instance's own `plugins/`.

With the shared folder (#155) the rest goes there too:
- `update/`: the plugin updater's zips and `update.json`, one folder for all instances. One fetches
  at a time (`update/.fetch.lock`, freed only when its holder dies); the others wait (30 s retries)
  and take its zip from `update.json`. Every hourly tick reads `update.json` again, so another
  instance's check counts and its new zip is swapped in. The instance's own
  `Config/ed3k/resolver/update` is copied along (when newer) and removed.
- `java/jre`: the downloaded Java runtime. Probed before the instance's own copy; one instance
  fetches it (`java/.install.lock`), the others wait and probe what it installed. An instance's
  own runtime is moved into an empty shared folder, or removed once the shared one runs.
- Version choice without a zip, and whenever installing a zip fails: the unpacked version of this
  daemon's api (`api.txt`; none told counts as ours) with the highest revision, then last used.
  Mixed app versions never run each other's plugins, and a failed unpack never leaves the
  resolver without plugins.
- Read-only folder (filled by an admin, e.g. root-owned under `/opt`): its plugins are used as
  they are, plugin updates are off, `.lastused` and pruning are skipped; logged once. Read-only
  and holding no plugins: per instance, as if unset. Java is still probed there; a fetch then
  goes to the instance's own `Config/ed3k/java/jre`.
- Layout for an admin to fill: `<dir>/<any 12 hex>/resolver/` = the zip's `resolver/` folder,
  `<dir>/java/jre/bin/java` = any Java 17+.

Compile-time switch: CMake `-DEMULE_HOSTER_JD=OFF`, qmake `CONFIG+=no_emule_hoster_jd`, MSBuild
`/p:EmuleHosterJd=false`. Off: no resolver, no Java lookup, no Hoster tab; ed3k still stores, passes
on and looks up links and answers `NO_RESOLVER`.

The Hoster tab shows only while ed3k is on (`ed3k: enabled`); the Torrents tab likewise follows
`bittorrent: enabled`. The **Hoster Verbose** log tab holds `emule.hoster` debug lines while hoster
downloads are on and `hosterVerboseLog` is set (Options › Extended › Verbose): the resolver's start,
connect and auth, each request (`-> op task host`, never url path, account or proxy credentials)
and each event (`<- task id ev: fields`, progress once in 10 s per task). Off: dropped.

### Plugin updates

Hosters change their pages weekly; a zip frozen at release time rots. `Ed3kResolverUpdater`
(daemon, on while `autoUpdate` and hoster downloads are on).

**No zip at all** (release bundles ship none now): nothing is fetched at start. The first hoster
link that finds no plugins (`Ed3kHosterJoin::begin` → `Ed3kResolver::wantPlugins` →
`Ed3kJdResolver::pluginsNeeded`) makes the daemon call `Ed3kResolverUpdater::fetchFirst()`, also
with `autoUpdate` off. The link waits as "no resolver" with "Downloading resolver plugins"; the zip
is unpacked on a worker thread, then waiting links start (`retryNotReady`). A failed try is
repeated by the next link after 10 min at the earliest. Scheduled checks only run once a zip is there.

Scheduled checks:

- Every `autoUpdateHours` (default 24; hourly tick, 6 h back-off after a failure) it reads
  `hosterResolver` of `emuleqt-version.json`: `{url, sha256, size, jdRevision, api}`.
- Taken only when `api` = `Ed3kJdResolver::kProtocol` (no `api`: never), `jdRevision` is newer than
  the bundled zip's and the last fetched one's, the URL is https and the size ≤ 256 MB.
- Fetched to `Config/ed3k/resolver/update/resolver-r<rev>.zip` (`HttpStreamDownload`: size + SHA-256,
  through the user's proxy), then the zip's own `api.txt`, `revision.txt` and layout must match, else
  it is deleted. `update.json` keeps the last check and the fetched zip. One older zip is kept (the
  resolver may still run from it), others are removed.
- The daemon uses the fetched zip while it is newer than the bundled one; an app update that
  ships a newer zip deletes the fetched one. After a `PROTOCOL` bump the old fetched zip is ignored.
- **Hand-over**: a new zip never cuts work. While the sidecar runs (an open download, check or
  puzzle) the zip waits (`pendingZip`, shown as "update rN waits until hoster downloads are idle"
  under the JD version). The sidecar stops after 5 min without tasks (or ends otherwise); then the
  zip is unpacked on a worker thread. Tasks asked meanwhile are held and sent once it is in
  (reason while unpacking: "Updating resolver plugins"). A JD download keeps the old plugins to its end.
- Trust: the same as the core auto-update, HTTPS from emule-qt.org and the manifest's SHA-256; no
  separate signature yet (ToDo).

Publishing a refresh between releases: `scripts/publish-resolver.sh` (svn update, build, upload as
`/pub/resolver-r<rev>.zip`, rewrite only `hosterResolver` of the live manifest; refuses a revision
not newer than the live one without `--force`; `--dry-run` prints the entry). `/pub/resolver.zip` is
never overwritten: release tags carry its SHA-256. A release's manifest job writes its own zip's
entry again (`update-manifest.sh` reads `api.txt`). After a `PROTOCOL` bump, publish refreshes only
once a release with the new protocol is out: the manifest has one entry, older clients skip it.

### Release builds

- CI has no JD sources: `scripts/publish-release.sh` runs `build-hoster-resolver.sh`, rsyncs the zip to
  `https://emule-qt.org/pub/resolver.zip` and puts `Hoster-Resolver-SHA256:` in the tag message, next
  to the GeoLite2 line. `release.yml`'s geoip job checks it; only the manifest job uses it now.
  The platform bundles and Docker ship no zip (`resolver: false`, the old line kept as a comment;
  33 MB smaller): the core fetches it on the first hoster link ("Plugin updates").
  `publish-release.sh` has `BUNDLE_RESOLVER=0`: no resolver build before the tag, no
  `Hoster-Resolver-SHA256:` line (CI: a notice; the manifest job keeps `hosterResolver`). Its last
  step, after the tag push, is `publish-resolver.sh` with build (svn update, compile, upload,
  manifest entry); `--no-build` is passed on. `--skip-resolver`: no resolver step at all.
  Bundling again: `BUNDLE_RESOLVER=1` plus the commented `resolver:` lines in `release.yml`.
- ed3k in CI: the workflows check out the private `ModderMule/packmule` at the commit in
  `scripts/packmule.ref` into `libs/packmule` (still gitignored), with the `PACKMULE_TOKEN` secret
  (fine-grained PAT, Contents: read). No secret (forks, `windows-3rd.yml`): no checkout, ed3k off.
  `publish-release.sh` refuses an unpushed packmule HEAD and commits the new pin with the release.

## Logging

Hoster lines (join, resolver, Java runtime) go to `emule.hoster` (`logHoster*`, `Ed3kHosterJoin::logCategory`),
not `emule.ed3k`: the GUI's **Hoster** log tab after Torrent, shown while `hosterDownloadsOn()`, and the
`_Hoster.log` file. Tab hidden: info/errors to Log, warnings/debug to Verbose. Debug follows `verbose`.

## Tests

- `tst_Ed3kHoster`: the join on a real engine with a fake resolver (mirrors, link checks `probe_*`,
  order `rank_*`); the JD resolver's event parser; the Java probe and Adoptium parsing.
- `tst_LinkGrabber`: the grabber with a fake resolver and backend (grouping, dupes, edits, confirm,
  crawl, deep, clipboard match, coalesced changes, save/load). `tst_HosterPanel`: the tab, pushes.
- Live, opt-in: `EMULE_HOSTER_LIVE=1` downloads a small archive.org file through the real sidecar
  and opens every container in `data/dlc`;
  `EMULE_HOSTER_LIVE_JAVA=1` fetches Temurin into a temp folder.

## Status column

While an item downloads without bytes yet, the sidecar's `status` event says what the plugin does
(`Reason::HosterStep`): opening the link, talking to the hoster (with an account: logging in), solving a
captcha, connecting; `transfer` clears it. An image puzzle shows as *Puzzle* with its dialog; a browser
captcha ends as *Needs you*.

**Skipping puzzles** (as JD: a skipped link is not retried by itself, no pause). The dialog has
*Skip* and *Skip hoster*; closing it or its timeout (300 s) is *Skip*. The sidecar tells how a
puzzle ended in `needsUser{kind}`: `skipped` (no answer), `wrong` (answered, the hoster refused),
`puzzle` (no solver: a browser captcha). Reasons: `PuzzleSkipped`, `PuzzleWrong`,
`NeedsBrowserPuzzle`, `PuzzlesOff` (`puzzle` while the switch is off). All move on to the next mirror.
*Skip hoster* (`AnswerHosterPuzzle` field 2) puts the hoster's `siteOf()` in `m_skipSites` for this
run: its open puzzles are answered empty, later ones never reach the GUI, and those links end as
`HosterSkipped`. JD's own skip variants (package, all, stop downloads) are not offered.

JD clears its `CaptchaBlackList` only when its download watchdog stops, which never runs in the sidecar:
a browser captcha (`BLOCK_HOSTER`, "no solver") would skip every later puzzle of that hoster. Each
`open` clears it (`OpenTask`); what to skip is the daemon's call.

JD's `CaptchaAPISolver` is switched off at sidecar start: headless, its `isEnabled()` starts MyJDownloader,
which asks for logins on the console. That once read the request pipe and hung; since protocol 2 requests
go over a loopback socket and stdin is empty, so any console read gets EOF.

## Open (ToDo in code)

- reCAPTCHA, hCaptcha, Turnstile need a browser: without a captcha solver account such links end
  as *Needs you*. A real download through a solver service is not tested yet (it costs per puzzle).
- Resume of a rootless item after a pause or a restart (the core's hash state is not saved).
- Peers' links for plain ed3k downloads (`Ed3kDownloads`) and the links of Kad answers; only
  hoster items use them now.
- The address check of `http-source.md` §5 for the sidecar's own requests (needs a hook in JD's `Browser`).
- A signature on `resolver.zip` refreshes beyond HTTPS + the manifest's SHA-256; an OS keychain for account passwords.
