# Indexer (newznab / torznab) module

eMuleQt searches Usenet through `eMule::Indexer`, a static library that is a peer
of `eMule::Core`, `eMule::Ipc` and `eMule::Usenet` rather than part of any of
them. `emulecored` links it; the GUI stays daemon-agnostic and talks IPC.

**It belongs to no network on purpose.** newznab (Usenet) and torznab
(BitTorrent) are the same HTTP API with a different XML attribute namespace, and
Prowlarr and NZBHydra2 serve both from one endpoint. Writing two clients would
mean writing the same `QXmlStreamReader` twice. The decision and its reasoning
are in `docs/BitTorrentModule-Research.local.md` §7.3, which amends
`docs/UsenetModule-Research.local.md` §4 (that document places `search/` inside
`src/usenet/`; it does not go there).

Dependency direction: `indexer → core` is fine. `indexer → usenet` must never
happen, or a future BitTorrent module could not reuse this. The daemon is the
glue — it owns the search list and routes a grab into whichever engine the
*result* belongs to.

**Status: implemented and in use for Usenet search.** The torznab half of the
reader is written and tested but nothing consumes it yet; there is no BitTorrent
engine to hand a `.torrent` to.

## Layout

```
src/indexer/
    IndexerConfig.h             alias of the record in core/prefs/IndexerConfig.h
    IndexerCaps.{h,cpp}         t=caps: limits, supportedParams, category tree
    IndexerCapsStore.{h,cpp}    the caps cache, one YAML sidecar per indexer
    IndexerQuery.{h,cpp}        URL construction, capability gating, key redaction
    IndexerResult.{h,cpp}       one result row, and the RSS reader
    IndexerClient.{h,cpp}       one QNetworkAccessManager: caps / search / fetch
    IndexerSearch.{h,cpp}       one user search fanned out across N indexers
    IndexerSearchList.{h,cpp}   the daemon's session: accounts, caps, searches
```

GUI side (linking `eMule::Core` + `eMule::Ipc` only, never `eMule::Indexer`):
`src/gui/controls/IndexerResultsModel.{h,cpp}`, plus the Usenet method in
`src/gui/panels/SearchPanel.{h,cpp}` and the Options page in
`src/gui/dialogs/OptionsDialog.cpp`.

The stored record lives in `src/core/prefs/IndexerConfig.h`, not here:
`Preferences` owns the file format and the at-rest encryption of the API key, and
core may not depend on `eMule::Indexer`. Same call, and the same one-line
aliasing header, that `NewsServer` already gets.

## HTTP

`IndexerClient` holds its own `QNetworkAccessManager` rather than going through
`core/net/HttpFileDownload`, for two reasons that matter only here:

- `HttpFileDownload::finishReply()` **discards the body on any HTTP error**, and
  a newznab error *is* the body. Losing it turns "Incorrect user credentials"
  into "server replied: Unauthorized".
- It mints a fresh `QNetworkAccessManager` per call, so a paged search across
  several indexers could not reuse a connection.

It does reuse `eMule::Http::makeRequest()` for the User-Agent and the
`NoLessSafeRedirectPolicy` — an https indexer redirected down to http would put
the API key on the wire in clear.

The one-shot `.nzb` fetch **does** go through `HttpFileDownload`: it already caps
the response size and gunzips a compressed payload.

## Six things that bite

1. **The API key is a query parameter.** Every log line, error string and status
   message that carries a URL carries the key with it. `redactApiKey()` exists
   for that, has both a `QUrl` and a plain-text overload — Qt's own network
   error strings embed the request URL — and is covered by a test, because there
   is no second layer catching a miss.
2. **An error arrives as HTTP 200 with an `<error code= description=/>` body.**
   `parseIndexerError()` runs *before* anything is treated as data. A client
   that only checks `QNetworkReply::error()` reports success and parses zero
   rows, and the user is told nothing.
3. **Attributes are bound by local name, not by prefix.** A prefix is a
   document-local label and Prowlarr, Jackett and NZBHydra2 do not agree on it.
   Feeds also exist that emit `<attr>` with no declaration at all. Since the two
   vocabularies share no field name, dispatching on the `name=` attribute cannot
   confuse them.
4. **`<enclosure length>` and the `size` attr disagree** on several indexers —
   the enclosure is sometimes the size of the `.nzb` itself. The attr wins; the
   enclosure is the fallback.
5. **Caps booleans are words**: `available="yes"`, not `"true"`. A naive
   `toBool()` greys out every field the indexer actually supports.
6. **`pubDate` day names are wrong in the wild.** Qt's `Qt::RFC2822Date` parser
   validates the day-of-week against the date and rejects the whole timestamp
   when they disagree. `parseFeedDate()` retries without the day name, then as
   ISO 8601. Getting this wrong blanks the Age column, which is the single most
   useful thing about a Usenet result.

## Paging and quota

`newznab:response @total` gives the result count. A search fetches the first page
and auto-pages only up to `indexers.maxPages` (default 3), never past the
indexer's advertised `limits/@max`.

This is a **spending limit, not a speed setting**: every page is an API call
against an allowance the user pays for. The same reasoning is why
`SearchType::UsenetIndexer` is not reachable from `Automatic` —
`resolveAutomaticSearchType()` picks between networks that cost nothing to ask,
and an indexer search also discloses the query to a third party. Both have to be
an explicit choice.

Asking for more rows than `limits/@max` is not an error: the indexer silently
truncates, and the offsets we page with then stop lining up.

## Configuration

A top-level `indexers:` block in `preferences.yml` — **not** a key under
`usenet:`, because a BitTorrent indexer configured inside a Usenet block would be
nonsense and moving it later would mean a migration.

```yaml
indexers:
  resultLimit: 100
  maxPages: 3
  timeoutSeconds: 30
  capsRefreshDays: 7
  accounts:
    - name: My Indexer
      url: https://api.example.org/
      kind: newznab       # newznab | torznab | both
      enabled: true
      apiKeyEnc: <base64 of IV||ciphertext>
```

`url` is stored **exactly as typed**. There is no single correct normalisation:
a bare host needs `/api` appended, and a Jackett endpoint
(`…/results/torznab/api`) must not have it. `IndexerConfig::apiUrl()` completes
the path only when it is empty or a bare slash, at request time — rewriting on
save would take away the user's ability to correct the field.

`name` is the identity: it keys the caps sidecar on disk and the
`GetIndexerCaps` request, so duplicates are collapsed.

API keys ride on the same file-wide AES key as the news-server passwords, under
the same four load-bearing rules (`docs/usenet-module.md` § IPC lists them; they
are enforced by `tst_IndexerPrefs`). As there, this is **obfuscation, not
secrecy** — the key sits in the same file as the ciphertext.

Capabilities are cached, not configured, so they are not in `preferences.yml`: a
caps document carries a category tree of a hundred nodes and that file is one
people edit by hand. They go to `Config/Indexers/<slug>.caps.yml`, re-probed on
**Test**, on a stale cache, and never as a precondition for searching — an
unprobed indexer is queried ungated, which every newznab implementation accepts.

## IPC

Indexers own request block **700–719** and push block **900–909**, reserved when
the Usenet blocks were allocated.

| Opcode | Shape |
|---|---|
| `GetIndexers = 700` | `[]` → one map per account |
| `SetIndexers = 701` | `[[{…}]]` → `[ok, error]`; replaces the list |
| `TestIndexer = 702` | `[{…}]` → `[ok, response, error]` |
| `GetIndexerCaps = 703` | `[name]` → limits, modes, categories |
| `StartIndexerSearch = 704` | `[query, cats, mode, indexers]` → `[ok, searchId]` |
| `StopIndexerSearch = 705` | `[searchId]` → `[ok]` |
| `RemoveIndexerSearch = 706` | `[searchId]` → `[ok]` |
| `GrabIndexerResult = 707` | `[searchId, resultId]` → `[ok, itemId]` |
| `PushIndexerResults = 900` | `[searchId, rows]` — as each indexer answers |
| `PushIndexerProgress = 901` | `[searchId, done, total]` |
| `PushIndexerSearchDone = 902` | `[searchId, error]` |

**The daemon never sends an API key to the GUI.** `GetIndexers` reports only
`hasApiKey`, and an entry arriving at `SetIndexers` *without* an `apiKey` field
keeps the stored one — the same contract `GetNewsServers` established for
provider passwords. It matters more here, because the key rides in the query
string of every request.

That is also why **grabbing runs daemon-side**: the row the GUI holds has no
download URL at all. It names a result by id, and the daemon fetches the `.nzb`
with its own key and hands it to `UsenetQueue::addNzb`.

`PushIndexerResults` is **not** coalesced. Every push carries a different batch —
this is an append, not a latest value — so a coalescing window would not merge
them, it would throw all but the last away.

`SetIndexers` emits `indexerConfigChanged`, forwarded through `IpcServer` to
`DaemonApp::applyIndexerConfig()`, the same route `usenetConfigChanged` takes.

## GUI

**One search panel, not two.** `SearchType` was already a network-selection enum
with an Automatic resolver, so it gained `UsenetIndexer = 5` and the Method
dropdown gained an entry beside Automatic / Kad / Ed2k. A future
`TorrentIndexer` slots in the same way.

A tab holds **one** model, never both: `SearchResultsModel` for ED2K/Kad,
`IndexerResultsModel` for an indexer search. An ED2K row is an MD4 hash with
source counts and media tags; an indexer row is a title, an age, a category and a
grab count. A union model would show half its columns empty whichever kind it
held. The view already swapped models per tab, which is what makes two row shapes
possible in one panel — the header binds to its own `uistate.yml` key per kind,
or one set of saved widths would be applied to the other's columns.

Results **append** rather than reset, because they arrive per indexer and a reset
on each batch would destroy the user's selection every time another one replied.

Two ids, one number space: an ED2K search and an indexer search can both be #1,
so every handler matches the *kind* as well. `tabForIndexerSearch()` exists for
exactly that, and `drainDirtySearches()` skips indexer tabs — without it an ED2K
push would refresh an indexer tab through a model it does not have.

Options → **Indexers** (`emuleqt --options indexers`): account list, details form
and a **Test** button, plus the four search settings. The API-key field is empty
even when a key is stored — the daemon never sent one — so it shows a
"(a key is stored — leave empty to keep it)" placeholder. Without that, an empty
box reads as "no key configured" and the user retypes one they already have.

## Tests

| Target | Covers |
|---|---|
| `tst_IndexerParse` | Recorded caps and search XML, newznab and torznab; the HTTP-200 error document; size attr vs enclosure length; RFC 822 and ISO dates; attributes under a non-standard prefix |
| `tst_IndexerQuery` | URL normalisation for bare hosts and Jackett paths, capability gating, paging, and that no formatted string leaks the API key |
| `tst_IndexerSearch` | Fan-out over an in-process fake HTTP server: two indexers, dedup, one failing, the page cap, cancel |
| `tst_IndexerPrefs` | `apiKeyEnc` round trip, the plaintext escape hatch, the mint condition, block ordering, decrypt failure |
| `tst_IndexerLiveSearch` | `live`-labelled. Needs `EMULE_INDEXER_APIKEY`, `EMULE_INDEXER_URL` and `EMULE_INDEXER_QUERY`, read from the gitignored `.env` or the process environment. **No defaults** — it skips rather than pick an indexer for you, and neither the key nor the URL is checked in. |

`tst_IndexerParse` and `tst_IndexerSearch` keep their raw-string fixtures *below*
the `Q_OBJECT` class. moc's preprocessor does not understand raw strings, and one
above the class makes it emit a zero-byte `.moc` — which surfaces as a "missing
vtable" link error pointing nowhere near the cause.

## Build

CMake globs `src/indexer/*`. MSBuild and qmake do not:
`scripts/sync_module_vcxproj.py` regenerates the source lists in
`emuleindexer.vcxproj(.filters)` — it covers `src/usenet` too — and
`src/indexer/indexer.pro` is maintained by hand.

A `Q_OBJECT` header must land in `<QtMoc>`, not `<ClInclude>`, or moc never runs
for it and the vtable goes missing on Windows only.
