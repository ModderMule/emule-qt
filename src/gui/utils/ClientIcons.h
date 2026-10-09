#pragma once

/// @file ClientIcons.h
/// @brief Client software icon (MFC GetDisplayImage) for every list showing peers.

#include <QIcon>

namespace eMule {

/// "Plus" variants mean the client has credit (scoreRatio > 1.0). Friends keep
/// the software icon with a small friend badge at the bottom-right.
[[nodiscard]] QIcon clientSoftwareIcon(int softwareId, bool hasCredit, bool isFriend);

/// MFC's five source state icons, in its image-list order
/// (DownloadListCtrl.cpp:181-185).
enum class SourceStateIcon { Downloading, OnQueue, Connecting, NoNeededOrFull, Unknown };

/// Which of them a source row shows (MFC DownloadListCtrl.cpp:569-594).
/// @param stateToken the daemon's download state token ("OnQueue", ...)
[[nodiscard]] SourceStateIcon sourceStateIconKind(const QString& stateToken, bool remoteQueueFull,
                                                  bool a4af);
[[nodiscard]] QIcon sourceStateIcon(SourceStateIcon kind);

/// Two 16 px icons side by side, 20 px apart as MFC draws them.
[[nodiscard]] QIcon iconPair(const QIcon& first, const QIcon& second);

} // namespace eMule
