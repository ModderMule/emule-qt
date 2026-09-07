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
