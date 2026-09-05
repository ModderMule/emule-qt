#include "queue/UsenetQueue.h"

#include "nzb/NzbFile.h"
#include "post/UsenetUnpacker.h"
#include "queue/ArticleWriter.h"
#include "queue/UsenetQueueStore.h"

#include "app/AppContext.h"
#include "files/SharedFileList.h"
#include "prefs/Preferences.h"
#include "stats/Statistics.h"
#include "utils/Log.h"
#include "utils/OtherFunctions.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QThread>
#include <QTimer>
#include <QUuid>

#include <algorithm>

namespace eMule::usenet {

namespace {

/// Scheduler period. Fast enough that a freed connection is reused promptly,
/// slow enough that an idle queue costs nothing — the dispatch also runs on every
/// completed segment, so this timer only has to cover the "nothing came back"
/// case (every server blocked, every connection busy).
constexpr int kTickMs = 250;

/// How many times a *connection* fault is retried on the same level before the
/// segment is given up. Transport faults cure themselves — "too many connections"
/// is the commonest one — so this is generous.
constexpr int kMaxTransportRetries = 6;

/// Upper bound on worker threads. Beyond this the TLS and decode work is no
/// longer the constraint and the thread count is just context switching.
constexpr int kMaxWorkers = 8;

/// How long one stream request keeps an item ahead of the priority field. Long
/// enough that a player's gaps between Range requests do not drop the boost,
/// short enough that a closed player stops starving the rest of the queue
/// without anything having to notice it closed.
constexpr qint64 kStreamingBoostMs = 30000;

/// Backstop on the download -> verify -> download cycle. requestPar2Volumes()
/// returning false is the real terminator; this catches the case where it keeps
/// finding volumes that somehow do not improve the block count.
constexpr int kMaxPar2Rounds = 8;

/// Create @p path and its directory, empty. Returns false with @p error set.
[[nodiscard]] bool createTargetFile(const QString& path, QString& error)
{
    QDir().mkpath(QFileInfo(path).absolutePath());

    ArticleWriter writer;
    QString openError;
    if (!writer.open(path, openError)) {
        error = QObject::tr("Cannot create %1: %2").arg(path, openError);
        return false;
    }
    writer.close();
    return true;
}

} // namespace

UsenetQueue::UsenetQueue(QObject* parent)
    : QObject(parent)
{
    qRegisterMetaType<UsenetFetchRequest>("eMule::usenet::UsenetFetchRequest");
    qRegisterMetaType<UsenetFetchResult>("eMule::usenet::UsenetFetchResult");
    qRegisterMetaType<QList<eMule::NewsServer>>("QList<eMule::NewsServer>");
}

UsenetQueue::~UsenetQueue()
{
    stop();
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void UsenetQueue::start()
{
    if (m_running)
        return;
    m_running = true;

    // Restore before the workers exist, so nothing dispatches into a half-built
    // queue.
    for (const QString& path : UsenetQueueStore::listStateFiles()) {
        auto item = std::make_unique<UsenetQueueItem>();
        QString error;
        if (!UsenetQueueStore::load(path, *item, error)) {
            logWarning(QStringLiteral("Usenet: ignoring unreadable queue state %1: %2")
                           .arg(path, error));
            continue;
        }
        item->initFileStates(thePrefs.usenetTempDir());

        auto rt = std::make_unique<ItemRuntime>();
        rt->item = std::move(item);
        rebuildPlan(*rt);
        m_items.push_back(std::move(rt));
    }

    if (!m_items.empty()) {
        logInfo(QStringLiteral("Usenet: restored %1 queued item(s)").arg(int(m_items.size())));
    }

    startWorkers();
    startPostProcessor();

    if (!m_tickTimer) {
        m_tickTimer = new QTimer(this);
        m_tickTimer->setInterval(kTickMs);
        connect(m_tickTimer, &QTimer::timeout, this, &UsenetQueue::onTick);
    }
    m_tickTimer->start();

    dispatch();
}

void UsenetQueue::stop()
{
    if (!m_running)
        return;
    m_running = false;

    if (m_tickTimer)
        m_tickTimer->stop();

    stopWorkers();
    stopPostProcessor();

    for (auto& rt : m_items)
        cancelDirectUnpack(*rt);

    for (auto& rt : m_items)
        persist(*rt);
    m_items.clear();
}

void UsenetQueue::applyServers(const QList<NewsServer>& servers, int retryIntervalSec)
{
    m_servers = servers;
    m_retryIntervalSec = retryIntervalSec;

    m_maxLevel = 0;
    for (const auto& s : servers)
        m_maxLevel = std::max(m_maxLevel, s.level);

    if (!m_running)
        return;

    // A changed server list changes the worker count, and a worker cannot be
    // resized while it holds connections. Rebuild.
    stopWorkers();
    startWorkers();
    dispatch();
}

// ---------------------------------------------------------------------------
// Queue operations
// ---------------------------------------------------------------------------

QString UsenetQueue::addNzb(const QByteArray& data, const QString& name, QString& error)
{
    NzbInfo nzb;
    if (!NzbFile::parse(data, nzb, error))
        return {};

    if (nzb.isEmpty()) {
        error = tr("The NZB contains no files.");
        return {};
    }

    auto item = std::make_unique<UsenetQueueItem>();
    item->id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    item->name = name.isEmpty() ? nzb.name : name;
    if (item->name.isEmpty())
        item->name = tr("Usenet download");
    item->nzb = std::move(nzb);
    item->nzb.name = item->name;
    item->status = UsenetItemStatus::Queued;
    item->initFileStates(thePrefs.usenetTempDir());

    // Lay each target file out once, up front. Growing a file by seeking past its
    // end article by article is what fragments it badly, and the size is only
    // knowable from the NZB's encoded total until the first =ybegin arrives — so
    // this is a first approximation that sealFile() trims.
    //
    // Recovery volumes are skipped: they are not in the plan and mostly never
    // will be. Creating them anyway leaves a work directory full of zero-byte
    // .par2 files, which par2 then logs its way past on every verify and — worse
    // — which a release with no archive to unpack would publish as payload.
    // requestPar2Volumes() creates the ones it actually asks for.
    for (int i = 0; i < item->files.size(); ++i) {
        if (item->nzb.files.at(i).isPar2Volume())
            continue;
        if (!createTargetFile(item->files[i].tempPath, error))
            return {};
    }

    const QString id = item->id;
    auto rt = std::make_unique<ItemRuntime>();
    rt->item = std::move(item);
    rebuildPlan(*rt);
    persist(*rt);
    m_items.push_back(std::move(rt));

    logInfo(QStringLiteral("Usenet: queued \"%1\" (%2 file(s), %3 article(s))")
                .arg(m_items.back()->item->name)
                .arg(m_items.back()->item->nzb.files.size())
                .arg(m_items.back()->item->segmentCount()));

    emit itemAdded(id);
    emit itemChanged(id);
    dispatch();
    return id;
}

bool UsenetQueue::removeItem(const QString& id, bool deleteFiles)
{
    for (int i = 0; i < int(m_items.size()); ++i) {
        if (m_items.at(size_t(i))->item->id != id)
            continue;

        // Anything still in flight will come back referring to an item that is
        // gone; onSegmentFinished tolerates that by looking the id up again.
        //
        // A post-processing job is different: it is *reading and writing* the
        // directory about to be deleted, and it can hold the thread for minutes
        // inside one repair. Ask it to stop at its next checkpoint. The
        // processor is serial, so the running job is necessarily this item's.
        if (m_items.at(size_t(i))->postRunning && m_postProcessor)
            m_postProcessor->requestStop();

        // Same reasoning for an extraction still following this item's volumes,
        // except it is ours to join: it blocks in a read of a directory that is
        // about to be deleted.
        cancelDirectUnpack(*m_items.at(size_t(i)));

        if (deleteFiles) {
            const QString itemDir =
                QDir(thePrefs.usenetTempDir()).filePath(id);
            QDir(itemDir).removeRecursively();
        }
        UsenetQueueStore::remove(id);
        m_items.erase(m_items.begin() + i);

        emit itemRemoved(id);
        dispatch();
        return true;
    }
    return false;
}

bool UsenetQueue::pauseItem(const QString& id)
{
    ItemRuntime* rt = runtimeFor(id);
    if (!rt || !rt->item->isActive())
        return false;

    rt->item->status = UsenetItemStatus::Paused;
    // No more volumes are coming, so a run would block until resume. Drop it;
    // the set restarts from volume one when the download does, at disk speed.
    cancelDirectUnpack(*rt);
    persist(*rt);
    emit itemChanged(id);
    return true;
}

bool UsenetQueue::resumeItem(const QString& id)
{
    ItemRuntime* rt = runtimeFor(id);
    if (!rt)
        return false;
    if (rt->item->status != UsenetItemStatus::Paused
        && rt->item->status != UsenetItemStatus::Failed)
        return false;

    rt->item->status = UsenetItemStatus::Queued;
    rt->item->error.clear();

    // A failed item retries from the top of the ladder: whatever was wrong may
    // have been fixed, and keeping the old exclusions would skip the very server
    // the user just repaired.
    rt->attempts.clear();
    rebuildPlan(*rt);
    persist(*rt);

    emit itemChanged(id);
    dispatch();
    return true;
}

bool UsenetQueue::setItemPriority(const QString& id, int priority)
{
    ItemRuntime* rt = runtimeFor(id);
    if (!rt)
        return false;

    rt->item->priority = priority;
    persist(*rt);
    emit itemChanged(id);
    dispatch();
    return true;
}

QList<const UsenetQueueItem*> UsenetQueue::items() const
{
    QList<const UsenetQueueItem*> out;
    out.reserve(int(m_items.size()));
    for (const auto& rt : m_items)
        out.append(rt->item.get());
    return out;
}

const UsenetQueueItem* UsenetQueue::findItem(const QString& id) const
{
    for (const auto& rt : m_items) {
        if (rt->item->id == id)
            return rt->item.get();
    }
    return nullptr;
}

UsenetQueue::PreviewInfo UsenetQueue::previewability(const QString& itemId, int fileIndex)
{
    PreviewInfo out;

    ItemRuntime* rt = runtimeFor(itemId);
    if (!rt)
        return out;

    const UsenetQueueItem& item = *rt->item;
    if (fileIndex < 0 || fileIndex >= item.files.size() || fileIndex >= item.nzb.files.size())
        return out;

    const UsenetFileState& st = item.files.at(fileIndex);

    // A media file posted raw needs no container work at all — that is phase 6a,
    // and answering it here keeps the index out of the hot path entirely.
    if (item.isFilePreviewable(fileIndex)) {
        out.previewable = st.availableEnd() > 0;
        return out;
    }

    const QString name = st.articleFileName.isEmpty() ? item.nzb.files.at(fileIndex).fileName
                                                      : st.articleFileName;
    if (name.isEmpty() || !UsenetUnpacker::isArchiveVolume(name))
        return out;

    // From here it is an archive volume, so the index decides. resolve() reads
    // headers but never fetches, and its refusals are cached — this is called
    // once per file on every queue push.
    const StreamResolve resolved = rt->streamIndex.resolve(item, fileIndex, 0);
    if (resolved.plan == StreamPlan::NotSeekable) {
        out.note = resolved.reason;
        return out;
    }
    if (resolved.plan != StreamPlan::Ready)
        return out;   // still reading headers; ask again next push

    const ED2KFileType type = getED2KFileTypeID(resolved.fileName);
    if (type != ED2KFileType::Video && type != ED2KFileType::Audio) {
        out.note = tr("The archive does not contain a playable file");
        return out;
    }

    out.previewable = availableFrom(item, resolved.extents, 0) > 0;
    return out;
}

UsenetQueue::StreamInfo UsenetQueue::requestStream(const QString& itemId, int fileIndex,
                                                   qint64 wantOffset, qint64 wantLength)
{
    StreamInfo info;

    ItemRuntime* rt = runtimeFor(itemId);
    if (!rt)
        return info;

    const UsenetQueueItem& item = *rt->item;
    if (fileIndex < 0 || fileIndex >= item.files.size())
        return info;

    info.found = true;

    // Register the interest first, and unconditionally within an active item:
    // every branch below either serves bytes or asks for some, and both want the
    // item ahead of an unattended download for the next kStreamingBoostMs.
    const bool active = item.isActive();
    if (active)
        rt->streamingUntilMs = QDateTime::currentMSecsSinceEpoch() + kStreamingBoostMs;

    const StreamResolve resolved = rt->streamIndex.resolve(item, fileIndex, wantOffset);

    switch (resolved.plan) {
    case StreamPlan::NotSeekable:
        // Nothing to fetch and nothing to wait for. Saying so beats holding the
        // request open for the full poll window and then refusing anyway.
        info.notSeekableReason = resolved.reason;
        return info;

    case StreamPlan::NeedBytes:
        // The index is missing a header or a declared size. Ask for exactly that
        // range; the caller's poll brings us back here a tick later.
        if (active) {
            promoteRange(*rt, resolved.needFileIndex, resolved.needOffset, resolved.needLength);
            dispatch();
        }
        return info;

    case StreamPlan::Unknown:
        return info;

    case StreamPlan::Ready:
        break;
    }

    info.fileName = resolved.fileName;
    info.totalSize = resolved.totalSize;
    info.availableEnd = availableFrom(item, resolved.extents, wantOffset);

    bool everyPieceFinal = !resolved.extents.isEmpty();
    for (const StreamExtent& e : resolved.extents) {
        if (e.fileIndex < 0 || e.fileIndex >= item.files.size())
            continue;
        const UsenetFileState& st = item.files.at(e.fileIndex);

        // finalPath first: once the item is published the scratch file is gone,
        // and a completed release is the case where preview is least interesting
        // but most likely to be asked for by a stale URL.
        const QString path = !st.finalPath.isEmpty() ? st.finalPath : st.tempPath;
        if (path.isEmpty())
            continue;
        info.pieces.append({path, e.virtualOffset, e.fileOffset, e.length});
        everyPieceFinal = everyPieceFinal && st.finalized;
    }
    info.complete = everyPieceFinal;

    if (active) {
        // Ask for what was requested, mapped back through the extents onto the
        // volumes that hold it. A seek to 80% therefore promotes the articles of
        // one volume and leaves everything before it alone.
        const qint64 span = wantLength > 0 ? wantLength : 1;
        qint64 cursor = wantOffset;
        const qint64 limit = wantOffset + span;
        for (const StreamExtent& e : resolved.extents) {
            if (e.virtualOffset + e.length <= cursor)
                continue;
            if (e.virtualOffset >= limit)
                break;
            const qint64 from = qMax(cursor, e.virtualOffset);
            const qint64 to = qMin(limit, e.virtualOffset + e.length);
            if (to <= from)
                continue;
            promoteRange(*rt, e.fileIndex, e.fileOffset + (from - e.virtualOffset), to - from);
            cursor = to;
        }
        dispatch();
    }

    return info;
}

QList<int> UsenetQueue::segmentsCovering(const UsenetQueueItem& item, int fileIndex,
                                         qint64 offset, qint64 length)
{
    if (fileIndex < 0 || fileIndex >= item.files.size() || fileIndex >= item.nzb.files.size())
        return {};

    const UsenetFileState& st = item.files.at(fileIndex);
    const NzbFileInfo& info = item.nzb.files.at(fileIndex);
    if (info.segments.isEmpty() || length <= 0)
        return {};

    // A volume nobody has touched has no part length of its own — and a seek
    // into one is exactly the case phase 6b exists for. Borrow a sibling's: the
    // volumes of a release come out of one posting run with one part size, so
    // this is not an approximation in practice, and the ±1 probe below absorbs
    // it if some release ever proves otherwise.
    qint64 partLength = st.partLength;
    if (partLength <= 0) {
        for (const UsenetFileState& other : item.files)
            partLength = qMax(partLength, other.partLength);
    }
    if (partLength <= 0)
        return {};

    const qint64 from = qMax<qint64>(0, offset);
    const qint64 to = from + length - 1;

    // §7.1: parts are uniform with a short remainder, so the part number is
    // arithmetic. One either side covers a poster who padded differently — the
    // doc's "probe ±1", paid up front because an extra article costs far less
    // than another round trip through the poll loop.
    // Part numbers are 1-based, so the part holding byte B is B/partLength + 1.
    const int firstPart = int(from / partLength) + 1 - 1;   // one early
    const int lastPart = int(to / partLength) + 1 + 1;      // one late

    QList<int> out;
    for (int i = 0; i < info.segments.size(); ++i) {
        const int number = info.segments.at(i).number;
        if (number >= firstPart && number <= lastPart)
            out.append(i);
    }
    return out;
}

void UsenetQueue::promoteRange(ItemRuntime& rt, int fileIndex, qint64 offset, qint64 length)
{
    const QList<int> wanted = segmentsCovering(*rt.item, fileIndex, offset, length);
    if (wanted.isEmpty())
        return;

    const UsenetFileState& st = rt.item->files.at(fileIndex);

    QList<quint64> keys;
    for (const int segmentIndex : wanted) {
        if (segmentIndex < st.done.size() && st.done.testBit(segmentIndex))
            continue;
        const quint64 key = SegmentKey{fileIndex, segmentIndex}.packed();
        if (rt.inFlight.contains(key))
            continue;
        keys.append(key);
    }
    if (keys.isEmpty())
        return;

    for (const quint64 key : std::as_const(keys)) {
        const qsizetype at = rt.plan.indexOf(key);
        if (at < 0)
            continue;
        rt.plan.removeAt(at);
        if (at < rt.planCursor)
            --rt.planCursor;
    }

    // Behind the cursor, so the next dispatch() round hands these out first and
    // then carries on reading ahead from the same point.
    rt.planCursor = qBound(0, rt.planCursor, int(rt.plan.size()));
    const int insertAt = rt.planCursor;
    for (qsizetype k = keys.size() - 1; k >= 0; --k) {
        if (!rt.plan.contains(keys.at(k)))
            rt.plan.insert(insertAt, keys.at(k));
    }
}

void UsenetQueue::setRateLimit(qint64 bytesPerSecond)
{
    m_rateLimit = std::max<qint64>(0, bytesPerSecond);

    const int workerCount = m_workers.size();
    if (workerCount <= 0)
        return;

    const qint64 share = m_rateLimit <= 0
                             ? 0
                             : std::max<qint64>(1, m_rateLimit / workerCount);
    for (UsenetWorker* w : m_workers) {
        QMetaObject::invokeMethod(w, "setRateLimit", Qt::QueuedConnection,
                                  Q_ARG(qint64, share));
    }
}

bool UsenetQueue::hasActiveDownloads() const
{
    for (const auto& rt : m_items) {
        if (rt->item->isActive())
            return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Private — workers
// ---------------------------------------------------------------------------

void UsenetQueue::startWorkers()
{
    int totalConnections = 0;
    for (const auto& s : m_servers) {
        if (s.enabled && s.isValid())
            totalConnections += std::max(0, s.maxConnections);
    }
    if (totalConnections <= 0)
        return;

    // One worker per connection at most: four threads sharing two connections is
    // two idle threads and a needlessly divided budget.
    const int ideal = std::max(1, QThread::idealThreadCount());
    const int workerCount = std::clamp(std::min(ideal, totalConnections), 1, kMaxWorkers);

    for (int i = 0; i < workerCount; ++i) {
        auto* thread = new QThread;
        thread->setObjectName(QStringLiteral("UsenetWorker%1").arg(i));

        auto* worker = new UsenetWorker(i);
        worker->moveToThread(thread);

        // The worker outlives quit() only long enough to be deleted inside its own
        // thread. A socket destroyed from another thread is a crash, not a leak.
        connect(thread, &QThread::finished, worker, &QObject::deleteLater);

        connect(worker, &UsenetWorker::segmentFinished,
                this, &UsenetQueue::onSegmentFinished, Qt::QueuedConnection);
        connect(worker, &UsenetWorker::capacityChanged,
                this, &UsenetQueue::onCapacityChanged, Qt::QueuedConnection);

        m_threads.append(thread);
        m_workers.append(worker);
        m_workerCapacity.append(0);
        m_workerInFlight.append(0);

        thread->start();
    }

    // Divide the connection budget rather than replicating it. N workers each
    // honouring maxConnections would open N times what the user configured, and
    // exceeding a provider's limit gets an account throttled or suspended.
    for (int i = 0; i < workerCount; ++i) {
        QList<NewsServer> slice;
        slice.reserve(m_servers.size());
        for (const NewsServer& s : m_servers) {
            NewsServer copy = s;
            const int base = std::max(0, s.maxConnections) / workerCount;
            const int remainder = std::max(0, s.maxConnections) % workerCount;
            copy.maxConnections = base + (i < remainder ? 1 : 0);
            if (copy.maxConnections > 0)
                slice.append(copy);
        }
        QMetaObject::invokeMethod(m_workers.at(i), "setServers", Qt::QueuedConnection,
                                  Q_ARG(QList<eMule::NewsServer>, slice),
                                  Q_ARG(int, m_retryIntervalSec));
    }

    setRateLimit(m_rateLimit);

    logInfo(QStringLiteral("Usenet: %1 worker thread(s) over %2 connection(s)")
                .arg(workerCount)
                .arg(totalConnections));
}

void UsenetQueue::stopWorkers()
{
    for (UsenetWorker* w : m_workers) {
        // Blocking, so every socket is torn down inside its own thread before the
        // event loop that owns it stops.
        QMetaObject::invokeMethod(w, "shutdown", Qt::BlockingQueuedConnection);
    }
    for (QThread* t : m_threads) {
        t->quit();
        t->wait();
        delete t;
    }

    m_threads.clear();
    m_workers.clear();
    m_workerCapacity.clear();
    m_workerInFlight.clear();

    for (auto& rt : m_items) {
        rt->inFlight.clear();
        rt->planCursor = 0;
    }
}

void UsenetQueue::startPostProcessor()
{
    if (m_postThread)
        return;

    qRegisterMetaType<UsenetPostJob>("eMule::usenet::UsenetPostJob");
    qRegisterMetaType<UsenetPostResult>("eMule::usenet::UsenetPostResult");

    m_postThread = new QThread;
    m_postThread->setObjectName(QStringLiteral("UsenetPostProcessor"));

    m_postProcessor = new UsenetPostProcessor;
    m_postProcessor->moveToThread(m_postThread);

    connect(m_postThread, &QThread::finished, m_postProcessor, &QObject::deleteLater);
    connect(m_postProcessor, &UsenetPostProcessor::stageChanged,
            this, &UsenetQueue::onPostStage, Qt::QueuedConnection);
    connect(m_postProcessor, &UsenetPostProcessor::finished,
            this, &UsenetQueue::onPostFinished, Qt::QueuedConnection);

    m_postThread->start();
}

void UsenetQueue::stopPostProcessor()
{
    if (!m_postThread)
        return;

    // The stop flag first, and not through the event loop: a repair can hold the
    // thread for minutes inside one Process() call, so a queued message would
    // not be looked at until it finished. requestStop() is atomic for exactly
    // this — it is the one piece of state the module shares across threads.
    if (m_postProcessor)
        m_postProcessor->requestStop();

    m_postThread->quit();
    m_postThread->wait();
    delete m_postThread;

    m_postThread = nullptr;
    m_postProcessor = nullptr;   // deleted by the finished -> deleteLater above
}

void UsenetQueue::setPostProcessingOptions(const PostProcessingOptions& options)
{
    m_par2Enabled = options.par2;
    m_renameEnabled = options.rename;
    m_unpackEnabled = options.unpack;
    m_cleanupEnabled = options.cleanup;
    m_directUnpackEnabled = options.directUnpack;
}

void UsenetQueue::onCapacityChanged(int workerIndex, int capacity)
{
    if (workerIndex >= 0 && workerIndex < m_workerCapacity.size()) {
        m_workerCapacity[workerIndex] = capacity;
        dispatch();
    }
}

// ---------------------------------------------------------------------------
// Private — scheduling
// ---------------------------------------------------------------------------

void UsenetQueue::rebuildPlan(ItemRuntime& rt)
{
    rt.plan.clear();
    rt.planCursor = 0;

    const UsenetQueueItem& item = *rt.item;

    // Two passes, PAR2 last: the index .par2 is small and only interesting if
    // something came up short, so there is no reason to spend a connection on it
    // first.
    //
    // Recovery volumes are a stronger case and are not scheduled at all. They
    // are typically a tenth of a release and are pure waste on a healthy one, so
    // isPlanned() keeps them out until a verify says how many are needed and
    // requestPar2Volumes() puts exactly those into requestedPar2.
    for (const bool par2Pass : {false, true}) {
        for (int f = 0; f < item.nzb.files.size(); ++f) {
            if (item.nzb.files.at(f).isPar2() != par2Pass)
                continue;
            if (f >= item.files.size())
                continue;
            if (!isPlanned(rt, f))
                continue;

            const UsenetFileState& st = item.files.at(f);
            const auto& segments = item.nzb.files.at(f).segments;

            // In part-number order, not document order. An NZB is free to list
            // its <segment> elements in any order and plenty do; NzbInfo only
            // sorts inside hasAllSegments(). Fetching in part order is what
            // makes the written prefix grow from byte 0, which is the whole of
            // what streaming needs.
            //
            // The *indices* are sorted, never the segment list itself: `done`
            // is a bit per index into that list, so reordering it would
            // silently reattribute every bit in the release.
            QList<int> order;
            order.reserve(segments.size());
            for (int s = 0; s < segments.size(); ++s) {
                if (s < st.done.size() && st.done.testBit(s))
                    continue;
                order.append(s);
            }
            std::stable_sort(order.begin(), order.end(), [&segments](int a, int b) {
                return segments.at(a).number < segments.at(b).number;
            });

            for (const int s : std::as_const(order))
                rt.plan.append(SegmentKey{f, s}.packed());
        }
    }
}

void UsenetQueue::dispatch()
{
    if (!m_running || m_workers.isEmpty())
        return;

    // A dispatch round that found nothing leasable stays parked until the next
    // tick clears this. Without it, every "no connection available" result would
    // trigger another identical round.
    if (m_starved)
        return;

    // Highest priority first, then insertion order. Sorting the view rather than
    // m_items keeps the queue's own order stable for the GUI.
    QList<ItemRuntime*> order;
    order.reserve(qsizetype(m_items.size()));
    for (auto& rt : m_items) {
        // postRunning as well as isActive(): the status flips synchronously in
        // beginPostProcessing(), but the flag is what makes it impossible for a
        // second job to be queued for the same item while the first is out.
        if (rt->item->isActive() && !rt->postRunning)
            order.append(rt.get());
    }
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    std::stable_sort(order.begin(), order.end(), [nowMs](ItemRuntime* a, ItemRuntime* b) {
        // An item somebody is watching outranks the priority field. Preview is
        // a real-time deadline and the user is staring at the result; a normal
        // download is not and they are not.
        const bool as = a->streamingUntilMs > nowMs;
        const bool bs = b->streamingUntilMs > nowMs;
        if (as != bs)
            return as;
        return a->item->priority > b->item->priority;
    });

    for (int w = 0; w < m_workers.size(); ++w) {
        while (m_workerInFlight.at(w) < m_workerCapacity.at(w)) {
            bool dispatched = false;

            for (ItemRuntime* rt : order) {
                while (rt->planCursor < rt->plan.size()) {
                    const quint64 key = rt->plan.at(rt->planCursor);
                    const int fileIndex = int(key >> 32);
                    const int segIndex = int(key & 0xFFFFFFFFu);

                    if (rt->inFlight.contains(key)) {
                        ++rt->planCursor;
                        continue;
                    }
                    if (fileIndex >= rt->item->files.size()) {
                        ++rt->planCursor;
                        continue;
                    }
                    const UsenetFileState& st = rt->item->files.at(fileIndex);
                    if (segIndex < st.done.size() && st.done.testBit(segIndex)) {
                        ++rt->planCursor;
                        continue;
                    }

                    const NzbFileInfo& info = rt->item->nzb.files.at(fileIndex);
                    const SegmentAttempt attempt = rt->attempts.value(key);

                    UsenetFetchRequest req;
                    req.itemId = rt->item->id;
                    req.fileIndex = fileIndex;
                    req.segmentIndex = segIndex;
                    req.segment = info.segments.at(segIndex);
                    req.targetPath = st.tempPath;
                    req.group = info.groups.isEmpty() ? QString() : info.groups.first();
                    req.level = attempt.level;
                    req.ignoreServers = attempt.tried;

                    rt->inFlight.insert(key);
                    ++rt->planCursor;

                    if (rt->item->status == UsenetItemStatus::Queued) {
                        rt->item->status = UsenetItemStatus::Downloading;
                        emit itemChanged(rt->item->id);
                    }

                    m_workerInFlight[w] += 1;
                    QMetaObject::invokeMethod(
                        m_workers.at(w), "fetchSegment", Qt::QueuedConnection,
                        Q_ARG(eMule::usenet::UsenetFetchRequest, req));

                    dispatched = true;
                    break;
                }
                if (dispatched)
                    break;
            }

            if (!dispatched)
                return;   // nothing left to hand out to any worker
        }
    }
}

void UsenetQueue::onTick()
{
    m_starved = false;

    m_currentRate = m_bytesThisTick * 1000 / kTickMs;
    m_bytesThisTick = 0;

    for (auto& rt : m_items) {
        if (rt->dirty) {
            persist(*rt);
            emit itemChanged(rt->item->id);
        }
    }

    // A segment that found every server blocked comes back immediately and its
    // plan cursor has already moved past it, so a rewind is what actually retries
    // it once the backoff expires.
    for (auto& rt : m_items) {
        if (!rt->item->isActive() || !rt->inFlight.isEmpty())
            continue;
        if (rt->planCursor < rt->plan.size())
            continue;
        // Only worth rewinding if something is still unresolved. Without this an
        // item stuck for another reason — no incoming directory, say — would
        // rebuild a 10 000-entry plan four times a second forever.
        //
        // isPlanned() rather than a bare loop over files: the recovery volumes
        // nobody asked for are never finalized, so counting them here would make
        // *every* finished item look pending and rebuild its plan forever.
        bool pending = false;
        for (int f = 0; f < rt->item->files.size(); ++f) {
            if (isPlanned(*rt, f) && !rt->item->files.at(f).finalized) {
                pending = true;
                break;
            }
        }

        if (pending) {
            rebuildPlan(*rt);
        } else {
            // Nothing left to fetch. Normally checkFileCompletion() has already
            // moved the item on, but a restored one arrives with every file
            // sealed and no segment event coming to trigger that — so this is
            // what resumes post-processing after a restart.
            checkItemCompletion(*rt);
        }
    }

    dispatch();
}

// ---------------------------------------------------------------------------
// Private — results
// ---------------------------------------------------------------------------

void UsenetQueue::onSegmentFinished(const UsenetFetchResult& result)
{
    if (result.workerIndex >= 0 && result.workerIndex < m_workerInFlight.size()
        && m_workerInFlight.at(result.workerIndex) > 0) {
        m_workerInFlight[result.workerIndex] -= 1;
    }

    ItemRuntime* rt = runtimeFor(result.itemId);
    if (!rt) {
        // Removed while in flight. Nothing to record.
        dispatch();
        return;
    }

    const quint64 key = SegmentKey{result.fileIndex, result.segmentIndex}.packed();
    rt->inFlight.remove(key);

    if (result.error == NntpError::None) {
        markSegmentDone(*rt, result);
    } else if (result.noServerAvailable) {
        // Nothing was leasable. Put the segment back untouched — no retry spent,
        // no level moved — and wait for the tick. Re-dispatching here instead
        // would spin: the next attempt would fail the same way, immediately.
        //
        // Back at the cursor, not at the tail, for the same reason as the retry
        // in handleSegmentFailure — and appending here silently undoes that one.
        // A dropped connection backs its server off, so the retry comes straight
        // back as "nothing leasable", and a tail append then moves it behind
        // every segment it was supposed to jump ahead of.
        //
        // It cannot block the rest of the plan: dispatch() advances the cursor
        // as it hands a segment out, so the same round goes on to the next one,
        // and m_starved is what holds this to one attempt per tick.
        rt->plan.insert(qBound(0, rt->planCursor, int(rt->plan.size())),
                        SegmentKey{result.fileIndex, result.segmentIndex}.packed());
        m_starved = true;
        return;
    } else {
        handleSegmentFailure(*rt, result);
    }

    dispatch();
}

void UsenetQueue::markSegmentDone(ItemRuntime& rt, const UsenetFetchResult& result)
{
    if (result.fileIndex < 0 || result.fileIndex >= rt.item->files.size())
        return;

    UsenetFileState& st = rt.item->files[result.fileIndex];
    if (result.segmentIndex >= 0 && result.segmentIndex < st.done.size())
        st.done.setBit(result.segmentIndex);

    st.decodedBytes += result.decodedBytes;

    // Where those bytes actually landed. This is the only record of it: `done`
    // above means *resolved*, and a segment missing on every server sets that
    // bit having written nothing at all.
    st.addWritten(result.decodedOffset, result.decodedBytes);

    // The largest article seen is the part length: a poster cuts a file into
    // equal parts and one short remainder. Phase 6b maps an unfetched byte to
    // its article with it, and there is no other source for the number.
    st.partLength = qMax(st.partLength, result.decodedBytes);

    if (st.articleFileName.isEmpty() && !result.articleFileName.isEmpty())
        st.articleFileName = result.articleFileName;
    if (st.declaredSize == 0 && result.declaredFileSize > 0)
        st.declaredSize = result.declaredFileSize;

    rt.attempts.remove(SegmentKey{result.fileIndex, result.segmentIndex}.packed());
    rt.dirty = true;

    m_bytesThisTick += result.decodedBytes;

    // Usenet bytes are real received bytes and belong in the session total the
    // same as ED2K's. The counter is std::atomic, so this is safe wherever it
    // runs — though in practice this is the daemon thread.
    if (theApp.statistics && result.decodedBytes > 0)
        theApp.statistics->addSessionReceivedBytes(quint64(result.decodedBytes));

    checkFileCompletion(rt, result.fileIndex);
}

void UsenetQueue::handleSegmentFailure(ItemRuntime& rt, const UsenetFetchResult& result)
{
    const quint64 key = SegmentKey{result.fileIndex, result.segmentIndex}.packed();
    SegmentAttempt attempt = rt.attempts.value(key);

    if (escalatesToNextLevel(result.error)) {
        // This account does not have the article. Move up the ladder and never ask
        // it again for this one.
        if (!result.serverKey.isEmpty() && !attempt.tried.contains(result.serverKey))
            attempt.tried.append(result.serverKey);
        attempt.level += 1;

        if (attempt.level > maxFailoverLevel()) {
            // Genuinely missing everywhere. The file is short; PAR2 repair in
            // phase 4 is what will rescue it. Do not fail the whole item — a
            // release with one dead article is usually still repairable.
            if (result.fileIndex >= 0 && result.fileIndex < rt.item->files.size())
                rt.item->files[result.fileIndex].missingSegments += 1;

            if (result.fileIndex >= 0 && result.fileIndex < rt.item->files.size()) {
                UsenetFileState& st = rt.item->files[result.fileIndex];
                if (result.segmentIndex >= 0 && result.segmentIndex < st.done.size())
                    st.done.setBit(result.segmentIndex);   // stop retrying it
            }
            rt.attempts.remove(key);
            rt.dirty = true;
            logWarning(QStringLiteral("Usenet: article %1 is missing on every server")
                           .arg(result.messageId));
            checkFileCompletion(rt, result.fileIndex);
            return;
        }
    } else {
        // A connection fault, not a content one. Stay on this level — the worker
        // has already backed the server off, so a sibling account picks it up.
        attempt.transportRetries += 1;
        if (attempt.transportRetries > kMaxTransportRetries) {
            rt.item->status = UsenetItemStatus::Failed;
            rt.item->error = result.text.isEmpty() ? describeNntpError(result.error)
                                                   : result.text;
            rt.attempts.remove(key);
            rt.dirty = true;
            persist(rt);
            emit itemChanged(rt.item->id);
            emit itemFinished(rt.item->id, false, rt.item->error);
            return;
        }
    }

    rt.attempts.insert(key, attempt);

    // The cursor has already passed this segment, so put it back in the plan —
    // at the cursor, so it is the *next* thing dispatched rather than the last.
    //
    // Appending it to the tail is what the first cut did, and it pins the
    // written prefix at this byte for the rest of the download: every later
    // segment lands, and the one hole the player is waiting on is refetched
    // only once everything else is finished. Streaming aside, retrying near the
    // front also finishes files sooner.
    //
    // This does not weaken the retry bound. Termination is still whatever
    // handleSegmentFailure decided above: the level ladder, kMaxTransportRetries,
    // or the missing-everywhere path that sets the done bit and returns before
    // reaching here. Only the position changed.
    rt.plan.insert(qBound(0, rt.planCursor, int(rt.plan.size())), key);
}

void UsenetQueue::checkFileCompletion(ItemRuntime& rt, int fileIndex)
{
    if (fileIndex < 0 || fileIndex >= rt.item->files.size())
        return;

    UsenetFileState& st = rt.item->files[fileIndex];
    if (st.finalized || !st.allSegmentsDone())
        return;

    sealFile(rt, fileIndex);
    pumpDirectUnpack(rt, fileIndex);
    checkItemCompletion(rt);
}

bool UsenetQueue::isPlanned(const ItemRuntime& rt, int fileIndex)
{
    if (fileIndex < 0 || fileIndex >= rt.item->nzb.files.size())
        return false;

    // A recovery volume counts only once somebody has asked for it. Everything
    // else — payload and the index .par2 alike — is always in the plan.
    if (!rt.item->nzb.files.at(fileIndex).isPar2Volume())
        return true;
    return rt.item->requestedPar2.contains(fileIndex);
}

void UsenetQueue::sealFile(ItemRuntime& rt, int fileIndex)
{
    UsenetFileState& st = rt.item->files[fileIndex];

    // The streaming index caches volume paths and parsed headers, and this
    // renames the file out from under both. A stale path is indistinguishable
    // from a missing one, so drop the lot rather than try to patch it.
    rt.streamIndex.invalidate();

    // Pad the file out to the length yEnc declared for it.
    //
    // A missing article leaves a hole, and PAR2 recovers a hole perfectly well
    // — it slides a window over the file and matches blocks by CRC. What it
    // cannot do anything with is a file that is simply *short*, which is what a
    // missing article at the end of a file produces: every block after the hole
    // sits at the wrong offset and the release reads as unrecoverable.
    //
    // declaredSize arrives with the first article that turns up, so it is zero
    // only when a file is missing its opening article too. Nothing can be
    // inferred in that case — the NZB's own `bytes` is the *encoded* size — so
    // the file is left as it is and verification will call it damaged, which is
    // the honest answer.
    if (st.declaredSize > 0) {
        QFile f(st.tempPath);
        if (f.open(QIODevice::ReadWrite)) {
            if (f.size() < st.declaredSize)
                f.resize(st.declaredSize);
            f.close();
        }
    }

    // Drop the .usenetpart suffix and take the real filename, still inside the
    // item's scratch directory.
    //
    // Everything downstream works by extension: par2 finds its own set by name,
    // the unpacker picks volume one out of ".part01.rar", and neither can see
    // anything through a ".usenetpart" tail. Post-processing therefore needs the
    // real names before it runs, and it runs before anything is published.
    //
    // That gives up guard 2 of the four that keep Usenet scratch off the network
    // — the by-name suffix skip — for these files, deliberately. The two that
    // matter still hold: shouldBeShared() refuses the whole usenetTempDir() tree
    // whatever a file is called, and the share scan does not recurse into it in
    // the first place. Guard 2 goes on doing its real job at the other end, on
    // the staged copy inside the incoming directory.
    const NzbFileInfo& info = rt.item->nzb.files.at(fileIndex);
    QString name = st.articleFileName;
    if (name.isEmpty())
        name = info.fileName;
    if (name.isEmpty())
        name = QStringLiteral("%1-%2").arg(rt.item->name).arg(fileIndex);
    name = QFileInfo(name).fileName();       // never let an NZB choose a directory

    const QString itemDir = QFileInfo(st.tempPath).absolutePath();
    const QString sealedPath = uniqueDestination(itemDir, name);

    if (st.tempPath != sealedPath) {
        if (QFile::rename(st.tempPath, sealedPath)) {
            st.tempPath = sealedPath;
        } else {
            logWarning(QStringLiteral("Usenet: cannot name \"%1\" in the work folder; "
                                      "post-processing may not recognise it").arg(name));
        }
    }

    st.finalized = true;
    rt.dirty = true;

    // The file is now its declared length on disk, so the whole of it is
    // readable — holes included, as zeros. Collapsing the interval list says
    // exactly that and keeps a completed file's state to one entry.
    //
    // A hole is not a lie here: the bytes exist and every later byte is at its
    // right offset, which is what a player needs. PAR2 repair is what turns the
    // zeros back into content. With declaredSize unknown nothing was padded, so
    // the ranges stay as they are.
    if (st.declaredSize > 0) {
        st.written.clear();
        st.written.append({qint64(0), st.declaredSize});
    }

    if (st.missingSegments > 0) {
        logInfo(QStringLiteral("Usenet: assembled \"%1\" with %2 article(s) missing")
                    .arg(QFileInfo(st.tempPath).fileName())
                    .arg(st.missingSegments));
    }
}

void UsenetQueue::checkItemCompletion(ItemRuntime& rt)
{
    if (rt.postRunning)
        return;

    // "Every *planned* file is finished", not every file. The recovery volumes
    // deliberately left out of the plan must not hold the item open forever.
    for (int f = 0; f < rt.item->files.size(); ++f) {
        if (!isPlanned(rt, f))
            continue;
        if (!rt.item->files.at(f).finalized)
            return;
    }

    // Every volume is in. Tell the extractions so, then wait for them: a run
    // still reading is about to produce exactly what post-processing would
    // otherwise redo, and its result has to be in the job.
    endDirectUnpackSets(rt);
    for (const DirectUnpackRun& run : std::as_const(rt.directUnpack)) {
        if (run.running)
            return;   // onDirectUnpackFinished() comes back here
    }

    beginPostProcessing(rt);
}

// ---------------------------------------------------------------------------
// Direct unpack — extraction that keeps pace with the download
//
// A volume is complete the moment its last article lands, and libarchive reads
// a set front to back, so the extraction can simply follow the download instead
// of starting after it. What arrives here is one sealed volume; the run that is
// following that set takes it and carries on.
// ---------------------------------------------------------------------------

void UsenetQueue::pumpDirectUnpack(ItemRuntime& rt, int fileIndex)
{
    if (!m_directUnpackEnabled || !m_unpackEnabled)
        return;
    if (fileIndex < 0 || fileIndex >= rt.item->files.size())
        return;

    const UsenetFileState& st = rt.item->files.at(fileIndex);
    const auto position = UsenetUnpacker::volumePositionOf(QFileInfo(st.tempPath).fileName());
    if (position.index < 0)
        return;   // not an archive volume; nothing to follow

    // A hole is padded with zeros by sealFile(), which decompresses into
    // garbage or a CRC failure. Stop the set here and let PAR2 do its job; the
    // end-of-download unpack will run on the repaired volumes.
    if (st.missingSegments > 0) {
        auto it = rt.directUnpack.find(position.baseName);
        if (it != rt.directUnpack.end() && it->running && it->worker)
            it->worker->cancel();
        return;
    }

    // The naming schemes do not agree on where a set starts: `.partNN.rar`
    // counts from 1, while a bare `.rar` is volume 0 of the `.rNN` scheme. So
    // the position number is not the ordinal — rank the whole set and use that.
    const int ordinal = volumeOrdinal(rt, fileIndex, position.baseName);
    if (ordinal < 0)
        return;

    DirectUnpackRun& run = rt.directUnpack[position.baseName];
    if (!run.running && run.worker == nullptr) {
        // Only ever start on volume one. Handed a later volume first, libarchive
        // would read a headerless fragment and call the set corrupt.
        if (ordinal != 0)
            return;
        if (m_directUnpackRuns >= kMaxDirectUnpacks)
            return;   // over the cap: this set falls back to the end-of-download path

        run.worker = new UsenetDirectUnpack;
        run.thread = new QThread;
        run.thread->setObjectName(QStringLiteral("UsenetDirectUnpack"));
        run.worker->moveToThread(run.thread);
        connect(run.worker, &UsenetDirectUnpack::finished,
                this, &UsenetQueue::onDirectUnpackFinished, Qt::QueuedConnection);
        run.thread->start();
        run.running = true;
        ++m_directUnpackRuns;

        UsenetDirectUnpackJob job;
        job.itemId = rt.item->id;
        job.setKey = position.baseName;
        job.destDir = QDir(QFileInfo(st.tempPath).absolutePath())
                          .filePath(QString(kUnpackDirName));
        job.password = rt.item->nzb.password;
        QMetaObject::invokeMethod(run.worker, "run", Qt::QueuedConnection,
                                  Q_ARG(eMule::usenet::UsenetDirectUnpackJob, job));
    }

    if (run.running && run.worker)
        run.worker->offerVolume(ordinal, st.tempPath);
}

int UsenetQueue::volumeOrdinal(const ItemRuntime& rt, int fileIndex, const QString& baseName)
{
    QList<QPair<int, int>> members;   // (volume number, NZB file index)
    for (int f = 0; f < rt.item->files.size(); ++f) {
        const auto pos = UsenetUnpacker::volumePositionOf(volumeNameOf(rt, f));
        if (pos.index >= 0 && pos.baseName == baseName)
            members.append({pos.index, f});
    }
    std::sort(members.begin(), members.end());

    for (int k = 0; k < members.size(); ++k) {
        if (members.at(k).second == fileIndex)
            return k;
    }
    return -1;
}

QString UsenetQueue::volumeNameOf(const ItemRuntime& rt, int fileIndex)
{
    if (fileIndex < 0 || fileIndex >= rt.item->files.size())
        return {};
    const UsenetFileState& st = rt.item->files.at(fileIndex);
    // A sealed file already carries its real name; before that, the best guess
    // is what the article header said, then what the NZB claimed.
    if (st.finalized)
        return QFileInfo(st.tempPath).fileName();
    if (!st.articleFileName.isEmpty())
        return st.articleFileName;
    return fileIndex < rt.item->nzb.files.size() ? rt.item->nzb.files.at(fileIndex).fileName
                                                 : QString();
}

void UsenetQueue::endDirectUnpackSets(ItemRuntime& rt)
{
    for (DirectUnpackRun& run : rt.directUnpack) {
        if (run.running && run.worker)
            run.worker->endOfSet();
    }
}

void UsenetQueue::cancelDirectUnpack(ItemRuntime& rt)
{
    for (DirectUnpackRun& run : rt.directUnpack) {
        if (!run.worker)
            continue;
        run.worker->cancel();
        run.thread->quit();
        run.thread->wait();
        delete run.worker;
        delete run.thread;
        if (run.running)
            --m_directUnpackRuns;
        run.worker = nullptr;
        run.thread = nullptr;
        run.running = false;
    }
    rt.directUnpack.clear();
}

void UsenetQueue::onDirectUnpackFinished(const eMule::usenet::UsenetDirectUnpackResult& result)
{
    ItemRuntime* rt = runtimeFor(result.itemId);
    if (!rt)
        return;

    auto it = rt->directUnpack.find(result.setKey);
    if (it == rt->directUnpack.end())
        return;

    it->result = result;
    if (it->running)
        --m_directUnpackRuns;
    it->running = false;

    // The worker has returned from run(); its thread can go. Deleting it here
    // rather than in cancelDirectUnpack() keeps a finished run from holding a
    // thread for the rest of a long download.
    if (it->thread) {
        it->thread->quit();
        it->thread->wait();
        delete it->worker;
        delete it->thread;
        it->worker = nullptr;
        it->thread = nullptr;
    }

    checkItemCompletion(*rt);
}

void UsenetQueue::beginPostProcessing(ItemRuntime& rt)
{
    if (!m_postProcessor) {
        // No pipeline at all: fall back to phase 3's behaviour, minus the part
        // of it that was wrong. A clean release is published; a short one is not.
        int missing = 0;
        for (const auto& st : rt.item->files)
            missing += st.missingSegments;

        if (missing > 0) {
            failItem(rt, tr("%n article(s) are missing and post-processing is unavailable",
                            nullptr, missing));
            return;
        }

        UsenetPostResult synthetic;
        synthetic.itemId = rt.item->id;
        synthetic.success = true;
        for (const auto& st : rt.item->files) {
            if (st.tempPath.isEmpty() || !QFile::exists(st.tempPath))
                continue;
            const QString name = QFileInfo(st.tempPath).completeBaseName();
            const QString finalPath = uniqueDestination(thePrefs.incomingDir(), name);
            if (QFile::rename(st.tempPath, finalPath + QString(Preferences::kUsenetPartSuffix)))
                synthetic.staged.append({finalPath + QString(Preferences::kUsenetPartSuffix),
                                         finalPath});
        }
        onPostFinished(synthetic);
        return;
    }

    rt.postRunning = true;
    rt.item->status = UsenetItemStatus::Verifying;
    rt.item->postPercent = 0;
    rt.item->postDetail.clear();
    rt.item->error.clear();
    persist(rt);
    emit itemChanged(rt.item->id);

    UsenetPostJob job;
    job.itemId = rt.item->id;
    job.workDir = QDir(thePrefs.usenetTempDir()).filePath(rt.item->id);
    job.destDir = thePrefs.incomingDir();
    job.password = rt.item->nzb.password;
    job.par2Enabled = m_par2Enabled;
    job.renameEnabled = m_renameEnabled;
    job.unpackEnabled = m_unpackEnabled;
    job.cleanupEnabled = m_cleanupEnabled;

    for (const DirectUnpackRun& run : std::as_const(rt.directUnpack)) {
        if (run.result.ok)
            job.directUnpacked.append(run.result);
    }

    for (const auto& st : rt.item->files) {
        if (st.missingSegments > 0) {
            job.hasMissingSegments = true;
            break;
        }
    }

    QMetaObject::invokeMethod(m_postProcessor, "process", Qt::QueuedConnection,
                              Q_ARG(eMule::usenet::UsenetPostJob, job));
}

void UsenetQueue::onPostStage(const QString& itemId, int stage, int percent,
                              const QString& detail)
{
    ItemRuntime* rt = runtimeFor(itemId);
    if (!rt)
        return;

    switch (PostStage(stage)) {
    case PostStage::Repairing: rt->item->status = UsenetItemStatus::Repairing; break;
    case PostStage::Unpacking: rt->item->status = UsenetItemStatus::Unpacking; break;
    case PostStage::Verifying:
    case PostStage::Staging:
    case PostStage::Idle:      rt->item->status = UsenetItemStatus::Verifying; break;
    }

    rt->item->postPercent = percent;
    rt->item->postDetail = detail;
    emit itemChanged(itemId);
}

void UsenetQueue::onPostFinished(const UsenetPostResult& result)
{
    ItemRuntime* rt = runtimeFor(result.itemId);
    if (!rt)
        return;

    rt->postRunning = false;
    rt->item->postPercent = 0;
    rt->item->postDetail.clear();

    // Repair rewrites volumes, rename moves them and unpack publishes elsewhere.
    // Every cached path and parsed header in the streaming index is suspect.
    rt->streamIndex.invalidate();

    // -- the cycle: verification came up short, go and fetch the blocks ----
    if (result.needsMoreBlocks) {
        if (++rt->par2Rounds > kMaxPar2Rounds) {
            failItem(*rt, tr("Repair still incomplete after %1 rounds").arg(kMaxPar2Rounds));
            return;
        }
        if (!requestPar2Volumes(*rt, result.blocksNeeded)) {
            failItem(*rt, tr("Not enough recovery data: %n more block(s) needed",
                             nullptr, result.blocksNeeded));
            return;
        }

        logInfo(QStringLiteral("Usenet: \"%1\" needs %2 recovery block(s); fetching volumes")
                    .arg(rt->item->name).arg(result.blocksNeeded));

        rt->item->status = UsenetItemStatus::Downloading;
        rebuildPlan(*rt);
        persist(*rt);
        emit itemChanged(rt->item->id);
        dispatch();
        return;
    }

    if (!result.success) {
        failItem(*rt, result.message);
        return;
    }

    publishStaged(*rt, result);

    for (const QString& path : result.consumed)
        QFile::remove(path);

    rt->item->status = UsenetItemStatus::Complete;
    rt->item->error.clear();
    persist(*rt);

    emit itemChanged(rt->item->id);
    emit itemFinished(rt->item->id, true, tr("Download complete"));

    // Everything worth keeping has been moved out by now; what is left is
    // scratch, archive volumes and recovery data.
    QDir(QDir(thePrefs.usenetTempDir()).filePath(rt->item->id)).removeRecursively();
}

bool UsenetQueue::requestPar2Volumes(ItemRuntime& rt, int blocks)
{
    struct Candidate {
        int fileIndex;
        int blocks;
    };
    QList<Candidate> available;

    for (int f = 0; f < rt.item->nzb.files.size(); ++f) {
        if (rt.item->requestedPar2.contains(f))
            continue;
        const int supplied = rt.item->nzb.files.at(f).par2RecoveryBlocks();
        if (supplied > 0)
            available.append({f, supplied});
    }
    if (available.isEmpty())
        return false;

    // Smallest first, so covering a two-block shortfall costs a two-block volume
    // rather than the 64-block one that happens to come first in the NZB.
    std::sort(available.begin(), available.end(),
              [](const Candidate& a, const Candidate& b) { return a.blocks < b.blocks; });

    int covered = 0;
    for (const Candidate& c : available) {
        // addNzb() skipped these, so the scratch file has to appear now — the
        // worker writes into it at absolute offsets and will not create it.
        QString error;
        if (c.fileIndex >= rt.item->files.size()
            || !createTargetFile(rt.item->files.at(c.fileIndex).tempPath, error)) {
            logWarning(QStringLiteral("Usenet: %1").arg(error));
            continue;
        }

        rt.item->requestedPar2.insert(c.fileIndex);
        covered += c.blocks;
        if (covered >= blocks)
            break;
    }

    // Even taking everything left may not be enough. Say so now rather than
    // downloading the whole recovery set and failing afterwards.
    return covered > 0;
}

void UsenetQueue::publishStaged(ItemRuntime& rt, const UsenetPostResult& result)
{
    for (const auto& [stagedPath, finalPath] : result.staged) {
        // The staged file already sits in the incoming directory carrying the
        // .usenetpart suffix, which the share scan skips by name. This rename is
        // in-place and therefore atomic: there is no instant at which a
        // half-written file is visible under a shareable name.
        if (!QFile::rename(stagedPath, finalPath)) {
            logError(QStringLiteral("Usenet: cannot name \"%1\"")
                         .arg(QFileInfo(finalPath).fileName()));
            QFile::remove(stagedPath);
            continue;
        }

        // addFileInSharedLocation(), not addSingleSharedFile(): the latter is for
        // a file no shared directory covers, and it refuses the incoming
        // directory outright because isShareableDirectory() excludes it. Using
        // the wrong one logs a warning and shares nothing until a full rescan.
        if (theApp.sharedFileList && !theApp.sharedFileList->addFileInSharedLocation(finalPath)) {
            logWarning(QStringLiteral("Usenet: \"%1\" completed but is not in a shared "
                                      "location; it will not be offered to peers")
                           .arg(QFileInfo(finalPath).fileName()));
        }

        logInfo(QStringLiteral("Usenet: completed \"%1\"")
                    .arg(QFileInfo(finalPath).fileName()));
    }

    // The per-file record is what the GUI shows; point it at where the file
    // actually ended up rather than the scratch path it no longer occupies.
    for (int f = 0; f < rt.item->files.size() && f < result.staged.size(); ++f)
        rt.item->files[f].finalPath = result.staged.at(f).second;
}

void UsenetQueue::failItem(ItemRuntime& rt, const QString& message)
{
    rt.postRunning = false;
    // No more volumes are coming, so a run would hold its thread until shutdown.
    cancelDirectUnpack(rt);
    rt.item->status = UsenetItemStatus::Failed;
    rt.item->error = message;
    persist(rt);

    logWarning(QStringLiteral("Usenet: \"%1\" failed: %2").arg(rt.item->name, message));

    emit itemChanged(rt.item->id);
    emit itemFinished(rt.item->id, false, message);
}

void UsenetQueue::persist(ItemRuntime& rt)
{
    rt.dirty = false;
    UsenetQueueStore::save(*rt.item);
}

UsenetQueue::ItemRuntime* UsenetQueue::runtimeFor(const QString& id)
{
    for (auto& rt : m_items) {
        if (rt->item->id == id)
            return rt.get();
    }
    return nullptr;
}

} // namespace eMule::usenet
