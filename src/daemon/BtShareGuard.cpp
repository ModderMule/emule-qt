#include "BtShareGuard.h"

#ifdef EMULE_HAVE_BITTORRENT

#include "files/SharedFileList.h"

#include <QDir>
#include <QFileInfo>

#include <algorithm>

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
    // its whole content, for btShareMode; no name yet (magnet): not the save dir
    bool rootNowBlocked = false;
    QStringList rootReleased;
    if (!row->name.isEmpty()) {
        const QString rootPath = QDir::cleanPath(row->savePath + u'/' + row->name);
        const Root r{SharedFileList::pathKey(rootPath), row->isPrivate || row->i2p};
        const auto old = m_roots.constFind(id);
        const bool changed = old == m_roots.constEnd() || old->key != r.key;
        rootNowBlocked = rootBlocked(r) && (changed || !rootBlocked(*old));
        // an allowed root over a blocked one's files shares them (public root wins)
        if (!rootBlocked(r) && (changed || rootBlocked(*old)) && overlapsBlocked(r.key))
            rootReleased = {rootPath, QFileInfo(rootPath).absolutePath()};
        m_roots.insert(id, r);
    }

    // #122: loaded at a restart, a torrent completed before is checked first (progress 0).
    // Blocking it then unshared every seeding file until the check ended, and the re-share
    // hashed them all again for ed3k. Judged once settled; unfinished ones stay blocked.
    if (!known && !settled(row->state) && row->state != bt::BtState::Moving && row->completedAt > 0)
        return {.released = rootReleased, .blockedMore = rootNowBlocked};

    Change change;
    change.released = rootReleased;
    change.blockedMore = rootNowBlocked;
    Entry next;
    {
        const QString root = QDir::cleanPath(row->savePath + u'/' + row->name);
        next.done = row->progress >= 1.0 && settled(row->state);
        // kept through a check or move (done, then Moving, then Seeding)
        next.downloading = settled(row->state) ? row->progress < 1.0 : known && it->downloading;
        // done stays done until it is not: no file list per status tick
        if (known && next.done && it->done)
            return {.released = rootReleased, .blockedMore = rootNowBlocked};
        // loaded complete (a restart) is not finished: a file unshared since stays so
        const bool finished = known && it->downloading && next.done;
        if (!next.done) {
            // no name yet (magnet, no metadata): nothing on disk; root = the save dir
            if (!row->name.isEmpty())
                next.paths = {root};
        } else if (files) {
            const auto list = files();
            if (!list)
                return {.released = rootReleased, .blockedMore = rootNowBlocked};   // entry kept: judged again when they come
            for (const bt::BtFileRow& f : *list) {
                if (f.pad)
                    continue;
                const QString path = QDir::cleanPath(row->savePath + u'/' + f.path);
                if (f.done < f.size)
                    next.paths.append(path);
                else if (finished)
                    change.finished.append(path);
            }
        }
        for (const QString& p : std::as_const(next.paths))
            next.keys.append(SharedFileList::pathKey(p));
    }

    if (known) {
        for (qsizetype i = 0; i < it->keys.size(); ++i) {
            if (next.keys.contains(it->keys.at(i)))
                continue;
            // a file or a torrent folder: not stat'ed here (#84), both rescanned; the
            // share list adds the folders below (rescanTrees)
            const QString path = it->paths.at(i);
            for (const QString& dir : {path, QFileInfo(path).absolutePath()})
                if (!change.released.contains(dir))
                    change.released.append(dir);
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
    if ((m_entries.isEmpty() && (m_mode == BtShareMode::All || m_roots.isEmpty())) || filePath.isEmpty())
        return false;
    const QString key = SharedFileList::pathKey(filePath);
    const auto under = [&key](const QString& k) { return isUnder(key, k); };
    for (const Entry& e : m_entries)
        if (std::ranges::any_of(e.keys, under))
            return true;
    if (m_mode == BtShareMode::All)
        return false;
    // blocked only when every torrent over it is: one the mode allows shares it anyway
    bool blocked = false;
    for (const Root& r : m_roots) {
        if (!under(r.key))
            continue;
        if (!rootBlocked(r))
            return false;
        blocked = true;
    }
    return blocked;
}

bool BtShareGuard::setShareMode(BtShareMode mode)
{
    if (mode == m_mode)
        return false;
    m_mode = mode;
    return true;
}

// ---------------------------------------------------------------------------
// private
// ---------------------------------------------------------------------------

bool BtShareGuard::rootBlocked(const Root& r) const
{
    return m_mode == BtShareMode::Off || (m_mode == BtShareMode::Public && r.restricted);
}

bool BtShareGuard::overlapsBlocked(const QString& key) const
{
    return std::ranges::any_of(m_roots, [&](const Root& r) { return rootBlocked(r) && (isUnder(r.key, key) || isUnder(key, r.key)); });
}

bool BtShareGuard::isUnder(const QString& key, const QString& root)
{
    return key == root || (key.startsWith(root) && key.at(root.size()) == u'/');
}

} // namespace eMule

#endif // EMULE_HAVE_BITTORRENT
