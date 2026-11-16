# Client Analyzer

Port of WiZaRd's ClientAnalyzer (eMuleFuture 1.1, `AntiLeech/ClientAnalyzer.cpp` v1.5).
Option: *Options › Security › Anti-Leecher › Score clients by their behaviour (Client Analyzer)*,
pref `clientAnalyzer`, **off** by default.

Code: `src/core/client/ClientAnalyzer.{h,cpp}` (record, score, file), hooks in `UpDownClient`,
`UploadClient`, `DownloadClient`, `UploadQueue`, `PartFile::punishCorruptionSenders`.
Test: `tests/tst_ClientAnalyzer.cpp`.

## Score

Per user hash, starts at 1.0, clamped to [0.01, 10]. Multiplies the queue score **together with**
the credit ratio (eMuleFuture replaced the credits; we don't). Also in
`getCombinedFilePrioAndCredit()` (soft queue limit).

| behaviour | change |
|---|---|
| chat spam (DLP spam rule, first message) | −0.025 each |
| source requests turned down as too soon, > 3 | −0.025 × count |
| our source requests > 10 and > 3 × its answers (files ≥ 25 sources) | −0.5 |
| avg reask + 5 min < reask time of its software, > 2 reasks (TCP + UDP) | −0.025 × reasks |
| DLP verdict (nick/mod thief, fake mod string, …) | −0.3 |
| file faker / UDP FNF faker | −0.5 |
| failed download sessions in a row > 2 | −0.1 × count |
| only while it has something for us (we download from it): chunks got | +0.1 each |
| — " —: chunks sent | −0.1 each |

Weights: full-file downloads 1.5×, rare-file (< 25 sources) downloads +1×, part-file uploads 1.5×;
80 % of a chunk counts as one. The +0.025 per week of a kept hash is **dropped on purpose**
(comment in `AnalyzerRecord::score`).

## Flags

- `kFlagDlp`: set by any DLP verdict, also while the DLP ban switch is off (then: no ban, no ⅓ cut).
  DLP checks run when either switch is on. Replaces eMuleFuture's own nick tags and mod lists.
- `kFlagFileFaker`: complete source asking us for the same file, or a complete part status in its request.
- `kFlagUdpFnfFaker`: UDP "file not found" for a file it requests from us, after reporting parts of it.
- A flag can't be cleared for 2 h after the last hit.

## Other actions

- Failed upload sessions in a row > 3: a slot only while it is connected.
- Corrupt data sender (AICH ban, proven identity): kept 60 days, banned on every hello.
- eMuleFuture's ban after 6 failed downloads: left to `FruitlessSessionLedger` (pause/quarantine).

## Identity

Records change only for peers that passed secure ident or can't do it. Others work on a session
copy, merged once identified (eMuleFuture `CTempCAList`). Fresh records seed from credits.

## File

`clientanalyzer.met` in the config dir, separate from `clients.met`. Version byte 0x01, record count,
per record hash + tag list (ids in `ClientAnalyzer.cpp`), then corrupt senders (hash + time).
Saved every 18 min and at exit via `BackgroundFileWriter`; loaded with `clients.met`, also while off.
Dropped: unseen 150 days, or nothing worth keeping (no flag, no bad counter, < 1 chunk either way).

Client Details › Scores shows the factor and reasons while on. Log lines `[CA] …` go to the Leecher tab (when Log banned clients is on): flag set/cleared, fast reask, XS exploiter reached/cleared, XS spammer reached, chat spam (analyzer only; a DLP ban logs its own), empty upload sessions limit, corrupt sender remembered, corrupt sender banned.
