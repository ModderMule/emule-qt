# Usenet (NNTP) module

eMuleQt downloads from Usenet through `eMule::Usenet`, a static library that is a
peer of `eMule::Core` and `eMule::Ipc` rather than a part of either. The
dependency runs one way only: `usenet → core` is fine, `core → usenet` never.
`emulecored` links it; the GUI stays daemon-agnostic and talks IPC.

Design research and the phase plan are in `docs/UsenetModule-Research.local.md`,
amended by `docs/BitTorrentModule-Research.local.md` §7.

**Status: phases 0–5, 6a and 6b are implemented.** The module connects,
authenticates, parses an NZB, downloads it across a pool of worker threads,
assembles it byte-identically, verifies it against its PAR2 set, repairs it,
restores obfuscated filenames, unpacks the archives — as their volumes land,
not after — and offers the payload, and only the payload, to the ED2K network. It has a queue with persistence, a
Usenet tab in the GUI, a share of the global download budget, and streaming
preview — of a raw-posted release, and of the file inside a stored RAR set, with
a seek anywhere in it. The web interface has a Usenet page and the REST API a
`/api/v1/usenet` resource — see § Web interface and REST API. Keyword search is
phase 5: it lives in the shared `eMule::Indexer` module rather than here, because
newznab and torznab are one protocol — see `docs/indexer-module.md`.

## Layout

```
src/usenet/
    UsenetSession.{h,cpp}       what DaemonApp owns; start/stop and prefs
    nntp/
        NewsServer.h            alias of the record in core/prefs/NewsServer.h
        NntpSocket.{h,cpp}      transport, TLS, greeting, AUTHINFO, watchdog
        NntpCommand.{h,cpp}     CAPABILITIES / GROUP / STAT / BODY
        NntpServerPool.{h,cpp}  leasing by priority level, backoff, rotation
        ArticleFetcher.{h,cpp}  one segment: BODY -> decode -> write
    nzb/
        NzbFile.{h,cpp}         QXmlStreamReader parse
        NzbInfo.{h,cpp}         the queue-item model
        SubjectParser.{h,cpp}   filename and (n/m) recovery from a subject
    decode/YencDecoder.{h,cpp}  streaming yEnc + per-part CRC32
    post/
        Par2Verifier.{h,cpp}        libpar2-turbo: verify, repair, rename
        UsenetUnpacker.{h,cpp}      volume-set detection, then libarchive
        UsenetPostProcessor.{h,cpp} one thread, one job at a time
    queue/
        ArticleWriter.{h,cpp}   sparse writes at absolute offsets
        UsenetQueueItem.{h,cpp} one NZB, plus its per-segment completion bitmap
        UsenetQueue.{h,cpp}     scheduling, failover routing, completion
        UsenetQueueStore.{h,cpp} one YAML sidecar per item, under Config/Usenet/
        UsenetWorker.{h,cpp}    one thread: its own pool, its own sockets
```

GUI side (linking `eMule::Core` + `eMule::Ipc` only, never `eMule::Usenet`):
`src/gui/panels/UsenetPanel.{h,cpp}` and
`src/gui/controls/UsenetQueueModel.{h,cpp}`.

Web side: `src/core/webserver/UsenetWebBackend.h` (the interface core may see),
`src/daemon/DaemonUsenetWebBackend.{h,cpp}` (its implementation) and
`src/daemon/UsenetBridge.{h,cpp}` (what IPC, the page and the REST API share).

## Sockets

`NntpSocket` is a new class on a bare `QSslSocket`, modelled on
`core/net/SmtpClient`. It deliberately does **not** reuse eMule's socket stack:
`EMSocket` derives from `EncryptedStreamSocket`, which implements eMule's own RC4
obfuscation rather than TLS, speaks length-prefixed eMule packets rather than
CRLF text, and registers with the *upload* bandwidth throttler in a
download-dominant protocol.

Two things it fixes relative to `SmtpClient`, which has neither:

- **A response/idle watchdog.** `SmtpClient` has no timeouts at all, so a server
  that accepts a connection and then goes quiet wedges the object permanently.
  A pooled connection must not be able to do that.
- **An explicit `TlsMode`** (`None` / `Implicit` / `StartTls`) instead of
  deciding from a hardcoded port number. NNTP's implicit-TLS port is 563,
  providers also offer 443, and STARTTLS runs on the cleartext port 119.

Inbound rate limiting is `NntpSocket::setReadRateLimit()`, a token bucket with a
refill timer. `0` means unlimited, as everywhere else in eMuleQt. It is driven by
the budget split described below.

### Through the user's proxy

The Proxy page's proxy carries news-server connections too, unless its
**Use for news servers** box (`usenet.useProxy`, default on) is cleared.
`toNetworkProxy()` in `core/net/ProxySettings.h` is the one translation from the
page to a `QNetworkProxy`, shared with `EMSocket`; `Preferences::usenetProxySettings()`
is the one answer to "which route", read by `UsenetSession::applyPreferences()` for
the workers and by `handleTestNewsServer()`, so the Test button proves the route a
download takes. It reaches each socket through `UsenetQueue::setProxy()` →
`UsenetWorker::setServers()` → `NntpServerPool::setProxy()`, and takes effect at the
worker rebuild `applyServers()` does on every save — news servers switch over at
once, unlike eD2K sockets.

- **`NoProxy` is explicit**, not Qt's `DefaultProxy`: with the box cleared, an
  application-wide proxy set by anything else must not carry the connection.
- **SOCKS4 and SOCKS4a are reached as SOCKS5**, as `EMSocket` always did: Qt has no
  SOCKS4 client. The Test button says so when such a proxy refuses.
- Qt's SOCKS5 and HTTP clients send the **host name** to the proxy (SOCKS5 ATYP 3),
  so a provider's name is never resolved locally.
- Many HTTP proxies only allow `CONNECT` to port 443; an error names the proxy.

⚠️ **A dead proxy is not a dead provider.** Qt's `Proxy*Error` codes map to
`NntpError::ProxyFailed`, appended to the enum so no stored value moved. As a
`ConnectFailed` it backed *every* account off for a minute, counted connection
errors against each, spent every article's transport retries, failed items on
required accounts and booked articles **missing** on optional ones. Now the worker
blocks no server, statistics blame nobody, and the queue parks for the retry
interval with "waiting for the proxy" on each item — the same shape as the disk
floor — until an article comes through or a settings save lifts it. The floor
outranks it while both hold (see the wait-reason order below); lifting the park
hands a Checking item back to "checking availability" rather than to silence.

## Transport and commands are separate, on purpose

`NntpSocket` owns transport, greeting and authentication and knows nothing about
`GROUP` or `BODY`. Everything after "connection is ready" is an `NntpCommand`
subclass. Adding a verb means adding a subclass, never touching the socket.

This is not stylistic. ngPost hit exactly this problem — same transport,
different command sequence — and answered it by copy-pasting `NntpConnection`
into `NntpCheckCon` with a different state enum, leaving two near-identical
500-line files to fix twice.

## Failover

`NntpServerPool` leases connections by **priority level**, ported from NZBGet's
`ServerPool`:

- Level 0 is tried first. A higher level is reached only when every server below
  reported the article **missing** (430) — never when one merely timed out.
- Within a level, servers are equals and rotate, so one account does not absorb
  the whole queue.
- The caller passes the servers already tried, keyed by `NewsServer::key()`
  (`host:port/user`), so an escalation never asks the same account twice.
- A server that fails at the transport level is **blocked** for a retry interval
  rather than removed: "too many connections" is the commonest Usenet failure
  and it cures itself. Blocking drops that server's **idle** connections at once
  and flags the leased ones to be dropped when their article finishes: aborting
  a leased socket raises no `failed()`, so the article on it would never
  finish — its in-flight slot held forever, its item stalled, and the job left
  pointing at a deleted socket.

`NntpError` encodes that distinction, and `escalatesToNextLevel()` is the single
place the rule lives. Getting it backwards either hammers a dead server or gives
up on an article a fill server would have had.

Three account fields shape the ladder, and all three are consumed — `group` in
the pool, `optional` and `retention` in the queue:

- **`group`** — accounts sharing a non-zero group id are one account for
  connection limits, so the same provider reached through two host names cannot
  open twice what the plan allows. The bucket's limit is the **smallest**
  `maxConnections` among its members. `0` is ungrouped and gets a bucket of its
  own. Because `UsenetQueue` divides `maxConnections` across workers with a
  split that is monotone and sums exactly, the per-worker bucket caps sum to
  exactly the configured group limit.
- **`optional`** — a block or fill account is allowed to be down. Its transport
  faults still count against the segment's retry budget, but when the budget runs
  out with *only* optional accounts to blame, the **article** is recorded missing
  rather than the **item** failed. An account with no key at all (a local disk
  fault) counts as required, and when every configured account is optional the
  flag is ignored — otherwise a lone optional account being briefly down would
  mark every article missing and publish a silently ruined release.
- **`retention`** — see below. It is an optimisation and never a verdict.

## Configuration and credentials

Servers live in `preferences.yml` under `usenet:`, which the daemon owns. The
record type is `eMule::NewsServer` in `src/core/prefs/NewsServer.h` — a stored
preference first and a protocol object second, the same call made for
`HttpCacheServerConfig`, and the reason core does not have to know about the
usenet module.

```yaml
usenet:
  enabled: true
  retryIntervalSeconds: 60
  servers:
    - name: main
      host: news.example.com
      port: 563
      tls: 1            # 0 none, 1 implicit, 2 STARTTLS
      user: someone
      passEnc: <base64>
      level: 0
      group: 0              # 0 = ungrouped; see Failover
      optional: false       # a block account: never fails a download on its own
      retention: 0          # days, 0 = unknown
      joinGroup: false      # issue GROUP before each fetch
      maxConnections: 40      # default; kDefaultMaxConnections
      certVerification: 2   # 0 none, 1 minimal, 2 strict
      enabled: true
```

### Preferences

```yaml
usenet:
  par2Repair: true          # verify and repair; off means damaged releases fail
  par2RenameFiles: true     # restore real filenames from PAR2 metadata
  unpack: true              # extract RAR/7z/ZIP volume sets
  cleanupAfterUnpack: true  # publish the payload only
  directUnpack: true        # extract each volume as it lands, not at the end
  encryptedPreview: true    # preview an encrypted RAR by re-running the unpacker
  externalUnpacker: ""      # explicit 7-Zip/unrar path; empty = search. Used or nothing
  healthCheck: 1            # 0 off, 1 sample one article per file, 2 every article
  healthMinPercent: 95      # below this a release is queued *paused*, never failed
  autoAddPaused: false      # queue automatic adds paused, so a feed proposes
  useProxy: true            # news servers through the Proxy page's proxy
  sfvCheck: true            # verify against an .sfv when PAR2 did not run
  unrepairableAction: 1     # provably unrepairable: 0 keep going, 1 pause, 2 fail
  unwantedAction: 1         # a media release carrying unwanted files: same values
  unwantedExtensions: "exe, com, scr, pif, lnk, bat, cmd, vbs, vbe, js, jse, wsf, hta, cpl, msi, ps1, jar, reg"
  watchDir: ""              # a folder .nzb files are picked up from; empty = off
  subjectPatterns: []       # absent = the built-in heuristics; see § Subject parsing
  servers:
    - host: news.example.com
      port: 563
      level: 0              # rung on the failover ladder; lower is tried first
      group: 0              # >0 pools connection limits across host names
      optional: false       # true = a block account; never fails a download alone
      retention: 3000       # days, 0 = unknown. A guess, never a verdict
      joinGroup: false      # GROUP before each BODY; a few old servers need it
      accountId: 6f1c…      # stable identity for the usage meter; minted, not typed
      quotaKind: 1          # 0 unmetered (default), 1 monthly, 2 prepaid block
      quotaBytes: 500000000000
      quotaResetDay: 17     # billing day; a short month rolls on its last day
      quotaFallThrough: false  # spent -> wait, rather than spend the next level
```

Every `quota*` key is written only when an allowance is set, so an unmetered
account's block is byte-identical to what it was before the feature existed.

`accountId` is deliberately **not** `key()`. `key()` is `host:port/user` and is
the right identity for the pool and for "servers already tried" — a changed host
really is a different connection — but switching a provider to TLS changes the
port and rotating a credential changes the user, and under `key()` either would
silently start a fresh meter. Under-counting over-spends, so the meter gets its
own opaque id, minted in `Preferences::setUsenetServers()` where every writer
passes.

Passwords are AES-encrypted under the one file-wide key that
`notifications.emailEncryptionKey` carries. **This is obfuscation, not secrecy**
— the key sits in the same file as the ciphertext. It defeats a shoulder-surfer,
a config pasted into a bug report, a grep across a backup and an unattended
screenshot. It does not defeat anyone holding `preferences.yml`.

Four rules that are load-bearing and silent when broken:

1. The `usenet:` block is loaded **and** written **after** `notifications:`,
   which is where the key is read and minted. Earlier and every password loads
   back empty.
2. The mint condition includes "any Usenet password is non-empty", or a user who
   configures only Usenet gets no key and their passwords are dropped on save.
3. A plaintext `pass:` is accepted on load for first setup and hand-editing, and
   never written back.
4. `aesDecryptFromBase64()` returns empty both for "was empty" and "wrong key",
   so a `passEnc` that was present and decrypted empty is **logged** as a
   decrypt failure. Otherwise the user sees only their provider saying
   "authentication failed".

## IPC

Usenet owns request block **720–799** and push block **910–949**. (The core
request space runs 100–299 against a response block at 300 and has roughly
thirty slots left, so a second network cannot take the next free integer.
700–719 / 900–909 belong to the shared newznab/torznab indexer client, which
phase 5 built — see `docs/indexer-module.md`.)

| Opcode | Shape |
|---|---|
| `GetNewsServers = 720` | `[]` → one map per account |
| `SetNewsServers = 721` | `[[{…}]]` → `[ok, error]`; replaces the list |
| `TestNewsServer = 722` | `[{…}]` → `[ok, [success, response]]` |

**The daemon never sends a password to the GUI.** `GetNewsServers` reports only
`hasPassword`, and an entry arriving at `SetNewsServers` *without* a `password`
field keeps the stored one. That is what lets the Options page round-trip a list
whose secrets it was never given, and it means a screenshot, an IPC log or a
session on a non-loopback socket cannot carry a credential.

`TestNewsServer` returns the provider's **literal status line** —
`281 Authentication accepted`, `481 Authentication failed` — because that tells a
user which half of the form to fix where "connection failed" does not.

`SetNewsServers` emits `usenetConfigChanged`, forwarded through `IpcServer` to
`DaemonApp::applyUsenetServers()`, the same route `webServerConfigChanged` takes.
`UsenetSession` is constructed unconditionally and `usenetEnabled` gates only the
auto-start, so the switch never needs a daemon restart.

## GUI

Options → **Usenet** (`OptionsDialog::PageUsenet`, reachable as
`emuleqt --options usenet`), in the sidebar's **Usenet** accordion group with
Indexers and Feeds. The "Enable Usenet downloads" switch sits above two tabs:

- **Account** — the news-server list with Add/Remove, and **everything belonging
  to the row selected in it**: the "Account" box (Enabled, Name, Host, Port +
  Encryption, User, Password, Connections, Priority level, **Test**) and the
  "Account options" box below it (Retention, Connection group, Certificate check,
  Optional, Send GROUP, the allowance block, Used).
- **Advanced** — settings that apply to the **whole Usenet engine**, never to one
  account: "Downloading" (retry-after, share of the download limit), "When
  adding", the watch folder, the .nzb file association, and "After downloading".

The tabs divide the page by **scope, not by how advanced a setting looks**, and
that is the whole rule. The account chooser is the news-server table, and it can
only be on one tab; a per-account field on the other tab therefore sits on a tab
with nothing saying which account it edits, and reads as a global setting. The
per-account tuning fields did start out on the Advanced tab for balance — the
Account tab is the taller of the two now — and it was the wrong trade.
`tst_OptionsDialogSizing::usenetPageHasAccountAndAdvancedTabs` pins the rule by
object name (`usenetAccountIdentity`, `usenetAccountTuning`), not by group title,
so a translated build cannot pass it by finding nothing.

Splitting the page at all is not cosmetic either. As one column it needed
~1270 px of layout minimum, and `QStackedLayout::minimumSize()` is the maximum
over *all* eighteen option pages — so it set the height of the whole Options
window on every category. Both tabs are built up front and both scroll on their
own; the fields on the hidden tab are real widgets, because
`applyNewsServerDetails()` reads every one of them on each commit and would
otherwise write defaults over half the account. Column layout persists under the
`optionsUsenetServers` key in `uistate.yml`.

The "Open .nzb files with eMule Qt" checkbox is absent on macOS, where the bundle
declares its document types and there is nothing to switch.

Options → **Feeds** (`PageFeeds`) is a sibling of the Indexers page and copies it
function for function, with **Check now** where Indexers has **Test** — a feed is
not something you can test, only something you can make run early. Its status
line and Last-checked column update from `PushIndexerFeedStatus` while the dialog
is open, because a feed can finish a poll on its own schedule with the page in
front of you.

It also carries a **Download category** combo. Named in full because
"Categories" four rows above it is the *indexer's* category id list; the combo
stores category indices as item data and resolves with `findData()`, so a feed
naming a category the list no longer holds falls back to "No category" rather
than silently selecting whoever took the slot.

### The category tab strip

Both the Transfers tab and the Usenet tab show the same `CategoryTabBar`, and it
owns the whole of the category *list* — fetching it, mirroring it, the add/edit/
remove dialogs, drag-reorder with "All" pinned, and the half of the context menu
that edits the category itself. That part is identical for both networks.

What is not identical is the bulk action, so the widget does not own it: it emits
`menuRequested(QMenu*, int)` while building, and each panel puts its own entries
on top. Transfers keeps its five `CategoryAction`s and the a4af priority submenu;
Usenet adds Pause, Resume and Cancel and nothing else.

Filtering is `CategoryFilterProxy`, stacked on each panel's existing sort proxy.
It reads the category off the **row**, through `kCategoryRole`, which both
`DownloadListModel` and `UsenetQueueModel` answer.

⚠️ That role is the whole point of the rewrite. The proxy used to reach the
category by casting `sourceModel()` to `DownloadListModel*` through the sort
proxy, and its own comment recorded the cost: when the cast failed the fallback
accepted **every** row, so the tab bar looked like it worked and filtered
nothing. A role lookup is forwarded down the stack by every proxy in between, so
the arrangement cannot change the answer — and an unanswered role now reads as
*uncategorised* and hides the row rather than showing it everywhere.
`tst_CategoryFilterProxy` covers both, including the stacked arrangement.

⚠️ The Usenet view is now two proxies away from its model, so every view index
needs **both** hops — `UsenetPanel::toSourceIndex()` / `fromSourceIndex()`.
Mapping through only one yields the *wrong row*, not an invalid index.

The Category column is appended after Health rather than slotted beside
Priority: the column order is what `uistate.yml` stores widths against, and
inserting one in the middle would shift every saved width by one. Growing the
count at all still invalidates a stored blob — `QHeaderView::restoreState()`
refuses a state whose section count does not match — so the Usenet list falls
back to its defaults once and saves a nine-column layout from then on. That is
the safe failure: refused, not misapplied. Category
**colour** is deliberately not rendered — the ED2K list does not render it
either, and parity means not inventing a difference.

### Opening a finished release, and looking inside an unfinished one

Double-click and Enter share one handler, so the mouse and the keyboard cannot
diverge:

| row | condition | action |
|---|---|---|
| item | `Complete` | Open File — the largest non-PAR2 file it published |
| item | otherwise | expand / collapse, which is what it has always meant |
| file | it was published under its own name | Open that file |
| file | otherwise | the details dialog |

Alt+Enter always opens the details dialog, and the context menu carries **Open
File** (single selection, enabled only when there is something to open, bold as
the default action) and **Details…**.

**Local versus remote is the same decision the ED2K lists make**, through
`IpcClient::isLocalConnection()`. A local core's recorded path is a path on this
machine, so it goes to the OS default handler. A remote core's is not, and the
bytes are only reachable over its web server — media through the browser player
page, everything else as a download. That is a *third* incoming URL shape:
`daemonIncomingUrl()`'s `?path=` names a **folder**, and the listing route answers
400 for one naming a file, so `daemonIncomingFileUrl()` builds `?play=` or
`/api/v1/incoming/download?file=` instead.

**What a completed release published is its own list.** `publishedPaths` on the
item, persisted, because `files` cannot answer it: an unpacked release publishes
the *extracted members*, which are not NZB files at all. Before it existed
`publishStaged()` zipped the payload list into `item.files` by position — and the
payload list is sorted by name with every `.par2` dropped, so it agreed with the
NZB's file list only by luck. Both lists are still attributed, now by the staged
file's `source` path, which is the only thing that can say which NZB file a
published file came from. `tst_UsenetQueue::aPublishedFileIsAttributedToTheNzbFileItCameFrom`
pins it with an NZB whose file order is the reverse of its name order, where the
old code misattributed *both* files.

The details dialog polls `GetUsenetItemDetails` once a second while it is open
and stops existing when it closes. Its file table sorts on the magnitude behind
each cell rather than the text in it — "9.9 MiB" sorts above "10.0 MiB" and
"9/100" above "10/100" otherwise, which is wrong on every numeric column at once.

The download queue is a **standalone Usenet tab**. Merging it into the shared
transfer list is a separate refactor for after every phase is built and tested —
nothing in phases 0–6 is shaped around that merge. The tab accepts dropped .nzb
files, as does the main window, which switches to it.

### Segment bars, file status and picking files

The Progress column is a **segment map**, drawn by `UsenetProgressDelegate` in
MFC's bar style through `PartBarPainter.h`. The eD2K download and shared-files
delegates now use the same painter, so the three cannot drift apart.

| colour | means |
|---|---|
| dark grey | articles in |
| blue | still to fetch |
| amber | in flight right now |
| red | missing on every server |
| light grey | a skipped file, or a recovery volume nobody asked for |
| solid green | a finished file or release |

The top 3 px strip shows percent. During post-processing it shows the stage's
own percent. Paused and failed releases use the muted palette.

**What the daemon sends, and what the GUI builds.**

- Each file row in `itemToCbor()` carries a `state` (`UsenetFileWireState`:
  Queued, Partial, Complete, Missing, Skipped, Held) and, **only when the file is
  not uniform**, a `segmentMap`.
- The map is one byte per bucket, at most 128. A bucket shows the worst article
  in it: missing, then in flight, then queued, then done.
- A finished or untouched file sends no map at all, so a 250 ms push carries
  bytes only for the few files actually arriving.
- The release bar is built GUI-side (`usenetItemBar()`): the files are laid end
  to end by size, and held recovery volumes are left out. A tenth of the release
  that mostly never downloads would otherwise read as a permanent blue tail.
- The Status column carries an icon per file, from the existing artwork:
  `Download.ico` downloading, `ClientsOnQueue.ico` queued, `Cancel.ico` articles
  missing, `Pause.ico` skipped.

**Picking files inside an NZB (#4).**

- Every non-PAR2 file row has a checkbox, both in the queue tree and in Release
  Details.
- Release Details adds Select All / Select None / Invert Selection, through the
  shared `UsenetFileCheckList`. The queue's context menu offers Skip / Download
  Selected Files.
- A click only *asks* (`SetUsenetFilesSkipped`). The row follows the next push,
  so a refusal cannot leave a box lying.
- Add NZB gains **Choose Files…** (`NzbFileChooserDialog`), which has the daemon
  parse each picked file (`InspectNzb`, since the GUI never links the NZB
  parser). The unchecked indices ride on `AddNzb` field 8.
- Drops, URL adds, the watch folder and indexer grabs still add everything.

**Pause All** on the Usenet toolbar, **Pause Usenet** in the tray and the web
page's toggle all send one request (`SetUsenetPaused`). All three follow
`PushUsenetEngineState`, seeded on connect from `usenetPaused` in
`GetPreferences`. See "Pausing the whole engine".

## yEnc

`YencDecoder` streams: it is fed one dot-unstuffed line at a time and writes
decoded bytes through a sink. Nothing buffers a whole article — they run to
roughly 750 KB encoded.

Four things it handles that a naive decoder does not:

- **`=ypart begin` is 1-based.** The file offset is `begin - 1`. An off-by-one
  here still passes every CRC, because `pcrc32` verifies the payload and not
  where it lands, so only a byte-for-byte comparison catches it.
- **The glued header.** Real posters emit
  `=ybegin … name=file.dat=ypart begin=1 end=100000` on one line. Taking the
  whole tail as the filename yields a garbage name *and* no offset, so the part
  overwrites part 1.
- **An escape can straddle a line.** A trailing `=` escapes the first byte of the
  next line — the only decoder state that crosses a line boundary, and therefore
  the state a per-line API is most likely to drop. `reset()` must clear it, or a
  truncated article corrupts the *next* one.
- **`<segment bytes>` in an NZB is the ENCODED size.** Decoded offsets cannot be
  derived from an NZB at all; only the article's own `=ypart` is authoritative.

Decoding and CRC32 use [rapidyenc](https://github.com/animetosho/rapidyenc)
(public domain, runtime SIMD dispatch) when `EMULE_USENET_RAPIDYENC` is on, which
is the default. The scalar decoder in the same file is a complete fallback and
runs the identical test suite; `-DEMULE_USENET_RAPIDYENC=OFF` selects it, so a
fetch or toolchain problem can never block the module.

## Subject parsing

`parseSubject()` recovers a filename and the `(n/m)` counter. Two rules taken
from nZEDb's `collection_regexes`, the only battle-tested corpus of these:

- The part counter has **no end anchor** — subjects carry trailing junk, and
  anchoring drops a large minority of posts.
- **"No filename" and "no counter" are normal outcomes, not errors.** Obfuscated
  releases carry neither; the real name arrives only in the first article's
  `=ybegin name=`, which `ArticleFetcher::articleFileName()` exposes — and for a
  *fully* obfuscated post not even there. See § What a file is called.

The rules are **data**, under `usenet.subjectPatterns:`, because obfuscation
schemes change faster than releases ship and a heuristic that needs a rebuild to
follow them falls behind. Absent means the compiled-in set, which is what every
installation runs until somebody edits the file — and the block is written back
only when it is non-empty, so an existing `preferences.yml` is unchanged by this.

```yaml
usenet:
  # Single-quote these: in a double-quoted YAML scalar '\d' is an escape.
  subjectPatterns:
    - {name: yenc-part-counter, role: part, pick: last,
       pattern: '\((?<index>\d{1,5})\s*/\s*(?<total>\d{1,5})\)'}
    - {name: bracketed-file-counter, role: file,
       pattern: '\[(?<index>\d{1,5})\s*/\s*(?<total>\d{1,5})\]'}
    - {name: quoted, role: name, pattern: '"(?<name>[^"]{1,255})"'}
    - {name: bare-extension, role: name, pick: last, caseInsensitive: true,
       pattern: '(?<name>[^\s"]+\.(?:part\d+\.rar|vol\d+\+\d+\.par2|par2|rar|r\d{2,3}|7z|zip|nfo|sfv|mkv|mp4|avi|iso|\d{3}))'}
```

That block **is** the default set, transcribed. Four things about the shape:

- **`role` splits the three answers**, and they are evaluated independently.
  Supplying a rule for one role replaces that role's built-ins and leaves the
  others alone — because the realistic edit is *"a new scheme appeared, I want
  one more filename pattern"*, and under whole-list replacement that user
  silently loses the part counter and gets a queue of releases with no `(n/m)`.
- **`pick` is most of why this was worth doing.** "First match or last match" used
  to be expressed as *which helper function a call site happened to use*, and it
  is what keeps a `(2011)` in a title from beating a genuine `(1/9)` at the end.
- **Order within a role is the order rules are tried, and the first one that
  *matches* ends the role.** That reproduces "a quoted filename wins outright"
  with no special case — including its edge: a quoted rule that captures only
  whitespace still wins, and still yields nothing. Shipping behaviour, pinned by
  a test; changing it is its own decision.
- **Named captures are required** (`index`/`total` for a counter, `name` for a
  filename). Positional groups are refused, because wrapping an alternation
  shifts group 1 and 2 by one and the parser then reports *part 97 of 3* — and a
  component whose contract is "returning nothing is normal" has no error channel
  to say so on.

**A rule that will not compile is dropped and the rest of the set stands**, which
is the opposite of `IndexerFeedMatch`'s rule and deliberately so. There, a broken
*reject* pattern matches nothing and lets everything through, so failing open
spends the user's money unattended and the safe direction is to stop the feed.
Here the worst a missing rule can do is return no filename — an outcome
obfuscated posts produce every day — while failing closed would stop every NZB in
the queue over one typo, at parse time, with no status line to read the reason
from. A role left with no usable rule falls back to its built-ins, so a config
that is entirely typos behaves like no config at all. A role whose rules are all
*disabled* keeps none: that is a decision rather than a mistake, and the only way
to turn a role off.

`sanitizeSubjectPatterns()` deliberately does **not** compile-check, so a broken
pattern survives a save: the typo is the only record of what the user meant.
It drops only what is structurally unusable — no name, no pattern, no role. The
asymmetry is intentional: a bad regex still says what was intended and has
somewhere to sit; a rule with no role is not a rule, and guessing one would fill
the wrong field. The *name* is trimmed; the **pattern is not**, because a regex
can legitimately end in a literal space.

⚠️ **A subject over `kMaxSubjectChars` (2048) is skipped, not truncated.** Qt 6
exposes no PCRE2 `match_limit`, `depth_limit` or offset limit and no way to
interrupt a match in progress, so with user-supplied patterns catastrophic
backtracking is exponential in the input length and there is nothing to catch it;
the length cap is the only bound available. It skips rather than truncates
because the part counter sits at the *end* of the line, so truncating would
corrupt exactly the field the cap exists to protect.

The compiled set is cached per thread — the module has no mutex and keeps it —
and thrown away when `Preferences::usenetSubjectPatternsRevision()` moves.
⚠️ That counter is process-scope and never reset: `Preferences::load()` replaces
the whole `Data` object, so a member would go back to 0 and a cache still holding
0 would look *fresh* with stale rules. `NzbFile::parse()` takes the set once for
the whole document, which costs a refcount and buys the property that one NZB is
read by one rule set even if the preference changes mid-parse.

Hand-edited only: no Options page and no IPC. The GUI never parses a subject —
`eMule::Usenet` is linked by the daemon alone — and `usenetServers`, the one
existing structured list in this block, already travels over its own opcode for
the same reason. One surprise worth knowing: yaml-cpp re-quotes patterns in its
own style, so a single-quoted `'\d'` comes back double-quoted after the next
save. Semantically identical.

## What a file is called

An obfuscated release posts its files under meaningless names. A *partially*
obfuscated one scrambles the subject and leaves `=ybegin name=` real, which is
what six places in this module assumed when they said *"the only place an
obfuscated post's real name appears"*. A **fully** obfuscated one scrambles that
too, and there the assumption fails silently: `volumePositionOf()` answers -1,
`UsenetStreamIndex` reads the file as "not an archive volume", and the release
becomes not-seekable with no error anywhere.

> **Three of the four sources for a filename are claims. Only the PAR2 one is a
> proof — and a proof that arrives late is worth less than a guess that arrives
> on time.**

- **Rank by evidence, not by convenience.** The subject is a stranger's
  formatting, `=ybegin name=` is what the poster typed, the NZB's `<file>` is the
  indexer's copy of the subject. Only the PAR2 name is checkable against the
  bytes on disk, so it outranks the other three and is the only one allowed to
  overrule a name already in use. `UsenetQueueItem::bestFileName()` is that
  order, in one place: six sites used to open-code it with three different
  fallback chains between them, which is how five end up right and one wrong.
- **A name nobody has yet is a name nobody can act on.** Volume ordering, the
  unpacker's first-volume pick, the seek map and the GUI all ask *during* the
  download. A name recovered at post-processing time is recovered after every
  decision it could have informed.
- **Never fewer names than today.** No par2 in the release, a damaged index, an
  ambiguous match, `EMULE_HAVE_PAR2` off — every path degrades to the old chain.

### Reading the set before verifying anything

`Par2Repairer::PreProcess()` ends in `CreateSourceFileList()`, so **the real
names are known with no data scanned and no source file needing to exist**.
`Par2Verifier::listFiles()` stops exactly there. PAR2 identifies a file by its
length plus the MD5 of its first 16 KiB, which is why a release can be named
while it is still downloading.

Four traps in reading it, each silent when got wrong:

- ⚠️ **`MD5Hash::print()` prints the bytes reversed** (`md5.cpp:53`, `hash[15]`
  first). Copy `hash[16]` raw or nothing ever matches a `QCryptographicHash`
  digest.
- ⚠️ **`sourcefiles[i]` can be null** — `CreateSourceFileList()` pushes its map
  lookup unconditionally, so a set that lost a description packet leaves a hole
  rather than a shorter vector. A partial list is normal, not an error.
- ⚠️ **PAR2 stores paths, not bare names**, and a `.par2` off Usenet is as
  untrusted as an NZB subject. Everything goes through `QFileInfo::fileName()`
  and `sanitizeName()`.
- **`PreProcess()` also loads the `name.*.par2` siblings**, which is free during
  a download (only the index is on disk) and not free afterwards.

Matching lives in `Par2NameIndex`, which needs no par2 headers and therefore
compiles in a build without the library. Its rules: the key is **(length,
hash16k) together** — length alone is useless, since every volume of a RAR set is
the same length but the last, and that is the whole reason the hash is exposed;
**two matches mean no name**, because a set can hold two byte-identical files
under different names and guessing swaps them; **one name, one file**, claimed in
NZB file order so a restart reaches the same answer; and **not matching is
normal**, since a set covers the archive volumes, not the `.nfo`, the sample or
itself.

The name is learned in `markSegmentDone()` once `availableFrom(0)` reaches the
window — **`availableFrom(0)`, never `QFileInfo::size()`**, because the scratch
file is written at absolute offsets and its length on disk is the highest byte
written, not how much of it is real. (`ArticleWriter::reserve()`, which would
have preallocated it, has no production caller — tests only.) It is applied by
`sealFile()`, which stays the only thing that puts a name on disk.

⚠️ **A name is only ever set for a file that has not been sealed.** Setting one
afterwards would leave the GUI, `fileNameOf()` and the unpacker's directory scan
disagreeing about the same file, and renaming a sealed file instead would strand
any direct-unpack run keyed on its old base name. When it happens anyway,
post-processing's rename pass is still the answer.

### ⚠️ The index `.par2` is scheduled last, and that is backwards here

`rebuildPlan()` orders PAR2 last: *"the index .par2 is small and only interesting
if something came up short."* Exactly right for a release whose names are
readable, exactly backwards for one whose names are not — it is then the only
file that says what the others are. It is already in the plan either way, so
hoisting costs no extra articles, only one small file of latency.

`looksObfuscated()` decides, from `bestFileName()` so it sharpens as article
names land: true when no payload file has a name that is either an archive volume
or a playable media name. Crude on purpose, because the cost of being wrong is
asymmetric — a false positive delays the first playable byte by about one
article, a false negative is exactly the old behaviour. It is still not
unconditional: hoisting on every release would spoil streaming's "the first bytes
fetched are byte 0 of the movie" for no gain.

Three hazards in that restructure:

- ⚠️ **The hoist tests `isPar2() && !isPar2Volume()`.** Written as `isPar2()` it
  would drag *requested* recovery volumes to the front of a round-two plan, ahead
  of the payload the repair is for.
- ⚠️ **The prefetch takes enough articles to reach 16 KiB, not one.** "One
  article" is only the same thing while articles are ~700 KB; with small ones
  every name would stay unsettled forever and the prefetch would ask again every
  tick. Sized from the NZB's *encoded* bytes, which over-estimates the payload,
  and capped so a pathological NZB cannot turn the prefetch into the download.
- ⚠️ **`kRarHeaderProbeBytes` is 8192 and PAR2 needs 16384.** A naming prefetch
  that asks for 8 KB can never produce `hash16k`. With ~700 KB articles both
  arrive together and the bug never shows.

`UsenetStreamIndex` had to stop treating "the name is non-empty" as "the name is
an answer": a junk name silently orders a set from a *subset* of its volumes,
which is a mis-map rather than a miss. `nameIsSettled()` is the replacement — a
PAR2 name, or no PAR2 index in the release at all, or the bytes PAR2 would need
were there and it still did not match.

### What the extra-file list was costing

`Par2Verifier::run()` used to collect the sibling files **only for the rename
pass**, so `verify()` and `repair()` were handed an empty extra-file list. par2
scans only the list it is given, and `renameonly` stops at the first partial
match — so on an obfuscated release a **damaged** file was invisible to every
pass: the rename refused it for not being perfect, and verify was never told it
existed. It was booked as an entirely missing file, `missingBlocks` absorbed its
whole block count, and `requestPar2Volumes()` bought recovery volumes for damage
that did not exist. Measured on a 15-block file with 2 damaged blocks: 2 blocks
needed with the list, the whole 15 without, and a release that repairs fine
instead reported *"Not enough recovery data: 7 more block(s) needed"*.

Every pass gets the list now, rebuilt on each call — the rename pass renames
files on disk between calls, and par2 skips a path it cannot open, so a reused
list degrades in silence. It costs nothing on a healthy release: `VerifyExtraFiles`
is entered only when files are already missing, and `diskFileMap` skips anything
matched as a target. One consequence for readers: `verify()` can now report
`renamedFiles > 0` having renamed nothing, because `RenameTargetFiles()` lives
inside `if (dorepair)`.

### ⚠️ A repair leaves the release on disk twice

par2 does not overwrite what it repaired, and `purgefiles` is off — deliberately,
since par2's purge deletes `RemoveParFiles()` too and the queue's on-demand
recovery round would reload a set that is gone. So a repair leaves a second copy,
in one of two shapes:

- The damaged file had the **right** name: `RenameTargetFiles()` moves it aside
  as `<name>.1` and reports it. This happens on *every* repaired release, not
  only obfuscated ones. `Par2Result::backupFiles` carries them and the
  post-processor deletes them after each call that makes them.
- The damaged file had an **obfuscated** name: par2 never matched a target to it,
  built the correct file from the blocks it could read, and left the original
  exactly where it was — **unreported**. This is the case that reaches
  `payloadFilesIn()`, which returns every non-`.par2` file in the work folder, so
  a release with no archive to unpack published a known-damaged copy of the
  payload beside the good one and offered it to ED2K peers. Precisely the failure
  § What gets published says phase 4 closed.

`removeStaleCopiesOfCoveredFiles()` answers the second: a covered file is
published under its covered name and nowhere else. `hash16k` identifies an
undamaged copy exactly; a damaged one cannot be hashed into place — the damage
may be *inside* the identity window, which is the very reason it was never named
— so a file of exactly a covered file's length, under a name the set does not
use, while the properly named file is right there, counts too. It only ever
deletes when the good copy exists, because otherwise that file *is* the release,
merely misnamed.

### The one release this cannot help

A post that obfuscates the `.par2` **subjects** as well leaves nothing with the
extension, and `NzbFileInfo::isPar2()` is a substring test over the filename or
the subject — so the queue never recognises those files either, and such a
release was never verified, never repaired and never unpacked, in silence. The
hoist genuinely cannot help: you cannot fetch first a file you cannot identify.
Post-processing can, and does — every PAR2 packet begins with the eight bytes
`PAR2\0PKT`, so `par2FilesIn()` falls back to a content check once the bytes are
on disk. `payloadFilesIn()` applies the same test, or a par2 file under a hidden
name would be published as if it were the movie.

`par2FileName` persists as one optional sidecar key. **`kStateVersion` does not
move for it** — absent means "no PAR2 name known", which is what every sidecar
written before it existed meant, and `load()` refuses a version it does not know,
so a bump would make an older daemon drop the whole queue rather than lose one
name. The *list* is not persisted: it re-derives from the index `.par2` still in
the work directory, and a stored copy would only be a second thing that can
disagree.

### Measured live

`tst_UsenetLiveDownload` over `data/nzb` against a real provider, 2026-09-12. The
observable is *when* `Usenet: PAR2 set names N file(s)` appears. On `Ubuntu.nzb`,
which names itself, it is after `Downloading 72%` — the index `.par2` is
scheduled last, so the proof arrives once every guess has already been acted on.
On the same release with its payload subject replaced by
`81be626b76986c848ac749fd88b1fa9f`, it is before the first progress line, and the
release then downloads, unpacks and publishes identically — 3 files, the same
3267 MB `.vdi`. `vina.nzb` names 1 of its 53 volumes, the other 52 having sealed
before its index landed: the `!finalized` invariant, not a miss.

That also settles `hash16k` against data this repository did not produce — a
recovery set built in 2010 by somebody else's packer, matched by
`QCryptographicHash` over the real first 16 KiB.

Two paths the corpus cannot reach: neither release needs a **repair**, so the
extra-file fix and the backup deletion are proven offline only; and no post in it
has a junk `=ybegin` name, so the *fully* obfuscated case stays a fake-server
test. Junking subjects in a copy of an NZB moves the schedule, not the yEnc
header.

## Tests

| Target | Covers |
|---|---|
| `tst_UsenetSmoke` | the library links and moc ran |
| `tst_NntpSocket` | greeting, auth, TLS modes, watchdog, dot-unstuffing, rate limit; and every "cannot answer for this article" code (430/423/420/412) mapping to `ArticleNotFound` rather than to a protocol error, which is fatal to the connection and would back the whole account off on every probe. Proxy, against `tests/FakeProxyServer.h`: SOCKS5 with credentials **asked by host name** (ATYP 3), HTTP `CONNECT` answering a 407, an application-wide proxy **ignored** by a socket with none of its own, and a refused proxy reported as `ProxyFailed` naming the proxy |
| `tst_NntpServerPool` | level normalisation, rotation, exclusion, backoff; grouped accounts sharing one connection budget and an ungrouped pair keeping separate ones; a server divided down to zero connections keeping its rung but leasing nothing; the shared ladder built from the configured levels only; every lease connecting through the pool's proxy |
| `tst_UsenetReleaseChecks` | the checks without a queue. SFV: comments, tabs, quotes and paths parsed; a clean set in either case; a damaged file named; an expected file gone is damage while **a listed sample nobody posted is not**; nothing present checks nothing; cancel. Unwanted: extension lists normalised; video tags; a program in a media release, **none in a software release**, a double extension without media, an empty list off; a program named `.mkv` fake while an MP4 named `.mkv` and **an unrecognised container are not**. The repair estimate: a covered hole repairable, losing more than the set holds unrepairable, **a file still downloading claiming no damage**, a repost counting its best copy, a repeated part number not a hole, **recovery data counted whatever it is called**, and nothing to judge from being Unknown |
| `tst_UsenetPrefs` | round trip, encryption, ordering, mint condition, caps; news servers following the proxy unless switched off, **eD2K's route unmoved by that switch**, and SOCKS4 reached as SOCKS5; subject patterns: a round trip, **no `subjectPatterns` block written while the defaults are in use** — what keeps every existing preferences.yml byte-identical — a pattern that will not compile surviving a save because the typo is the record of what was meant, one with no role dropped on load, the cap and the name de-duplication, and a trailing space in a pattern *not* trimmed away |
| `tst_UsenetYenc` | CRC vector, every byte value, the four traps above |
| `tst_UsenetSubjectPatterns` | the heuristics once they became data. A 16-row corpus run against a **frozen verbatim copy** of the hand-written parser this replaced *and* against golden columns — including the quoted-whitespace quirk, preserved rather than fixed, and the length cap, which is the one intentional divergence. Policy: a pattern that will not compile skipped rather than fatal, a role whose rules are all typos falling back to the built-ins while one whose rules are all *disabled* does not, positional groups refused, a counter rule missing `total` refused so a part never arrives without one, a pattern matching the empty string refused, **configuring one role leaving the others on their built-ins**, first-match-wins within a role, `pick: last` keeping a year out of the counter, an absurdly long subject yielding nothing rather than hanging, and the same rule set handed out until the preference changes |
| `tst_UsenetNzbParse` | schema, namespaces, HTML error pages, subject heuristics; and the NZB's own shortfall — a short segment run reported with its bytes priced at *that file's* mean, an obfuscated post with no part counter **not** called incomplete, and the par2 index file kept out of the recovery total it carries no recovery data for |
| `tst_UsenetArticleFetch` | a multi-part file assembled **out of order**, byte-identical |
| `tst_UsenetQueue` | the ladder: every account on a level asked before escalating (a sibling used to be skipped on the first 430); an article older than an account's retention skipped while another can serve it, and **fetched anyway when none can** — the fail-safe; a dead *optional* account costing one article rather than the download, and the same account not optional still failing it; an NZB naming itself through `<meta type="name">` when the caller supplies no name. Allowances: raw wire bytes booked against the account that served them and against one that only answered 430; an account over its allowance skipped while a sibling serves, and **never asked**; a spent allowance parking the download rather than failing it or declaring an article missing, and resuming when the limit is raised; a dead *optional* account unable to strand an article the allowance is hiding; and a spent rung parking rather than spending the next one until asked; the 90% warning said once rather than four times a second. Health: STAT issued **before** any BODY; sampling asking exactly one article per file; an article the second server holds **not** counted unavailable — the ladder-blind version pauses four releases that download perfectly; a probe whose answer is wrong changing nothing at all (`missingSegments == 0`, payload byte-identical) — the fail-safe twin of the retention case; a short release added *paused* and downloading in full on resume; a checking item not counted as a live download; and no verdict at all when there is nothing to ask. Duplicates: the same NZB refused under a different name, a manual re-add of a completed release allowed and an automatic one skipped, a repost with fresh message-ids **not** a duplicate, the guard surviving a restart, and the refusal sentence carrying no em dash; `addNzb()` reporting Added / Duplicate / Invalid, and an automatic add queued *paused* when the user asked for that while a manual one is not. Categories: a release completing into its category's folder **and not into the global incoming dir**; a category whose folder was deleted mid-download landing in the global one rather than being stranded, and keeping its label; an index the list no longer holds resolving to "All" rather than reaching past the end; the category surviving a restart through the sidecar, and a sidecar with no `category:` key at all reading as "All"; a deleted category renumbering the live queue **and the sidecars of a queue that is not running** — the case ED2K has no analogue for; and an automatic add still auto-categorised while one the caller categorised is never second-guessed. Proxy: a dead proxy parking the queue with the reason on the item, **no article missing, no connection error charged, no `BODY` issued**, and a settings save that turns it off letting the release straight through; the disk floor **outranking** a proxy the round never reaches, and the proxy wait coming back when space does; a recovered proxy handing a Checking item back to "checking availability" rather than to silence |
| `tst_UsenetWatchFolder` | intake: a file still being written left alone until its size and mtime hold still — the regression files it into `_failed/`, which looks exactly like a corrupt download; a queued .nzb moved to `_processed/` and an unreadable one to `_failed/`; a **duplicate** treated as processed rather than failed; files already present when the daemon started picked up, since no watcher event ever fires for those; and the two output folders never rescanned — otherwise the scanner re-queues its own output forever |
| `tst_NzbDrop` | what a drop is: .nzb files and http(s) links whose *path* ends .nzb, several at once, and an indexer link with a query string after it — against `.emulecollection`, `server.met`, an `ed2k:` link, an ftp URL and a plain-text drag, all of which must fall through untouched |
| `tst_PendingOpenQueue` | what waits for the daemon on a cold start: that the kind survives the wait (a path replayed as a link reaches the eD2K importer and is lost), that a re-entrant release cannot replay anything twice, arrival order, and the cap |
| `tst_FileAssociation` | what registration *would* write, on every platform at once: the desktop entry claiming both .nzb and the ed2k scheme, an `Exec=` path with spaces quoted, and the registry values staying under `HKEY_CURRENT_USER` — an `HKLM` write needs elevation a portable zip cannot ask for |
| `tst_UsenetUsage` | the meter, without a socket or a thread: the billing day clamped to a short month and coming back out of one; a daemon off across eleven billing days resetting **once**, and the second call a no-op; a clock that moved back refusing to resurrect a spent period; a block account never resetting; a flush that runs three times changing nothing and an unclean exit losing only the unflushed session; a correction surviving the next flush; re-applying the server list keeping the counters; and the meter surviving a port and username edit — the whole reason it is keyed by `accountId` |
| `tst_UsenetNzbUrl` | the URL intake: `file:`, `qrc:`, `ftp:`, `data:`, hostless and relative links refused; a **private address accepted**, pinned so nobody blocks the self-hosted-indexer case; the display name stripped of `.nzb`/`.gz`, percent-decoded, and empty for API-style URLs; a plain and a gzipped `.nzb` fetched; a 404 reported as a *download* failure rather than a parse one; and no callback after its context dies |
| `tst_UsenetPar2` | verify, repair, rename and the blocks-needed figure, against sets built in-process by `Par2::par2creator`. The file list: every name returned with **every source file deleted first**, `hash16k` equal to a `QCryptographicHash` digest of the first 16 KiB — the case that fails the moment anyone reaches for `MD5Hash::print()`, which emits `hash[15]` first — a short file hashing whole for both fields, and a truncated index answering rather than dereferencing the null `CreateSourceFileList()` leaves behind. The defect: a damaged obfuscated file costing **the 2 blocks it lost and not the 15 it has**, and a repair reporting the `.1` it moved aside — plus the second, unreported leftover, the obfuscated original par2 never touched. `Par2NameIndex`: length and opening bytes keyed *together*, two identical files in a set left unnamed, and a scratch file one byte short of the window not matched |
| `tst_UsenetUnpack` | volume-set detection across all three naming schemes; path-traversal and reserved-name refusals; a multi-volume RAR set extracted through the whole list, and a set skipped because it was unpacked during the download |
| `tst_UsenetPostPipeline` | a repair discarding what was unpacked while downloading, and corrupt volumes never reaching the published release; phase 4 end to end: a healthy release fetches **no** recovery volumes; a damaged one fetches them, repairs, unpacks and publishes the payload alone; an unrepairable one publishes nothing. Names: a fully obfuscated release — par2 built over the real names, the payload then renamed to hex and only then posted, so the subject *and* `=ybegin` are junk — carrying `par2FileName` and sealed under the real one, which is what separates this from the post-processing rename that would also end up with the right name on disk; the index `.par2` fetched **before** the payload and dragging no recovery volume with it; a release with no par2 scheduled exactly as before; a damaged obfuscated release buying **≤4 recovery blocks for 2 damaged ones** rather than 15; and `<name>.1` never published, with and without the cleanup preference. Without PAR2: an SFV mismatch failing the release (and the check off publishing it), a clean SFV publishing the payload but not itself, an unposted sample listed and still publishing. Held back: a program in a movie release, **a program in a software release published**, an archive whose only member is `movie.mkv.exe` **never extracted**; through the queue, a stopped release paused with its reason, **left alone by a bulk resume and a password**, keeping both across a restart, and published by the user's Resume. Unrepairable: six volumes with the first gone and two recovery blocks, stopped **before a single `BODY` for the second volume**, with rename off; Resume downloading it anyway and failing it **without buying the recovery volumes** that could never cover it. The cycle re-entering a queue that is waiting: paused mid-verify, both ways back into fetching — for recovery volumes and for a file the user skipped — say **why** instead of going blank, buy nothing while they wait, and finish when the pause lifts |
| `tst_UsenetStream` | phase 6a: part-number dispatch order over a shuffled NZB, a failed article retried *before* later ones, interval merge and the hole that stops it, `written` surviving a restart, the previewable predicate. Phase 6b: a stored RAR set resolving to the file inside it and reading back byte-identically across volume boundaries; **a seek to 80% that never issues a `BODY` for the volumes it skipped** — the exit criterion, asserted; a compressed set saying why; a `.001` split set. Multi-file sets: every inner file enumerated with its ordinal, each mapping to its own bytes across a volume boundary, **a set whose first file is an `.nfo` streaming the movie with no `entry=` named**, and a listing on a paused item that neither fetches nor guesses. Preview from the extraction: a **solid stored** set — refused by the map, extractable by libarchive, which is the only fixture that separates the two sources — streamed out of `_unpacked/` with the archive's own declared size as the total, listed and marked playable, and a control case proving the same set is refused when nothing is extracting it |
| `tst_RarReader` | phase 6b: RAR4 and RAR5 stored volumes — name, method, packed/unpacked sizes, data offset, split flags, `LHD_LARGE` sizes past 4 GB, RAR5 multi-byte vints; compressed read but not stored; solid and encrypted refused with a reason; every truncated prefix asking for more bytes rather than reading past the buffer; a volume carrying two file headers listing both; resuming mid-volume at a computed offset, and a resumed block at a wrong address caught by its header CRC |
| `tst_UsenetPassword` | password-protected releases: a header-encrypted RAR failing rather than completing empty with its volumes deleted; a data-encrypted one naming the password; ZIP decrypting through libarchive and refusing a wrong passphrase; an encrypted 7z going through `ExternalUnpacker` and coming back byte-identical; a configured unpacker that is not a binary refusing rather than substituting; stored paths harvested out of a staging directory; an incomplete 7z set listing nothing; the biggest playable member chosen over the sample; the `{{password}}` name convention reaching the queue; manual beating the NZB where automatic does not; a failed release retrying — without re-downloading — the moment a password is set; and, end to end, a genuinely encrypted release posted as articles, downloaded, unpacked with its password and published byte-identically |
| `tst_UsenetDirectUnpack` | extraction that keeps pace with the download: volumes offered one at a time, a run blocking on one that has not landed and resuming when it does, cancel unwinding without leaving half a file, a set that ends short failing rather than hanging, and end to end — the payload complete before post-processing starts, and the same release unchanged with the option off. A blocked run reporting bytes that read back as the payload's prefix and naming the volume it needs; **a set whose NZB scrambles its volume order still unpacked while downloading**, which is what the sealed-volume replay exists for |
| `tst_UsenetDetailsDialog` | the release details view, driven through `applyDetails()`: every NZB file becoming a row with its article tally, an **unassessed health rendering as `—` and never as 100%**, a per-release fact that says so when the files disagree rather than quietly naming the first one, numeric columns sorting by magnitude rather than by text, and PAR2 rows greyed with the theme-following brush while a payload row carries no explicit brush at all |
| `tst_UsenetArchiveEntryDialog` | the chooser: unplayable rows listed but neither selectable nor enabled and carrying the disabled palette brush, a playable row with no explicit brush at all, the chosen ordinal surviving a re-sort, and a single playable file answered without ever showing a window |
| `tst_UsenetLiveConnect` | real TLS + auth (`live`) |
| `tst_UsenetLiveFetch` | a real `.nzb` downloaded and hashed (`live`) |
| `tst_UsenetLiveDownload` | every `.nzb` in `EMULE_NZB_DIR` through the **real queue** against a real provider: download, PAR2, unpack, publish — then assertions that no `.par2`, no volume and no `.usenetpart` reached the incoming directory and that the scratch tree is gone (`live`) |
| `tst_UsenetLivePreview` | the same releases previewed **while they download**, asserting whichever of three outcomes the release earns: a mappable member streamed out of its volumes; a member the map refuses streamed out of the extraction instead (`vina.nzb` — a compressed 53-volume RAR5 set — served 11.6 MB of its 1.38 GB mp4 over a real `Range` GET, 28 s after the row started); or a release with nothing playable in it refused with a reason and a 406 (`Ubuntu.nzb`, whose one volume holds a `.vdi`). The target is volume one of the largest set, not the first non-PAR2 file — a release that posts its own `.nzb` alongside would otherwise have that previewed instead (`live`) |

Offline tests run against `tests/FakeNntpServer.h`, a scriptable in-process NNTP
server modelled on NZBGet's `daemon/nserv/`. It exists because the interesting
cases are the refusals — 400 at the greeting, 481 on auth, 430 for a missing
article, a server that simply stops answering — and none can be provoked against
a real provider on demand.

The live tests skip when their environment is unset, so an unconfigured checkout
never looks broken:

```sh
export EMULE_NNTP_HOST=news.example.com EMULE_NNTP_USER=… EMULE_NNTP_PASS=…
export EMULE_NZB_DIR=/path/to/some/nzbs      # one data row per .nzb in it
cmake -S . -B build -DEMULE_LIVE_TESTS=ON    # cached, so later build.sh runs keep it
ctest --test-dir build -L live -R tst_Usenet
```

`.env` in the project root is read for all of these, so the variables need not be
exported by hand — see `.env.example`. Two things are worth knowing before
pointing `EMULE_NZB_DIR` at something large:

- **`tst_UsenetLiveDownload` fetches the whole release**, every row, and that is
  the point of it. `EMULE_NZB_MAX_MB` skips rows above a size;
  `EMULE_NZB_TIMEOUT_MIN` (default 45) bounds each one. Everything it writes
  goes into a temporary tree that is removed, and checked to have been removed,
  when the row ends — the user's own incoming directory is never touched.
- **Never set `EMULE_NNTP_MAXCONN` above what the account allows.** A provider
  answers an over-subscribed login with `502 Too many connections`, and the
  server pool then backs that server off for a minute. Consecutive rows are the
  usual way to trip it: the previous row's sockets are not yet released
  server-side when the next one opens its own.

## Adding a source file

The CMake glob picks new files up; MSBuild does not. Run
`scripts/sync_module_vcxproj.py` to regenerate the source lists in
`src/usenet/emuleusenet.vcxproj(.filters)` — a `Q_OBJECT` header must land in
`<QtMoc>`, not `<ClInclude>`, or moc never runs for it.

⚠️ Adding a `Q_OBJECT` header to this target can leave CMake's AUTOMOC parse
cache stale: the `.cpp` compiles, the header is silently not moc'd, and the link
fails in a *consumer* with a missing vtable. Neither re-running `cmake` nor
rebuilding fixes it. Check
`build/src/usenet/emuleusenet_autogen/mocs_compilation.cpp` for the expected
`moc_*.cpp`, and if it is absent delete
`build/src/usenet/emuleusenet_autogen` and rebuild.


## Threading

One `QThread` per worker, each owning its **own** `NntpServerPool` and its own
sockets. The pool has no locking and its sockets have thread affinity, so sharing
one is not an option — a pool per worker is what makes the design lock-free.
Nothing but queued signals crosses a thread boundary, and there is not a mutex in
the module.

**The connection budget is divided, not replicated.** N workers each honouring
`NewsServer::maxConnections` would open N times what the user configured, and
exceeding a provider's limit gets an account throttled or suspended — a worse
failure than downloading slowly. `UsenetQueue` hands each worker a *copy* of the
server list with `maxConnections` already divided, remainder distributed to the
first workers, so the sum equals the configured limit.
`tst_UsenetQueue::connectionBudgetIsDividedNotReplicated` asserts it.

Worker count is `clamp(min(idealThreadCount, totalConfiguredConnections), 1, 8)`,
so two configured connections get one worker rather than four idle ones.

Teardown is `finished → deleteLater`, then `quit()` + `wait()` inside
`UsenetSession::stop()`, which `DaemonApp` calls before the IPC server goes away.
A socket destroyed off its own thread is a crash, not a leak.

**Workers carry a generation, and every result is checked against it.** Worker
slots are indexed, the indices restart at 0 on each rebuild, and *every settings
save rebuilds them* — while the results the old workers already posted are still
in the queue. Each worker's connections capture `m_workerGeneration` (bumped by
`stopWorkers()`), so a late result can still be counted but never booked against
its replacement's slot. A failure from a torn-down worker — or any result flagged
`aborted` — spends no retry and blames no server: `stopWorkers()` has already
cleared the in-flight markers and rewound every plan cursor, so the segment is
simply dispatched again. Without that, roughly seven settings saves during one
article failed the download with "Shutting down".

## Failover

The one rule, and it must not be re-derived anywhere else:

- `escalatesToNextLevel(error)` — this server cannot supply the article: no copy
  (430), or a copy that does not decode (`ArticleCorrupt`). Retry at `level + 1`,
  adding that account to `ignoreServers` so the escalation never asks it twice.
- Anything else is a *connection* fault. Retry the **same** level; the worker has
  already backed the server off, so a sibling account picks the article up.

**A damaged article is content, not transport.** The decoder's verdict is read
after the terminating "." on a socket that is already back to Ready, so the
connection is in sync and the copy on that server will be just as damaged next
time. Treating it as a transport fault (until 2026-09-12) dropped a healthy
connection, backed the whole account off for 60 s — a single-provider setup then
stalled — and after `kMaxTransportRetries` failed the *download* over one
article that PAR2 would have repaired. NZBGet does the same by default
(`RetryOnCrcError=no`). Every yEnc verdict routes this way, not just CRC:
truncated, malformed and "no binary data" are equally unusable.

Getting it backwards either hammers a fill server every time the main provider is
briefly busy, or leaves a paid block account never used.

An article missing on every level is recorded in `missingSegments` and its bit is
set in `done` — the bit means **resolved**, not "arrived". The file is then short
by design; PAR2 repair in phase 4 is what fills the hole.

**The rung is derived, never stored.** `SegmentAttempt` carries the accounts
already tried and `nextServableLevel()` computes the rung from them on every
dispatch. That is what makes "escalate only when *every* account on this level
said 430" true by construction: while an untried sibling remains, the same rung
comes back. A stored rung has to be incremented by somebody, and it used to be
incremented on the *first* 430 — sending the article to a paid fill server past
an idle level-0 sibling. It also removes the second place the ladder was
numbered: `nntpLevelLadder()` is now the only definition, shared by the queue and
by every worker's pool, and a worker's divided slice keeps every row precisely so
the two cannot number the rungs differently.

### Retention is a guess, so it may never be a verdict

There is no retention command in NNTP. `GROUP` returns article *numbers*,
`LIST ACTIVE.TIMES` returns group creation times, and nothing reports an expiry
policy — so `NewsServer::retention` is a figure the user copied off a provider's
pricing page. Nor do providers *enforce* it: retention is when articles age out
of the spool, and what you get for an expired one is `430`, the same answer as a
takedown, an incomplete post, or an article that never propagated.

The two failure modes are therefore not symmetric. Asking a server that cannot
have the article costs one round trip. *Skipping* a server on a wrong figure
turns a fetchable article into a missing one, which spends PAR2 blocks or fails
the release. So:

> Retention may reorder, and may skip an account while another could still serve
> the article. It may **never** be the reason an article is declared missing.

`nextServableLevel()` implements that directly: it looks for the lowest rung with
an untried account that retention says could hold the article, and if no such
account exists *anywhere* it returns the lowest rung with any untried account at
all and tells the caller to send no retention exclusions. The relaxed search
restarts at rung 0 deliberately — the last account tried before giving up is the
one retention had skipped. `tst_UsenetQueue::aRetentionGuessNeverMakesAnArticleMissing`
is the assertion, and it is the reason the field is safe to honour.

### "Missing" is a verdict about a moment, not about the post

`markSegmentMissing()` is the one terminal in the module, and every filter above
is forbidden from reaching it for exactly that reason. But the verdict it records
is narrower than it reads: *no server **I had** has this, **when I asked**.* Add
a block account a week later and the sentence is no longer true, and until 2026-09-12
nothing could revisit it — the done bit means "resolved", `rebuildPlan()` and
`dispatch()` both skip a set bit, and nothing in the module ever cleared one.

So `UsenetFileState` carries a second bitmap, `missing`, saying which of the
resolved segments are holes rather than arrivals. `retryMissingArticles()` clears
the *done* bit of each one and lets the scheduler ask again, from rung 0 over the
accounts configured now — `markSegmentMissing()` having already erased the
attempt, there is no stale `tried` list to get in the way.

Three things make that safe, and each of them was a way to be quietly wrong:

- ⚠️ **`missingSegments` is not a damage estimate.** Direct unpack (`:2038`), the
  sealed-volume replay (`:2114`), `sealedVolumesOf()` for the encrypted preview
  (`:2601`) and `job.hasMissingSegments` (`:2783`) all read it as *this file has
  zeros in it right now*. So a retry does **not** decrement it when it arms a
  segment; `markSegmentDone()` does, when the bytes are actually on disk.
- ⚠️ **Nothing is un-sealed.** The file keeps `finalized`, its sealed name and its
  padded length, and the re-fetched article overwrites its own zeros at the
  absolute offset — `ArticleWriter` opens `ReadWrite` and never truncates. A
  second `sealFile()` would rename `movie.mkv` to `movie (1).mkv`, because
  `uniqueDestination()` tests `QFile::exists()` with no identity check and a file
  collides with itself; strand every `DirectUnpackRun` keyed on the old base
  name; and leave `onTick()` rebuilding the plan four times a second while the
  file is not finalized. Completion instead arrives through the "nothing pending"
  branch, the same path a restored item uses.
- ⚠️ **The file has to still be on disk under a name we know.** Post-processing
  renames (par2's rename pass repairs as a side effect), repairs and deletes
  files without writing the new name back into `tempPath`, and
  `ArticleWriter::open()` *creates* what it cannot find — so a careless retry
  would build a fresh sparse file holding one article, beside the real one, for
  the unpack and share scans to find. The re-arm therefore requires the file at
  `tempPath` to be `declaredSize` long, rescues it by `bestFileName()` when par2
  renamed it, and otherwise refuses that file and says so.

**The invariant** is `missing.count(true) == missingSegments`, checked before a
retry is allowed. A sidecar written before the map existed fails it — a count
with no map — and the retry refuses rather than guessing which articles it stood
for, which is also what keeps the first run after an upgrade quiet.

**When it runs.** Resume, on a Failed item, when the account list differs from
what it was at the failure (`failedLadder`, a digest of enabled `NewsServer::key()`
values recorded by `failItem()`). Resume has three callers — the button, the
category-wide resume where "All" is the whole queue, and `setItemPassword()`,
which resumes so the new passphrase is tried — and re-asking a DMCA'd release's
four thousand dead articles across three rungs on each of them is not what any of
the three asked for. `retryMissingArticles()` is public and unconditional for a
future explicit action; nothing calls it that way yet.

A second failure says what the retry achieved — *"…; none of the 5 missing
article(s) came back"* — because otherwise it is word for word the first one and
the button reads as broken. What *did* come back is kept, so a later retry starts
from the smaller hole set.

### A disk floor waits too, and may never be a verdict

The fourth member of the family, and until now the counter-example.
`Preferences::checkDiskspace()` and `minFreeDiskSpace()` had Options controls and
no reader anywhere — not here, not in eD2K — so what actually happened when a
volume filled up was this: `ArticleWriter` reported a short write, `ArticleFetcher`
called it a `ProtocolError`, `isFatalToConnection()` called that fatal, and the
account was **backed off for 60 s and named in the log**. After
`kMaxTransportRetries` the item either failed or, if the serving account happened
to be `optional`, the article was booked
`markSegmentMissing(… "optional server unavailable")` — a hole invented in a
release that was fine, and PAR2 blocks bought to repair it.

> A floor is a filter. Below it the queue **waits**, with the reason on the item;
> it never touches `tried`, never spends a retry, never backs off an account and
> can never reach `markSegmentMissing()`.

- `NntpError::WriteFailed` is its own value, neither escalating nor fatal to the
  connection (the body was read to its terminating `.`; it was the writer that
  failed). `handleSegmentFailure()` gives it a branch that requeues the segment at
  the cursor and re-measures the volume immediately. After `kMaxWriteFailures`
  with space to spare — a read-only volume, a permission — the item fails with
  the local error naming the folder, which is the one thing the old path never
  did.
- `refreshDiskState()` runs from `onTick()` *and* from `dispatch()`, self-gated to
  `kDiskCheckIntervalMs`, because an add dispatches immediately and the first
  release of a session would otherwise be fetched before anything looked at the
  volume. Below the floor it sets `m_diskBlocked`, writes a reason on every
  waiting item and logs once; it unparks only at floor + `kDiskUnparkHeadroom`, or
  a resumed queue re-parks after one article.
- ⚠️ **The edge is not the whole story.** The reason is written when the floor is
  *crossed*, but items start waiting long after that — an add, a resume, a health
  verdict, a retry, a repair cycle — and each of those used to clear the reason
  and leave the row blank, or, with the probe on, leave it saying "checking
  availability" for a check `dispatch()` was never going to reach. Every one of
  those paths now calls `noteWaitReason()`, and the floor's own sweep skips a
  quota-parked item: nothing caches the allowance sentence, so overwriting it
  would silence that row until the next rollover.
- **Two volumes, two checks.** Scratch and Incoming are knowingly on different
  filesystems — `stageForPublish()` renames and falls back to a copy when that
  crosses one — so `beginPostProcessing()` checks the incoming volume as well and
  leaves the item where it is until there is room. Failing there would throw away
  a download that finished.
- ⚠️ **A path that does not exist yet is not an unreadable volume.** `QStorageInfo`
  answers for a missing path by being *invalid*, and a scratch tree is created on
  first use, so `tryFreeDiskSpace()` climbs to the nearest existing ancestor
  before measuring. Without that the guard parks a fresh install forever — which
  is exactly what it did the first time `spaceReturningResumesWithoutTheUser` ran.
  The climb is by path string, because `QDir::cdUp()` refuses to move into a
  parent that does not exist either.
- The unknown case is a park, not a pass: a volume nothing can measure is not one
  to keep writing to, and waiting is always recoverable where a wrong verdict
  about an article is not.

**eD2K got the same floor**, where the machinery was already written and unused:
`PartFileStatus::Insufficient`, the `status()` overlay, `pauseFile(true)` with its
own log line, and `resumeFile()` clearing `m_insufficient` — with **zero callers**
until `DownloadQueue::checkDiskspaceTimed()` (a minute, against MFC's fifteen)
started sweeping. Its companion fix matters more: `PartFile::flushBuffer()`
ignored the return of `seek()`, `write()` and `flush()` and cleared the buffer
regardless, so a full disk silently left a hole that failed its MD4 later and was
charged to the **peer** that sent it. The buffer is now kept and retried, and a
failed write asks the queue to re-check the volume.

### A stop is a pause with a proof, never a guess

Two checks may now stop a download on their own — the first things in the module
allowed to — and they share one piece of machinery.

> Every filter above may never be a verdict. These two may act only because they
> act on something provable, their default is to **pause** with the reason on the
> row, and the user's Resume overrules them for that release for good.

**The machinery.** `stopForCheck()` reads the setting for the check
(`unrepairableAction` / `unwantedAction`: keep going, pause, fail), records
`stopReason` and `stopDetail` on the item — optional sidecar keys, `kStateVersion`
unchanged — and pauses or fails it from *any* status, since one check fires at the
end of post-processing where `pauseItem()` refuses. The detail is loaded back into
`stalledReason` for a paused item, so the row still says why after a restart.
`resumeItem()` now takes a `ResumeIntent`:

- **User** sets the matching bit in `checksOverridden`, which is never cleared;
- ⚠️ **Bulk** — the category-wide resume, where "All" is every release — refuses an
  item a check stopped. Otherwise one click publishes a fake nobody looked at;
- **Password** (`setItemPassword()`) refuses too: it answers a different question.

Every Resume also clears `stalledReason`, which fixes a health-paused row that went
on saying "only N% looks available" after the user had answered it. And Resume no
longer restarts direct unpack when every file is sealed, or a release stopped after
its download would re-extract every set from volume one first.

**Unrepairable.** `estimateRepair()` (`queue/UsenetRepairEstimate.cpp`) is pure and
answers from bounds — at least N blocks damaged, at most M in the release — and
stops only when N > M. Each input errs towards downloading:

- damage is counted only for a file **every** article of which has resolved,
  priced as the size the PAR2 set declares minus the bytes that arrived, with a
  one-block minimum for a hole whose part number no other segment supplied. NZB
  `bytes` never enter the damage side: they are encoded sizes, and the test
  harness declares 4200 for 3000-byte articles exactly as real NZBs misstate them;
- a repost puts one volume in the NZB twice, so a set file takes the **least**
  damage among its copies;
- capacity counts **every** file the set does not cover as a source, by
  `max(bytes / blockSize, par2RecoveryBlocks())` — an obfuscated post names its
  volumes nothing like `.vol00+01.par2`, and counting by name would call such a
  release hopeless;
- no block size, no matched file, or a segment with no declared size is *Unknown*.

The block size comes from the index `.par2`, which `learnPar2Names()` now reads
whenever PAR2 is on — the rename switch moved into `resolvePar2Name()`, because
`sealFile()` renames from `par2FileName`. With the check active, `rebuildPlan()`
hoists **the index alone** to the front (`hoistIndex`, beside the naming
`hoistPar2`): at the end of the plan the block size arrives when there is nothing
left to save. It is evaluated when a file with a hole seals — synchronously, before
the next article goes out — before post-processing, and on the tick.

`requestPar2Volumes()` had the same disease one level up: it returned
`covered > 0`, so a shortfall no remaining volume could cover downloaded all of
them and failed a round later, under a comment saying it did not. It now totals
what is left first and fails at once. That failure is par2's own count, not an
estimate, so it fails whatever the setting says.

**Unwanted files** are the other check; see § Post-processing.

### An allowance may not be a verdict either — but it waits instead of asking

`NewsServer::quotaKind` / `quotaBytes` cap what an account may spend. The rule is
the retention rule with one deliberate difference:

> An allowance may **never** be the reason an article is declared missing, and
> never the reason an item fails. Where retention falls back to *asking anyway* —
> believing a wrong figure costs one round trip — an allowance falls back to
> *waiting*: ignoring it costs the user money.

Three things make that true, and all three are load-bearing:

1. **`nextServableLevel()` never sees an allowance.** It is the exhaustion
   verdict — `dispatch()` turns its `-1` into `markSegmentMissing()` — so it
   passes `respectQuota = false` into the shared `lowestUntriedRung()` scan, and
   the scheduling question `nextAffordableLevel()` is a separate caller. Wiring
   the allowance into the verdict is the tidy-looking implementation that ruins
   releases: with one spent account, *every* article in the queue is declared
   "no configured server can supply it", the release is assembled full of holes,
   and the item fails. `tst_UsenetQueue::aQuotaNeverMakesAnArticleMissing` is
   that assertion.
2. **The two other terminal transitions gate on
   `quotaBlockedCandidateExists()`.** `handleSegmentFailure()`'s `optional`
   exemption marks an article missing when the transport budget runs out, and
   its `requiredFailure` path fails the whole item — neither consults the ladder.
   So a required account excluded for *spending* rather than for saying no would
   let a flaky block account quietly declare articles missing that the required
   account was holding all along. A retry made while such a candidate exists is
   also not charged: `kMaxTransportRetries` was sized on the premise that a
   sibling picks the article up, and the allowance removed the sibling.
3. **Order: the allowance is hard, retention is soft.** Exclusions are computed
   as `tried ∪ over-allowance` first; retention is added only while something is
   still left after it. Otherwise a retention figure and an allowance can empty a
   rung between them and the download stops dead with nothing to say — a stall
   rather than a missing bit, which the retention test cannot see.

**Out of allowance parks by default.** Falling through to the next rung spends
the *next* account's money to honour a limit on this one, and block credit is
normally dearer per GB than the plan it would be covering — so it is an opt-in
per account (`quotaFallThrough`), and the default is to wait. A parked item drops
out of the dispatch order until a rollover, a usage correction or a settings save
can change the answer; without that the plan cursor reaches the end with work
still pending and `onTick()` rebuilds a 10 000-entry plan four times a second
until the billing day. It also stops counting as an active download, or
`updateBandwidthSplit()` would reserve a quarter of Usenet's share for an engine
fetching nothing, indefinitely.

The ceiling is **soft**: articles already in flight are not aborted, because the
bytes are spent either way and killing the transfer wastes the article too. The
overshoot is one article per connection — about 32 MB at the default 40 — which
is 0.005% of a 1 TB cap and far below the measurement error below. An abort path
would spend *more* money to enforce the limit; the effort went into warning at
90% of the allowance instead, once per bucket per period, because a download that
stops at 3 a.m. is a surprise and a log line the day before is not.

### What the meter counts, and what it cannot

There is no NNTP command that reports usage, so the figure is measured locally:
`NntpSocket::drain()` already meters every inbound line for the rate limiter, and
the counter rides the same line, before the CRLF is chopped. `UsenetWorker`
takes the delta per job, so a 430 and a transfer that died at 90% are both
billed — the provider billed them.

| not counted | size | material at a monthly cap? |
|---|---|---|
| TLS record framing | 0.1-0.2% | no |
| TCP/IP headers, if the provider bills at that level | 2-3% | yes, but unmeasurable from `QSslSocket` |
| commands we send | ~0.015% | no |

So the number reads a few percent **under** a provider's own, and the Options
page says so. Counting `decodedBytes` instead would have been the largest error
in the feature — yEnc expansion, CRLFs and dot-stuffing are 2-4%, in the wrong
direction, and the ladder's 430s would have been free.

Grouped accounts share one meter and one allowance: `group` already means "one
provider reached through two host names", so the plan behind them is one plan,
and the allowance is the smallest configured in the bucket — the same
smallest-member rule the connection limit uses.

### A health check is only ever advice — it downloads anyway

The third instance of the same rule, and the weakest of the three, which is
exactly why it is worth writing down:

> A health check may never be the reason an article is not fetched, and never the
> reason an item fails. Retention falls back to **asking anyway**; an allowance
> falls back to **waiting**; a health check falls back to **downloading anyway**.

`StatCommand` asks whether an account still holds an article without
transferring it. That is worth doing before a 15 GB release is paid for, and the
answer is worth almost nothing on its own: `430` is expired, taken down, never
propagated, or simply not on *this* server, and NNTP has one code for all four.

Two consequences shape the whole implementation.

**A probe that asks one account is worthless.** The ladder exists because
accounts differ, so an article counts as unavailable only when *every rung* has
refused it. That is what `nextServableLevel()` already means, so the probe reuses
it — along with `lowestUntriedRung()`, `nextAffordableLevel()`,
`dispatchExclusions()` and `workerCanServe()`, all of which are stateless in the
pass. The reuse is the correctness argument: a 430 appends to `tried`, the rung
is re-derived, and escalation happens by construction. Deriving it separately
would be writing the ladder a second time and getting it wrong — with a
ladder-blind probe, `tst_UsenetQueue` does not merely mis-report, it *pauses four
releases that download perfectly*.

**The probe may not share the download's state.** `plan`, `planCursor`,
`inFlight` and `attempts` are keyed by one `SegmentKey` space, and both of their
terminals are destructive for a STAT: `markSegmentDone()` would seal empty files
and start post-processing, and `markSegmentMissing()` sets the resolved bit *and*
increments `missingSegments` — simultaneously stopping the article from ever
being fetched and inflating the PAR2 damage estimate. So `ItemRuntime` carries a
second, parallel key space (`checkPlan`, `checkCursor`, `checkInFlight`,
`checkAttempts`), all of it runtime only.

### Two numbers, deliberately not merged

| | source | cost | a shortfall means |
|---|---|---|---|
| NZB completeness | the `.nzb`, via `NzbInfo::shortfall()` | free | the *indexer* never saw those articles; they may still be on every server |
| Server availability | `STAT` across the ladder | one round trip, ~110 bytes | the *servers* no longer hold them |

`hasAllSegments()` returns **true** when `partsTotal` is 0, and that is right: an
obfuscated post carries no `(n/m)` counter, so completeness is unknowable rather
than perfect. Those files land in `NzbShortfall::unknownFiles` and in neither
column — counting them either way is the easiest way to get this badly wrong, and
`anObfuscatedPostWithNoPartCounterIsNotCalledIncomplete` is what stops it.

Neither number is the verdict on its own, because a release ships its own
redundancy. `par2RecoveryBlocks()` reads the recovery-volume sizes straight out
of the NZB names, so a release twelve articles short with 512 MB of recovery data
is fine, and saying "97%" without that context is alarmism. The comparison is
byte-level and deliberately crude — the par2 block size is not knowable until the
index file is downloaded — and a volume the probe found missing stops counting
towards it.

### Sampling, and what it costs

`usenet.healthCheck`: `0` off, `1` sample (the default), `2` full.

Sampling asks about the **first listed article of each file**, capped at 200.
Expiry is wholesale — a provider retires articles by post date and every article
of one posted file shares that date — so one article stands for its file, and a
release of 50-100 files costs 50-100 status lines: two or three round trips deep
across the pool. Full mode is certain and honest about its cost: a 15 GB release
is tens of thousands of round trips.

Probe bytes are billed to the account that answered, through the same
`rawBytes` path as everything else, because the provider billed them. Probe
sockets are excluded from `applyRateLimits()`'s divisor and left unlimited: sixty
bytes needs no token bucket, and counting them would shrink every concurrent
download's share for as long as the probe held a connection.

### What happens to a short release

It is added **`Paused`**, with the reason in `stalledReason` — never `Failed`,
and never refused at the door. Resuming downloads it exactly as if the probe had
never run, which `aShortReleaseIsAddedPausedNotFailed` asserts both ways.

`UsenetItemStatus::Checking` is a real status rather than another runtime flag,
because `isActive()` is false for it — so a checking item leaves the dispatch
order *and* stops counting as a live download for
`UsenetSession::updateBandwidthSplit()`, in one edit rather than at two call
sites somebody has to remember. `quotaParked` is the counter-example; it needed
patching in twice. `describeUsenetItemStatus()` has no `default`, so appending
the enumerator turned every exhaustive switch into a `-Wswitch` warning: a
compiler-enforced list of everywhere that now needs an opinion.

A sidecar that says `Checking` loads as `Queued`, on the same line that already
does this for `Downloading` and the post-processing states. No probe survives a
restart, and re-probing on every start would spend round trips re-learning
advice.

A probe is skipped entirely — no verdict, no delay — when the engine is stopped,
no account is configured, the ladder is empty, or every account that could answer
is over its allowance. `healthPercent` is then **-1**, which is not 100 and must
never render as it.

### Not adding the same release twice

`addNzb()` identifies a release by a digest over its **sorted message-ids**. That
is identity, not a heuristic: two NZBs sharing message-ids fetch literally the
same articles from literally the same servers, whatever their names say. A
*repost* carries fresh ids and is correctly not a match — only the soft key
(folded name plus total size, the shape `IndexerResult::dedupKey()` already uses)
can see that, and it is therefore only ever logged.

Both are re-derived on load rather than stored: every message-id is already in
the sidecar, so a persisted digest would only be a second thing that could
disagree with the first. Without that step the guard is blind to everything
restored from disk, which after one restart is every item there is.

`findDuplicate()` answers with a **verdict**, not a pointer, because there are two
kinds of "already" and confusing them is expensive in both directions:

- `Duplicate` — in the queue and still arriving. Nothing overrides it. A release
  whose articles are on their way cannot usefully arrive twice.
- `AlreadyDownloaded` — finished, or in the history. Whether that is a refusal or
  a question belongs to the caller: a person is asked, and a feed is given the
  verdict, exactly the split `Ed2kLinkImporter::Source` makes for eD2K links.
  `addNzb(force)` suppresses **this verdict and no other** — structurally, not by
  convention: the history is consulted in a branch `force` never enters, and the
  live-queue scan runs before it unconditionally.

Finished-and-still-listed and finished-and-cleared deliberately give the same
answer. They used to differ, which made the behaviour depend on whether the user
happened to have pressed Clear.

The watch folder and the feed poller are the two automatic callers, and both
inherit the split without knowing about it — the guard lives in `addNzb()`, so
every present and future intake path gets it by construction.

⚠️ **A refusal sentence must not contain `" — "`.** `AddNzbUrlDialog` formats a
failed line as `"<url> — <reason>"` and recovers the URL with
`section(" — ", 0, 0)`, so an em-dash-space inside the reason silently truncates
its retry list. `aRefusalSentenceCarriesNoEmDash` is the guard.

### What survives leaving the queue

An item removed from the queue used to leave no trace, so re-adding its NZB
downloaded it again without a word. `UsenetHistory`
(`src/usenet/queue/UsenetHistory.h`) is the Usenet half of `known.met` and
`cancelled.met`, and it answers to the **same two preferences** —
`rememberDownloadedFiles` and `rememberCancelledFiles` — because the question a
person answers in Options is the same question.

`<configDir>/Usenet/history.yml`, beside the `.nzbstate` sidecars and `usage.yml`
(they are globbed by `*.nzbstate`, so a `.yml` among them is never mistaken for a
queue item), written through the same `writeSidecarAtomically()`. Five keys per
entry: `digest`, `name`, `size`, `when`, `state`.

Four decisions are not recoverable from reading it:

- **The digest is stored**, and it is the one deliberate exception to the
  re-derive rule above. A `.nzbstate` can recompute its digest because it still
  holds every message-id; this cannot, because by the time it is read the NZB is
  gone. `releaseKey` and the folded name are *not* stored — both derive from
  `name` and `size` on load, and a stored copy would only be a second thing that
  could disagree.
- **`Downloaded` is monotone.** Finishing a release records it, and clearing its
  row records it again; the second call must not turn "you already have this"
  into "you gave up on this". The other direction is not monotone: something
  abandoned and later downloaded for real is downloaded.
- **Every preference check lives inside the class**, the way MFC gates
  `CKnownFileList::AddCancelledFileID` and the way `addNzb()` enforces
  `usenetAutoAddPaused` — every present and future caller inherits the rule
  instead of each one having to remember it. Turning a preference off *forgets*:
  the entries it governs are neither read nor written, so the next save drops
  them and turning it back on does not resurrect them. That is `cancelled.met`'s
  behaviour, deliberately matched.
- **The cap is generous** (5000, oldest evicted) because there is no second layer
  beneath it. `IndexerFeedStore`'s seen set can afford to be tight precisely
  because this guard catches what it evicts; an entry evicted from *here* makes a
  release silently re-addable, with nothing behind it.

Recorded at exactly two points, both in `UsenetQueue`: `onPostFinished()` at the
terminal `Complete` transition (the PAR2 re-round and a failed verify both return
above it, so one completion records once), and `removeItem()` before the erase,
where a `Complete` item is `Downloaded` and anything else is `Cancelled`. Written
through on both — they are rare, so an unclean exit between two downloads loses
nothing.

`knownTypeForTitle()` is the same store asked the other question, for search-row
marking. It joins on `usenetFoldedReleaseName()` — an indexer row carries no
message-ids, so a folded *name* is the only join there is. It marks a row and
raises a question; what an add actually does is still the digest's decision.

`addNzb()` also reports **why** it did what it did, through an optional
`UsenetAddOutcome` out-parameter: `Added`, `Duplicate`, `Invalid` or `Failed`.
An automatic actor that cannot tell "we already have this" from "that did not
work" retries a duplicate on every poll forever, and matching on the error
*text* to avoid that breaks the first time a sentence is reworded or translated.
The watch folder uses it to decide between `_processed/` and `_failed/`; the feed
poller uses it to decide between forgetting a release and trying it again.

### Automatic adds, and the one setting that governs all of them

`usenet.autoAddPaused` (off by default) queues anything added automatically in
`Paused`, so a feed proposes rather than decides. It is enforced **inside
`addNzb()`** rather than at each call site, for the same reason the duplicate
guard is: `quotaParked` is the counter-example, a flag whose two call sites had
to be remembered separately, and forgetting the second one cost the user a share
of their line indefinitely.

It composes with the health check for free. An auto-paused item still goes
through `beginHealthCheck()`, and `ItemRuntime::checkResumeStatus` — added for
the paused-recheck case — puts it back to `Paused` when the probe finishes.

## Categories

A release is queued into a **download category** — the same numbered list ED2K
downloads use (`Preferences::categories()`), not a Usenet-only one — and finishes
in that category's own folder instead of the single global incoming dir. Nothing
in `DownloadCategory` needed changing; it was already a plain value type in
`core/prefs` with the queue only ever asking it questions.

> **A category is an index into a list the user can edit, and a folder resolved
> at the last moment.**

Three clauses, and each is a way this goes silently wrong:

- **The index is stored; the path is asked for.** A release is categorised when
  it is queued and lands hours later, and in between the user can repoint the
  folder, rename the category or delete it. So completion calls
  `incomingDirForCategory()` and never caches a path at add time. That function
  is documented as *"the single resolution point for the whole core"* and
  re-tests the directory's existence on every call, which is what makes an
  unmounted volume fall back to the global incoming dir instead of stranding a
  finished release.
- **Everything holding a category index is remapped in one transaction.**
- **An unusable category is never a reason to fail.** Out of range, deleted,
  unmounted — the release still lands, and the item loses its label, not its
  bytes.

Three sites resolve the folder, and the third is the one that is easy to miss:
`job.destDir` for the post-processing pipeline, the synthetic staging path taken
when post-processing is off, and **preview following a file after staging renamed
it**. Get the last one wrong and a preview that is already playing goes dead at
the exact moment the download finishes, because the follow-up looks in the old
folder.

Nothing creates the category folder here. `sanitizeCategories()` already degrades
*"a path we cannot create"* to the global incoming dir when the list is stored,
and a category folder is a **share root** — it is in `allIncomingDirs()` — so
`addFileInSharedLocation()` accepts it and the non-recursive share scan still
sees the file. The rule that a completed release is renamed *directly* into an
incoming directory and never into a subfolder of one is unchanged.

### Renumbering, and the queue that is asleep

The list order **is** the identity: `part.met` stores `FT_CATEGORY` as an index
and `.nzbstate` now stores `category` the same way. So `handleSetCategories`
renumbers before storing the new list, *"so nothing in between can read an index
that no longer names what it used to"* — and it now has four stores to renumber,
through one shared rule, `remapCategoryIndex()`:

| store | renumbered by |
|---|---|
| the ED2K queue | `DownloadQueue::remapCategories()` |
| the Usenet queue, while running | `UsenetQueue::remapCategories()` |
| the Usenet sidecars, while it is not | `UsenetQueueStore::remapCategories()` |
| the feeds | `remapFeedCategories()` |

⚠️ **The third row is the one that is not obvious.** `UsenetQueue::start()` is
what loads the `.nzbstate` sidecars, so with Usenet disabled the queue holds
*nothing* and those items exist only as files on disk. Turn Usenet off, delete a
category, turn it back on, and every release in it would be filed into whatever
moved into that slot. ED2K has no equivalent exposure — `theApp.downloadQueue` is
live for as long as the daemon is — which is exactly why the case is easy to miss
by analogy with it. `UsenetSession::remapCategories()` picks between the two
paths, and only sidecars whose category actually moves are rewritten, because
`load()` demotes a resumable status on the way in and writing that back for every
item would persist a change nobody asked for.

⚠️ **A feed is the worst of the four to get wrong.** A queue item pointing at a
deleted category costs one release in the wrong folder; a feed pointing at one
costs **every release it ever matches, for as long as it runs**. That is why the
feed remap is the one with a bold test and the one that logs.

Editing `preferences.yml` by hand while the daemon is **down** still reorders
categories with no remap at all. ED2K has that identical exposure and always has,
so this is the existing contract rather than a new hole — stated here rather than
mechanised.

### Where a category comes from

In precedence order, and only the first two are decisions anyone makes:

1. **A feed's `downloadCategory`.** See `docs/indexer-module.md`.
2. **Assign To Category**, on the queue's context menu, batched over the
   selection — `SetUsenetItemCategory`.
3. **Auto-categorisation** by name, when the caller picked none. `matchAutoCategory()`
   is `DownloadQueue::applyAutoCategory()`'s inner half, split out of it when this
   queue became its second caller, so both networks match `'|'`-separated terms,
   wildcards and regexps by exactly the same rule — including the deliberate
   divergence from MFC recorded in `docs/categories.local.md`.

Like the duplicate guard and `autoAddPaused`, auto-categorisation runs **inside
`addNzb()`** rather than at each call site. That is what gives the watch folder,
the command line, a drop and `AddNzbUrl` a category for nothing.

Everything an intake path may choose now travels in one `UsenetAddOptions`
struct — source, force, password, category, priority, paused — rather than as
five trailing arguments, which is why `addNzb()` could gain three choices without
touching a call site that does not use them. A caller names only what it means:
`addNzb(data, name, error, {.category = 3, .paused = true})`.

**Priority has five levels**, not three: very high (+2), high, normal, low, very
low (−2), which is NZBGet's set and the right one for a queue holding a few large
releases. The value is the same persisted int, so the original three keep their
meaning, no sidecar migrates and `SetUsenetItemPriority` is unchanged — the
daemon simply clamps what it is sent, because a sixth bucket would be one nothing
can name and no menu entry could select again. The names live in the GUI
(`usenetPriorityName()`), which does not link eMule::Usenet; the Priority column
shows the word and sorts on the number.

The category's `prio` is **not** consulted. It is MFC's a4af rank, deciding which
paused *ED2K* file resumes first; Usenet has its own priority and no a4af
concept. `SetUsenetCategoryStatus` refuses `Stop` and `ResumeNext` for the same
reason, with a message rather than silence — a GUI sending either has a bug worth
seeing.

## Intake

There are five ways an .nzb reaches the queue, and only the first two existed
before: the file dialog, a pasted URL, a drop, the desktop, and the watch folder.
A feed is the sixth and lives in `docs/indexer-module.md`, because the polling
belongs to the indexer module.

**Drag and drop.** `nzbDropCandidates()` (`src/gui/utils/NzbDrop.h`) decides what
a droppable .nzb is, once, for both `UsenetPanel` and `MainWindow`. Answering it
in two places is how a window ends up accepting something the panel then refuses.
It takes local files ending `.nzb` and http(s) URLs whose *path* does — the path,
because an indexer's download link carries its API key after it. Everything else
falls through untouched, which is what keeps a future `.emulecollection`,
`server.met` or `ed2k:` drop free to be handled properly rather than swallowed
here. A drop is a deliberate act, so the source stays `Manual`.

**The desktop.** `ExternalLinkHandler` already received `QEvent::FileOpen` and
dropped anything `Ed2kLinkImporter::linkFromFileOpenEvent()` did not claim — a
fall-through that class's header documents and nothing consumed. It now consumes
it. ⚠️ Read `QFileOpenEvent::file()`, never `url()`: an `ed2k:` URL comes back
empty through `url()`, and this code path has been bitten by that before.

**The command line.** A positional argument may be an `ed2k:` link, a `.nzb`
path, or a `file://` URL — a desktop launcher runs `Exec=… %U`, so the same
argument arrives one way from a file manager and another from a shell. Links keep
their old first-one-only behaviour; every `.nzb` is queued, because opening a
selection of them is an ordinary thing to do. The daemon has `--add-nzb` beside
its existing `--add-link`.

### ⚠️ Both desktop routes arrive before the daemon does

A `.nzb` opened from outside is the same shape of problem as an `ed2k:` link
opened from outside, and for the same reason: **the file's bytes travel to the
daemon**, so an add is impossible until the IPC handshake completes. On a cold
start neither route has one.

The command line is the sharper of the two, because it *looks* sequential:
`main.cpp` calls `connectToDaemon()` and then `handleOpenArguments()` on the next
line — but the connect is **asynchronous**, so the argument is always read while
the socket is still opening. `UsenetPanel::addNzbFile()` opens a modal *"Not
connected to the eMule core."* over a window the user has not seen yet, and under
`--screenshot` that modal **deadlocks the process outright**.

⚠️ The deadlock is sharper than "a modal blocks": `handleOpenArguments()` runs at
`main.cpp:682` and `QApplication::exec()` at `:684`, so the box ran a nested event
loop *before the main loop had ever been entered*. The screenshot timer's
`app.exit()` quit **that** loop, the box closed, and `exec()` then started fresh
with nothing left to quit it — the process afterwards ignored even SIGTERM. A
modal opened later, once `exec()` is running, exits perfectly well; the eD2K
importer's own "download the following file(s)?" question proves it. So the rule
is not "no modals under `--screenshot`" but **nothing modal before `exec()`**.

The Finder route was worse in a quieter way — it
arrives during the splash screen's `processEvents()`, before there is a
`MainWindow` at all, and the file was dropped with no message.

`ExternalLinkHandler` already solved this for links and its header says so:
*"links are queued until there is somewhere to put them, and released when the
daemon answers. Dropping them instead is what made a cold start from a browser
click do nothing at all."* Files now wait in the same queue —
`ExternalLinkHandler::openFile()` is the one door, and both routes go through it
rather than reaching for the panel.

The waiting rules live in `PendingOpenQueue` (`src/gui/app/PendingOpenQueue.h`)
rather than in the handler, because the handler reaches `MainWindow` and cannot
be linked into a test binary. Three of them are worth naming:

- **The kind travels with the value.** A path and a link are both `QString`. The
  queue was a bare `QStringList` when only links could wait; a path put through
  that comes back indistinguishable from a link, is replayed into the eD2K
  importer, and is silently lost.
- **The queue empties before the first replay, never after the last.** Acting on
  an item opens dialogs, a dialog spins the event loop, and the loop can deliver
  `connected()` again straight back into the release. Emptying afterwards hands
  the same `.nzb` over twice and asks *"download it again?"* twice.
- **A flood is capped, not unbounded** — 16, shared with links, for the reason
  the link cap already gave: stacking confirmation dialogs behind each other
  helps nobody.

A released `.nzb` switches to the Usenet tab *before* the add, not after: the add
is asynchronous and may come back asking "download it again?", and that question
wants the queue behind it.

**File-type registration.** `bundle-win.ps1` makes a bare zip and
`bundle-linux.sh` a bare tarball, so there is no installer to hang a registration
step off and the application registers itself. Everything is per-user —
`HKEY_CURRENT_USER\Software\Classes` and `$XDG_DATA_HOME` — because a portable
archive has no moment at which to ask for elevation. macOS is declarative and has
no runtime path at all: `Info.plist`'s `CFBundleDocumentTypes` is the whole
mechanism, with `LSHandlerRank` **Alternate** so a dedicated NZB client keeps
priority.

The two generators in `src/gui/utils/FileAssociation.h` are pure and produce what
registration *would* write, which is the only part of it a developer machine can
check — the Windows and Linux writers compile on their own platform only. The
Linux desktop entry also claims `x-scheme-handler/ed2k`, which costs nothing and
finally gives Linux the ed2k handling the macOS bundle has had all along.

The setting lives in **`uistate.yml`**, not `preferences.yml`: the daemon owns
that file and knows nothing about the desktop it is not running on, and it is the
*GUI's* executable path being registered. It is re-applied at every start, so an
application that takes the association does not keep it.

### The watch folder

`usenet.watchDir`, empty when off. Any `.nzb` left there is queued and then moved
into `_processed/` — or `_failed/`, when it could not be read. Moved, never
deleted: the queue is not a receipt, and someone who wants to know what became of
a file they dropped should be able to look.

Three things are not obvious from the code:

- **A file still being written has not arrived yet.** The watcher fires when a
  file is *created*, which for anything larger than a buffer is before the writer
  has finished. `NzbFile::parse()` rejects a truncated document, so half a file
  can never become half a release — the damage is the **move**: without a settle
  check the fragment is declared invalid and filed into `_failed/`, which looks
  exactly like a corrupt download and takes the user's .nzb away from where they
  put it. A candidate is read only once its size and mtime have held still for
  two seconds.
- **The watcher alone is not enough, twice over.** FSEvents coalesces, and no
  event ever fires for files that were already there when the daemon started.
  Hence a 30 s rescan *and* a scan at startup.
- **`_processed/` and `_failed/` are excluded from the scan.** A scanner that
  reads its own output re-queues every release it ever handled, on every scan,
  forever.

A duplicate goes to `_processed/`, not `_failed/`: "we already have it" is the
answer, not a failure. That is the `UsenetAddOutcome::Duplicate` seam earning its
keep — and the reason it exists.

The directory is refused when it sits at or below the temp, incoming or config
trees. That is *containment*, where `isShareableDirectory()` is equality, because
the scanner would otherwise be reading files the daemon itself is writing.

## Post-processing

A finished download is not a finished item. Since phase 4, "every planned segment
resolved" ends the *download phase* and hands the item to
`UsenetPostProcessor` — one `QThread`, one job at a time, because a repair and an
unpack are both disk- and CPU-bound and interleaving two of them is slower than
running them in sequence.

The pipeline is **rename → verify → repair → unpack → stage**, and it stops at
staging: it copies the payload to `<incoming>/<name>.usenetpart`, which no share
scan will look at, and hands the queue a list of renames. All the slow IO
therefore happens off the daemon thread, and what is left on it is an atomic
in-place rename and the call that offers the file to ED2K.

Rename runs **first**. An obfuscated release verifies as entirely missing until
its real filenames are back, and the unpacker cannot pick volume one out of a set
of hex strings either.

### On-demand recovery volumes

PAR2 recovery volumes are typically a tenth of a release and are discarded unread
on a healthy one, so `rebuildPlan()` leaves them out of the download plan
entirely. Only the index `.par2` — the file list, no recovery data — is fetched,
and it is fetched last.

Verification then decides what happens next:

- clean → rename, unpack, publish;
- damaged, enough blocks already present → repair;
- damaged and short → `requestPar2Volumes()` adds the **smallest** set of volumes
  covering the shortfall, the item returns to `Downloading`, and post-processing
  resumes at repair when they land.

That makes completion a **cycle**, which is the one genuinely new hazard in this
phase: every path out of it has to terminate. `requestPar2Volumes()` returning
false — nothing left to ask for, or not enough left to cover the shortfall — is
the real terminator, and `kMaxPar2Rounds` is the backstop. `requestedPar2` is persisted for the same reason: a queue that
forgot which volumes it had asked for would drop them from the rebuilt plan and
re-request them once per restart, forever.

`NzbFileInfo::par2RecoveryBlocks()` is what makes any of this possible — it reads
the block count out of a `.vol{start}+{count}.par2` name. Both spellings occur
(par2cmdline writes `rel.vol0+1.par2`, QuickPar and MultiPar pad to
`rel.vol000+01.par2`), so the digits are parsed and never matched.

### A skipped file is fetched only when a repair needs it

A skipped file (`UsenetFileState::skipped`) is out of `isPlanned()`, so everything
that plans through it ignores it: the plan, completion, SFV expectations and the
retry. A few paths needed their own guard:

- **`dispatch()`**, because retries, aborts and preview promotion re-insert keys
  without asking;
- **sealing**, so an article that outlives the skip cannot publish the file;
- **direct unpack**, both a set's start and its replay;
- **the health probe**, and the item totals, so percent and remaining describe
  what will arrive;
- **the repair estimate.**

A skip is widened to the **whole archive set**, because one volume short fails
the unpack. PAR2 files cannot be skipped, and neither can every payload file at
once (`expandSkipRequest()`).

**When post-processing starts**, `discardSkippedFiles()` deletes each skipped
file's scratch file and forgets its articles. It runs *after* the room check, so
a disk wait deletes nothing. `checkItemCompletion()` first waits out any article
of a skipped file still in flight, because `ArticleWriter` would otherwise
recreate the file mid-verify.

**What verify then sees.** A PAR2 set often covers the sample too, so verify
reads the absent file as missing blocks.

- `Par2Result::files` now reports, **per file**, whether par2 found it complete
  (`GetCompleteFile()`) and whether it is on disk. These are whole-file facts,
  never per-file block counts. par2 finds a block by its content in whichever
  file holds it, so repeated data puts one file's blocks at another's offsets.
  A block count would then name the wrong file, and could even read a damaged
  file as short of nothing. `tst_UsenetPar2` found exactly that with patterned
  test data.
- **Only skipped files are short:** verify counts as clean. No repair runs and
  no recovery volume is bought.
- **Real damage as well, and short of blocks:** the result carries
  `needsSkippedFiles`. The queue marks those files `neededForRepair`, fetches
  them (cheaper than their blocks), and runs another round, bounded by
  `kMaxPar2Rounds`. A damaged file posted under a name the set does not use
  cannot be told from a skipped one, so an unexplained absent file brings every
  skipped file back. That costs bytes, where guessing wrong would cost the
  release.
- **Either way, not published.** Once PAR2 is done, every name a skipped or
  repair-only file can carry (`discardAfterVerify`) is deleted before unpack.
  The one exception is a name a wanted file also carries.

`tst_UsenetPostPipeline::aSkippedFileTheSetCoversNeedsNoRecoveryVolumes` and
`realDamageFetchesTheSkippedFileTheRepairNeeds` pin both halves.

### Pausing the whole engine

`UsenetQueue::setEnginePaused()` is persisted as `usenet.paused`, and only
`SetUsenetPaused` writes it: it is kept out of the `SetPreferences` chain on
purpose, so an Options dialog left open cannot undo a pause. While paused:

- `dispatch()` returns before probes and articles alike; articles already in
  flight still land;
- `checkItemCompletion()` starts no post-processing;
- `hasActiveDownloads()` is false, so eD2K gets the whole line.

**No item's status changes.** That is the difference from the "All" category's
Pause, which pauses every release and whose Resume skips ones a check stopped.

Every waiting item's `stalledReason` reads "all downloads paused", and that text
**wins** over the quota, disk and proxy texts.

One function owns that order. `noteWaitReason()` answers `dispatch()`'s own gates
in `dispatch()`'s order — **pause → disk floor → proxy → a pending check → nothing**
— since an item held at the first gate must not name a later one the round never
reached, and a Checking item may not claim it is being checked while a gate ahead
of the probes holds it. The floor before the proxy is `dispatch()`'s own order and
not an arbitrary ranking: with the volume full the round returns at the floor and
never looks at the proxy, so naming the proxy would send the user to fix something
that would change nothing. `noteProxyStall()` and `refreshDiskState()` therefore
both set their text and re-ask the helper, rather than stamping items themselves.
Every path back into waiting calls it, which is what makes a reason reach an item
that started waiting *after* the wait began. `restoreWaitReasons()` re-asks it at
the resume, and `unparkQuotaStalls()` lets the next round re-park anything still
over its allowance.

This is the hook the scheduler's Usenet actions (#6) need.

### Four things about libpar2-turbo that are not in its headers

The library is the **nzbgetcom fork** of par2cmdline-turbo, not animetosho's
original: only the fork carries `BUILD_LIB`/`BUILD_TOOL` and an `include/par2`
tree. Each of these cost a debugging session:

1. **`PreProcess()` is not optional.** It loads the packets and builds
   `mainpacket`; `Process()` walks straight into
   `mainpacket->RecoverableFileCount()` and segfaults on a null pointer without
   it. The library's own `par2repair()` convenience function omits it and is
   just as broken — do not "simplify" to it.
2. **`Process()`'s `basepath` parameter is dead.** `basepath` is assigned in
   exactly one place, `PreProcess()`, from the `CommandLine`. The base directory
   has to travel as `-B`, or every source file resolves against the wrong
   directory and the whole release reads as missing — indistinguishable from a
   genuinely dead download.
3. **Extra files must be passed twice.** `PreProcess()` reads them off the
   `CommandLine`; `Process()` ignores that and uses its own parameter. And par2
   never walks a directory by itself, so a rename pass with an empty list runs to
   completion, reports success, and renames nothing.
4. **`renameonly` still needs `dorepair=true`.** `RenameTargetFiles()` lives
   inside `if (dorepair)`.

Two more, on the build rather than the API. `HAVE_CONFIG_H` and the three
`PARPAR_*` definitions are set with directory-scoped `add_compile_definitions()`
and therefore do **not** reach a consumer; without them `md5.h` declares nothing
and `libpar2.h` picks different integer typedefs, which on LP64 is `uint64_t`
versus `unsigned long long` — same width, different mangled name, undefined
reference to `Process()`. And the library is built `-fno-rtti`, so its classes
ship a vtable and no typeinfo: anything subclassing `Par2Repairer` must be
compiled the same way, which `src/usenet/CMakeLists.txt` does for
`Par2Verifier.cpp` alone.

`EMULE_USENET_PAR2=OFF` drops all of it. The queue still downloads, assembles and
publishes; it simply cannot verify, and a release with missing articles then
fails rather than being published — see below.

### Unpacking

`UsenetUnpacker` hands `ArchiveReader` the **whole volume list**, in volume
order. libarchive reads a multi-volume set as one continuous byte stream over
the files the client supplies and never opens a sibling volume by name, so a set
opened on volume one alone stops at the end of volume one — silently, with a
split-file error that reads exactly like a corrupt download. `tst_ArchiveReader`
asserts both halves of that. Volume order is what `volumePositionOf()` computes,
and it matters: the old `.rNN` scheme puts its first volume under a different
extension (`name.rar`, then `name.r00`), so a set sorted by filename starts in
the middle.

### Passwords

Passwords come from `NzbInfo::password`, and **libarchive decrypts ZIP and
nothing else** — ZipCrypto and WinZip AES. For 7z it stops at "Crypto codec not
supported yet" and for RAR at "RAR encryption support unavailable"; both are
detection, never decryption. Since most password-protected releases are RAR, a
password on its own would be inert, so an encrypted set libarchive cannot handle
goes to `ExternalUnpacker` — whichever of `7zz`, `7z`, `7za` or `unrar` is
installed, or the explicit `usenetExternalUnpacker` path. A configured path is
used **or nothing**: substituting a different binary for the one the user named
turns a typo into behaviour they cannot account for later.

Three things about that seam are load-bearing:

- **Only encryption routes there.** A genuinely corrupt archive must keep failing
  as a corrupt archive, or every damaged release spawns a subprocess.
- **The tool's output is untrusted like any other archive's.** 7-Zip and unrar
  strip `/` and `..` themselves, but that is their policy and not one we control,
  so extraction goes to a private staging directory and every member is moved out
  through `ArchiveReader::safeEntryPath()`. The harvest walks subdirectories:
  7-Zip restores stored paths, so a member archived as `sub/deep/movie.mkv` lands
  two levels down and a flat scan would silently publish nothing.
- **The password never reaches a log.** It rides on the command line, which is
  what both tools offer; every message names the redacted form.

`ArchiveReader` gained `encryptionBlocked()` for the routing decision, and it is
deliberately not `hasEncryptedEntries()`. The two encrypted cases announce
themselves differently: a header-encrypted set (`rar a -hp`, RAR5 `HEAD_CRYPT`,
7z `-mhe=on`) fails on its *first* header and lists nothing at all, so no entry
flag is ever set; a data-encrypted one lists happily and only fails when
something reads a member. An encrypted ZIP is the case where the flag is set and
everything still works.

**A header-encrypted set used to be silent data loss**, and this is why both
guards below it exist. `scanEntries()` looped `while (… == ARCHIVE_OK)` and
returned `true` regardless, so an archive that fatally failed on its first header
read as an *empty but valid* one: the unpack produced no files, reported success,
and filled `consumedArchives` with every volume — which `UsenetQueue` then
deleted, leaving a "Complete" download with nothing in it and no way back.
`ArchiveReader` now reports why a header walk stopped; `UsenetUnpacker` refuses to
call an extraction that produced nothing a success; and `UsenetPostProcessor`
refuses to publish an empty payload while holding a delete list. Three layers for
one bug, because the consequence is unrecoverable and each layer is reachable on
its own. `tst_UsenetPassword` pins all of it.

`UsenetUnpacker::Result` therefore carries `passwordRequired` beside
`encryptedUnsupported`: the first says *this needs a password we do not have*,
the second describes the archive whether or not the external tool then rescued
it. Only the first becomes the GUI's "Set Password…", so an ordinary failure must
never set it. `wrongPassword` splits "the one you gave is wrong" from "there
isn't one", which ZIP and both external tools can tell apart and RAR cannot.

Two defects in `ArchiveReader` became serious once it started reading archives
written by strangers, and are fixed here rather than worked around: `extractAll()`
joined member names onto the destination unsanitised, so a `../` member escaped
it, and `extractEntry()` reopened and re-scanned the whole archive per member,
which on a solid multi-volume set re-read every volume once per file.

### Direct unpack

An archive volume is finished the moment its last article lands, and libarchive
reads a set front to back, so the extraction does not have to wait for the
download: it can keep pace with it and be done when the last article is. That is
all `post/UsenetDirectUnpack` does. It *is* an `ArchiveVolumeSource` — the one
thing that differs from the end-of-download path is where the next volume comes
from — and the extraction itself is `ArchiveReader`'s, unchanged. So every
format libarchive reads is covered, compressed and solid RAR included, and
nothing in the class knows what a RAR is.

It runs on its own thread rather than `UsenetPostProcessor`'s. A run is blocked
for as long as the download takes, and that thread has to stay free for other
items' repairs. Two runs at a time (`kMaxDirectUnpacks`); a set past the cap
simply unpacks at the end, which is the fallback every refusal here lands on —
a missing article in a volume, an encrypted RAR, a set whose first volume never
arrives, the preference switched off.

`checkItemCompletion()` holds the item open until every run has answered, so by
the time post-processing starts the payload is either there or it never was.
`UsenetUnpacker::unpack()` then takes a list of first volumes to skip, and
`nothingToDo` keeps meaning *no archives at all* rather than *nothing left to
do* — the two have different consequences for what gets published.

**A repair invalidates all of it.** Anything extracted from a volume that PAR2
later rewrote came from bytes that no longer exist, so those files are deleted
and the repaired set is unpacked again. The signal for that is not the verify
outcome: par2's *rename* pass repairs as a side effect — `RenameTargetFiles()`
lives inside `if (dorepair)` — so a release can be silently repaired during
rename and verify clean afterwards. `UsenetPostProcessor` therefore records a
repair at every stage rather than reading the final outcome.
`tst_UsenetPostPipeline` covers exactly that, because the first version of this
code read the outcome and would have published the stale files.

Volume order comes from `UsenetUnpacker::volumePositionOf()`, but the *number*
it returns is not the ordinal: `.partNN.rar` counts from 1 while a bare `.rar`
is volume 0 of the `.rNN` scheme. The set is ranked and the rank used, or a
`.part01.rar` release never starts.

### What gets published

Only the payload. Archive volumes and recovery data are of no use to an ED2K
peer, and keeping them roughly doubles the disk cost of every release; the
`usenetCleanupAfterUnpack` preference turns that off for anyone who wants the
originals.

And **a release that failed verification is not published at all**. Phase 3
shared short files, holes and all, and nothing in the system complained — the
client quietly advertised corrupt data. `missingSegments > 0` with no usable
recovery set is now a failure, and the files stay in the work folder.

### A release without PAR2 is checked against its SFV

`UsenetPostProcessor::process()` used to publish a release unchecked whenever PAR2
did not run — none shipped, `par2Repair` off, or a build without libpar2. Many such
releases carry an `.sfv`, and `verifySfv()` (`post/UsenetReleaseChecks.cpp`) now
reads it: CRC32 per file, streamed through `yencCrc32()`, cancellable, exact names
first and then case-folded, paths in a line reduced to a bare name. A mismatch
fails the release and discards anything direct unpack took from the damaged
volumes; a clean check sends the `.sfv` to `consumed` beside the par2 set.

⚠️ **A file the SFV lists and nobody posted is not damage.** Posters list the
sample and the `.nfo` and then leave them out. `UsenetPostJob::expectedNames` —
what the download actually sealed — is what separates "expected and gone" from
"never posted", which is only logged. There is no Resume override: an SFV mismatch
is as much a fact as a failed PAR2 verify.

### A media release carrying programs is held back

The commonest fake on Usenet is a movie whose archive holds `Codec Pack.exe`, or a
`movie.mkv.exe`. `unwantedFileNames()` judges names against `unwantedExtensions`
and only for a **media** release — one whose NZB or payload has a playable name
(`isPlayableName()`, the streaming index's own test), or whose title carries a
video tag (1080p, x265, WEB-DL, S01E02…) — so a software download keeps its
`setup.exe`. A disguised double extension counts regardless. `isFakeMediaFile()`
adds a `.mkv` whose bytes are a program or an archive: `ContainerSniffer` finding no
container **and** an `MZ`/ELF/Mach-O/ZIP/RAR/7z/`#!` head, because an unrecognised
container may still be real video.

Checked at three points, earliest first:

1. **Direct-unpack progress**, while the release is still downloading — noted in
   `onDirectUnpackProgress()` and acted on from the tick, because stopping cancels
   the very run the handler was called for;
2. **The member list**, through `UsenetUnpacker::setVeto()`, before `extractAll()`
   writes a byte (after extraction for an encrypted set, whose members only an
   external tool can list — and what it wrote is deleted);
3. **The payload**, all together, just before staging.

The result is `UsenetPostResult::unwantedFiles` rather than a failure: extracted
files are deleted, downloaded ones stay, and `onPostFinished()` hands the item to
`stopForCheck()` ahead of the statistics, so an overruling Resume that re-runs the
pipeline does not count its verify twice.

## Storage, and why Usenet scratch is never shared

In progress: `tempDirs().first()/Usenet/<id>/<file>.usenetpart`.
Complete: renamed **directly into `incomingDir()`**, not a subfolder.

Both halves matter. `SharedFileList::addFilesFromDirectory` iterates with
`QDir::Files | QDir::NoDotAndDotDot` and **no `Subdirectories` flag**, so it does
not recurse: a `Usenet/` folder under a shared directory is never walked, and
equally a completed file in a *subfolder* of incoming would never be offered.

Four independent guards keep in-progress articles off the network, because the
failure is silent — nothing errors if one leaks, the client just quietly
advertises garbage:

1. The scan does not recurse (above).
2. `Preferences::kUsenetPartSuffix` is skipped by name, alongside `.part` and
   `.part.met`. This covers a user who shares the Usenet temp directory itself,
   and the cross-volume completion below.
3. `SharedFileList::shouldBeShared()` refuses anything under
   `Preferences::usenetTempDir()` **before** the incoming-directory rule, which
   returns true unconditionally. Without that ordering, a user whose temp
   directory sits inside incoming would advertise every half-written article.
4. Completion stages through `<dest>.usenetpart` and then renames in place. Moving
   across volumes degrades `QFile::rename` to a copy, and a growing file inside
   the always-shared incoming directory would otherwise be visible to a scan
   mid-copy. The in-place rename is atomic.

Once it lands, the queue calls `SharedFileList::addFileInSharedLocation()` —
**not** `addSingleSharedFile()`, which is for a file no shared directory covers
and refuses the incoming directory outright, because `isShareableDirectory()`
excludes it. Using the wrong one logs a warning and shares nothing until the next
full rescan. `tst_UsenetSharing` covers all of it.

## Bandwidth

There is no download-side throttler in the repo: `UploadBandwidthThrottler` and
`ThrottledSocket` are upload-only, and ED2K limits download through a 10 Hz
proportional feedback loop in `DownloadQueue::process()`.

So the one global ceiling is split. `UsenetSession` recomputes every 500 ms, in
`computeDownloadSplit()` — a pure function, covered by `tst_UsenetBandwidthSplit`.

`usenetDownloadSharePercent` (default 50) is a **floor while both engines are
busy, never a cap on an engine whose counterpart is idle.** Each engine reports
whether it is *active* — structurally, never from its rate, or a throttled engine
would read as uninterested and never get its share back — and what it measurably
uses:

| | active | rate |
|---|---|---|
| Usenet | `UsenetQueue::hasActiveDownloads()` | `UsenetQueue::currentRate()` — wire bytes, 2 s window |
| ED2K | `DownloadQueue::hasActiveTransfers()` | `DownloadQueue::datarate()`, 10 s window |

An active engine *reserves* what it uses plus a quarter, clamped between a
quarter of its floor and its whole floor. Each engine may then take the ceiling
minus what the **other** reserves:

```
reserve     = active ? clamp(rate * 5/4, floor / 4, floor) : 0
ED2K cap    = Usenet active ? ceiling - reserve(Usenet) : whole ceiling
Usenet cap  = ceiling - reserve(ED2K)
```

- Both saturated → each reserves its whole floor, and the configured split holds.
- One idle → the other gets the whole line.
- One busy but slow (a few ED2K sources, every news server backed off) → the other
  gets everything it leaves, less a quarter of its floor kept so it can restart
  without waiting a tick.

Usenet's rate is NNTP **wire** bytes, counted as lines are read
(`NntpSocket::totalWireBytesRead()`), never decoded bytes at article completion.
Throttled to 3500 KB/s over 40 connections, one 750 KB article takes ~8.6 s,
and forty that started together finish together: a completion count reads as
bursts and silence while the line is full throughout. A split fed from it —
`currentRate()` used to be a single 250 ms tick of exactly that — lent Usenet's
floor away in every silence. Measured live, that flapped ED2K's budget between
1750 and 3063 KB/s for the first twenty seconds of a download.

ED2K's slice is published as `Preferences::setEd2kDownloadBudget()` and read by
`DownloadQueue::process()` through **`maxDownloadForEd2k()`, not
`maxDownload()`** — aiming both engines at the full ceiling lands the combined
rate at roughly double the cap. The budget is runtime-only and never persisted,
so a crash cannot leave a user throttled to a stale share. `UsenetSession::stop()`
clears it. Usenet's cap is always a number when a ceiling is set: its `0` means
unlimited, which would ignore `maxDownload` outright.

The split in force travels in `GetStats` and `PushStatsUpdate` as
`maxDownloadKb` / `usenetLimitKb` / `ed2kBudgetKb` — effective caps in KB/s — and
the Usenet panel's summary line says when eD2K is holding Usenet below the
ceiling. `log.log` gets one line when the split moves by more than 10 %, at most
every 5 s, and none at all while nothing contends.

ED2K gives bandwidth back more slowly than Usenet takes it: its limiter is a
multiplicative feedback loop on a 10 s average, where Usenet's is a token
bucket. A combined overshoot lasting a few seconds after Usenet wakes is that
loop converging, not the split.

### Why a download is slow

Measured 2026-09-05 against a real provider at 216 ms command RTT, on an account
that allows **100** connections. Three things cap a Usenet download, and only one
of them is in this module's code:

1. **Something outside the client caps a single connection.** ~0.2 MB/s that
   day, and no client-side tuning moved it: a raw non-Qt TLS socket got the same
   figure, and a 4 MB `SO_RCVBUF` set before connect changed nothing (0.228 →
   0.242 MB/s, inside noise). It is not a window or bandwidth-delay problem, so
   there is nothing to optimise in `NntpSocket` for it. Nor is it a fixed
   property of the provider: on 2026-09-12 the same machine, account and 40
   connections sustained **44-55 MB/s**, ten times the table below, with nothing
   in the client changed. Before reading a slow run as a regression, measure one
   connection outside the client on the same day.

2. **Throughput is therefore the connection count**, and `maxConnections` is
   usually set far below what the account allows:

   | connections | MB/s (2026-09-05) |
   |---|---|
   | 8 | 1.2 |
   | 40 | 4.4-4.9 |
   | 60 | 5.8-6.3 |

   Sublinear but still climbing at 60 — not even two thirds of the account's
   100 — with no `502 Too many connections` during the run. A client reporting
   several times our rate on the same post is using more connections, not a
   better protocol implementation. Never set the figure above what the plan
   sells: the answer is a 502 and a backed-off server, and providers suspend
   repeat offenders.

   `kDefaultMaxConnections` (`src/core/prefs/NewsServer.h`) is therefore **40**,
   not the 8 this module shipped with — 8 is below what any plan sold today
   allows and cost a new install three quarters of its rate for nothing. Nor
   this account's 100: a default above what a smaller plan sells is a 502 on
   first start.

3. **The bandwidth split above, which the tests never see.** They construct a
   `UsenetQueue` directly and never call `setRateLimit`, so their numbers are the
   unthrottled ceiling. In the daemon, `maxDownload: 3500` KB/s with a 50% share
   caps Usenet at 1.71 MB/s *while eD2K is also downloading* — with eD2K idle it
   may use the whole 3.4 MB/s — so a rate measured in a test does not transfer to
   the running application. The panel summary says when the split is the cap.

The read budget itself is a token bucket with a one-second burst
(`kBurstTicks`), and it has to be: a socket on a high-latency link spends two
whole refill ticks per article waiting for the server's first byte, and a budget
that is *assigned* each tick rather than accumulated forfeits that credit and can
never average the rate it was given. `tst_NntpSocket::readRateLimit_repaysTimeSpentWaiting`
pins it — the same transfer takes ~950 ms without the carry and ~250 ms with it.

`setReadRateLimit()` must be **idempotent**. `UsenetWorker::applyRateLimits()`
re-applies the figure on every lease, every release and every split tick, and a
call used to reset the budget to one fresh tick and restart the refill timer.
With data streaming in, each call was a free refill — measured live, the engine
ran ~12 % over its cap; re-applied faster than the 100 ms refill, the timer never
fired and a buffered socket stalled outright. It also threw away the banked
credit above. An unchanged figure is now a no-op, and a new one keeps the
socket's credit or debt within the new burst cap —
`readRateLimit_reapplyingKeepsBankedCredit` and
`readRateLimit_reapplyingGrantsNoExtraBudget`.

## IPC

Requests `720–799`, pushes `910–949`.

| Opcode | |
|---|---|
| `GetNewsServers = 720` / `SetNewsServers = 721` / `TestNewsServer = 722` | provider accounts; the password never travels to the GUI |
| `GetUsenetQueue = 723` | the whole queue |
| `AddNzb = 724` | `[bytes, name, automatic, force, password, category, priority, paused, skippedFileIndices]` → `[ok, idOrError, outcome]`. Fields 4–8 are optional; a skip list naming a PAR2 file or every payload file refuses the add. The file's **contents**, not a path — the daemon may be on another machine |
| `AddNzbUrl = 730` | `[url, automatic, force]` → `[ok, idOrError, outcome]`. **The daemon fetches.** A `.nzb` link is often reachable only from the daemon's own network — a self-hosted indexer on its LAN — and the bytes have to reach the queue regardless, so a GUI-side fetch is two hops. `http`/`https` only: `QNetworkAccessManager` also speaks `file:` and `qrc:`, and a client naming one of those would be asking the daemon to read its own disk. Private addresses are deliberately allowed — that is where self-hosted indexers live. One URL per request, so a dead link costs its own line and no reply waits out another URL's timeout. |
| `SetNewsServerUsage = 731` | `[accountId, periodBytes, totalBytes]` → `[ok, error]`. -1 leaves a figure alone, so a reset is `[id, 0, -1]`. It exists because the meter is *measured*, not reported — a user who switches plans, corrects a billing day or tops up a block account has no other way to make it true. The counters themselves ride **read-only** on `GetNewsServers`, the way `hasPassword` does |
| `SetUsenetItemPassword = 735` | `[itemId, password]` → `[ok, error]`. **Write-only** — the password never travels the other way, exactly as `GetNewsServers` reports only `hasPassword`; the item map carries `hasPassword` and `passwordRequired` instead. An empty string clears it, which is how a user takes back a wrong guess. Setting it on a *failed* item **retries** it: the whole release is already on disk, and asking the user to press Resume after they just answered the only question blocking it is a step with no decision in it. `ok` false means the item is unknown, never that the password is wrong — nothing knows that until the unpack runs. Numbered explicitly because 733 and 734 are not: an opcode inserted mid-block renumbers everything after it |
| `SetUsenetItemCategory = 736` | `[itemId, category]` → `[ok, error]`. The *index* travels, never a path: which folder that is gets resolved at completion, so a category repointed between queueing and landing does the right thing with nothing re-sent. |
| `SetUsenetCategoryStatus = 737` | `[category, action]` → `[ok, error]`, reusing `Ipc::CategoryAction`. **Pause, Resume and Cancel only.** `Stop` is ED2K's "keep the file, drop the sources" and Usenet has no sources; `ResumeNext` ranks by the category's a4af priority, which is an ED2K concept. Both are refused with a reason rather than ignored. Category 0 means every release, categorised or not — the same thing the "All" tab shows. |
| `CheckUsenetItem = 732` | `[itemId]` → `[ok, error]`. Re-run the availability probe: ask the accounts, with STAT, whether they still hold the release. A release queued a week ago is a different question from the one answered when it was added. The **verdict** needs no opcode — it rides the item map `GetUsenetQueue` and `PushUsenetQueueItem` already carry, as `postPercent` and `stalledReason` do. `ok` false means "not right now" (unknown item, wrong state, nothing to ask), never "this release is bad" |
| `GetUsenetKnownTypes = 733` | `[titles]` → `[ok, types]`. What we already know about each indexer search row. The Usenet counterpart of `GetKnownTypes = 156`, and it reuses that enumeration's **numbering** so one colour helper serves both result models: 0 unknown, 2 in the queue now, 3 downloaded, 4 cancelled (1 `Shared` is never emitted — a release is not a shared file). **Titles only, no sizes**: an indexer's reported size is its own arithmetic over the NZB and disagrees with `totalEncodedBytes()` often enough that matching on it produces *false negatives* — no warning at all, which is the wrong way to be wrong. A false positive costs one dismissible question |
| `RemoveUsenetItem = 725` … `SetUsenetItemPriority = 728` | per-item actions |
| `ListUsenetArchiveEntries = 729` | the files inside an archive set; **this one fetches** |
| `GetUsenetStats = 738` | `[]` → `[ok, map]`. The Statistics window's Usenet branch: session and cumulative counter blocks, live figures, the queue summary and one row per account (the `GetNewsServers` row plus its session figures). Its own request because `GetStats` is polled every second for the status bar; this one only while the panel is on screen. See "Statistics" |
| `SetUsenetFilesSkipped = 739` | `[itemId, [fileIndex…], skipped]` → `[ok]` or `409 why`. Widened to whole archive sets; refuses PAR2 files, every payload file at once, and a release post-processing or complete. See "A skipped file is fetched only when a repair needs it" |
| `SetUsenetPaused = 740` | `[paused]` → `[ok]`. The engine-wide pause; persisted as `usenet.paused` and **not** settable through `SetPreferences`. Needs no running engine |
| `InspectNzb = 741` | `[bytes, name]` → `[ok, {name, files: [{index, name, size, isPar2, setKey}]}]` or `[false, error]`. Parses without queueing, for the Add NZB chooser; `setKey` groups an archive set's volumes |
| `GetUsenetItemDetails = 734` | `[itemId]` → `[ok, map]`. The queue row plus everything it leaves out: per-file article tallies, poster, date, newsgroups, the NZB's own subject and part counter, and the scratch path. On-demand and **never pushed** — `PushUsenetQueueItem` fires every 250 ms per changing item, and a newsgroup list plus a per-file segment tally on each of those is a continuous cost for a dialog nobody has open. Built on `usenetItemToCbor()` rather than beside it, so the list and the details view cannot disagree about a field they share |
| `PushUsenetQueueItem = 910` | one item, coalesced on its id |
| `PushUsenetItemRemoved = 911` | uncoalesced — a removal behind a later change would be dropped |
| `PushUsenetItemFinished = 912` | uncoalesced — a transition, not a latest value |
| `PushUsenetEngineState = 913` | `[{paused}]`, uncoalesced. The item rows also carry per-file `state`, `skipped` and — only for non-uniform files — `segmentMap` |

Two fields carry the "you already downloaded this" exchange, and both are on the
existing opcodes rather than a new pair:

- **`force`** re-sends an add the user was asked about and said yes to. The shape
  is `ProbeHttpCacheServer` then `ApplyHttpCacheConfig`: the daemon keeps *no*
  state between the question and the answer, so there is no token to expire, no
  parked payload to bound, and nothing for a disconnect to strand. The one path
  where re-sending would cost something real — an indexer grab, against the daily
  API limit — never pays it, because `GetUsenetKnownTypes` has already marked the
  row and the question is asked *before* the grab.
- **`outcome`**, a `UsenetAddOutcome` int on the reply. Without it a caller has to
  tell "we already have this" from "that broke" by reading the sentence, which
  stops working the first time one is reworded or translated — the same failure
  the enum was introduced to prevent for automatic callers. It is what lets
  `AddNzbUrlDialog` split a pasted batch and ask **one** question about it.

All three intake handlers share `IpcClientHandler::sendAddNzbResult()`, so their
codes cannot drift apart.

Preview deliberately owns **no** opcode. It is HTTP on the daemon's web server,
because a media player has to be able to fetch it; `PreviewDownload = 250`
survives only as a 410 telling an old GUI so.

`DaemonApp::connectUsenetPushes()` owns the seam. It is not in
`CoreNotifierBridge` because that class is built on core signals, and routing a
non-core object through it would mean linking `eMule::Usenet` into a class whose
whole job is core.

## Streaming preview

Two phases. **6a** (Tier A of `UsenetModule-Research.local.md` §7.3) fetches in
order and serves the part of the file that has arrived — enough for a
**raw-posted** release, a `.mkv` in yEnc or a `.001` split. **6b** (Tier B) adds the
container: it maps reads of the file *inside* a stored RAR set onto the volumes
holding it, so a release plays from byte 0 and a seek fetches only the articles
it lands on.

Almost all of the serving half already existed for ED2K preview. What phase 6a
added is the three things Usenet needs that ED2K does not.

**Order.** `rebuildPlan()` sorts by `NzbSegment::number`, not document order — an
NZB may list its segments in any order and `NzbInfo` only sorts inside
`hasAllSegments()`. It sorts the *indices*: `done` is a bit per index into the
segment list, so reordering the list itself would reattribute every bit in the
release.

**A retry goes back at the cursor, not the tail.** Both requeue paths —
`handleSegmentFailure()` and the `noServerAvailable` branch of
`onSegmentFinished()` — insert at `planCursor`. One early article refetched last
pins the readable prefix at that byte for the whole download, and fixing only one
of the two paths does nothing: a dropped connection backs its server off, so the
retry comes straight back as "nothing leasable" and the other path moves it to
the tail anyway. Termination is unchanged; only the position moved.

**`UsenetFileState::written`, a merged interval list.** `done` cannot answer "is
byte X readable?" — its bit means *resolved*, and an article missing on every
server sets it having written nothing. Nor can the NZB: `<segment bytes>` is the
*encoded* size. The ranges come from the article's own `=ypart begin`, carried
`YencDecoder::offset()` → `ArticleFetcher::decodedOffset()` →
`UsenetFetchResult` → here, and are persisted in the sidecar. `availableEnd()` is
the contiguous run from byte 0 — deliberately not a byte count, because a hole
stops playback whatever is beyond it. A missing article therefore stalls preview
until PAR2 repair fills the hole; nothing is synthesised, because a guessed
offset serves misaligned data and looks like corruption.

`UsenetQueue::requestStream()` is the whole seam. It is not a pure lookup: it
also boosts the item above the priority field for 30 s, so a preview does not
share the connection budget with an unattended download, and the boost lapses on
its own.

### Mapping a stored RAR set

A stored (`-m0`) volume's payload is a byte-identical slice of the original
file, so playing a release is reading the right volume at the right offset.
Nothing here could do the mapping: `ArchiveReader` is path-based
(`archive_read_open_filename`) and exposes neither the compression method, the
packed size, nor an entry's data offset — those live only in libarchive's
private RAR structs — and it could not be pointed at a half-downloaded volume
anyway. So `stream/RarReader` parses RAR4 and RAR5 headers directly.

Multi-volume is not what gates this; **compression is**, and the two are
independent flags. Stored means `packedSize == unpackedSize` and a volume's
payload is a contiguous byte-identical slice, so a read is arithmetic — no
decoder, no state, and none of the bytes before the offset. A compressed member
is LZSS with a sliding window and the container records no restart points, so
byte *n* requires decoding everything before it; solid is worse still, the
window carrying across file boundaries. That is why a compressed release is
still *unpacked* while downloading — libarchive handles it fine — and still not
*seekable*: extraction runs forward from volume 1, preview needs random access. It is pure
(a `QByteArray` in, a struct out), truncation-safe at every field, and
deliberately does **not** verify header CRCs: we are mapping, not extracting,
and a wrong map fails the `=ypart` cross-check regardless.

`stream/UsenetStreamIndex` turns that into extents, and its defining property is
that **it never fetches and never blocks**. Missing something, it returns
`NeedBytes`; the queue promotes the articles covering that range and 6a's
existing 250 ms poll asks again. The state machine advances one step per tick,
which is why 6b needed no new waiting machinery at all.

**Three articles place an arbitrarily long set.** `rar -v` pads every volume
alike, so parsing volumes 1, 2 and N and checking that `p1 + (N-2)·p2 + pN ==
unpackedSize` proves the shape of the whole thing without reading anything in
between. A set that fails the identity falls back to walking headers one volume
at a time — correct, one article each, and rare. Either way a volume is only
ever *served* from once its own header has been parsed, because the model places
a volume without saying where inside it the payload begins: RAR5's volume-number
field grows a byte at volume 128, and a header assumed one byte short maps every
read in that volume off by one.

**A seek does not open connections.** `UsenetWorker` divides `maxConnections`
across workers so the sum matches what the user configured, and exceeding a
provider's limit gets an account suspended. So `requestStream()` promotes the
covering articles to `planCursor` and drags the cursor back with them, which
also makes the sequential read-ahead follow the play head. Which article holds a
byte is §7.1's arithmetic: `UsenetFileState::partLength` — the largest decoded
article seen, since a poster cuts a file into equal parts with a short remainder
— with one part probed either side. A volume nobody has touched has no part
length of its own and borrows a sibling's; the volumes of a release come out of
one posting run.

The fetched bytes are written to their real place on disk rather than cached,
which is where this departs from nzbdav's design. nzbdav has no download behind
it and needs an LRU of decoded articles; here the volume file is already open
and preallocated to its full length, so a seek is permanent, counts as real
progress, and needs no eviction policy at all.

**What the map cannot do, the extraction can.** A compressed, solid or
header-encrypted archive has no byte mapping and never will — but direct unpack
is already writing the payload out as the volumes land, and that growing file is
what a player wants. So `UsenetQueue` has *two* byte sources and picks between
them: the map first, because only it can seek ahead of the write head, and the
extraction for everything the map refuses.

The join is at the queue, not in the index. `UsenetStreamIndex` stays a pure
function of bytes on disk — which is what makes `invalidate()` free — and
`ItemRuntime` already owned both halves. `ArchiveReader` gained an opt-in
progress sink, flushed *before* each report so the byte count is one a reader can
actually get back, and `UsenetDirectUnpack` ships those out as a queued signal
rather than a lock the HTTP thread would take on every poll.

Three rules make it safe, and each of them is a bug that was found rather than
foreseen:

- **Always a real total, or no answer at all.** `serveRange` derives the total
  from the pieces when it is not told one, and for a growing file that total is
  whatever has been extracted — so the player's opening request, which carries no
  Range header, was answered `200 OK` with "the whole movie, 4 MB long". The
  route now says `bytes X-Y/*` when the length is genuinely unknown, and the
  queue would rather wait than serve without a total.
- **Never advertise less than before.** An extraction that restarts truncates its
  output to zero; the high-water mark outlives the run, and a fresh run below it
  reads as "wait".
- **A refusal is final, a wait is not.** The route treats `notSeekableReason` as
  terminal, so a set that is *going* to be extracted must never be refused while
  it is still coming — but only a media member the map could not place earns that
  wait. A release with nothing playable in it is refused at once, as before.

**What is still refused** says why: an encrypted **7z** set, an encrypted set
with no password or no external unpacker installed, one whose extraction failed,
an item with direct unpack switched off, and the third concurrent set
(`kMaxDirectUnpacks`). A password-protected **RAR** is no longer on that list —
see below. 406 from the route, `previewNote` over IPC,
a tooltip on the disabled menu entry. §7.3 asked for exactly that: the answer
surfaced rather than discovered at play time.

A **fully obfuscated RAR4** set used to be on that list too — RAR4 has no
volume-number field, so with the names scrambled there was nothing to order the
volumes by, and it degraded to not-seekable. It is orderable now, from the PAR2
set's own file list; see § What a file is called. Without an index `.par2` in the
release the old limit stands, and the degradation is still to not-seekable rather
than to a mis-map.

### The third byte source: previewing an encrypted RAR

Both sources above fail on a password-protected set, and for different reasons.
The map cannot work **in principle** — the bytes on disk are encrypted, so a
slice of a volume is not a slice of the movie, and no amount of header parsing
changes that. Direct unpack cannot work **in practice**: it feeds libarchive one
volume at a time through `ArchiveVolumeSource`, and libarchive is precisely the
thing that cannot decrypt RAR.

What is left is to run the external tool over the volumes that *have* landed,
let it fail at the first one that has not, and keep the prefix it produced. RAR
allows that because its headers sit at the front of every volume.

**7z does not, and never will.** A 7z set keeps its metadata at the *end*, so an
incomplete one opens as `Cannot open the file as [7z] archive` and yields **zero
bytes** — measured, not assumed
(`tst_UsenetPassword::anIncomplete7zSetCanNeverBePreviewed`). For 7z the answer
is a refusal, not a wait.

`UsenetEncryptedPreview` is that run, and everything about it is a cost control:

- **Nothing starts on its own.** The trigger is a `requestStream()` on an
  encrypted set, because every run re-decrypts from volume one and there is no
  resume. `streamingUntilMs` is the window; when it lapses the process is killed.
  One run per item, `kMaxEncryptedPreviews` across the queue, and never more often
  than `kEncryptedPreviewRerunMs` or without new volumes since the last one.
- **Only contiguously sealed volumes are staged.** A target file is preallocated
  to its full length, so an in-progress volume reads as a run of *zeros* rather
  than as a short file — hand one to the tool and the prefix it produces is
  quietly wrong instead of quietly short. Same for a volume with a hole:
  `sealFile()` pads with zeros, and zeros inside an encrypted stream decrypt to
  garbage. They are hardlinked into a scratch directory of their own, because both
  tools follow a set by *name* from whichever volume they are handed and would
  otherwise open the volumes still being written beside them.
- **A run always restarts at byte 0, so it writes to `<member>.partial`** and is
  promoted over the visible file only when it is longer. The prefix is
  deterministic — same archive, same password — so a reader keeps reading the old
  file untouched while the next run builds. That is the "never advertise less than
  before" rule again, load-bearing rather than defensive.
- **The member is chosen once**, on the worker thread: `x -so` with no member name
  concatenates every member, and a release's sample and subtitles are playable
  names too. `l -slt` gives names and sizes; unrar's equivalents are column
  layouts that shift with locale, so with only unrar installed the sizes come back
  0 and the preview declines rather than guessing a total it cannot reach.

The join is at the queue again. `streamFromEncryptedPreview()` produces exactly
what `streamFromExtraction()` already consumed — a growing file and a readable
byte count — so nothing new reaches `UsenetStreamIndex`, `WebServer` or the GUI.
`usenetEncryptedPreview` turns it off.

**Keeping the extractor fed is part of the feature.** A run may only *start* on
volume one, so every volume that sealed before it has to be replayed into it —
without that, a release whose NZB lists `part03, part04, part01, …` (real posts
do) parked the run on an index nobody would ever offer and the whole direct
unpack silently degraded to unpacking at the end. And since a compressed member
has no byte map, a seek promotes the volume the extraction is *parked on* rather
than the articles covering the requested offset — or volume one, before there is
a run at all.

### Serving

`GET /api/v1/usenet/<itemId>/<fileIndex>/preview?token=…` on the daemon's web
server, beside the ED2K route and gated by the same per-process stream token. The
server runs for preview even with the web UI and the REST API both switched off,
bound to localhost.

`WebServer` may not include a Usenet header — `core → usenet` never happens — so
`DaemonApp` installs a `setUsenetStreamResolver()` callback, the same seam
`setLogProvider()` uses.

Three things about the response are load-bearing:

- **The body is capped at 4 MiB.** A player opens with `Range: bytes=0-`, and
  before the cap that allocated the whole file in the daemon: tens of gigabytes
  for a release. A short 206 is ordinary HTTP and the player asks for the next
  window. This was a live defect on the ED2K route too, and the fix is shared.
- **Nothing past `availableEnd` is served.** The file is preallocated to its
  final length, so reading further returns zeros — which does not look like an
  error, it looks like a corrupt file.
- **A request at the write head waits, up to 15 s, polled at 250 ms.** A
  zero-byte 206 reads as end-of-stream and stops playback. The wait is a deferred
  `QFuture<QHttpServerResponse>`, never a block: the handler runs on the daemon
  thread, which is the thread doing the downloading. The wait ends early if the
  item disappears, or if the release turns out to be unmappable. Each poll
  re-calls the resolver deliberately: that is what keeps the streaming boost
  alive and the promoted articles at the front of the plan.
- **A response may span several files.** `serveRange()` walks the pieces the
  window touches, so a read landing on a volume boundary comes back as one
  continuous run. The ED2K route passes a single piece, so there is still
  exactly one implementation of Range in the daemon.

The GUI reuses `PreviewLauncher::launchPreview()`; only the URL builder is new.
`GetUsenetQueue` sends `index`, `previewable` and `previewNote` per file, all
decided daemon-side — the GUI has neither the real post-yEnc filename, nor the
prefix length, nor any way to know that a `.rXX` is a mappable stored volume.
Every volume of a seekable set reports `previewable`, because they all describe
the same *set*; that is why the tree needed no new kind of row. Which file
*inside* the set gets played is a separate question — see below.

### Choosing a file inside the set

A set may hold several files: a feature beside its `.nfo`, or a season pack of
ten episodes. The preview URL therefore carries an optional `&entry=N`, an
ordinal into the set's own header order. **Absent means the first *playable*
file**, which is the whole back-compat story — a URL with no `entry=` is
byte-identical to what the GUI built before the chooser existed, and it now
plays the movie in a release that packs an `.nfo` ahead of it instead of
serving 500 bytes of text as the feature.

`ListUsenetArchiveEntries` (729) enumerates the set. **It fetches, where
`previewable` does not**: the header of the second file sits past the first
file's payload, usually in a later volume, so listing costs articles where the
per-push previewability check costs nothing. Its `status` is therefore a state
machine the GUI polls — only `Scanning` is non-terminal. It deliberately does
*not* take `requestStream()`'s 30 s cross-item boost: opening a list is not
watching a video, and an item should not outrank every other download because
someone opened a dialog. Nothing is wasted either way — the articles a scan
pulls are written to their real place in the preallocated volume file, so a
cancelled scan has merely brought work forward.

The GUI shows `UsenetArchiveEntryDialog` only when more than one file is
playable. It is built hidden and reveals itself after 200 ms, so an ordinary
single-video release answers in one loopback round trip and no window ever
appears. Unplayable entries are listed greyed out and unselectable — both
`ItemIsSelectable` and `ItemIsEnabled` are withheld, or the row still takes
arrow-key focus — with the reason in a Status column, because a greyed row with
no explanation is the failure `previewNote` exists to prevent.

**Only volume slots are ever predicted, never byte addresses.** Placing a
member's run works from one interior part size, `ceil(R / p)` volumes long —
`ceil`, because a member ending flush on a volume boundary would otherwise be
placed one volume past its end. The prediction is then *verified* by parsing
the predicted volume's head, and a miss falls back to a binary search on "does
volume *s* still hold this member", which is monotone in *s*. A guessed
mid-volume address would have no such check: a volume begins with a RAR marker,
and RAR5 defeats address arithmetic anyway, since `DataSize` is a vint and a
short tail volume's header is physically smaller than a full one's. Resumed
(mid-volume) blocks are the one place header CRCs *are* verified — their
address was computed from a previously trusted `packedSize`, so an error there
would propagate silently into every later member.

Compression is judged **per member**, not per set: `-m0 movie.mkv -m5 readme.nfo`
is legal, and refusing the archive around one compressed text file would be the
difference between "this release does not stream" and "this release streams
fine, one useless entry aside". Solidity and header encryption stay set-level,
because they are properties of the whole archive.

## Statistics

The Statistics window's **Usenet** branch (Session, Cumulative, News Servers,
Queue) and the Download graph's **Usenet** line.

**Core stores, this module counts.** `UsenetCounters` and `IndexerCounters`
(`core/stats/NetworkCounters.h`) are plain `uint64` blocks with one field walk
each. `Statistics` holds the session half and Preferences banks the rest as
nested `cumUsenet:` / `cumIndexer:` maps under `statistics:` — so a statistics
reset, a restore, `statsbackup.yml` and the periodic flush treat them like every
other cumulative counter, with nothing Usenet-specific in core. Results that
`stopWorkers()` delivers after `theUsenetSession` is nulled are still counted,
and `DaemonApp::stop()` flushes once more after the engine is gone. Counting is a
no-op while `theApp.statistics` is null, which a bare queue in a test is unless
it installs `testing::ScopedStatistics`.

**The judgement lives in `UsenetStatistics`** (a member of `UsenetQueue`), not in
the call sites:

| A fetch that ended… | counts as |
|---|---|
| with an article | downloaded, + its decoded bytes |
| with a 430 (`escalatesToNextLevel`) | not found on *that* server — the ladder asks the next one |
| with a body that would not decode (`NntpError::ArticleCorrupt`) | corrupt — tested *before* the 430 branch, which it also escalates through |
| any other transport fault, on a connection | connection error |
| because **we** stopped the worker (`aborted`: engine stop, settings save) | nothing — every settings save would otherwise count one error per article in flight |
| as a STAT probe | a probe, found or not |
| with nothing leasable, or a local fault (no connection) | nothing |

Every result charges its **wire bytes** first, the same figure the billing meter
takes. "Network Traffic" and every rate are wire bytes (what the bandwidth split
charges, and smooth where decoded bytes arrive in bursts); "Downloaded Data" is
decoded payload; the difference is the overhead — yEnc, NNTP, 430s, probes and
handshakes. Per-account session figures live here as well, keyed by `accountId`,
and **not** in the billing meter: `setUsage()` zeroes the meter's session half,
and a statistics reset must never touch billing.

Other counting sites: `markSegmentMissing` (missing on all servers, all three
reasons), `finishItem()` (the one place a terminal outcome is announced, so each
release counts once however it ended), `onPostFinished` (only a *terminal* verify
counts — a `NeedMoreBlocks` round sends the item back for volumes), the stage
clock on `ItemRuntime` (time per post-processing stage), `requestPar2Volumes`,
the health check's begin/finish, and the queue tick (download time while the rate
is non-zero; peaks once a second, after the 2 s rate window has filled). PAR2's
repaired-block count is captured in `BeginRepair()`, because afterwards the
library's counters read clean — and the rename pass can be the one that repairs.

Intake is counted at the call sites with `UsenetAddOrigin` (file, URL, watch
folder, feed, indexer grab), because each caller knows its origin and `addNzb()`'s
signature does not need a ninth argument. A `Failed` add is not counted: the watch
folder and feeds retry it. Indexer load is counted in `IndexerClient::finish()`;
a reply `abortAll()` cancelled is not a request the indexer served, and a 4xx
whose body is an error document is one error, not two.

"Open Connections" comes from a registry of **authenticated** `NntpSocket`s per
account — the one cross-thread piece, hence a mutex, written only on connect and
close. With no idle timeout a pooled connection stays open between articles, so
"Active Connections" (fetches in flight, `UsenetQueue::activeFetches()`) is shown
beside it.

The **Transfer** branch is eD2K's alone. Usenet used to add its decoded bytes to
`Statistics::addSessionReceivedBytes()` — the only caller it had — so "Downloaded
Data", both ratios and `cumTotalDownloaded` counted Usenet and nothing else.
Usenet bytes banked into `cumTotalDownloaded` before that change cannot be
separated out again.

## Persistence

One YAML sidecar per item at `Config/Usenet/<id>.nzbstate`, globbed on start —
the same shape as `DownloadQueue::init()` scanning for `*.part.met`, and for the
same reason: one item's corruption costs one item. The write is
`PartFile::savePartFile()`'s dance (write `.backup`, rotate live to `.bak`,
rename into place, restore from `.bak` on failure).

Segment completion is a `QBitArray`, base64 inside the YAML — 1.25 KB for a
10 000-article release, which is what makes per-segment resume affordable.
`Downloading` is demoted to `Queued` on load: nothing is in flight after a
restart.

Per file, `skipped` and `neededForRepair` are optional keys written only when
true. Absent means "download it", which is what every older sidecar meant, so
`kStateVersion` stays at 2. Both are loaded as well as written, because
`remapCategories()` round-trips sidecars through `load()` and `save()`. The
engine-wide pause is not an item fact and lives in `preferences.yml` as
`usenet.paused`.

`Config/Usenet/history.yml` holds what has *left* the queue — see "What survives
leaving the queue" above — and `Config/Usenet/usage.yml` the per-account meters, both beside those sidecars
(they are globbed by `*.nzbstate`, so a `.yml` among them is never mistaken for
a queue item), through the same rotation dance — extracted as
`writeSidecarAtomically()` rather than copied a third time.

Two rules the counter is shaped by, both learned elsewhere in this codebase:

- **Base + session, written absolutely.** `load()` reads the base, `add()` only
  touches the session, `flush()` writes their sum — so flushing twice changes
  nothing and an unclean exit loses only the session. The increment-on-save shape
  it replaces lost a whole session to a `kill -9` and double-counted the moment
  it ran on a timer. A correction zeroes base *and* session together, or the next
  absolute write emits `0 + session` and brings the old figure back. Flushed on
  `statsSaveInterval` **and** after 256 MB, because a minute at 5 MB/s is 300 MB
  of prepaid credit.
- **A period identifier, not a "last reset" timestamp.** `periodStart` is
  recomputed from the calendar and today's date, so a daemon that was off across
  four billing days resets exactly once and lands in the right period, and a
  clock that moved backwards is refused rather than allowed to resurrect a spent
  period. The check runs on the queue's own tick, not lazily inside `add()`: a
  parked queue spends nothing, so a lazy check would never fire and a
  single-account queue would sit parked past its own billing day forever.

## Web interface and REST API

The daemon's web server has a **Usenet** tab beside Transfer, Kad and the rest,
and the REST API a `/api/v1/usenet` resource. Both are the Qt window's queue,
reached without the GUI.

### The seam

`WebServer` lives in eMule::Core, which may never name eMule::Usenet, so it reads
the queue through `UsenetWebBackend` (`src/core/webserver/UsenetWebBackend.h`).
`DaemonApp` owns a `DaemonUsenetWebBackend` and installs it next to the preview
resolver; it survives web-server restarts because a URL fetch in flight holds on
to it. Without a backend every route answers **503 "Usenet engine unavailable"**
and the page says the same — which is what `tst_WebServer`'s plain fixture gets.

Rows cross that seam as the IPC's own CBOR maps. The serializers moved out of
`IpcClientHandler.cpp` into `src/daemon/UsenetBridge.{h,cpp}`, together with the
add paths (`addNzbData`, `addNzbUrl`, both counted exactly once for the
statistics), the availability re-check and the category-wide walk. IPC, the page
and the REST API are thin wrappers over the same functions, so a field added to
the GUI's row is in the web page and the API the moment it exists.

The one thing the daemon cannot say is a per-item speed — the engine measures
itself as a whole. `ByteRateSampler` (`src/core/utils/ByteRateSampler.h`) derives
it from `decodedBytes` for the GUI's model and for the web server alike; the web
server samples on every request, whichever browser or script asked.

### The page

`w=usenet` mirrors `UsenetPanel`:

- **Toolbar** Add NZB… · Pause · Resume · Remove, acting on the selection
  (click, Ctrl/Cmd-click or the checkbox column), and the **category tabs**
  right-aligned beside it, coloured like `CategoryTabBar`.
- **Queue** with the model's nine columns and styling — bold while working, red
  failed, grey paused, blue post-processing — and each release's files as
  expandable child rows. Column headers sort server-side on raw values, with the
  shared `usenetStatusRank()` (`src/core/utils/UsenetDisplay.h`).
- **Context menu**: Add NZB…, Add NZB from URL…, Pause, Resume, the five
  priorities, Assign to category, Set Password…, Check Availability, Preview,
  Open File (bold, the double-click), Open Folder, Details…, Remove, Remove and
  Delete Files. The category tabs have Pause / Resume / Cancel.
- **Add NZB panel** — files and pasted links (at most 20, like
  `AddNzbUrlDialog`), password, category, priority, add paused. An
  `AlreadyDownloaded` answer is asked about and re-sent with `force`.
- **Details** — the dialog's fields and file table, refreshed every second.
- **Preview** opens `/api/v1/usenet/<id>/<file>/preview` after asking
  `/usenet/entries`, exactly as `UsenetArchiveEntryDialog` does: one playable
  entry plays at once, several open a chooser, a solid or encrypted set says why.
- **Summary line** — count, active, percent, stall reason, the eD2K split note,
  plus the engine's rate, which the web UI has no status bar for.

Deliberate differences from the Qt window:

- **Language** follows the app, and the header's menu overrides it per session,
  like every web page — see `docs/web-interface.md`.
- **A poll, not pushes.** The list fragment (`part=list`) is fetched every two
  seconds while the tab is visible and swapped in place; selection, expanded rows
  and scroll position are kept in the script. A menu that is open holds the poll.
- **Actions are JSON POSTs** to `/usenet/action` and `/usenet/add`, gated by an
  admin session — not GET-and-render like the Transfer page, so a reload never
  repeats an action and a passphrase never lands in a URL (beside a raw `.nzb`
  body it rides the `X-Nzb-Password` header).
- **Guests** see the queue, Details, Preview and Open; every change is refused
  with 403. The stream token is written into the page for both, because Preview
  and Open go through the token-gated preview and incoming routes.
- **Open Folder resolves from `publishedFiles`**, never `files[].finalPath`, so
  the page does not have the Qt window's defect listed under § Not built yet.

Every user-supplied string goes through `htmlText()`. The template engine fills
`[Key]` in one pass and never rescans a value, so a release named `[Session]`
stays that name. Escaping and translation rules: `docs/web-interface.md`.

### REST

All under the REST API's `X-Api-Key`. Rows are the `GetUsenetQueue` maps plus
`speed`; details are the `GetUsenetItemDetails` map.

| Method & path | Does | Answers |
|---|---|---|
| `GET /api/v1/usenet[?category=N]` | the queue | `200 [row…]` |
| `GET /api/v1/usenet/stats` | `count`, `active`, `percent`, `totalBytes`, `decodedBytes`, `stalledReason`, `rate`, `maxDownloadKb`, `usenetLimitKb`, `ed2kBudgetKb`, `paused` | `200 {…}` |
| `POST /api/v1/usenet/pause` · `resume` | the whole engine; persisted, no item status changes | `200 {paused}` |
| `GET /api/v1/usenet/<id>` | one release in full | `200` · `404` |
| `GET /api/v1/usenet/<id>/<file>/entries` | the archive listing; asks for header bytes | `200 {status, note, entries}` |
| `POST /api/v1/usenet` | add — see below | `200 {id, outcome}` · `400` · `409` · `429` · `500` |
| `POST /api/v1/usenet/<id>/pause` · `resume` · `check` | per-release action; resume overrules a check-stop (`ResumeIntent::User`) | `200 row` · `404` · `409 {error}` when the queue refuses |
| `PATCH /api/v1/usenet/<id>` | any of `{priority, category, password, skipFiles, unskipFiles}` (the last two are NZB file-index arrays, widened to archive sets); all validated before any applies | `200 row` · `400` · `404` · `409` when the queue refuses a skip |
| `DELETE /api/v1/usenet/<id>[?deleteFiles=true]` | remove | `200 {removed, deletedFiles}` · `404` |
| `POST /api/v1/usenet/categories/<n>/pause` · `resume` · `cancel` | the category, 0 being all; resume is `Bulk` | `200 {affected}` · `400` |

An add takes one of three shapes:

```
POST /api/v1/usenet                      Content-Type: application/json
{"url": "https://indexer/getnzb?id=…", "category": 2, "priority": 1, "paused": false,
 "password": "…", "force": false}

POST /api/v1/usenet?name=Release.nzb&category=2&priority=1   Content-Type: application/x-nzb
<the .nzb bytes>                         (password in X-Nzb-Password, percent-encoded)

POST /api/v1/usenet                      Content-Type: application/x-www-form-urlencoded
url=…&category=2&password=…
```

`outcome` is `UsenetAddOutcome`: 0 Added, 1 Duplicate, 2 Invalid, 3 Failed,
4 AlreadyDownloaded. **409 is a question**: Duplicate is final, AlreadyDownloaded
is answered by re-sending with `force`. 429 means four URL fetches are already
running. A URL add answers when the fetch lands, not before.

The literal routes are registered before the `<arg>` ones, so `stats` and
`categories` are never taken for an item id; `tst_WebServer` asserts it.
Streaming stays on the token-gated `/api/v1/usenet/<id>/<file>/preview`, and a
row's `files[].previewable` / `previewNote` say whether it will work.

## Not built yet

The Usenet features this module still lacks, found by sweeping these docs, the
source, and a feature-by-feature comparison with SABnzbd and NZBGet (2026-09-15).
**The numbers are stable** — refer to "#19" in plans and commits; a built item is
marked, never renumbered or removed. Sizes are relative (S/M/L).

**Queue and downloading**

1. **History view** of finished and failed releases. The daemon keeps
   `UsenetHistory`, but only to catch duplicates; the GUI can neither see nor clear
   it. (M)
2. **Clear Completed, an explicit "Re-fetch missing articles", and re-running
   post-processing on a finished item.** Resume only re-asks missing articles when
   the account list changed since the failure. (S–M)
3. **Stop downloads that cannot be repaired**, instead of spending the allowance
   until the final verify. (S–M) *(built 2026-09-15)*
4. **Pick files inside an NZB** — skip samples and extras, pause one file. (M)
   *(built 2026-09-15 — at add time and in the queue; see § Segment bars, file
   status and picking files)*
5. **Propagation delay** — do not fetch posts younger than N minutes. (S)
6. **Scheduler actions for Usenet** — the scheduler cannot pause or resume it. (S)
7. **Speed limit per server or per item**; there is one global Usenet limit. (M)
8. **Queue order within a priority level survives a restart.** Sidecars load in
   UUID filename order. (S)

**Post-processing**

9. **SFV/CRC32 check when a release has no PAR2**; such releases are published
   unchecked. (S) *(built 2026-09-15)*
10. **A folder per release in Incoming**; everything lands flat. Sorting into
    TV/movie folders on top of that is L. (S)
11. **Rename junk-named output to the release name** after unpacking. (S)
12. **Archives inside archives, and split zips named `.zip` + `.z01`.** (S–M)
13. **Unwanted-extension / fake-release check** — `.exe`, `.scr`, `.lnk` inside a
    video release. (S–M) *(built 2026-09-15)*
14. **Per-category post-processing settings**; repair, unpack and cleanup are
    global switches. (S–M)
15. **User scripts after a download, per category.** (M)
16. **A password list to try, and acting on encryption as soon as it is
    detected.** (S)
17. **Keep a copy of every added `.nzb`**; only the watch folder keeps originals. (S)
18. **Accept `.nzb` inside `.zip`/`.gz` for local adds**; URL and indexer grabs
    already unwrap them. (S)

**Integration**

19. **SABnzbd-compatible API** so Sonarr, Radarr and Lidarr can add and track
    releases. Needs #1 for output paths. (M–L)
20. **Completion and failure notifications** — email and tray popups exist only
    for eD2K. (S)
21. **Usenet speed in the tray tooltip, status bar and MiniMule**, which show eD2K
    only. (S)
22. **Proxy for news-server connections**; eD2K sockets honour the proxy, NNTP
    connects directly. (S–M) *(built 2026-09-15)*
23. **Usenet in the web interface**; only the preview link exists there. (M–L)
    *(built 2026-09-15, with a REST API — see § Web interface and REST API)*
24. **Merge the Usenet tab into the shared transfers list** — the refactor
    deferred under § GUI. (L)

**Indexer search**

25. **Typed search** — TV/movie modes, season/episode/IMDb, categories, a choice
    of indexer. The daemon can report what each indexer supports; the GUI never
    asks. (M)
26. **Easynews search**, through an unofficial endpoint that will break. (S)
27. **Search sites without a newznab API** such as binsearch and nzbindex, by
    scraping. (M)

**Security and plumbing**

28. **Keep the encryption key in the OS keychain**; it sits in `preferences.yml`
    beside the passwords it encrypts. (M)
29. **A dedicated IPC message for post-processing progress.** Mostly delivered
    already. (S)

**Found comparing with Newshosting (2026-09-15)**

30. **Segment-map progress bars and per-file status icons** — done, missing,
    in-flight and queued articles by position, per file and per release; the
    Progress column is a bare percentage. (S–M) *(built 2026-09-15)*
31. **Pause the whole Usenet engine** — stop starting new work and leave every
    item's state alone. Only per-item and per-category pause exist, and both
    rewrite item states. The hook #6 needs. (S) *(built 2026-09-15 — toolbar,
    tray, web page and REST)*
32. **Details for an indexer result before grabbing** — cover, rating, IMDb id,
    NFO from the search feed; the release's file list only on request, because
    fetching the NZB spends a grab against the daily API limit. (M)
33. **Per-connection NNTP activity view** — what each socket is doing right now.
    Statistics shows only open and active counts. (M)

**Known defects, not features**

- Open Folder on an unpacked release says "Nothing has completed yet":
  `UsenetPanel::onOpenFolder` reads only `files[].finalPath`, which unpacking
  deliberately leaves empty, while `publishedFiles[].path` has the answer
  (`UsenetPanel.cpp:802`, `:884`).
- The eD2K Priority column prints `Auto [%1]` with a number where MFC prints a
  word (`DownloadListModel.cpp:311`).
- A `SetPreferences` from a client other than the GUI does not re-apply Usenet
  settings; the GUI's OK works only because it sends `SetNewsServers` after it.

Deliberately not on the list, because these docs argue against them: posting,
crawling newsgroup headers into a local index, an in-memory article cache, and an
Options page for subject patterns.

## Build note

`src/usenet/`, `src/gui/panels/`, `src/daemon/` and `src/core/webserver/` are
globbed by CMake, so new files need no CMake edit — but MSBuild and qmake do
(`src/daemon/daemon.pro` and `emulecored.vcxproj` for the web backend). Run `scripts/sync_module_vcxproj.py` and
update `src/usenet/usenet.pro`; a new test also needs
`python3 scripts/generate_test_vcxprojs.py`.

After adding any `Q_OBJECT` header, check that
`build/src/<module>/<target>_autogen/mocs_compilation.cpp` lists a `moc_` line for
it. AUTOMOC's parse cache goes stale and re-running cmake does not fix it; delete
the `*_autogen` tree and rebuild. This has bitten this module more than once.
