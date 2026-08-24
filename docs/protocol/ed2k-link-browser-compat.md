# eD2K links and the URL Standard

*Why clicking an eD2K link in a browser stopped working, why no amount of work on our side can
fix it, and what eMuleQt does instead.*

This is a property of the link grammar, not of the GUI. `ed2k://|file|…|/` was designed in 2002,
years before there was a normative URL parser to satisfy, and it no longer satisfies the one every
browser now ships. Nothing in eMuleQt caused this and nothing in eMuleQt can undo it — but the
shape of the client's link plumbing is a direct consequence of it, so it is worth writing down
once rather than rediscovering it every time somebody asks why the clipboard watcher exists.

---

## 1. The symptom

Clicking an eD2K link in Chrome, Edge, Brave or any other Chromium browser navigates to
`about:blank#blocked`. The external-protocol prompt ("Open eMule Qt?") never appears, whether or
not the client is installed and registered.

Firefox behaved correctly until **Firefox 122** (January 2024) and now does the same thing.

Safari/WebKit is reported to be the outlier and to still dispatch — **not verified here**; the
macOS route was tested by sending the Apple Event directly (§7), which is the layer below the
browser. Either way it is one implementation decision away from joining the others; nothing in the
standard obliges WebKit to keep dispatching, and `[NSURL URLWithString:]` has itself refused these
strings since macOS 14.

## 2. Why: `|` is a forbidden host code point

`ed2k` is not one of the URL Standard's *special schemes* (`http`, `https`, `ws`, `wss`, `ftp`,
`file`). That changes how the path is parsed but **not** whether an authority is parsed: the `//`
after the colon is what triggers authority parsing, and it does so for any scheme. So in

```
ed2k://|file|Name.rar|4960062|95818F7E4E4E0C1D1E2A3B4C5D6E7F80|/
       ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
       everything up to the first '/' is read as the host
```

everything between `ed2k://` and the first `/` is handed to the **opaque-host parser**, whose
second step is:

> If input contains a forbidden host code point, host-invalid-code-point validation error,
> return failure.

and the *forbidden host code points* are:

```
U+0000 NULL   U+0009 TAB   U+000A LF   U+000D CR   U+0020 SPACE
#   /   :   <   >   ?   @   [   \   ]   ^   |
```

`|` is in that set, so host parsing returns failure, so URL parsing returns failure. Under the
standard as written today, **`ed2k://|file|…` is not a URL at all.** It is not a URL that browsers
choose to block; it is a string that does not parse.

## 3. Why the standard forbids it — a Windows drive-letter bug

This is the part worth knowing, because the intuitive explanation ("browsers are hostile to P2P")
is wrong and leads people to look for a workaround where there isn't one.

`|` was added to the forbidden host code points by whatwg/url commit
[`4025253`](https://github.com/whatwg/url/commit/40252530f93fe37f092be90583f82e9f337da1ab),
*"Forbid U+007C (|) in hosts"*, on **2021-03-22**. It closes
[whatwg/url#559](https://github.com/whatwg/url/issues/559), which is not about eD2K, or P2P, or
anything adjacent. It is about `file:` URLs and the old DOS drive-letter spelling `C|` for `C:`:

| input | first parse | second parse |
|---|---|---|
| `file://C%7C` | `file://c\|/` | `file:///c:/` |

Parsing the *output* of a parse gave a different URL from the input — the drive-letter rule fired
on the second pass and turned a host into a path. A URL parser that is not idempotent is a
security bug, not just an inelegance: it means one component can check a URL with one API while
another component acts on it with a different API and they disagree about what it names. That is
the raw material of hostname-spoofing and request-smuggling bugs.

Of the three fixes proposed in the issue — percent-encode `|` after host parsing, fail on it, or
special-case the drive letter — the standard took the second, because it kills the whole class
rather than one instance. `^` was added at the same time for the same reason.

eD2K links are collateral damage of a `file://` normalization bug. There is no argument to make
to anybody, and no exception to request: unforbidding `|` would reopen #559.

**Timeline**

| date | event |
|---|---|
| 2020-10-26 | [whatwg/url#559](https://github.com/whatwg/url/issues/559) filed — `file://C%7C` reparse bug |
| 2021-03-22 | `\|` becomes a forbidden host code point ([commit `4025253`](https://github.com/whatwg/url/commit/40252530f93fe37f092be90583f82e9f337da1ab), tests in [wpt#28118](https://github.com/web-platform-tests/wpt/pull/28118)) |
| ~2021 | Chromium's URL parser follows; eD2K links start landing on `about:blank#blocked` |
| 2024-01 | Firefox 122 ships the Rust `DefaultURI` parser for unknown schemes ([Bug 1603699](https://bugzilla.mozilla.org/show_bug.cgi?id=1603699)) and inherits the same behaviour ([Bug 1876491](https://bugzilla.mozilla.org/show_bug.cgi?id=1876491)) |
| 2024-01-29 | Mozilla files [whatwg/url#815](https://github.com/whatwg/url/issues/815), *"Web compatibility issue with various unknown (external) protocols like ed2k"*, noting that even Wikipedia's own eD2K page carries unparseable examples. **Still open.** |

Firefox users can set `network.url.useDefaultURI` to `false` in `about:config` to restore the old
parser. That is a temporary escape hatch on a pref that exists to be removed, not a fix, and there
is no Chromium equivalent.

## 4. The failure is unreachable from our side

This is the operationally important part, and it is easy to get wrong.

The parse failure happens **inside the browser, at URL-parse time, before the external-protocol
handler is ever consulted.** The browser never asks the operating system to open anything, so
nothing on the receiving end participates:

- macOS `CFBundleURLTypes` / `LSSetDefaultHandlerForURLScheme` (`src/gui/app/Ed2kSchemeHandler.cpp`)
- the Windows `HKCU\Software\Classes\ed2k` `URL Protocol` key (same file)
- the Linux `x-scheme-handler/ed2k` desktop entry (same file)
- our `QFileOpenEvent` handler (`src/gui/app/ExternalLinkHandler.cpp`)

Registering a better handler for a call that is never placed changes nothing. **Fixing the
receiving side does not fix Chrome, and any change described as doing so is mistaken.**

## 5. What would parse — and why we are not doing it

`|` is *not* in the path percent-encode set (that set is the query set plus `?`, `^`, `` ` ``, `{`
and `}`), so a pipe survives literally in a path. Three reshapes therefore parse and round-trip
byte for byte:

| reshape | why it parses |
|---|---|
| `ed2k:\|file\|Name.rar\|4960062\|HASH\|/` | opaque path, no `//`, so no authority and no host to reject |
| `ed2k://x/\|file\|Name.rar\|4960062\|HASH\|/` | dummy host; the link lives in the path |
| `ed2k://file/Name.rar/4960062/HASH` | fully conforming — a different format entirely |

Each one is a fork of the eD2K link grammar. Every other client, every index site, and every
`server.met` description in existence emits `ed2k://|`; a client that only accepted the reshaped
form would be alone with it, and one that accepted both would still not make the links already
published on the web clickable. It would also need changes at every entry point listed in §6.

The cost is a spec fork and a compatibility break. The benefit is a route into a handler we can
already reach by other means. It is not worth it.

## 6. What eMuleQt actually does

Every route below hands **plain `QString` link text** to `Ed2kLinkImporter::importLinks()`, which
is the only place that turns link text into downloads or into an HTTP Cache configuration. None of
them puts the link through a URL parser.

| route | entry point |
|---|---|
| **clipboard watcher** | `MainWindow::onClipboardChanged()` |
| macOS Apple Event (`QEvent::FileOpen`) | `ExternalLinkHandler::eventFilter()` |
| command line (`emuleqt ed2k://…`) | `CommandLineExec::handleEd2kLinks()` |
| daemon command line | `emulecored --add-link` |
| a click in Server Info / chat / IRC | `TextLinks.h` → `ExternalLinkHandler::open()` |
| `Tools → Paste eD2K Links…`, context menus | `PasteLinksDialog`, `Ed2kLinkImporter::linkKindsIn()` |

**The clipboard is the answer to §1**, and it is the right answer precisely because it is the one
route with no URL parser anywhere in it: the browser copies a string, eMuleQt reads a string. A
user whose browser refuses to dispatch copies the link instead of clicking it, and the watcher
picks it up — on `QClipboard::dataChanged`, on the application becoming active, and on the IPC
connection coming up, so a link already on the clipboard at startup is not missed. This is what
eMuleQt ships, and §2–§4 are the argument for it.

The OS-dispatch routes are still worth having. They serve Safari, `Ed2kSchemeHandler`-registered
launches, scripted senders, and any future client of the scheme that hands over the raw string.
They are simply not a Chrome fix.

## 7. The Qt trap on the receiving end

Documented here because it is the same root cause seen from the other side, and because it cost us
a handler that looked correct and had never once worked.

On macOS a link click arrives as a `kAEGetURL` Apple Event carrying a plain string. Qt's Cocoa
plugin (`qcocoaapplicationdelegate.mm`, `getUrl:withReplyEvent:`) does:

```objc
const QString qurlString = QString::fromNSString(urlString);
if (const QUrl url(qurlString); url.isValid())
    QWindowSystemInterface::handleFileOpenEvent(url);
else
    QWindowSystemInterface::handleFileOpenEvent(qurlString);   // ← every eD2K link
```

`QUrl` rejects `ed2k://|file|…` for the same reason browsers do — `|` in the host — so Qt takes
the fallback. `handleFileOpenEvent(QString)` builds `QUrl::fromLocalFile(link)`, i.e. scheme
`file` with the entire link as its *path*, and `QFileOpenEvent(const QUrl&)` sets
`m_file = url.toLocalFile()`. The result:

| accessor | value |
|---|---|
| `QFileOpenEvent::file()` | `ed2k://\|file\|Name.rar\|4960062\|95818F…\|/` — the raw link, byte for byte |
| `QFileOpenEvent::url().toString()` | a `file:` URL with the link percent-encoded into its path |

So the payload does arrive; it is in the accessor whose name suggests it should not be. Reading
`url().toString()` and testing it with `startsWith("ed2k://")` — which is what eMuleQt did until
this was found — is never true for any eD2K link, so the macOS route silently did nothing for file
links, server links and `ed2k://|httpcache|` links alike. It went unnoticed because the routes
that were actually exercised (clipboard, `argv`, pane clicks) pass raw `QString`s.

`Ed2kLinkImporter::linkFromFileOpenEvent()` reads `file()` first and `url()` second, and
`tests/tst_Ed2kLinkImporter.cpp` pins the round trip by building the event exactly the way Qt does.

**Version floor: Qt 6.6.** Before that the plugin passed the unparseable string through as an
invalid `QUrl`, which stringifies to the empty string, and the link was destroyed inside Qt with
no accessor left holding it. On Qt ≤ 6.5 the macOS Apple Event route cannot be made to work at
all. eMuleQt builds against Qt 6.10.

### Verifying it by hand

`/usr/bin/open` is **not** a usable test harness on current macOS. It parses the argument with
NSURL before dispatching, and NSURL has been strict since macOS 14:

```console
$ open 'ed2k://|file|Name.rar|4960062|95818F…|/'
Unable to interpret 'ed2k://|file|Name.rar|4960062|95818F…|/' as a path or URL
```

That is the `open` tool refusing, not eMuleQt. To exercise the real path, send the Apple Event
directly — this bypasses every URL parser and delivers the raw string, which is exactly what a
dispatching browser does:

```console
$ osascript -e 'tell application id "org.emule.emuleqt" to «event GURLGURL» "ed2k://|file|Probe.rar|4960062|95818F7E4E4E0C1D1E2A3B4C5D6E7F80|/"'
```

With the GUI running this raises the window and shows the download confirmation. With the GUI
*not* running it launches it, and the link is held by `ExternalLinkHandler` until the IPC
connection to the daemon comes up — the log line to look for in `emuleqt.log` is
`Link received before the daemon was ready — queued (1)`, followed a second or so later by
`Connected to daemon via IPC.` and the confirmation dialog. Both were verified on
macOS 26.5.2 / Qt 6.10.2.

---

## References

- [URL Standard](https://url.spec.whatwg.org/) — forbidden host code points, opaque-host parser, path percent-encode set
- [whatwg/url#559](https://github.com/whatwg/url/issues/559) — the `file://C%7C` reparse bug that forbade `|`
- [whatwg/url commit `4025253`](https://github.com/whatwg/url/commit/40252530f93fe37f092be90583f82e9f337da1ab) — "Forbid U+007C (|) in hosts", 2021-03-22
- [whatwg/url#815](https://github.com/whatwg/url/issues/815) — "Web compatibility issue with various unknown (external) protocols like ed2k", open since 2024-01-29
- [Mozilla Bug 1603699](https://bugzilla.mozilla.org/show_bug.cgi?id=1603699) — DefaultURI for unknown schemes, shipped in Firefox 122
- [Mozilla Bug 1876491](https://bugzilla.mozilla.org/show_bug.cgi?id=1876491) — the eD2K regression report against it
- [Chromium issue 40293604](https://issues.chromium.org/issues/40293604) — `ed2k:`, `apt:`, `mailto:` and `|`
- `docs/protocol/http-cache-spec.md` §8.1 — the `ed2k://|httpcache|` link, which inherits this grammar and this fate
