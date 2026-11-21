#pragma once

/// @file BtFileIndex.h
/// @brief Which torrents cover a file on disk (#212): the Shared Files list marks such
/// files and offers to remove the torrent while keeping the files. A file of a multi-file
/// (folder) torrent counts as covered too.

#ifdef EMULE_HAVE_BITTORRENT

#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

namespace eMule {

class BtFileIndex {
public:
    struct Torrent {
        QString id;
        QString name;
        bool operator==(const Torrent&) const = default;
    };

    /// @p paths: every non-pad file of torrent @p id, absolute; @p root: save path + name,
    /// to tell a move. Returns the pathKeys whose torrents changed (empty: nothing new).
    QSet<QString> update(const QString& id, const QString& name, const QString& root, const QStringList& paths);
    /// The keys it covered.
    QSet<QString> remove(const QString& id);
    /// Listed with @p root: no need to ask for its files again
    [[nodiscard]] bool isCurrent(const QString& id, const QString& root) const
    {
        const auto it = m_entries.constFind(id);
        return it != m_entries.constEnd() && it->root == root;
    }
    [[nodiscard]] QList<Torrent> torrentsOf(const QString& filePath) const;
    [[nodiscard]] bool isEmpty() const { return m_entries.isEmpty(); }

    /// The daemon's one index (Shared Files rows read it).
    static BtFileIndex& instance();

private:
    struct Entry {
        QString name;
        QString root;
        QStringList keys;   ///< SharedFileList::pathKey of each file
    };
    QHash<QString, Entry> m_entries;
    QHash<QString, QStringList> m_byKey;   ///< pathKey -> torrent ids
};

} // namespace eMule

#endif // EMULE_HAVE_BITTORRENT
