# Usenet (NNTP) module

eMuleQt downloads from Usenet through `eMule::Usenet`, a static library that is a
peer of `eMule::Core` and `eMule::Ipc` rather than a part of either. The
dependency runs one way only: `usenet → core` is fine, `core → usenet` never.
`emulecored` links it; the GUI stays daemon-agnostic and talks IPC.

Design research and the phase plan are in `docs/UsenetModule-Research.local.md`,
amended by `docs/BitTorrentModule-Research.local.md` §7.

**Status: phases 0–2 are implemented.** The module connects, authenticates,
parses an NZB, and downloads articles into a file byte-identically. The queue,
the connection pool's scheduler, PAR2 repair, unpacking and keyword search are
phases 3–5 and not built yet.

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
    queue/ArticleWriter.{h,cpp} sparse writes at absolute offsets
```

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
refill timer. `0` means unlimited, as everywhere else in eMuleQt. Nothing drives
it yet; phase 3 feeds it a share of the one global `maxDownload` budget.

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
