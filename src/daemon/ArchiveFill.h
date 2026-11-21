#pragma once

/// @file ArchiveFill.h
/// @brief #224: Usenet volumes filled from files on disk here. A new Usenet download's
/// archives are looked up in ed3k (contents records, by the NZB's file ids); entries whose
/// root is on disk here (Shared Files, a download, a folder scan) are handed to par2 once the
/// index .par2 is there (Par2LocalFill): the blocks of stored volumes found in them are written
/// into the part files, their articles are not fetched (UsenetQueue::acceptExternalRange).
/// Compressed archives find nothing and download as always. On with ed3kArchiveContents.

#include <QObject>
#include <QString>
#include <QStringList>

#include <atomic>
#include <map>
#include <memory>

namespace eMule {

namespace ed3k {
class Ed3kArchiveContents;
}
namespace usenet {
class UsenetQueue;
}

class ArchiveFill : public QObject {
    Q_OBJECT

public:
    ArchiveFill(ed3k::Ed3kArchiveContents& contents, usenet::UsenetQueue& queue, QObject* parent = nullptr);
    ~ArchiveFill() override;

    void setEnabled(bool on) { m_enabled = on; }
    /// Items whose entries on disk are known (tests).
    [[nodiscard]] QStringList sourcesOf(const QString& itemId) const;

    /// The lookup's answer for @p itemId: files on disk (tests call it directly).
    void setSources(const QString& itemId, const QStringList& paths);

signals:
    /// The fill of @p itemId ran: @p bytes written from disk.
    void filled(const QString& itemId, qint64 bytes);
    /// The articles under file @p fileIndex's filled ranges taken as done (not fetched).
    void accepted(const QString& itemId, int fileIndex, int articles);

private:
    struct Item {
        QStringList sources;
        bool running = false;
        bool done = false;
        quint32 lookup = 0;
        /// file index → ranges written, not yet given to the queue: a volume without an
        /// article of its own has no part size (the one borrowed may be a small .par2's)
        std::map<int, std::vector<std::pair<qint64, qint64>>> pending;
    };

    void onItemAdded(const QString& id);
    void onItemChanged(const QString& id);
    void onItemRemoved(const QString& id);
    /// The index .par2 is on disk: the fill on a worker.
    void tryFill(const QString& id);
    /// The pending ranges of files with a part size of their own, to the queue.
    void acceptPending(const QString& id);

    ed3k::Ed3kArchiveContents& m_contents;
    usenet::UsenetQueue& m_queue;
    std::map<QString, Item> m_items;
    std::shared_ptr<std::atomic<bool>> m_cancel;
    bool m_enabled = true;
};

} // namespace eMule
