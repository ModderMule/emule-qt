#include "pch.h"
/// @file SharedDirWatcher.cpp
/// @brief Tells the shared file list which shared directories changed on disk.

#include "files/SharedDirWatcher.h"
#include "utils/Log.h"

#include <QDir>
#include <QDirIterator>
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
    qRegisterMetaType<DirListing>();
    m_thread->setObjectName(QStringLiteral("SharedDirWatcher"));
    m_worker->moveToThread(m_thread);
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_worker, &SharedDirWatchWorker::directoryChanged,
            this, [this](const QString& dir) { onDirectoryEvent(dir); });
    connect(m_worker, &SharedDirWatchWorker::watched, this, &SharedDirWatcher::onWatched);
    connect(m_worker, &SharedDirWatchWorker::rewatched, this, &SharedDirWatcher::onRewatched);
    connect(m_worker, &SharedDirWatchWorker::probed, this, &SharedDirWatcher::onProbed);
    m_thread->start(QThread::LowPriority);

    m_flushTimer.setInterval(250);
    connect(&m_flushTimer, &QTimer::timeout, this, &SharedDirWatcher::flush);

    m_pollTimer.setInterval(m_pollMs);
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

void SharedDirWatcher::setBeforeProbeHook(std::function<void(const QString&)> hook)
{
    QMetaObject::invokeMethod(m_worker, [w = m_worker, hook = std::move(hook)] { w->beforeProbe = hook; });
}

void SharedDirWatcher::setLister(std::function<DirListing(const QString&)> lister)
{
    QMetaObject::invokeMethod(m_worker, [w = m_worker, lister = std::move(lister)] { w->lister = lister; });
}

void SharedDirWatcher::setPollInterval(int pollMs)
{
    m_pollMs = std::max(pollMs, 0);
    if (m_pollMs > 0) {
        m_pollTimer.setInterval(m_pollMs);   // restarts a running timer
        // roots that came while polling was off: list them now, later polls compare
        for (auto it = m_roots.begin(); it != m_roots.end(); ++it)
            if (isPolled(*it) && !it->listed && !it->probing)
                sendProbe(it.key(), *it);
    }
    updatePollTimer();
}

void SharedDirWatcher::setTimings(int settleMs, int maxHoldMs, int pollMs)
{
    m_settleMs = settleMs;
    m_maxHoldMs = maxHoldMs;
    m_flushTimer.setInterval(std::clamp(settleMs / 4, 10, 250));
    setPollInterval(pollMs);
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

void SharedDirWatchWorker::probe(const QString& dir, quint64 request)
{
    if (beforeProbe)
        beforeProbe(dir);
    const QDir d(dir);
    if (!d.exists()) {
        emit probed(dir, 0, {}, request);
        return;
    }
    DirListing listing = lister ? lister(dir) : listFiles(dir);
    // Order-free sum: the OS may list the same content in another order.
    // Subfolders count too, a share tree grows by them.
    quint64 fp = 1;
    for (const ListedFile& f : listing)
        fp += qHashMulti(1, f.info.absoluteFilePath(), f.info.size(),
                         f.info.lastModified().toMSecsSinceEpoch());
    for (const QString& sub : d.entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden))
        fp += qHash(sub, 2);
    fp = qHashMulti(fp, listing.size());
    emit probed(dir, fp != 0 ? fp : 1, listing, request);
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

DirListing SharedDirWatchWorker::listFiles(const QString& dir)
{
    DirListing out;
    QDirIterator it(dir, QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden);
    while (it.hasNext()) {
        it.next();
        ListedFile f{it.fileInfo(), {}};
        f.info.stat();   // cached for the receiver
        out.append(std::move(f));
    }
    return out;
}

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
    for (auto it = m_roots.begin(); it != m_roots.end(); ++it) {
        if (!isPolled(*it))
            continue;
        // a share that has not answered the last round is not asked again
        if (it->probing) {
            logDebug([&]() -> QString {
                return QStringLiteral("Shared directory still busy from the last poll, skipped: %1").arg(it.key());
            });
            continue;
        }
        // try the watch again: the directory may exist now, or the volume be back
        if (!it->watched)
            requestWatch(it.key(), *it, false);
        sendProbe(it.key(), *it);
    }
    sendWatches(false);
}

void SharedDirWatcher::requestWatch(const QString& dir, Root& root, bool catchUp)
{
    root.pending = true;
    root.catchUp = catchUp;
    root.request = m_nextRequest++;
    root.askedMs = QDateTime::currentMSecsSinceEpoch();
    m_toWatch.append({dir, root.request});
}

void SharedDirWatcher::sendProbe(const QString& dir, Root& root)
{
    root.probing = true;
    root.probeRequest = m_nextRequest++;
    QMetaObject::invokeMethod(m_worker, [w = m_worker, dir, req = root.probeRequest] {
        w->probe(dir, req);
    });
}

void SharedDirWatcher::onProbed(const QString& dir, quint64 fingerprint, const DirListing& listing,
                                quint64 request)
{
    const auto it = m_roots.find(dir);
    if (it == m_roots.end() || it->probeRequest != request)
        return;   // dropped or asked again since
    it->probing = false;
    const bool first = !it->listed;
    const bool same = it->fingerprint == fingerprint;
    it->listed = true;
    it->fingerprint = fingerprint;
    // The first listing is what the scan behind setRoots() saw: nothing to report.
    // The file dates count, so a file rewritten in place is a change too.
    if (first || same)
        return;
    if (fingerprint != 0)
        emit directoryListed(dir, listing);
    onDirectoryEvent(dir);
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
    // first listing of a polled root, the one later polls compare with
    if (first && isPolled(*it) && !it->listed && !it->probing && m_pollMs > 0)
        sendProbe(dir, *it);
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
    const bool anyPolled = m_pollMs > 0 && std::ranges::any_of(m_roots, &SharedDirWatcher::isPolled);
    if (anyPolled && !m_pollTimer.isActive())
        m_pollTimer.start();
    else if (!anyPolled)
        m_pollTimer.stop();
}

} // namespace eMule
