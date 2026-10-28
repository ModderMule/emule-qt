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
| `Ed3kResolver` | `src/ed3k/Ed3kResolver.h` | the interface: match, check, open, expand, accountCheck, puzzles |
| `Ed3kJdResolver` | `src/ed3k/` | the JDownloader sidecar over stdin/stdout JSON lines; installs `resolver.zip` |
| `Ed3kJavaRuntime` | `src/ed3k/` | finds Java 17+, else downloads Temurin 21 into `<eMuleQt>/runtime/jre` |
| sidecar driver | `tools/hoster-resolver/` | Java, GPL v3+: JD's plugins unmodified, `SingleDownloadController` subclass, puzzle solver |
| IPC | `src/daemon/Ed3kHosterBridge.*` | requests 1100-1106, pushes 1150-1154, `hoster` map in `GetEd3kStatus` |
| GUI | `panels/HosterPanel`, `dialogs/HosterPuzzleDialog`, Options › ed3k › Hoster | list, add links, image puzzles, settings, accounts |

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
- Once the node vouches for the item's own link (`LinkVouched`) and "Publish hoster links" is on, the
  join publishes its `ALIAS` record and calls `linkShare(root, {url})`: the link goes with the
  `SOURCE` record (at most 3) and to downloaders that set `LINKS` (at most 8). Switching "Publish
  hoster links" off stops it (`linkShare(root, {})`); a peer's link is never handed on.

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

## Tests

- `tst_Ed3kHoster`: the join on a real engine with a fake resolver; the JD resolver's event parser;
  the Java probe and Adoptium parsing.
- Live, opt-in: `EMULE_HOSTER_LIVE=1` downloads a small archive.org file through the real sidecar;
  `EMULE_HOSTER_LIVE_JAVA=1` fetches Temurin into a temp folder.

## Open (ToDo in code)

- reCAPTCHA, hCaptcha, Turnstile need a browser: such links end as *Needs you*.
- Resume of a rootless item after a pause or a restart (the core's hash state is not saved).
- Peers' links for plain ed3k downloads (`Ed3kDownloads`) and the links of Kad answers; only
  hoster items use them now.
- The address check of `http-source.md` §5 for the sidecar's own requests (needs a hook in JD's `Browser`).
- A signed online refresh of `resolver.zip`; an OS keychain for account passwords.
