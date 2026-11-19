/// @file BtFileIndex.cpp
/// @brief Which torrents cover a file on disk — see BtFileIndex.h.

#include "BtFileIndex.h"

#ifdef EMULE_HAVE_BITTORRENT

#include "files/SharedFileList.h"

namespace eMule {

QSet<QString> BtFileIndex::update(const QString& id, const QString& name, const QString& root,
                                  const QStringList& paths)
{
    QStringList keys;
    keys.reserve(paths.size());
    for (const QString& p : paths)
        keys.append(SharedFileList::pathKey(p));
    if (const auto it = m_entries.constFind(id); it != m_entries.constEnd() && it->name == name && it->keys == keys) {
        m_entries[id].root = root;
        return {};
    }
    QSet<QString> touched = remove(id);
    for (const QString& k : std::as_const(keys)) {
        m_byKey[k].append(id);
        touched.insert(k);
    }
    m_entries.insert(id, Entry{name, root, std::move(keys)});
    return touched;
}

QSet<QString> BtFileIndex::remove(const QString& id)
{
    QSet<QString> touched;
    const auto it = m_entries.constFind(id);
    if (it == m_entries.constEnd())
        return touched;
    for (const QString& k : it->keys) {
        touched.insert(k);
        if (auto b = m_byKey.find(k); b != m_byKey.end()) {
            b->removeAll(id);
            if (b->isEmpty())
                m_byKey.erase(b);
        }
    }
    m_entries.erase(it);
    return touched;
}

QList<BtFileIndex::Torrent> BtFileIndex::torrentsOf(const QString& filePath) const
{
    QList<Torrent> out;
    if (m_byKey.isEmpty() || filePath.isEmpty())
        return out;
    const auto it = m_byKey.constFind(SharedFileList::pathKey(filePath));
    if (it == m_byKey.constEnd())
        return out;
    for (const QString& id : *it)
        out.append(Torrent{id, m_entries.value(id).name});
    return out;
}

BtFileIndex& BtFileIndex::instance()
{
    static BtFileIndex index;
    return index;
}

} // namespace eMule

#endif // EMULE_HAVE_BITTORRENT
