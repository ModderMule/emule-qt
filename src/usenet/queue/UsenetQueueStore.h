#pragma once

/// @file UsenetQueueStore.h
/// @brief One state sidecar per queued NZB, so a restart resumes rather than refetches.
///
/// Shaped after DownloadQueue: there is no monolithic queue file in this codebase.
/// `DownloadQueue::init()` globs `*.part.met` sidecars out of the temp directories
/// and rebuilds itself from them, and this does the same with `*.nzbstate` under
/// `configDir()/Usenet/`. One item's corruption then costs one item.
///
/// The durability dance is PartFile::savePartFile()'s, verbatim in shape: write a
/// `.backup`, rotate the live file to `.bak`, rename the backup into place, and
/// fall back to the `.bak` if the rename fails. A queue file truncated by a power
/// cut is otherwise indistinguishable from an empty queue.
///
/// YAML rather than CBOR purely so a stuck item can be read with `cat`. The
/// segment bitmaps are base64 inside it; everything else is meant to be legible.

#include "utils/Types.h"

#include <QHash>
#include <QString>

namespace eMule::usenet {

class UsenetQueueItem;

/// Write @p text to @p path through PartFile::savePartFile()'s rotation dance:
/// write a `.backup`, rotate the live file to `.bak`, rename the backup into
/// place, and put the `.bak` back if that rename fails. A sidecar truncated by
/// a power cut is otherwise indistinguishable from an empty one.
///
/// Free rather than a member because the usage meter needs the same dance and
/// this would otherwise be its third open-coded copy in the module's orbit.
[[nodiscard]] bool writeSidecarAtomically(const QString& path, const char* text);

class UsenetQueueStore {
public:
    /// Directory holding the sidecars. Created on demand.
    static QString stateDir();

    static QString statePath(const QString& itemId);

    /// Write @p item's state. Returns false and logs on failure; the caller
    /// carries on, because losing resume state is not worth aborting a download.
    static bool save(const UsenetQueueItem& item);

    /// Read one sidecar. Returns false if it is unreadable or malformed.
    static bool load(const QString& path, UsenetQueueItem& out, QString& error);

    /// Remove an item's sidecar and its rotation backups.
    static void remove(const QString& itemId);

    /// Every sidecar path currently on disk, sorted for a stable restore order.
    static QStringList listStateFiles();

    /// Renumber the category of every item on disk. Returns how many changed.
    ///
    /// The queue only loads its sidecars in `start()`, so with Usenet disabled
    /// the items exist *only* as these files -- and deleting a category then
    /// would leave every one of them pointing at whatever moved into that slot.
    /// ED2K has no equivalent exposure because its queue is live for as long as
    /// the daemon is. Same fallback as `DownloadQueue::remapCategories()`: an
    /// index absent from the map means the category is gone, and the item keeps
    /// its place and loses only its label.
    ///
    /// Only sidecars whose category actually moves are rewritten.
    static int remapCategories(const QHash<uint32, uint32>& oldToNew);
};

} // namespace eMule::usenet
