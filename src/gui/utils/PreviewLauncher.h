#pragma once

/// @file PreviewLauncher.h
/// @brief Launch a media player, a browser or the file manager for what the
///        daemon holds.

#include <QString>

namespace eMule {

class IpcClient;

/// Launch the configured video player with the given streaming URL.
/// If VLC is detected, reuses an existing instance instead of spawning a new one.
void launchPreview(const QString& url);

/// Build the daemon's HTTP URL for @p fileHash — the channel that backs both live
/// preview and, against a remote core, opening a finished file. @p streamToken is
/// the one handed out with the daemon's stats (IpcClientHandler "streamToken").
///
/// Returns an empty string when the exchange cannot be built — no connection, no
/// token, no hash — which every caller must treat as "not available" rather than
/// as a URL, because the web server may simply be switched off.
[[nodiscard]] QString daemonStreamUrl(const IpcClient* ipc, const QString& fileHash,
                                      const QString& streamToken);

/// The same channel for one file of one Usenet queue item.
///
/// A separate route rather than the one above because a Usenet file has no ED2K
/// hash to be named by: it is an item UUID plus the file's position in the NZB.
/// Empty on the same terms — no connection, no token, no id — and for the same
/// reason: the daemon's web server may simply be off.
/// @p entry picks a file *inside* an archive set; -1 leaves the choice to the
/// daemon, which plays the first playable one. A -1 URL is byte-identical to
/// what this produced before the chooser existed, so nothing cached breaks.
[[nodiscard]] QString daemonUsenetStreamUrl(const IpcClient* ipc, const QString& itemId,
                                            int fileIndex, const QString& streamToken,
                                            int entry = -1);

/// The browse page for the core's Incoming folder, or a folder inside it.
///
/// Same lane as the two stream URLs above -- the daemon's web server, gated by
/// the stream token -- because a remote core's Incoming folder is not on this
/// machine and cannot be handed to the file manager. @p relPath is relative to
/// the Incoming folder; empty means its root. Empty return on the same terms as
/// daemonStreamUrl().
[[nodiscard]] QString daemonIncomingUrl(const IpcClient* ipc, const QString& streamToken,
                                        const QString& relPath = {});

/// The daemon's page for one file inside its Incoming folder.
///
/// A separate builder from daemonIncomingUrl() because the listing route answers
/// **400 "Not a folder"** for a `path=` naming a file — the player page is the
/// same route under `play=`, and a non-media file is served by
/// /api/v1/incoming/download. So a folder URL cannot stand in for this.
///
/// @p play picks the browser player page — one <video>/<audio> tag plus a
/// download link — over the plain attachment download. Empty on the same terms
/// as the other builders: no connection, no token, no path.
[[nodiscard]] QString daemonIncomingFileUrl(const IpcClient* ipc, const QString& streamToken,
                                            const QString& relPath, bool play);

/// Open that page in the default browser, refusing through
/// incomingBrowseUnavailableReason() so a loopback-only web server produces one
/// wording here and in openIncomingInBrowser(), never two.
bool openIncomingFileInBrowser(const IpcClient* ipc, const QString& streamToken,
                               const QString& relPath, bool play);

/// The core's web interface: `<scheme>://<daemonHost>:<webServerPort>/`.
///
/// The root page is the login form and there is no deep link past it -- a session
/// id can only be minted by POSTing the password -- so the bare root is the only
/// sensible target. Empty when there is no connection to build a host from. Says
/// nothing about whether the web UI is switched on; that is the caller's gate,
/// because only the caller can tell the user where to switch it on.
[[nodiscard]] QString daemonWebUiUrl(const IpcClient* ipc);

/// Empty when the browse page above is reachable, otherwise the user-facing
/// reason it is not -- no connection, no stream token yet, or a remote core whose
/// web server is pinned to loopback because both web surfaces are off.
///
/// Split out so a menu action can put the reason in a message box while
/// openIncomingInBrowser() logs the same sentence: the two can then never
/// disagree about when the page works. A *local* core needs neither surface
/// enabled -- the browse route is registered unconditionally.
[[nodiscard]] QString incomingBrowseUnavailableReason(const IpcClient* ipc,
                                                      const QString& streamToken);

/// Show the core's Incoming folder in the default browser, whatever machine the
/// core runs on. The browse page has play and download links the file manager
/// cannot offer, which is why a local core can want it too.
bool openIncomingInBrowser(const IpcClient* ipc, const QString& streamToken,
                           const QString& relPath = {});

/// Show the core's Incoming folder: the OS file manager when the core runs on
/// this machine, the browse page in the default browser when it does not.
///
/// The single place that decision is made. Returns false, having logged why,
/// when neither is possible -- most often because the daemon binds its web
/// server to loopback while both web surfaces are switched off, which no URL can
/// work around.
/// @param localPath  which folder to open against a local core. Empty means the
///        global incoming directory; a category's own folder goes here.
/// @param relPath     the same folder addressed for the remote browse page —
///        "!N" for category N. Empty means the root listing.
bool openIncomingFolder(const IpcClient* ipc, const QString& streamToken,
                        const QString& localPath = {}, const QString& relPath = {});

} // namespace eMule
