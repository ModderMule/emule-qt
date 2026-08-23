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

} // namespace eMule
