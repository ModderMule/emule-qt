# libtorrent upstream reports: I2P SAM

Five reports for https://github.com/arvidn/libtorrent/issues. Each `## Issue N` section is one GitHub issue: title in **Title**, the rest is the body.
Issue 1 found on Windows 11 (vcpkg libtorrent 2.1.2, MSVC Debug, i2pd 0.9.71 SAM). Issues 2 to 5 found and reproduced on macOS (FetchContent v2.1.2, i2pd SAM).

| Issue | Upstream state | eMuleQt patch (`src/vcpkg-ports/libtorrent/`) |
|---|---|---|
| 1 `i2p_stream` buffer use-after-free | fixed on RC_2_1 by 51000e88, not released; RC_2_0 still has it | `fix-i2p-stream-buffer-uaf.patch` (same change as 51000e88). Drop once a release has it |
| 2 reconnect backoff stays at 1 s | not filed (checked 2026-11-04) | `fix-i2p-reconnect-backoff.patch` |
| 3 `outgoing_interfaces` drops all I2P peers | not filed | `fix-i2p-outgoing-bind.patch` |
| 4 incoming I2P dead after an accept error | not filed | `fix-i2p-accept-after-router-restart.patch` |
| 5 dead SAM session not detected | not filed | `fix-i2p-accept-after-router-restart.patch` (same patch as 4) |

Before filing 2 to 5, check RC_2_1 HEAD and the open issues. They were only checked against v2.1.2 (6da363d2).

Local state:
- the overlay port applies all four patches in order: uaf, outgoing-bind, accept-after-router-restart, reconnect-backoff. The FetchContent v2.1.2 build (Linux/macOS) applies the same files via `cmake/ApplyPatch.cmake`. A Homebrew or distro libtorrent found by `find_package` is not patched
- issue 1 verified 2026-11-04 with the patch, Debug: two cores, each with its own bundled i2pd, 4 MB torrent. Done in 6m50s over public I2P trackers, sha256 matches, no assert and no freeze
- issues 2 to 5 verified 2026-11-12 with a standalone session probe and a scratch i2pd (see each issue)

---

## Issue 1: i2p_stream::read_line frees m_buffer under a pending async_write (heap use-after-free)

**Title:** `i2p_stream: m_buffer freed after the handler, while the handler's SAM command is still being written (2.0.14+, 2.1.x)`

### Affected
- releases 2.0.14, 2.0.15, 2.1.1, 2.1.2
- RC_2_0 HEAD
- fixed on RC_2_1 by 51000e889558a702e7bf591b07c280ad9a4b3b28, not in a release

Introduced by "don't hand a stack buffer to async_write in i2p_stream" (7f13c61d on RC_2_1, back-ported to 2.0.14). That change made `send_*` format into the member `m_buffer`. `read_line()` was not reordered to match.

### What happens
`read_line()` calls the handler, then frees the buffer:

```cpp
case read_connect_response:
case read_session_create_response:
case read_name_lookup_response:
    std::forward<Handler>(h)(ec);
    std::vector<char>().swap(m_buffer);   // frees the buffer the handler just queued a write on
    break;
```

(Same order in the `cmd_incoming` branch and in the `read_hello_response` / `cmd_none|cmd_name_lookup|cmd_incoming` case.)

The handler often starts the next command on the same stream right away:
- `i2p_connection::on_sam_connect` (session-create done) calls `do_name_lookup("ME", ...)`.
  That runs `send_name_lookup`, then `format_command` into `m_buffer`, then `async_write(m_sock, buffer(m_buffer))`
- `i2p_connection::on_name_lookup` starts the next queued lookup the same way (tracker `.b32.i2p` names)

The swap then frees the vector the write is still reading from. The write sends freed memory to the SAM bridge.

### Reproduce
1. Run any SAM bridge (i2pd, Java I2P), `i2p_hostname=127.0.0.1`, `i2p_port=7656`
2. Start a session, wait for SESSION CREATE to finish (10–30 s on a fresh router)
3. MSVC Debug: boost.asio's buffer debugging (on by default with MSVC iterator debugging) fires a CRT iterator assert inside `buffer_debug_check`. The dangling buffer is the `NAMING LOOKUP` write's
4. ASan / Valgrind on Linux should report heap-use-after-free at the same place (not tried)

What we saw:
- Debug, headless process: the assert is a modal dialog nobody can see. It blocks the network thread, and every `session_handle` sync call (`listen_port()`, ...) then hangs the caller.
  Until then, trackers fail with "Waiting for i2p acceptor from SAM bridge"
- Release: undefined behaviour. The write sends whatever is in the freed block. Often still the old bytes, so it can appear to work

### Fix
Swap the buffer out before calling the handler, at all three places. Values parsed from the line were already copied into `m_name_lookup` / `m_dest` (std::string), so nothing reads `m_buffer` afterwards. This is what 51000e88 does:

```diff
@@ read_line
 			m_dest = &m_buffer[0];
-			h(e);
 			std::vector<char>().swap(m_buffer);
+			h(e);
 			return;
@@
 				case cmd_incoming:
-					std::forward<Handler>(h)(ec);
 					std::vector<char>().swap(m_buffer);
+					std::forward<Handler>(h)(ec);
 			}
 			break;
 		case read_connect_response:
 		case read_session_create_response:
 		case read_name_lookup_response:
-			std::forward<Handler>(h)(ec);
 			std::vector<char>().swap(m_buffer);
+			std::forward<Handler>(h)(ec);
 			break;
```

Calling the handler last also makes it safe for the handler to destroy the stream.

### Ask
- cherry-pick 51000e88 to RC_2_0
- tag 2.0.16 / 2.1.3, so packagers (vcpkg, Homebrew, distros) pick it up.
  Every release since 2.0.14 has it, and everyone using I2P hits it on the first session

---

## Issue 2: I2P reconnect backoff never grows (SAM bridge down → retry every 1 s forever)

**Title:** `session_impl: update_i2p_bridge() resets m_i2p_reconnect_delay, so the I2P reconnect backoff stays at 1 s`

### Affected
2.1.1, 2.1.2, RC_2_1 HEAD. This is the reconnect logic added in 2.1.

### What happens
`schedule_i2p_reconnect()` arms the timer with `m_i2p_reconnect_delay`, then doubles it (cap 5 min). The timer handler is:

```cpp
void session_impl::on_i2p_reconnect_timer(error_code const& ec)
{
    ...
    update_i2p_bridge();
}
```

and `update_i2p_bridge()` starts with

```cpp
m_i2p_reconnect_timer.cancel();
m_i2p_reconnecting = false;
m_i2p_reconnect_delay = initial_reconnect_delay;   // 1 s
```

So each retry resets the delay to 1 s before trying again. If it fails, `on_i2p_open` sets a new 1 s timer. The doubling is undone every time.

### Reproduce
- set `i2p_hostname=127.0.0.1`, `i2p_port` to a port nothing listens on (bridge down, router not started yet, or crashed)
- you get one `i2p_alert` ("connection refused") and one "i2p open failed" log line per second, forever

Why this matters: an app cannot "turn I2P off" by leaving `i2p_hostname` empty. With it empty, `.i2p` tracker URLs go out as plain HTTP: a DNS lookup of the `.i2p` name, and the announce sent in the clear if anything resolves it. A privacy app therefore points the bridge at a dead loopback port while the router starts. That case hits this loop all the time.

### Fix
Reset the backoff only when the user calls `update_i2p_bridge()` (settings change), not on the timer path. For example:

```cpp
void session_impl::on_i2p_reconnect_timer(error_code const& ec)
{
    ...
    m_i2p_reconnecting = false;
    open_i2p_bridge();          // the open part of update_i2p_bridge(), without the reset
}

void session_impl::update_i2p_bridge()
{
    m_i2p_reconnect_timer.cancel();
    m_i2p_reconnecting = false;
    m_i2p_reconnect_delay = initial_reconnect_delay;
    open_i2p_bridge();
}
```

Then success in `on_i2p_open` resets the delay, as it already does.

### Measured (v2.1.2, macOS, dead loopback port, 70 s)
- unpatched: 108 `attempting i2p reconnection` lines in about 2 minutes, one per second
- with the fix: 6 attempts in 70 s, at 0.5, 3, 7, 15, 31 and 63 s

The patch eMuleQt ships is smaller than the split above: `on_i2p_reconnect_timer` saves `m_i2p_reconnect_delay` before it calls `update_i2p_bridge()` and restores it afterwards. Either way works. The split is cleaner for upstream.

Side question, not verified: should an empty `i2p_hostname` make `.i2p` tracker URLs fail rather than go out as clearnet HTTP?

---

## Issue 3: outgoing_interfaces drops every I2P peer connection

**Title:** `I2P peers never connect when outgoing_interfaces is set: i2p_stream sockets get the clearnet bind and verify_bound_address checks`

### Affected
v2.1.2 (6da363d2). RC_2_1 HEAD not checked. Code paths look the same in 2.0.x.

### What happens
With `outgoing_interfaces` set (e.g. a LAN address, to keep traffic off a VPN or another interface), not one I2P peer connects, in either direction. I2P trackers still work, because `http_connection` skips these checks. So the trackers return I2P peers, and libtorrent then drops every one of them.

An I2P peer socket's local end is the TCP link to the SAM bridge (usually loopback), not a clearnet interface. Three places treat it as clearnet:
1. `peer_connection::on_connection_complete`: `verify_bound_address(m_local.address(), ...)` fails for the loopback local address, so the peer is disconnected with `no_such_device` ("Operation not supported by device"). This is the main one.
2. `session_impl::incoming_connection`: the same check rejects every accepted I2P stream. The remote peer sees the stream close with no handshake.
3. `session_impl::bind_outgoing_socket`: binds the SAM socket to the pinned interface address. Connecting from a LAN address to a loopback bridge then fails.

### Reproduce
1. Run i2pd (SAM on 127.0.0.1:7656)
2. Session with `i2p_hostname=127.0.0.1`, `i2p_port=7656`, `outgoing_interfaces=<a LAN IPv4 of this host>`
3. Add a torrent with an active I2P tracker (e.g. a `.b32.i2p` opentracker)
4. Result: 24 to 26 I2P connection attempts in 150 s, all closed with `no_such_device`, 0 peers. Without `outgoing_interfaces` the same torrent gets peers

### Fix
Skip all three for an `i2p_stream`. I2P traffic never leaves through a clearnet interface, so the interface restriction does not apply to it:

```diff
--- a/src/peer_connection.cpp
+++ b/src/peer_connection.cpp
@@ on_connection_complete
-		if (!m_settings.get_str(settings_pack::outgoing_interfaces).empty())
+		if (!m_settings.get_str(settings_pack::outgoing_interfaces).empty()
+#if TORRENT_USE_I2P
+			&& std::get_if<i2p_stream>(&m_socket.var()) == nullptr
+#endif
+			)
--- a/src/session_impl.cpp
+++ b/src/session_impl.cpp
@@ incoming_connection
-		if (!m_outgoing_interfaces.empty())
+		if (!m_outgoing_interfaces.empty()
+#if TORRENT_USE_I2P
+			&& std::get_if<i2p_stream>(&s.var()) == nullptr
+#endif
+			)
@@ bind_outgoing_socket
 		tcp::endpoint bind_ep(address_v4(), 0);
+#if TORRENT_USE_I2P
+		if (std::get_if<i2p_stream>(&s.var()) != nullptr) return bind_ep;
+#endif
```

With the fix: 12 I2P peers in 150 s with the same setup.

An open question for upstream: should the SAM control connection itself honour `outgoing_interfaces` when the bridge is not on loopback? The patch leaves `i2p_connection` alone.

---

## Issue 4: incoming I2P stops for good after a failed STREAM ACCEPT (e.g. router restart)

**Title:** `session_impl::on_i2p_accept leaves m_i2p_listen_socket set on error, so no further STREAM ACCEPT is ever issued`

### Affected
v2.1.2 (6da363d2). RC_2_1 HEAD not checked.

### What happens
`open_new_incoming_i2p_connection()` does nothing while `m_i2p_listen_socket` is set:

```cpp
void session_impl::open_new_incoming_i2p_connection()
{
    if (m_abort) return;
    if (!m_i2p_conn.is_open()) return;
    if (m_i2p_listen_socket) return;
    ...
```

`on_i2p_accept()` resets it only on success. On error it posts `listen_failed_alert` and returns, leaving the dead socket in the optional. From then on every call returns early, and incoming I2P stays off until the session is destroyed. Outgoing I2P still works, which hides the problem.

The common trigger is a router restart: the pending `STREAM ACCEPT` gets EOF. The app then points the session at the restarted bridge (`apply_settings` with the new `i2p_port`), `on_i2p_open` succeeds and calls `open_new_incoming_i2p_connection()`, which returns early.

Log: `listening on 0.0.0.0:0 (device: i2p) failed: [sock_accept] [I2P] End of file`

### Reproduce
1. i2pd A with SAM on port P1. Session with `i2p_port=P1`. Wait for `SESSION CREATE` and the first `STREAM ACCEPT` (i2pd debug log: "SAM: Stream accept")
2. Stop i2pd A. The session posts the `listen_failed_alert` above
3. Start i2pd B with SAM on P2 and `apply_settings` with `i2p_port=P2`
4. i2pd B logs "SAM: Session create" but never "SAM: Stream accept". No incoming I2P peer ever connects

### Fix
Reset `m_i2p_listen_socket` on any accept error. If the SAM session is still current, see issue 5. If it is an older session's accept, start one on the current session:

```diff
-	void session_impl::on_i2p_accept(error_code const& e)
+	void session_impl::on_i2p_accept(error_code const& e, std::string const& sid)
 	{
 		COMPLETE_ASYNC("session_impl::on_i2p_accept");
 		if (e == boost::asio::error::operation_aborted) return;
 		if (e)
 		{
+			m_i2p_listen_socket.reset();
+			if (m_i2p_conn.is_open() && sid == m_i2p_conn.session_id())
+			{
+				error_code ignore;
+				m_i2p_conn.close(ignore);   // issue 5
+			}
+			else
+			{
+				open_new_incoming_i2p_connection();
+			}
```

with the accept started as

```cpp
s.async_connect(tcp::endpoint()
    , [this, sid = std::string(m_i2p_conn.session_id())](error_code const& ec)
    { on_i2p_accept(ec, sid); });
```

With the fix, step 4: i2pd B logs "SAM: Stream accept" 6 s after its "SAM: Session create".

---

## Issue 5: a dead SAM session is never noticed when the bridge comes back on the same port

**Title:** `i2p_connection::is_open() stays true after the SAM bridge dies; the reconnect health check never fires`

### Affected
v2.1.2 (6da363d2). RC_2_1 HEAD not checked.

### What happens
After `SESSION CREATE`, nothing reads the SAM control socket. When the router dies, `i2p_connection` does not notice, and `is_open()` keeps returning true. So:
- the second-tick health check (`!m_i2p_conn.is_open() && !m_i2p_reconnecting`) never schedules a reconnect
- when the router comes back on the same host and port, nothing reconnects. `i2p_connection::open()` would return early anyway, because host and port are unchanged and it believes the session is open
- outgoing I2P connects and name lookups fail against a SAM session id the new router does not know, and incoming stays dead (issue 4)

It only recovers if the app changes the bridge host or port, or restarts the session.

### Reproduce
1. i2pd with SAM on port P. Session with `i2p_port=P`. Wait for "SAM: Stream accept"
2. Restart i2pd on the same port P
3. i2pd never logs a new "SAM: Session create" from the session

### Fix
The smallest fix uses the accept that issue 4 already has outstanding: an accept error on the current session id means the session is gone, so close `m_i2p_conn` (see the issue 4 diff). The second tick then schedules a reconnect with backoff, and `on_i2p_open` starts a new accept. It does depend on an accept being outstanding, which `on_i2p_open` always starts.

A more direct fix would be an `async_read` on the SAM control socket after `SESSION CREATE`, closing the connection on EOF or error.

With the fix: router restarted on the same port at 20:42:12, new "SAM: Session create" at 20:42:16 and "SAM: Stream accept" at 20:42:49.
