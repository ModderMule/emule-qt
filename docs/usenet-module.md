# Usenet (NNTP) module

eMuleQt downloads from Usenet through `eMule::Usenet`, a static library that is a
peer of `eMule::Core` and `eMule::Ipc` rather than a part of either. The
dependency runs one way only: `usenet → core` is fine, `core → usenet` never.
`emulecored` links it; the GUI stays daemon-agnostic and talks IPC.

Design research and the phase plan are in `docs/UsenetModule-Research.local.md`,
amended by `docs/BitTorrentModule-Research.local.md` §7.

**Status: phases 0–4 are implemented.** The module connects, authenticates,
parses an NZB, downloads it across a pool of worker threads, assembles it
byte-identically, verifies it against its PAR2 set, repairs it, restores
obfuscated filenames, unpacks the archives, and offers the payload — and only the
payload — to the ED2K network. It has a queue with persistence, a Usenet tab in
the GUI, and a share of the global download budget. Keyword search is phase 5 and
not built yet.

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
  and it cures itself.

`NntpError` encodes that distinction, and `escalatesToNextLevel()` is the single
place the rule lives. Getting it backwards either hammers a dead server or gives
up on an article a fill server would have had.

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
      maxConnections: 20
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
```

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
700–719 / 900–909 are reserved for the shared newznab/torznab indexer client.)

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
`emuleqt --options usenet`). Account list plus a details form and a **Test**
button. Column layout persists under the `optionsUsenetServers` key in
`uistate.yml`.

The download queue is phase 3 and will be a **standalone Usenet tab**. Merging it
into the shared transfer list is a separate refactor for after every phase is
built and tested — nothing in phases 0–6 is shaped around that merge.

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
  `=ybegin name=`, which `ArticleFetcher::articleFileName()` exposes.

If these heuristics ever need tuning they belong in a YAML list under `usenet:`,
not compiled in — obfuscation schemes change faster than releases ship.

## Tests

| Target | Covers |
|---|---|
| `tst_UsenetSmoke` | the library links and moc ran |
| `tst_NntpSocket` | greeting, auth, TLS modes, watchdog, dot-unstuffing, rate limit |
| `tst_NntpServerPool` | level normalisation, rotation, exclusion, backoff |
| `tst_UsenetPrefs` | round trip, encryption, ordering, mint condition, caps |
| `tst_UsenetYenc` | CRC vector, every byte value, the four traps above |
| `tst_UsenetNzbParse` | schema, namespaces, HTML error pages, subject heuristics |
| `tst_UsenetArticleFetch` | a multi-part file assembled **out of order**, byte-identical |
| `tst_UsenetPar2` | verify, repair, rename and the blocks-needed figure, against sets built in-process by `Par2::par2creator` |
| `tst_UsenetUnpack` | volume-set detection across all three naming schemes; path-traversal and reserved-name refusals |
| `tst_UsenetPostPipeline` | phase 4 end to end: a healthy release fetches **no** recovery volumes; a damaged one fetches them, repairs, unpacks and publishes the payload alone; an unrepairable one publishes nothing |
| `tst_UsenetLiveConnect` | real TLS + auth (`live`) |
| `tst_UsenetLiveFetch` | a real `.nzb` downloaded and hashed (`live`) |

Offline tests run against `tests/FakeNntpServer.h`, a scriptable in-process NNTP
server modelled on NZBGet's `daemon/nserv/`. It exists because the interesting
cases are the refusals — 400 at the greeting, 481 on auth, 430 for a missing
article, a server that simply stops answering — and none can be provoked against
a real provider on demand.

The live tests skip when their environment is unset, so an unconfigured checkout
never looks broken:

```sh
export EMULE_NNTP_HOST=news.example.com EMULE_NNTP_USER=… EMULE_NNTP_PASS=…
cmake -S . -B build -DEMULE_LIVE_TESTS=ON
ctest --test-dir build -L live -R tst_Usenet
```

## Adding a source file

The CMake glob picks new files up; MSBuild does not. Run
`scripts/sync_usenet_vcxproj.py` to regenerate the source lists in
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

## Failover

The one rule, and it must not be re-derived anywhere else:

- `escalatesToNextLevel(error)` — the server does not have the article (430) —
  retry at `level + 1`, adding that account to `ignoreServers` so the escalation
  never asks it twice.
- Anything else is a *connection* fault. Retry the **same** level; the worker has
  already backed the server off, so a sibling account picks the article up.

Getting it backwards either hammers a fill server every time the main provider is
briefly busy, or leaves a paid block account never used.

An article missing on every level is recorded in `missingSegments` and its bit is
set in `done` — the bit means **resolved**, not "arrived". The file is then short
by design; PAR2 repair in phase 4 is what fills the hole.

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
false — nothing left to ask for — is the real terminator, and `kMaxPar2Rounds` is
the backstop. `requestedPar2` is persisted for the same reason: a queue that
forgot which volumes it had asked for would drop them from the rebuilt plan and
re-request them once per restart, forever.

`NzbFileInfo::par2RecoveryBlocks()` is what makes any of this possible — it reads
the block count out of a `.vol{start}+{count}.par2` name. Both spellings occur
(par2cmdline writes `rel.vol0+1.par2`, QuickPar and MultiPar pad to
`rel.vol000+01.par2`), so the digits are parsed and never matched.

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

`UsenetUnpacker` picks the **first** volume and hands it to `ArchiveReader`.
libarchive's RAR4 and RAR5 readers both follow the remaining volumes themselves,
but only when opened on volume one; handed any other member they report a
split-file error that reads exactly like a corrupt download. The old `.rNN`
scheme makes that worse by putting its first volume under a different extension
(`name.rar`, then `name.r00`), so a set sorted by filename starts in the middle.

Passwords come from `NzbInfo::password`. libarchive decrypts ZIP and 7z; **RAR
encryption it can only detect**, so that case is reported as unsupported rather
than left as an unexplained read error.

Two defects in `ArchiveReader` became serious once it started reading archives
written by strangers, and are fixed here rather than worked around: `extractAll()`
joined member names onto the destination unsanitised, so a `../` member escaped
it, and `extractEntry()` reopened and re-scanned the whole archive per member,
which on a solid multi-volume set re-read every volume once per file.

### What gets published

Only the payload. Archive volumes and recovery data are of no use to an ED2K
peer, and keeping them roughly doubles the disk cost of every release; the
`usenetCleanupAfterUnpack` preference turns that off for anyone who wants the
originals.

And **a release that failed verification is not published at all**. Phase 3
shared short files, holes and all, and nothing in the system complained — the
client quietly advertised corrupt data. `missingSegments > 0` with no usable
recovery set is now a failure, and the files stay in the work folder.

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

So the one global ceiling is split. `UsenetSession` recomputes once a second:

- `maxDownload() == 0` (unlimited) or Usenet idle → the split is cleared and both
  engines run against the raw ceiling.
- Both busy → Usenet takes `usenetDownloadSharePercent` (default 50), with
  give-back: whatever Usenet measurably is not using is lent to ED2K.

ED2K's slice is published as `Preferences::setEd2kDownloadBudget()` and read by
`DownloadQueue::process()` through **`maxDownloadForEd2k()`, not
`maxDownload()`** — aiming both engines at the full ceiling lands the combined
rate at roughly double the cap. The budget is runtime-only and never persisted,
so a crash cannot leave a user throttled to a stale share. `UsenetSession::stop()`
clears it.

## IPC

Requests `720–799`, pushes `910–949`.

| Opcode | |
|---|---|
| `GetNewsServers = 720` / `SetNewsServers = 721` / `TestNewsServer = 722` | provider accounts; the password never travels to the GUI |
| `GetUsenetQueue = 723` | the whole queue |
| `AddNzb = 724` | the file's **contents**, not a path — the daemon may be on another machine |
| `RemoveUsenetItem = 725` … `SetUsenetItemPriority = 728` | per-item actions |
| `PushUsenetQueueItem = 910` | one item, coalesced on its id |
| `PushUsenetItemRemoved = 911` | uncoalesced — a removal behind a later change would be dropped |
| `PushUsenetItemFinished = 912` | uncoalesced — a transition, not a latest value |

`DaemonApp::connectUsenetPushes()` owns the seam. It is not in
`CoreNotifierBridge` because that class is built on core signals, and routing a
non-core object through it would mean linking `eMule::Usenet` into a class whose
whole job is core.

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

## Build note

`src/usenet/` and `src/gui/panels/` are globbed by CMake, so new files need no
CMake edit — but MSBuild and qmake do. Run `scripts/sync_usenet_vcxproj.py` and
update `src/usenet/usenet.pro`; a new test also needs
`python3 scripts/generate_test_vcxprojs.py`.

After adding any `Q_OBJECT` header, check that
`build/src/<module>/<target>_autogen/mocs_compilation.cpp` lists a `moc_` line for
it. AUTOMOC's parse cache goes stale and re-running cmake does not fix it; delete
the `*_autogen` tree and rebuild. This has bitten this module more than once.
