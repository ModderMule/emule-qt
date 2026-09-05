#pragma once

/// @file PreviewLauncher.h
/// @brief Launch a media player for preview streaming.

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

} // namespace eMule
