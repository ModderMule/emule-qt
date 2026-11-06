# libtorrent upstream reports: I2P SAM

Two reports for https://github.com/arvidn/libtorrent/issues, ready to paste.
Found on Windows 11 with emuleqt (vcpkg libtorrent 2.1.2, MSVC Debug, i2pd 0.9.71 SAM).

Local state:
- issue 1 patched by overlay port `src/vcpkg-ports/libtorrent` (`fix-i2p-stream-buffer-uaf.patch`).
  Same code change as upstream 51000e88. Drop the overlay once a release has it.
  The FetchContent v2.1.2 build (Linux/macOS) applies the same file via `cmake/ApplyPatch.cmake`.
  A Homebrew/distro libtorrent found by `find_package` is not patched
- issue 2 not patched. Without a SAM bridge, `BtSession::drainAlerts()` drops the `i2p_alert`s.
  libtorrent still retries every second
- verified 2026-11-04 with the patch, Debug: two cores, each with its own bundled i2pd, 4 MB torrent.
  Done in 6m50s over public I2P trackers, sha256 matches, no assert and no freeze

Status 2026-11-04:
- issue 1 fixed on RC_2_1 by 51000e88 "fix i2p_stream handler invocation" (2026-11-02), not released yet.
  RC_2_0 still has it
- issue 2 still in RC_2_1. No issue/PR found

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

Possibly related, not verified: should an empty `i2p_hostname` make `.i2p` tracker URLs fail rather than go out as clearnet HTTP?
