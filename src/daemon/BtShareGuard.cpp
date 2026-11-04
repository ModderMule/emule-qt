#include "BtShareGuard.h"

#ifdef EMULE_HAVE_BITTORRENT

#include "files/SharedFileList.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>

namespace eMule {

namespace {

/// Data on disk is not yet what the torrent says it is
bool settled(bt::BtState state)
{
    using S = bt::BtState;
    return state != S::CheckingFiles && state != S::CheckingResume
        && state != S::DownloadingMetadata && state != S::Moving;
}

} // namespace

BtShareGuard::Change BtShareGuard::update(const QString& id, const std::optional<bt::BtTorrentRow>& row,
                                          const FilesFn& files)
{
    const auto it = m_entries.constFind(id);
    const bool known = it != m_entries.constEnd();

    // removed: what it left incomplete stays out for this session
    if (!row)
        return {};

    Entry next;
    {
        const QString root = QDir::cleanPath(row->savePath + u'/' + row->name);
        next.done = row->progress >= 1.0 && settled(row->state);
        // done stays done until it is not: no file list per status tick
        if (known && next.done && it->done)
            return {};
        if (!next.done) {
            next.paths = {root};
        } else if (files) {
            for (const bt::BtFileRow& f : files())
                if (!f.pad && f.done < f.size)
                    next.paths.append(QDir::cleanPath(row->savePath + u'/' + f.path));
        }
        for (const QString& p : std::as_const(next.paths))
            next.keys.append(SharedFileList::pathKey(p));
    }

    Change change;
    if (known) {
        for (qsizetype i = 0; i < it->keys.size(); ++i) {
            if (next.keys.contains(it->keys.at(i)))
                continue;
            const QFileInfo fi(it->paths.at(i));
            const QString dir = fi.isDir() ? fi.absoluteFilePath() : fi.absolutePath();
            if (!change.released.contains(dir))
                change.released.append(dir);
            // a torrent folder: its nested ones were scanned while blocked too
            if (fi.isDir()) {
                QDirIterator sub(dir, QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
                while (sub.hasNext())
                    if (const QString d = sub.next(); !change.released.contains(d))
                        change.released.append(d);
            }
        }
    }
    for (const QString& key : std::as_const(next.keys))
        if (!known || !it->keys.contains(key))
            change.blockedMore = true;

    m_entries.insert(id, next);
    return change;
}

bool BtShareGuard::blocks(const QString& filePath) const
{
    if (m_entries.isEmpty() || filePath.isEmpty())
        return false;
    const QString key = SharedFileList::pathKey(filePath);
    for (const Entry& e : m_entries)
        for (const QString& k : e.keys)
            if (key == k || (key.startsWith(k) && key.at(k.size()) == u'/'))
                return true;
    return false;
}

} // namespace eMule

#endif // EMULE_HAVE_BITTORRENT
