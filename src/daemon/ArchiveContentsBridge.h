#pragma once

/// @file ArchiveContentsBridge.h
/// @brief Search and Transfers Details › Archive over IPC (745, 746, hoster.md §6.1): what each
/// archive of a web row, an NZB, a torrent or a queued item holds, and its entries downloaded
/// by root. Without the ed3k module every request answers false.

#include <QCborArray>
#include <QCborMap>
#include <QCborValue>
#include <QHash>
#include <QString>
#include <QStringList>

#include <functional>

namespace eMule {

namespace ed3k {
class Ed3kArchiveContents;
class Ed3kDownloads;
class Ed3kHosterJoin;
} // namespace ed3k
namespace bt {
class BtSession;
}

namespace ArchiveContentsBridge {

/// Set by DaemonApp; null when absent.
void setTargets(ed3k::Ed3kArchiveContents* contents, ed3k::Ed3kDownloads* downloads, ed3k::Ed3kHosterJoin* hoster,
                bt::BtSession* bt);

using Reply = std::function<void(bool ok, const QCborValue& value)>;
/// LookupArchiveContents: @p reply once, mostly later.
void lookup(const QString& kind, const QCborMap& payload, Reply reply);
/// {firstVolumeName: [path]} of an IPC request: first names lower case, empty lists left out.
[[nodiscard]] QHash<QString, QStringList> entriesFromCbor(const QCborMap& map);
/// SetArchiveEntries: what a queued Usenet or hoster download (@p target {usenetItem} |
/// {hosterPackage, hosterItem}) extracts; empty @p entries: everything.
void setEntries(const QCborMap& target, const QCborMap& entries, Reply reply);
/// DownloadArchiveEntries.
void downloadEntries(const QCborArray& entries, const QString& folder, int category, Reply reply);
/// TakeArchiveFromDisk (#224): every entry of @p archives is on disk here; they come by root
/// from there into incoming/<@p folder>/, the archives' volumes in @p target are skipped.
void takeFromDisk(const QCborMap& target, const QCborArray& archives, const QString& folder, int category, Reply reply);
/// LocateLocalCopies (#224): a folder scan for the open ed3k downloads; @p reply when it ends.
/// One at a time.
void locateLocalCopies(const QStringList& folders, Reply reply);

} // namespace ArchiveContentsBridge

} // namespace eMule
