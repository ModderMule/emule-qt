#pragma once

/// @file UsenetHistory.h
/// @brief What became of releases that are no longer in the queue.
///
/// The Usenet half of known.met and cancelled.met, and it answers to the same
/// two preferences -- rememberDownloadedFiles and rememberCancelledFiles --
/// because the question a person answers in Options is the same question.
///
/// Without it the duplicate guard is blind to anything the user has cleared:
/// UsenetQueue::findDuplicate() scans the live queue, and an item removed from
/// it leaves no trace at all.

#include <QHash>
#include <QMultiHash>
#include <QString>

#include <QtGlobal>

namespace eMule::usenet {

class UsenetQueueItem;

/// How a release left the queue.
enum class UsenetHistoryState : quint8 {
    Downloaded = 0,   ///< finished -- rememberDownloadedFiles() governs it
    Cancelled  = 1,   ///< removed before finishing -- rememberCancelledFiles()
};

[[nodiscard]] QString usenetHistoryStateToString(UsenetHistoryState state);
[[nodiscard]] UsenetHistoryState usenetHistoryStateFromString(const QString& text);

struct UsenetHistoryEntry {
    /// nzbArticleDigest(). The one thing here that is *stored* rather than
    /// re-derived, and the whole reason this file exists: a .nzbstate can
    /// recompute its digest because it still holds every message-id, and this
    /// cannot, because by the time it is read the NZB is gone.
    QString digest;

    QString name;       ///< as shown to the user, for the sentence
    qint64 size = 0;    ///< totalEncodedBytes(), for the sentence only
    qint64 when = 0;    ///< epoch seconds -- eviction order
    UsenetHistoryState state = UsenetHistoryState::Downloaded;
};

/// Releases that have left the queue, so re-adding one can be noticed.
///
/// Every preference check lives *inside* this class rather than at its call
/// sites, the way MFC gates CKnownFileList::AddCancelledFileID and the way
/// addNzb() enforces usenetAutoAddPaused: every present and future caller then
/// inherits the rule by construction, instead of each one having to remember it.
/// Turning a preference off forgets -- the entries are neither read nor written,
/// which is MFC's behaviour for cancelled.met.
class UsenetHistory {
public:
    /// Beside the .nzbstate sidecars and usage.yml. Those are globbed by
    /// *.nzbstate, so a .yml among them is never mistaken for a queue item.
    [[nodiscard]] static QString historyPath();

    /// Read the file. Idempotent, and every accessor calls it, so an add that
    /// reaches a never-started queue still gets a truthful answer.
    void load();

    /// Remember @p item. Does nothing when the governing preference is off, or
    /// when the item has no article digest to be identified by.
    ///
    /// **Downloaded is monotone**: a later Cancelled for the same digest keeps
    /// Downloaded. Finishing a release and then clearing its row must not read
    /// back as "I gave up on it".
    void record(const UsenetQueueItem& item, UsenetHistoryState state);

    /// Exact match, and what the add path asks. Null when nothing matches or the
    /// matching entry's governing preference is off.
    [[nodiscard]] const UsenetHistoryEntry* findByDigest(const QString& digest) const;

    /// Heuristic match on the folded release name, for marking search results.
    /// An indexer row carries no message-ids, so this is the only join there is
    /// -- and it only ever colours a row and raises a question, never refuses an
    /// add. See usenetFoldedReleaseName().
    [[nodiscard]] const UsenetHistoryEntry* findByName(const QString& name) const;

    [[nodiscard]] int count() const;

    /// The whole file, through writeSidecarAtomically(). Called on completion and
    /// on removal, both rare, so write-through costs nothing and an unclean exit
    /// between two downloads loses nothing.
    bool save();

    /// Forget everything, on disk as well as in memory.
    bool clear();

    /// Evicted oldest-first. Unlike IndexerFeedStore's seen set there is no
    /// second layer beneath this one, so an evicted release becomes silently
    /// re-addable -- which is why the cap is generous rather than tight.
    static constexpr int kMaxEntries = 5000;

private:
    [[nodiscard]] bool isRemembered(UsenetHistoryState state) const;
    void evict();
    void reindex();

    QHash<QString, UsenetHistoryEntry> m_byDigest;
    /// folded name -> digest. Keyed rather than positional: eviction erases a
    /// prefix, which would invalidate every index into a list.
    QMultiHash<QString, QString> m_digestsByFoldedName;
    bool m_loaded = false;
};

} // namespace eMule::usenet
