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
#include <QThread>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace eMule {

namespace {
// How long the destructor waits for a worker stuck in an OS call before leaving it.
constexpr unsigned long kStopWaitMs = 2000;
} // namespace

SharedDirWatcher::SharedDirWatcher(QObject* parent)
    : QObject(parent)
    , m_thread(new QThread)
    , m_worker(new SharedDirWatchWorker)
{
    m_thread->setObjectName(QStringLiteral("SharedDirWatcher"));
    m_worker->moveToThread(m_thread);
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_worker, &SharedDirWatchWorker::directoryChanged,
            this, [this](const QString& dir) { onDirectoryEvent(dir); });
    connect(m_worker, &SharedDirWatchWorker::watched, this, &SharedDirWatcher::onWatched);
    connect(m_worker, &SharedDirWatchWorker::rewatched, this, &SharedDirWatcher::onRewatched);
    m_thread->start(QThread::LowPriority);

    m_flushTimer.setInterval(250);
    connect(&m_flushTimer, &QTimer::timeout, this, &SharedDirWatcher::flush);

    m_pollTimer.setInterval(5 * 60 * 1000);
    connect(&m_pollTimer, &QTimer::timeout, this, &SharedDirWatcher::poll);
}

SharedDirWatcher::~SharedDirWatcher()
{
    disconnect(m_worker, nullptr, this, nullptr);
    // the thread deletes itself once done, so a stuck OS call does not hold up the owner
    connect(m_thread, &QThread::finished, m_thread, &QObject::deleteLater);
    m_thread->quit();
    if (!m_thread->wait(kStopWaitMs))
        logWarning(QStringLiteral("Shared directory watcher still busy in the OS, left to finish"));
}

void SharedDirWatcher::setRoots(const QStringList& dirs)
{
    QHash<QString, Root> next;
    for (const QString& dir : dirs) {
        if (dir.isEmpty() || next.contains(dir))
            continue;
        if (const auto it = m_roots.constFind(dir); it != m_roots.constEnd()) {
            next.insert(dir, *it);
            continue;
        }
        Root root;
        requestWatch(dir, root, true);
        next.insert(dir, root);
    }
    sendWatches(true);

    for (auto it = m_roots.constBegin(); it != m_roots.constEnd(); ++it) {
        if (!next.contains(it.key())) {
            if (it->watched || it->pending)
                QMetaObject::invokeMethod(m_worker, [w = m_worker, dir = it.key()] { w->unwatch(dir); });
            m_pending.remove(it.key());
        }
    }
    m_roots = std::move(next);
    updatePollTimer();
}

QStringList SharedDirWatcher::polledRoots() const
{
    QStringList out;
    for (auto it = m_roots.constBegin(); it != m_roots.constEnd(); ++it)
        if (isPolled(*it))
            out.append(it.key());
    return out;
}

bool SharedDirWatcher::watchesPending() const
{
    return std::ranges::any_of(m_roots, [](const Root& r) { return r.pending; });
}

void SharedDirWatcher::setRemoteCheck(std::function<bool(const QString&)> check)
{
    // queued like the watches, so it is in place before the next one runs
    QMetaObject::invokeMethod(m_worker, [w = m_worker, check = std::move(check)] { w->isRemote = check; });
}

void SharedDirWatcher::setBeforeAddHook(std::function<void(const QString&)> hook)
{
    QMetaObject::invokeMethod(m_worker, [w = m_worker, hook = std::move(hook)] { w->beforeAdd = hook; });
}

void SharedDirWatcher::setTimings(int settleMs, int maxHoldMs, int pollMs)
{
    m_settleMs = settleMs;
    m_maxHoldMs = maxHoldMs;
    m_flushTimer.setInterval(std::clamp(settleMs / 4, 10, 250));
    m_pollTimer.setInterval(pollMs);
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

// ---------------------------------------------------------------------------
// SharedDirWatchWorker (worker thread)
// ---------------------------------------------------------------------------

SharedDirWatchWorker::SharedDirWatchWorker(QObject* parent)
    : QObject(parent)
{
}

void SharedDirWatchWorker::watch(const QList<Request>& requests, bool checkRemote)
{
    const QStringList have = watcher()->directories();
    QStringList add;
    for (const Request& r : requests)
        if (!have.contains(r.dir) && !add.contains(r.dir))
            add.append(r.dir);
    for (const QString& dir : add)
        if (beforeAdd)
            beforeAdd(dir);
    // a missing directory fails too; the poll picks it up when it appears
    const QStringList failed = add.isEmpty() ? QStringList{} : m_watcher->addPaths(add);
    for (const Request& r : requests) {
        const bool remote = checkRemote
            && (isRemote ? isRemote(r.dir) : SharedDirWatcher::isOnNetworkShare(r.dir));
        emit watched(r.dir, !failed.contains(r.dir), remote, r.id);
    }
}

void SharedDirWatchWorker::unwatch(const QString& dir)
{
    if (watcher()->directories().contains(dir))
        watcher()->removePath(dir);
}

void SharedDirWatchWorker::rewatchIfLost(const QString& dir, quint64 request)
{
    if (watcher()->directories().contains(dir))
        return;
    if (beforeAdd)
        beforeAdd(dir);
    emit rewatched(dir, m_watcher->addPath(dir), request);
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

QFileSystemWatcher* SharedDirWatchWorker::watcher()
{
    // Made here, on the worker thread: on Windows the watcher hooks a drive listener
    // into the current thread's event dispatcher and unhooks it from whatever thread
    // deletes it. Made on the main thread, the hook outlived the worker and crashed
    // the main loop.
    if (!m_watcher) {
        m_watcher = new QFileSystemWatcher(this);
        connect(m_watcher, &QFileSystemWatcher::directoryChanged,
                this, &SharedDirWatchWorker::directoryChanged);
    }
    return m_watcher;
}

bool SharedDirWatcher::onDirectoryEvent(const QString& dir)
{
    if (!m_roots.contains(dir))
        return true;

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
        return false;
    }
    if (!m_flushTimer.isActive())
        m_flushTimer.start();
    return true;
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
        if (const auto it = m_roots.constFind(dir);
            it != m_roots.constEnd() && it->watched && !it->pending)
            QMetaObject::invokeMethod(m_worker, [w = m_worker, dir, req = it->request] {
                w->rewatchIfLost(dir, req);
            });
        emit directoryChanged(dir);
    }
    if (!due.isEmpty())
        emit directoriesChanged(due);
}

void SharedDirWatcher::poll()
{
    // Keys first: a report can overflow, and the reload behind it replaces
    // m_roots under a live iterator (#117).
    QStringList due;
    for (auto it = m_roots.begin(); it != m_roots.end(); ++it) {
        if (!isPolled(*it))
            continue;
        // try the watch again: the directory may exist now, or the volume be back
        if (!it->watched)
            requestWatch(it.key(), *it, false);
        due.append(it.key());
    }
    sendWatches(false);
    // Reported every time: the directory's own date does not move when a file
    // is rewritten in place, and the rescan this leads to only stats.
    for (const QString& dir : due)
        if (!onDirectoryEvent(dir))
            break;   // overflow: the full rescan covers the rest
}

void SharedDirWatcher::requestWatch(const QString& dir, Root& root, bool catchUp)
{
    root.pending = true;
    root.catchUp = catchUp;
    root.request = m_nextRequest++;
    root.askedMs = QDateTime::currentMSecsSinceEpoch();
    m_toWatch.append({dir, root.request});
}

void SharedDirWatcher::sendWatches(bool checkRemote)
{
    if (m_toWatch.isEmpty())
        return;
    QMetaObject::invokeMethod(m_worker, [w = m_worker, batch = std::exchange(m_toWatch, {}), checkRemote] {
        w->watch(batch, checkRemote);
    });
}

void SharedDirWatcher::onWatched(const QString& dir, bool ok, bool remote, quint64 request)
{
    const auto it = m_roots.find(dir);
    if (it == m_roots.end() || it->request != request)
        return;   // dropped or asked again since
    const bool first = it->catchUp;
    const qint64 tookMs = QDateTime::currentMSecsSinceEpoch() - it->askedMs;
    it->pending = false;
    it->catchUp = false;
    it->watched = ok;
    if (first)
        it->remote = remote;

    if (first) {
        if (!ok)
            logDebug([&]() -> QString { return QStringLiteral("Shared directory is polled, not watched: %1").arg(dir); });
        else if (it->remote)
            logDebug([&]() -> QString { return QStringLiteral("Shared directory on a network share is polled too: %1").arg(dir); });
    }
    if (tookMs > kSlowWatchMs)
        logDebug([&]() -> QString { return QStringLiteral("Watching shared directory took %1 ms: %2").arg(tookMs).arg(dir); });
    updatePollTimer();
    // changes made while the OS was busy setting up the watch were not seen
    if (first && ok && tookMs > kSlowWatchMs)
        onDirectoryEvent(dir);
}

void SharedDirWatcher::onRewatched(const QString& dir, bool ok, quint64 request)
{
    const auto it = m_roots.find(dir);
    if (it == m_roots.end() || it->request != request || it->pending)
        return;
    it->watched = ok;
    updatePollTimer();
}

void SharedDirWatcher::updatePollTimer()
{
    const bool anyPolled = std::ranges::any_of(m_roots, &SharedDirWatcher::isPolled);
    if (anyPolled && !m_pollTimer.isActive())
        m_pollTimer.start();
    else if (!anyPolled)
        m_pollTimer.stop();
}

} // namespace eMule
