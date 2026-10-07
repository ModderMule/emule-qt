#include "pch.h"
/// @file SharedDirWatcher.cpp
/// @brief Tells the shared file list which shared directories changed on disk.

#include "files/SharedDirWatcher.h"
#include "utils/Log.h"

#include <QDir>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QSet>
#include <QStorageInfo>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace eMule {

SharedDirWatcher::SharedDirWatcher(QObject* parent)
    : QObject(parent)
    , m_watcher(new QFileSystemWatcher(this))
{
    connect(m_watcher, &QFileSystemWatcher::directoryChanged,
            this, &SharedDirWatcher::onDirectoryEvent);

    m_flushTimer.setInterval(250);
    connect(&m_flushTimer, &QTimer::timeout, this, &SharedDirWatcher::flush);

    m_pollTimer.setInterval(5 * 60 * 1000);
    connect(&m_pollTimer, &QTimer::timeout, this, &SharedDirWatcher::poll);
}

void SharedDirWatcher::setRoots(const QStringList& dirs)
{
    QHash<QString, Root> next;
    for (const QString& dir : dirs) {
        if (dir.isEmpty() || next.contains(dir))
            continue;
        if (const auto it = m_roots.constFind(dir); it != m_roots.constEnd() && it->watched) {
            next.insert(dir, *it);
            continue;
        }
        Root root;
        // false for a missing directory too; the poll picks it up when it appears
        root.watched = m_watcher->addPath(dir);
        root.remote = m_isRemote ? m_isRemote(dir) : isOnNetworkShare(dir);
        if (!root.watched)
            logDebug(QStringLiteral("Shared directory is polled, not watched: %1").arg(dir));
        else if (root.remote)
            logDebug(QStringLiteral("Shared directory on a network share is polled too: %1").arg(dir));
        next.insert(dir, root);
    }

    for (auto it = m_roots.constBegin(); it != m_roots.constEnd(); ++it) {
        if (!next.contains(it.key())) {
            if (it->watched)
                m_watcher->removePath(it.key());
            m_pending.remove(it.key());
        }
    }
    m_roots = std::move(next);

    const bool anyPolled = std::ranges::any_of(
        m_roots, [](const Root& r) { return !r.watched || r.remote; });
    if (anyPolled && !m_pollTimer.isActive())
        m_pollTimer.start();
    else if (!anyPolled)
        m_pollTimer.stop();
}

QStringList SharedDirWatcher::polledRoots() const
{
    QStringList out;
    for (auto it = m_roots.constBegin(); it != m_roots.constEnd(); ++it)
        if (!it->watched || it->remote)
            out.append(it.key());
    return out;
}

void SharedDirWatcher::setTimings(int settleMs, int maxHoldMs, int pollMs)
{
    m_settleMs = settleMs;
    m_maxHoldMs = maxHoldMs;
    m_flushTimer.setInterval(std::clamp(settleMs / 4, 10, 250));
    m_pollTimer.setInterval(pollMs);
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

void SharedDirWatcher::onDirectoryEvent(const QString& dir)
{
    if (!m_roots.contains(dir))
        return;

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    auto it = m_pending.find(dir);
    if (it == m_pending.end())
        it = m_pending.insert(dir, Pending{now, now});
    else
        it->lastMs = now;

    if (m_pending.size() > kMaxPending) {
        m_pending.clear();
        m_flushTimer.stop();
        emit overflow();
        return;
    }
    if (!m_flushTimer.isActive())
        m_flushTimer.start();
}

void SharedDirWatcher::flush()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QStringList due;
    for (auto it = m_pending.begin(); it != m_pending.end(); ) {
        if (now - it->lastMs >= m_settleMs || now - it->firstMs >= m_maxHoldMs) {
            due.append(it.key());
            it = m_pending.erase(it);
        } else {
            ++it;
        }
    }
    if (m_pending.isEmpty())
        m_flushTimer.stop();

    for (const QString& dir : due) {
        // a directory that was deleted and made again has lost its watch
        if (auto it = m_roots.find(dir); it != m_roots.end() && it->watched
            && !m_watcher->directories().contains(dir))
            it->watched = m_watcher->addPath(dir);
        emit directoryChanged(dir);
    }
    if (!due.isEmpty())
        emit directoriesChanged(due);
}

void SharedDirWatcher::poll()
{
    for (auto it = m_roots.begin(); it != m_roots.end(); ++it) {
        if (it->watched && !it->remote)
            continue;
        // try the watch again: the directory may exist now, or the volume be back
        if (!it->watched && m_watcher->addPath(it.key()))
            it->watched = true;
        // Reported every time: the directory's own date does not move when a file
        // is rewritten in place, and the rescan this leads to only stats.
        onDirectoryEvent(it.key());
    }
}

bool SharedDirWatcher::isOnNetworkShare(const QString& dir)
{
#ifdef Q_OS_WIN
    if (dir.startsWith(QLatin1String("//")) || dir.startsWith(QLatin1String("\\\\")))
        return true;
    const QString root = QStorageInfo(dir).rootPath();
    return !root.isEmpty()
        && GetDriveTypeW(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(root).utf16()))
               == DRIVE_REMOTE;
#else
    static const QSet<QByteArray> kNetworkTypes{
        "nfs", "nfs4", "smbfs", "cifs", "smb2", "smb3", "afpfs", "webdav", "davfs",
        "fuse.sshfs", "sshfs", "9p", "ncpfs", "ceph", "glusterfs", "fuse.glusterfs"};
    return kNetworkTypes.contains(QStorageInfo(dir).fileSystemType().toLower());
#endif
}

} // namespace eMule
