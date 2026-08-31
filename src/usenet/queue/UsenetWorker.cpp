#include "queue/UsenetWorker.h"

#include "nntp/ArticleFetcher.h"
#include "nntp/NntpServerPool.h"
#include "nntp/NntpSocket.h"
#include "queue/ArticleWriter.h"
#include "utils/Log.h"

#include <QDir>
#include <QFileInfo>

#include <algorithm>

namespace eMule::usenet {

/// One in-flight article.
///
/// The writer is per-job, never shared. Two fetchers on one ArticleWriter would
/// interleave seek() and write() and place bytes at each other's offsets, and
/// because yEnc verifies the payload rather than its placement the CRCs would
/// still pass. Separate QFile handles writing disjoint absolute ranges is the
/// only safe arrangement.
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

    /// Nothing was leasable; see UsenetFetchResult::noServerAvailable.
    bool noServerAvailable = false;
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

void UsenetWorker::setServers(QList<NewsServer> servers, int retryIntervalSec)
{
    // Constructed lazily and here rather than in the constructor: the object is
    // moveToThread()'d after construction, and the pool's sockets must belong to
    // the thread that runs them.
    if (!m_pool)
        m_pool = std::make_unique<NntpServerPool>();

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
        emit segmentFinished(result);
        return;
    }

    auto job = std::make_unique<Job>();
    job->request = std::move(request);

    // The directory is created here rather than by the queue because a user can
    // delete the temp tree while the daemon runs, and the next article should
    // recreate it instead of failing every remaining segment.
    QDir().mkpath(QFileInfo(job->request.targetPath).absolutePath());

    job->writer = std::make_unique<ArticleWriter>();
    QString error;
    if (!job->writer->open(job->request.targetPath, error)) {
        Job* raw = job.release();
        m_jobs.append(raw);
        finishJob(raw, NntpError::ProtocolError, error);
        return;
    }

    NntpSocket* socket = m_pool->acquire(job->request.level, job->request.ignoreServers);
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
    raw->failedConn = connect(socket, &NntpSocket::failed, this,
                              [this, socket](NntpError error, const QString& text) {
        if (Job* j = m_jobsBySocket.value(socket, nullptr); j && !j->finished)
            finishJob(j, error, text);
    });

    if (socket->isReady()) {
        startJob(raw);
        return;
    }

    // Leased before the handshake completed — that overlap is the point of the
    // pool. Wait for the outcome instead of blocking.
    raw->readyConn = connect(socket, &NntpSocket::ready, this, [this, socket] {
        if (Job* j = m_jobsBySocket.value(socket, nullptr); j && !j->finished)
            startJob(j);
    });
}

void UsenetWorker::shutdown()
{
    m_shuttingDown = true;

    // Fail everything still in flight so the queue is not left waiting on results
    // that will never arrive, then drop the pool. Both happen in this thread.
    const QList<Job*> jobs = m_jobs;
    for (Job* job : jobs) {
        if (!job->finished)
            finishJob(job, NntpError::Disconnected, QStringLiteral("Shutting down"));
    }
    qDeleteAll(m_jobs);
    m_jobs.clear();
    m_jobsBySocket.clear();
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
    connect(job->fetcher.get(), &ArticleFetcher::finished, this,
            [this, job](NntpError error, const QString& text) {
                finishJob(job, error, text);
            });

    const QString group = job->socket->server().joinGroup ? job->request.group : QString();
    job->fetcher->fetch(job->socket, job->request.segment, job->writer.get(), group);
}

void UsenetWorker::finishJob(Job* job, NntpError error, const QString& text)
{
    if (!job || job->finished)
        return;
    job->finished = true;

    UsenetFetchResult result;
    result.itemId = job->request.itemId;
    result.fileIndex = job->request.fileIndex;
    result.segmentIndex = job->request.segmentIndex;
    result.workerIndex = m_index;
    result.messageId = job->request.segment.messageId;
    result.error = error;
    result.text = text;
    result.noServerAvailable = job->noServerAvailable;

    if (job->socket)
        result.serverKey = job->socket->server().key();

    if (job->fetcher) {
        result.decodedBytes = job->fetcher->decodedBytes();
        result.articleFileName = job->fetcher->articleFileName();
        result.declaredFileSize = job->fetcher->declaredFileSize();
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
        // A connection that failed at transport level is not reusable, and handing
        // it out again is how one dead provider stalls the whole queue. A 430 is
        // not such a failure — the connection is fine, the article is elsewhere.
        const bool reusable = !isFatalToConnection(error);
        if (!reusable && !result.serverKey.isEmpty()
            && error != NntpError::ArticleNotFound && error != NntpError::GroupNotFound) {
            // Name the error. A backoff is the most consequential thing this
            // module does on its own — it takes a provider out for a minute —
            // and "backed off" with no cause is unactionable in a log.
            logWarning(QStringLiteral("Usenet: %1 on <%2>; backing off %3%4")
                           .arg(describeNntpError(error), result.messageId,
                                result.serverKey,
                                text.isEmpty() ? QString()
                                               : QStringLiteral(" (%1)").arg(text)));
            m_pool->blockServer(result.serverKey);
        }
        m_jobsBySocket.remove(job->socket);
        m_pool->release(job->socket, reusable);
        job->socket = nullptr;
    }

    m_jobs.removeOne(job);

    // The fetcher's finished() is what we are standing in, so the object cannot be
    // destroyed here.
    if (job->fetcher)
        job->fetcher.release()->deleteLater();
    delete job;

    applyRateLimits();
    emit segmentFinished(result);
}

void UsenetWorker::applyRateLimits()
{
    // Divide this worker's share across its live sockets. Re-run on every acquire
    // and release so a shrinking set of connections gets the whole share rather
    // than throttling itself against sockets that are no longer reading.
    const int live = int(m_jobsBySocket.size());
    const qint64 perSocket = (m_rateLimit <= 0 || live <= 0)
                                 ? 0
                                 : std::max<qint64>(1, m_rateLimit / live);

    for (auto it = m_jobsBySocket.cbegin(); it != m_jobsBySocket.cend(); ++it) {
        if (it.key())
            it.key()->setReadRateLimit(perSocket);
    }
}

int UsenetWorker::computeCapacity() const
{
    if (!m_pool)
        return 0;

    int capacity = 0;
    for (const auto& server : m_pool->servers()) {
        if (server.enabled && server.isValid())
            capacity += std::max(0, server.maxConnections);
    }
    return capacity;
}

} // namespace eMule::usenet
