#pragma once

/// @file UsenetWorker.h
/// @brief One download thread: its own connection pool, its own sockets.
///
/// Each worker owns an `NntpServerPool`. The pool has no locking and its sockets
/// are QObjects with thread affinity, so sharing one across threads is not an
/// option — a pool per worker is what makes the whole design lock-free. Nothing
/// but queued signals crosses a thread boundary here; there is not a mutex in the
/// file, and that is deliberate.
///
/// **The connection budget is divided, not replicated.** N workers each honouring
/// `NewsServer::maxConnections` would open N times what the user configured, and
/// exceeding a provider's connection limit gets an account throttled or suspended
/// — a worse failure than downloading slowly. UsenetQueue therefore hands each
/// worker a *copy* of the server list with `maxConnections` already divided, so
/// the sum across workers equals the configured limit. Because NewsServer is a
/// plain value type, that copy costs nothing.
///
/// Policy lives in UsenetQueue, with one exception: `blockServer()` acts on this
/// worker's own pool, so a transport failure is backed off here. Each worker
/// discovers a dead provider independently, which is correct — they hold
/// independent connections to it.

#include "nntp/NewsServer.h"
#include "nntp/NntpError.h"
#include "nzb/NzbInfo.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include <memory>

namespace eMule::usenet {

class ArticleFetcher;
class ArticleWriter;
class NntpServerPool;
class NntpSocket;

/// One unit of work. Carries its own failover position so the worker needs no
/// back-reference into the queue.
struct UsenetFetchRequest {
    QString itemId;
    int fileIndex = -1;
    int segmentIndex = -1;

    NzbSegment segment;
    QString targetPath;
    QString group;

    /// Failover rung, already normalized by the pool's level mapping.
    int level = 0;

    /// Server keys already asked for this article, so an escalation never asks
    /// the same account twice.
    QStringList ignoreServers;
};

struct UsenetFetchResult {
    QString itemId;
    int fileIndex = -1;
    int segmentIndex = -1;

    /// Which worker produced this, so the queue can decrement the right slot
    /// rather than guessing. Set by the worker, never by the caller.
    int workerIndex = -1;

    /// The article this was, for logging. The queue has it too, but a log line
    /// that names the message id is what makes a missing-article report useful.
    QString messageId;

    NntpError error = NntpError::None;
    QString text;

    /// Which account answered or failed. Appended to ignoreServers on a retry.
    QString serverKey;

    qint64 decodedBytes = 0;

    /// From `=ybegin`. The only source of truth for an obfuscated post's name.
    QString articleFileName;
    qint64 declaredFileSize = 0;

    /// The pool had nothing to lease — every candidate blocked, excluded, or at
    /// its connection limit. A "try again shortly", not a failure, and explicitly
    /// flagged because the queue must not spend a retry on it: a server backing
    /// off for 60 s would otherwise burn an item's whole retry budget in a second.
    bool noServerAvailable = false;
};

class UsenetWorker : public QObject {
    Q_OBJECT

public:
    explicit UsenetWorker(int index, QObject* parent = nullptr);
    ~UsenetWorker() override;

    [[nodiscard]] int index() const { return m_index; }

public slots:
    /// Replace this worker's slice of the server list. @p servers already carries
    /// the divided maxConnections — the worker does not divide anything itself.
    void setServers(QList<eMule::NewsServer> servers, int retryIntervalSec);

    /// This worker's share of the Usenet download budget, in bytes per second.
    /// 0 is unlimited, as everywhere else in eMuleQt. Split again across the
    /// worker's live sockets, so the sum stays within the share.
    void setRateLimit(qint64 bytesPerSecond);

    void fetchSegment(eMule::usenet::UsenetFetchRequest request);

    /// Close everything. Must run in this worker's own thread — a socket deleted
    /// from another thread is a crash, not a leak.
    void shutdown();

signals:
    void segmentFinished(eMule::usenet::UsenetFetchResult result);

    /// How many articles this worker can have in flight at once, recomputed
    /// whenever the server list changes. The queue uses it to size its dispatch.
    void capacityChanged(int workerIndex, int capacity);

private:
    struct Job;

    void startJob(Job* job);
    void finishJob(Job* job, NntpError error, const QString& text);
    void applyRateLimits();
    [[nodiscard]] int computeCapacity() const;

    int m_index = 0;
    std::unique_ptr<NntpServerPool> m_pool;
    QHash<NntpSocket*, Job*> m_jobsBySocket;
    QList<Job*> m_jobs;
    qint64 m_rateLimit = 0;
    bool m_shuttingDown = false;
};

} // namespace eMule::usenet

Q_DECLARE_METATYPE(eMule::usenet::UsenetFetchRequest)
Q_DECLARE_METATYPE(eMule::usenet::UsenetFetchResult)
