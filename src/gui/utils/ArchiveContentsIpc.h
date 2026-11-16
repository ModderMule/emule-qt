#pragma once

/// @file ArchiveContentsIpc.h
/// @brief Connects an ArchiveContentsView to the core: LookupArchiveContents for it and its
/// nested archives, DownloadArchiveEntries for "Download Checked from ed3k".

#include <QCborMap>
#include <QHash>
#include <QString>
#include <QStringList>

#include <functional>
#include <utility>

namespace eMule {

class ArchiveContentsView;
class IpcClient;

namespace ArchiveContentsIpc {

/// What to look up: kind ("web", "nzb", "torrent", "roots") and payload; asked at lookup time.
using RequestFn = std::function<std::pair<QString, QCborMap>()>;

/// {firstVolumeName: [path]} for AddHosterLinks, DownloadMetaResult, GrabIndexerResult.
[[nodiscard]] QCborMap entriesToCbor(const QHash<QString, QStringList>& entries);

/// Entries go to incoming/<@p folder>/ of @p category.
void attach(ArchiveContentsView* view, IpcClient* ipc, RequestFn request, QString folder, int category = 0);

} // namespace ArchiveContentsIpc

} // namespace eMule
