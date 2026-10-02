#include "queue/UsenetWorker.h"

#include "nntp/ArticleFetcher.h"
#include "nntp/NntpServerPool.h"
#include "nntp/NntpSocket.h"
#include "queue/ArticleWriter.h"
#include "utils/Log.h"

#include <algorithm>
#include <utility>

namespace eMule::usenet {

/// One in-flight article.
///
/// The writer is per-job, never shared. Two fetchers on one ArticleWriter would
/// interleave seek() and write() and place bytes at each other's offsets, and
/// because yEnc verifies the payload rather than its placement the CRCs would
/// still pass. Writers may share a file *handle* (ArticleFileCache), because
/// each keeps its own position and seeks before writing.
struct UsenetWorker::Job {
    UsenetFetchRequest request;
    NntpSocket* socket = nullptr;
    std::unique_ptr<ArticleWriter> writer;
    std::unique_ptr<ArticleFetcher> fetcher;

    /// Held so they can be undone when the job ends. A pooled socket outlives the
    /// job that leased it, so a connection left behind would fire again for the
    /// *next* article on the same connection.
    ///
    /// Qt::UniqueConnection is not an option here: it is silently rejected for a
    /// lambda ("unique connections require a pointer to member function"), and the
    /// connection is then never made at all — which presents as every download
    /// hanging the moment the socket goes through its handshake.
    QMetaObject::Connection readyConn;
    QMetaObject::Connection failedConn;

    /// Pipelined behind this one on the same socket; takes it over on finish.
    Job* follower = nullptr;
    bool pipelined = false;    ///< this job was sent behind another
    bool nearlyDone = false;   ///< its fetcher said so; arms a slot once current

    /// Nothing was leasable; see UsenetFetchResult::noServerAvailable.
    bool noServerAvailable = false;
    bool requeue = false;      ///< see UsenetFetchResult::requeue
    bool finished = false;
};

UsenetWorker::UsenetWorker(int index, QObject* parent)
    : QObject(parent)
    , m_index(index)
{
}

UsenetWorker::~UsenetWorker()
{
    shutdown();
}

// ---------------------------------------------------------------------------
// Public slots
// ---------------------------------------------------------------------------

void UsenetWorker::setServers(QList<NewsServer> servers, int retryIntervalSec,
                              QNetworkProxy proxy)
{
    // Constructed lazily and here rather than in the constructor: the object is
    // moveToThread()'d after construction, and the pool's sockets must belong to
    // the thread that runs them.
    if (!m_pool) {
        m_pool = std::make_unique<NntpServerPool>();
        // A connection-limit hold shrinks the pool without a setServers(). The
        // queue must hear it: an article it dispatches past the capacity comes
        // back "nothing leasable" and parks every dispatch until the next tick.
        connect(m_pool.get(), &NntpServerPool::capacityChanged, this, [this] {
            emit capacityChanged(m_index, computeCapacity());
        });
    }

    m_pool->setProxy(proxy);
    m_pool->setRetryInterval(retryIntervalSec);
    m_pool->setServers(std::move(servers));

    emit capacityChanged(m_index, computeCapacity());
}

void UsenetWorker::setRateLimit(qint64 bytesPerSecond)
{
    m_rateLimit = std::max<qint64>(0, bytesPerSecond);
    applyRateLimits();
}

void UsenetWorker::fetchSegment(UsenetFetchRequest request)
{
    if (m_shuttingDown || !m_pool) {
        UsenetFetchResult result;
        result.itemId = request.itemId;
        result.fileIndex = request.fileIndex;
        result.segmentIndex = request.segmentIndex;
        result.workerIndex = m_index;
        result.messageId = request.segment.messageId;
        result.error = NntpError::Disconnected;
        result.text = QStringLiteral("Worker is shutting down");
        result.aborted = true;
        emit segmentFinished(result);
        return;
    }

    auto job = std::make_unique<Job>();
    job->request = std::move(request);

    // A probe writes nothing, so it skips both of the steps below. They run
    // *before* the connection is leased, so a probe that branched any later
    // would create a directory and an empty file for every article it merely
    // asked about.
    if (!job->request.probeOnly) {
        // The cache recreates the directory if a user deleted the temp tree
        // while the daemon runs, instead of failing every remaining segment.
        job->writer = std::make_unique<ArticleWriter>(&m_files);
        QString error;
        if (!job->writer->open(job->request.targetPath, error)) {
            Job* raw = job.release();
            m_jobs.append(raw);
            finishJob(raw, NntpError::WriteFailed, error);
            return;
        }
    }

    // An idle or new connection first; else behind a nearly finished article.
    NntpSocket* socket = m_pool->acquire(job->request.level, job->request.ignoreServers);
    if (!socket && !job->request.probeOnly) {
        if (NntpSocket* host = takeSlot(job->request)) {
            Job* raw = job.release();
            raw->socket = host;
            raw->pipelined = true;
            m_jobs.append(raw);
            m_jobsBySocket.value(host)->follower = raw;
            reportSlots();
            startJob(raw);
            return;
        }
    }
    if (!socket) {
        // Not an error: every candidate is blocked, excluded or at its connection
        // limit. The queue re-dispatches on its next tick.
        Job* raw = job.release();
        raw->noServerAvailable = true;
        m_jobs.append(raw);
        finishJob(raw, NntpError::ServerUnavailable,
                  QStringLiteral("No connection available on level %1")
                      .arg(raw->request.level));
        return;
    }

    job->socket = socket;
    Job* raw = job.release();
    m_jobs.append(raw);
    m_jobsBySocket.insert(socket, raw);

    applyRateLimits();

    // Watch for a mid-article failure whether or not the handshake is done: a
    // provider dropping the connection during BODY reaches us this way, and
    // without it the job would simply never finish.
    watchForFailure(raw);

    if (socket->isReady()) {
        startJob(raw);
        return;
    }

    // Leased before the handshake completed — that overlap is the point of the
    // pool. Wait for the outcome instead of blocking.
    raw->readyConn = connect(socket, &NntpSocket::ready, this, [this, socket] {
        applyRateLimits();   // now it counts
        if (Job* j = m_jobsBySocket.value(socket, nullptr); j && !j->finished)
            startJob(j);
    });
}

void UsenetWorker::shutdown()
{
    m_shuttingDown = true;

    // Fail everything still in flight so the queue is not left waiting on results
    // that will never arrive, then drop the pool. Both happen in this thread.
    // One at a time: finishing an article also finishes its follower.
    while (!m_jobs.isEmpty()) {
        Job* job = m_jobs.constFirst();
        if (job->finished) {
            m_jobs.removeFirst();
            delete job;
            continue;
        }
        finishJob(job, NntpError::Disconnected, QStringLiteral("Shutting down"));
    }
    m_jobsBySocket.clear();
    m_slots.clear();
    m_pool.reset();
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

void UsenetWorker::startJob(Job* job)
{
    if (!job || job->finished || !job->socket)
        return;

    job->fetcher = std::make_unique<ArticleFetcher>();
    job->fetcher->setProgressSink(job->request.received);
    connect(job->fetcher.get(), &ArticleFetcher::finished, this,
            [this, job](NntpError error, const QString& text) {
                finishJob(job, error, text);
            });
    connect(job->fetcher.get(), &ArticleFetcher::nearlyDone, this,
            [this, job] { onNearlyDone(job); });

    const QString group = job->socket->server().joinGroup ? job->request.group : QString();
    if (job->request.probeOnly)
        job->fetcher->stat(job->socket, job->request.segment, group);
    else
        job->fetcher->fetch(job->socket, job->request.segment, job->writer.get(), group);
}

void UsenetWorker::finishJob(Job* job, NntpError error, const QString& text)
{
    if (!job || job->finished)
        return;
    job->finished = true;

    // A follower settled before its turn (its send was refused): nothing was
    // asked, and the socket belongs to the article ahead of it.
    if (job->socket && m_jobsBySocket.value(job->socket) != job) {
        if (Job* host = m_jobsBySocket.value(job->socket); host && host->follower == job)
            host->follower = nullptr;
        job->socket = nullptr;
        job->requeue = true;
    }

    UsenetFetchResult result;
    result.itemId = job->request.itemId;
    result.fileIndex = job->request.fileIndex;
    result.segmentIndex = job->request.segmentIndex;
    result.workerIndex = m_index;
    result.messageId = job->request.segment.messageId;
    result.error = error;
    result.text = text;
    result.noServerAvailable = job->noServerAvailable;
    result.probeOnly = job->request.probeOnly;
    result.aborted = m_shuttingDown;
    result.requeue = job->requeue;

    Job* follower = std::exchange(job->follower, nullptr);

    if (job->socket) {
        result.serverKey = job->socket->server().key();
        result.accountId = job->socket->server().accountId;
        // Before release(): the socket goes back in the pool and the next job
        // would otherwise take these bytes. Covers the whole job, so a lease
        // that had to connect and authenticate first charges that to the
        // account that opened it, exactly once.
        result.rawBytes = job->socket->takeBytesRead();
    }

    if (job->fetcher) {
        result.decodedBytes = job->fetcher->decodedBytes();
        result.decodedOffset = job->fetcher->decodedOffset();
        result.articleFileName = job->fetcher->articleFileName();
        result.declaredFileSize = job->fetcher->declaredFileSize();
        result.articleExists = job->fetcher->articleExists();
    }

    if (job->writer) {
        QString flushError;
        job->writer->flush(flushError);
        job->writer->close();
    }

    // Undo the per-job connections before the socket goes back in the pool, or
    // the next article leased onto it would re-enter this job's handlers.
    QObject::disconnect(job->readyConn);
    QObject::disconnect(job->failedConn);

    if (job->socket) {
        // A server that answered a pipelined pair out of sync gets one article
        // at a time from now on.
        if (error == NntpError::ProtocolError && (follower || job->pipelined)
            && !result.serverKey.isEmpty() && !m_noPipeline.contains(result.serverKey)) {
            m_noPipeline.insert(result.serverKey);
            logUsenetWarning(QStringLiteral("Usenet: %1 broke on pipelined commands; one article at a time")
                           .arg(result.serverKey));
        }

        // A damaged copy is the server's, not the connection's: name it once,
        // here, where the server is known. Nothing else logs it until the
        // article runs out of servers and is declared missing.
        if (error == NntpError::ArticleCorrupt && !m_shuttingDown) {
            logUsenetWarning(QStringLiteral("Usenet: damaged copy of <%1> on %2 (%3); asking another server")
                           .arg(result.messageId, job->socket->server().key(), text));
        }

        // A connection that failed at transport level is not reusable, and handing
        // it out again is how one dead provider stalls the whole queue. A 430 is
        // not such a failure — the connection is fine, the article is elsewhere.
        const bool reusable = !isFatalToConnection(error);
        // A socket we tore down ourselves is not the provider misbehaving. Without
        // this, stopping the engine backs the server off once per article still in
        // flight — dozens of warnings naming a fault that never happened, and a
        // real 60 s stall for any caller whose pool outlives the shutdown.
        // Nor is a dead proxy: it fails every account at once, and backing each
        // off would outlast the proxy coming back. The queue waits for it instead.
        // Nor is a provider at its connection limit: that costs this one
        // connection, and the others keep downloading (see below).
        if (!reusable && !m_shuttingDown && !result.serverKey.isEmpty()
            && error != NntpError::ArticleNotFound && error != NntpError::GroupNotFound
            && error != NntpError::ProxyFailed && error != NntpError::TooManyConnections) {
            // Name the error. A backoff is the most consequential thing this
            // module does on its own — it takes a provider out for a minute —
            // and "backed off" with no cause is unactionable in a log.
            logUsenetWarning(QStringLiteral("Usenet: %1 on <%2>; backing off %3%4")
                           .arg(describeNntpError(error), result.messageId,
                                result.serverKey,
                                text.isEmpty() ? QString()
                                               : QStringLiteral(" (%1)").arg(text)));
            m_pool->blockServer(result.serverKey);
        }
        NntpSocket* socket = std::exchange(job->socket, nullptr);
        m_jobsBySocket.remove(socket);
        m_slots.remove(socket);

        if (follower && reusable && !m_shuttingDown
            && m_pool->canPipeline(socket, follower->request.level,
                                   follower->request.ignoreServers)) {
            // The next article is already arriving: keep the lease, hand it over.
            // Its bytes read so far went into this job's rawBytes, same account.
            m_jobsBySocket.insert(socket, follower);
            watchForFailure(follower);
            if (follower->nearlyDone)
                onNearlyDone(follower);
        } else {
            if (follower) {
                // Never answered: the socket goes away under it. Reported
                // before this job, which the queue then puts in front of it.
                // Its fetcher's late commandFinished must not reach the deleted job.
                if (follower->fetcher)
                    follower->fetcher->disconnect(this);
                follower->socket = nullptr;
                follower->requeue = true;
                logUsenetDebug(QStringLiteral("Usenet: worker %1 requeues pipelined <%2>: %3")
                                   .arg(m_index)
                                   .arg(follower->request.segment.messageId,
                                        describeNntpError(error)));
                finishJob(follower, NntpError::Disconnected,
                          QStringLiteral("Connection closed before the pipelined article"));
            }
            m_pool->release(socket, reusable);
        }

        // After release(), so the refused connection is not counted into the cap.
        if (error == NntpError::TooManyConnections && !m_shuttingDown
            && !result.serverKey.isEmpty()) {
            if (const int cap = m_pool->limitConnections(result.serverKey); cap >= 0) {
                logUsenetWarning(QStringLiteral("Usenet: %1 is at its connection limit; worker %2 holds at %3 for %4 s")
                               .arg(result.serverKey)
                               .arg(m_index)
                               .arg(cap)
                               .arg(m_pool->retryInterval()));
            }
        }
    }

    m_jobs.removeOne(job);

    // The fetcher's finished() is what we are standing in, so the object cannot be
    // destroyed here.
    if (job->fetcher) {
        job->fetcher->disconnect(this);
        job->fetcher.release()->deleteLater();
    }
    delete job;

    applyRateLimits();
    reportSlots();
    emit segmentFinished(result);
}

void UsenetWorker::applyRateLimits()
{
    // Divide this worker's share across its live sockets. Re-run on every acquire
    // and release so a shrinking set of connections gets the whole share rather
    // than throttling itself against sockets that are no longer reading.
    //
    // Probes are excluded from the divisor and left unlimited. A STAT response is
    // one status line — sixty bytes, which needs no token bucket — but counting
    // it would shrink every concurrent download's share for as long as the probe
    // held a connection. A health check that slowed the downloads down would be
    // paying for itself twice.
    //
    // Only logged-in sockets count. One still in its handshake may yet get a 502
    // (another client on the account), and counting it starved the working ones:
    // 19 such sockets on 96 cut a 3000 KB/s limit to ~2400 for as long as it took.
    int live = 0;
    for (auto it = m_jobsBySocket.cbegin(); it != m_jobsBySocket.cend(); ++it) {
        if (it.value() && !it.value()->request.probeOnly && it.key()
            && it.key()->acceptsCommands())
            ++live;
    }
    if (live != m_reportedLive) {
        m_reportedLive = live;
        emit liveSocketsChanged(m_index, live);
    }

    const qint64 perSocket = (m_rateLimit <= 0 || live <= 0)
                                 ? 0
                                 : std::max<qint64>(1, m_rateLimit / live);

    for (auto it = m_jobsBySocket.cbegin(); it != m_jobsBySocket.cend(); ++it) {
        if (!it.key())
            continue;
        const bool probe = it.value() && it.value()->request.probeOnly;
        it.key()->setReadRateLimit(probe ? 0 : perSocket);
    }
}

int UsenetWorker::computeCapacity() const
{
    // The pool's own answer, not a sum over rows: grouped accounts share one
    // budget, so two hostnames of one provider must count once. Summing rows
    // would have the queue dispatch more work than this worker can lease, and
    // every excess job would come straight back as noServerAvailable.
    return m_pool ? m_pool->capacity() : 0;
}

void UsenetWorker::watchForFailure(Job* job)
{
    // Its follower needs none: finishing this job settles the follower too.
    NntpSocket* socket = job->socket;
    job->failedConn = connect(socket, &NntpSocket::failed, this,
                              [this, socket](NntpError error, const QString& text) {
        if (Job* j = m_jobsBySocket.value(socket, nullptr); j && !j->finished)
            finishJob(j, error, text);
    });
}

void UsenetWorker::onNearlyDone(Job* job)
{
    job->nearlyDone = true;
    NntpSocket* socket = job->socket;
    if (m_shuttingDown || !socket || job->follower || job->request.probeOnly
        || m_jobsBySocket.value(socket) != job
        || m_noPipeline.contains(socket->server().key()))
        return;
    m_slots.insert(socket);
    reportSlots();
}

NntpSocket* UsenetWorker::takeSlot(const UsenetFetchRequest& request)
{
    for (auto it = m_slots.begin(); it != m_slots.end(); ++it) {
        NntpSocket* socket = *it;
        if (!socket->acceptsCommands()
            || !m_pool->canPipeline(socket, request.level, request.ignoreServers))
            continue;
        m_slots.erase(it);
        return socket;
    }
    return nullptr;
}

void UsenetWorker::reportSlots()
{
    // Taken slots too: the queue counts their followers as in flight.
    int followers = 0;
    for (const Job* job : std::as_const(m_jobsBySocket))
        followers += job && job->follower ? 1 : 0;
    const int slotCount = int(m_slots.size()) + followers;
    if (slotCount == m_reportedSlots && followers == m_reportedFollowers)
        return;
    m_reportedSlots = slotCount;
    m_reportedFollowers = followers;
    emit pipelineSlotsChanged(m_index, slotCount, followers);
}

} // namespace eMule::usenet
