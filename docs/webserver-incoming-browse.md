# Incoming folder over HTTP

Three routes on the daemon's web server let a browser list, stream and download
what is in the core's Incoming folder. They exist for one case: a GUI whose core
runs on another machine, where the **Downloads Folder** toolbar button cannot
hand the local file manager a path that belongs to the daemon's filesystem.

| Route | Answer |
|---|---|
| `GET /api/v1/incoming?token=…[&path=<rel>]` | HTML listing of the folder |
| `GET /api/v1/incoming?token=…&play=<rel>` | one-element `<video>`/`<audio>` page |
| `GET /api/v1/incoming/stream?token=…&file=<rel>` | bytes, `inline`, Range-capable |
| `GET /api/v1/incoming/download?token=…&file=<rel>` | the whole file, `attachment` |

`<rel>` is always relative to the Incoming folder; absent or empty means its root.

## Authentication

The **stream token**, not the web-UI password and not the REST API key: a
per-process random UUID (`WebServer::streamToken()`) that reaches the GUI on
every `GetStats` reply and rides in the query string. This is the same gate the
two preview routes use, and it is why all three are registered unconditionally —
a remote GUI is exactly the case where both the web UI and the REST API may be
switched off.

**Caveat that follows from that:** with both surfaces off, `DaemonApp` binds the
HTTP listener to `127.0.0.1`, so these routes are then reachable only from the
daemon's own host. `openIncomingFolder()` (`src/gui/utils/PreviewLauncher.cpp`)
checks for that before building a URL and says so in the log rather than opening
a dead browser tab.

## Path safety

Everything client-supplied goes through `WebServer::resolveIncomingPath()`:
backslashes are normalised, an absolute path or any `..` **component** is refused
before the filesystem is touched, and the result is then *canonicalised* and
required to sit under the canonical Incoming folder. Canonicalisation is what
also stops a symlink inside the folder from pointing out of it — a prefix test on
an uncanonicalised path (which is what `handleStaticFile` does) would not.
`tests/tst_WebServer.cpp::incomingRefusesToEscapeTheIncomingDir` locks all of
that in.

## Streaming vs downloading

They are separate routes because they cannot share an implementation:

* **stream** reuses `serveRange()`, the daemon's one HTTP Range implementation.
  Its body is capped at `kPreviewChunkBytes` (4 MiB) and it answers `206` even to
  a request with no `Range` header, because the body is assembled in memory.
  Players drive it with Range requests and never notice.
* **download** must hand over the *whole* file, so a 206 window would silently
  save a truncated one. It is the only route in the daemon that answers through
  `QHttpServerResponder` instead of returning a `QHttpServerResponse`, writing a
  `QFile*` so Qt streams it with bounded memory. It deliberately does **not**
  advertise `Accept-Ranges`: it always sends the whole file, and promising
  resumability it does not implement would be worse than not offering it. A
  client that wants ranges has the stream route.

The listing offers **Play** for video/audio in a container a browser plays
natively, **Stream** for the rest of video/audio (VLC takes the URL and seeks in
it), and **Download** for everything.

## Not ported from MFC

MFC's web server had a `w=getfile` download link gated on
`MaxWebUploadFileSizeMB` and on admin rights (`srchybrid/WebServer.cpp:506`).
Neither the size cap nor the admin/guest distinction exists here: these routes
are gated by the stream token alone, which makes them exactly as reachable as
the preview route that already served file bytes.
