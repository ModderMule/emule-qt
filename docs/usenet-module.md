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
a seek anywhere in it. Keyword search is
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
| `tst_UsenetUnpack` | volume-set detection across all three naming schemes; path-traversal and reserved-name refusals; a multi-volume RAR set extracted through the whole list, and a set skipped because it was unpacked during the download |
| `tst_UsenetPostPipeline` | a repair discarding what was unpacked while downloading, and corrupt volumes never reaching the published release; phase 4 end to end: a healthy release fetches **no** recovery volumes; a damaged one fetches them, repairs, unpacks and publishes the payload alone; an unrepairable one publishes nothing |
| `tst_UsenetStream` | phase 6a: part-number dispatch order over a shuffled NZB, a failed article retried *before* later ones, interval merge and the hole that stops it, `written` surviving a restart, the previewable predicate. Phase 6b: a stored RAR set resolving to the file inside it and reading back byte-identically across volume boundaries; **a seek to 80% that never issues a `BODY` for the volumes it skipped** — the exit criterion, asserted; a compressed set saying why; a `.001` split set. Multi-file sets: every inner file enumerated with its ordinal, each mapping to its own bytes across a volume boundary, **a set whose first file is an `.nfo` streaming the movie with no `entry=` named**, and a listing on a paused item that neither fetches nor guesses. Preview from the extraction: a **solid stored** set — refused by the map, extractable by libarchive, which is the only fixture that separates the two sources — streamed out of `_unpacked/` with the archive's own declared size as the total, listed and marked playable, and a control case proving the same set is refused when nothing is extracting it |
| `tst_RarReader` | phase 6b: RAR4 and RAR5 stored volumes — name, method, packed/unpacked sizes, data offset, split flags, `LHD_LARGE` sizes past 4 GB, RAR5 multi-byte vints; compressed read but not stored; solid and encrypted refused with a reason; every truncated prefix asking for more bytes rather than reading past the buffer; a volume carrying two file headers listing both; resuming mid-volume at a computed offset, and a resumed block at a wrong address caught by its header CRC |
| `tst_UsenetDirectUnpack` | extraction that keeps pace with the download: volumes offered one at a time, a run blocking on one that has not landed and resuming when it does, cancel unwinding without leaving half a file, a set that ends short failing rather than hanging, and end to end — the payload complete before post-processing starts, and the same release unchanged with the option off. A blocked run reporting bytes that read back as the payload's prefix and naming the volume it needs; **a set whose NZB scrambles its volume order still unpacked while downloading**, which is what the sealed-volume replay exists for |
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

`UsenetUnpacker` hands `ArchiveReader` the **whole volume list**, in volume
order. libarchive reads a multi-volume set as one continuous byte stream over
the files the client supplies and never opens a sibling volume by name, so a set
opened on volume one alone stops at the end of volume one — silently, with a
split-file error that reads exactly like a corrupt download. `tst_ArchiveReader`
asserts both halves of that. Volume order is what `volumePositionOf()` computes,
and it matters: the old `.rNN` scheme puts its first volume under a different
extension (`name.rar`, then `name.r00`), so a set sorted by filename starts in
the middle.

Passwords come from `NzbInfo::password`. libarchive decrypts ZIP and 7z; **RAR
encryption it can only detect**, so that case is reported as unsupported rather
than left as an unexplained read error.

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

### Why a download is slow

Measured 2026-09-05 against a real provider at 216 ms command RTT. Three things
cap a Usenet download, and only one of them is in this module's code:

1. **The provider caps a single connection.** ~0.2 MB/s here, and no client-side
   tuning moves it: a raw non-Qt TLS socket gets the same figure, and a 4 MB
   `SO_RCVBUF` set before connect changes nothing (0.228 → 0.242 MB/s, inside
   noise). It is not a window or bandwidth-delay problem, so there is nothing to
   optimise in `NntpSocket` for it.

2. **Throughput is therefore the connection count**, and `maxConnections` is
   usually set far below what the account allows:

   | connections | MB/s |
   |---|---|
   | 8 | 1.2 |
   | 40 | 4.4-4.9 |
   | 60 | 5.8-6.3 |

   Sublinear but still climbing at 60, with no `502 Too many connections`
   during the run. A client reporting several times our rate on the same post is
   using more connections, not a better protocol implementation. Never set the
   figure above what the plan sells: the answer is a 502 and a backed-off server,
   and providers suspend repeat offenders.

   `kDefaultMaxConnections` (`src/core/prefs/NewsServer.h`) is therefore **40**,
   not the 8 this module shipped with — 8 is below what any plan sold today
   allows and cost a new install three quarters of its rate for nothing.

3. **The bandwidth split above, which the tests never see.** They construct a
   `UsenetQueue` directly and never call `setRateLimit`, so their numbers are the
   unthrottled ceiling. In the daemon, `maxDownload: 3500` KB/s with a 50% share
   caps Usenet at 1.71 MB/s no matter how many connections are open — a rate
   measured in a test does not transfer to the running application.

The read budget itself is a token bucket with a one-second burst
(`kBurstTicks`), and it has to be: a socket on a high-latency link spends two
whole refill ticks per article waiting for the server's first byte, and a budget
that is *assigned* each tick rather than accumulated forfeits that credit and can
never average the rate it was given. `tst_NntpSocket::readRateLimit_repaysTimeSpentWaiting`
pins it — the same transfer takes ~950 ms without the carry and ~250 ms with it.

## IPC

Requests `720–799`, pushes `910–949`.

| Opcode | |
|---|---|
| `GetNewsServers = 720` / `SetNewsServers = 721` / `TestNewsServer = 722` | provider accounts; the password never travels to the GUI |
| `GetUsenetQueue = 723` | the whole queue |
| `AddNzb = 724` | the file's **contents**, not a path — the daemon may be on another machine |
| `RemoveUsenetItem = 725` … `SetUsenetItemPriority = 728` | per-item actions |
| `ListUsenetArchiveEntries = 729` | the files inside an archive set; **this one fetches** |
| `PushUsenetQueueItem = 910` | one item, coalesced on its id |
| `PushUsenetItemRemoved = 911` | uncoalesced — a removal behind a later change would be dropped |
| `PushUsenetItemFinished = 912` | uncoalesced — a transition, not a latest value |

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

**What is still refused** says why: a password-protected set, one whose
extraction failed, an item with direct unpack switched off, and the third
concurrent set (`kMaxDirectUnpacks`). 406 from the route, `previewNote` over IPC,
a tooltip on the disabled menu entry. §7.3 asked for exactly that: the answer
surfaced rather than discovered at play time. A fully obfuscated RAR4 set cannot
be volume-ordered before download either, RAR4 having no volume-number field
where RAR5 has one, and degrades to not-seekable rather than mis-mapping.

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
CMake edit — but MSBuild and qmake do. Run `scripts/sync_module_vcxproj.py` and
update `src/usenet/usenet.pro`; a new test also needs
`python3 scripts/generate_test_vcxprojs.py`.

After adding any `Q_OBJECT` header, check that
`build/src/<module>/<target>_autogen/mocs_compilation.cpp` lists a `moc_` line for
it. AUTOMOC's parse cache goes stale and re-running cmake does not fix it; delete
the `*_autogen` tree and rebuild. This has bitten this module more than once.
