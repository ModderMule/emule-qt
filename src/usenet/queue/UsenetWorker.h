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

#include "nntp/ArticleFetcher.h"
#include "queue/ArticleFileCache.h"
#include "nntp/NewsServer.h"
#include "nntp/NntpError.h"
#include "nzb/NzbInfo.h"

#include <QHash>
#include <QList>
#include <QSet>
#include <QNetworkProxy>
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
    QStringList groups;   ///< the file's newsgroups; only used with joinGroup

    /// Failover rung, already normalized by the pool's level mapping.
    int level = 0;

    /// Server keys already asked for this article, so an escalation never asks
    /// the same account twice.
    QStringList ignoreServers;

    /// Ask STAT instead of BODY: does this account still hold the article?
    ///
    /// No payload is transferred and **no file is touched** — not even created.
    /// That matters more than it looks: the target directory and the output file
    /// are made before the connection is leased, so a probe honoured any later
    /// would still litter the temp tree with empty files for articles it only
    /// asked about.
    bool probeOnly = false;

    /// Body bytes read so far, shared with the queue. Null for a probe.
    ArticleProgress received;
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

    /// The same account's stable id, carried alongside rather than looked up.
    /// applyServers() replaces m_servers on every settings save, so a result
    /// still in flight can arrive after its row is gone — and resolving the id
    /// then would silently drop the bytes it is about to be charged.
    QString accountId;

    qint64 decodedBytes = 0;

    /// Raw inbound bytes this job cost on the wire, including the greeting and
    /// authentication when it opened the connection itself. What a provider's
    /// allowance counts, as against decodedBytes, which is payload only.
    /// Non-zero for a 430 and for a transfer that died half way, both of which
    /// were still billed.
    qint64 rawBytes = 0;

    /// Where those bytes landed in the final file, from the article's own
    /// `=ypart begin`. Together with decodedBytes this is the one byte range
    /// the queue can prove is readable — see UsenetFileState::written.
    qint64 decodedOffset = 0;

    /// From `=ybegin`. The only source of truth for an obfuscated post's name.
    QString articleFileName;
    qint64 declaredFileSize = 0;

    /// Answer to a probeOnly request: this account has the article. Meaningless
    /// for a normal fetch, and never a verdict on its own — one account saying no
    /// is exactly what the failover ladder exists to survive.
    bool articleExists = false;

    /// Echoed back so the queue can route the result to the probe pass rather
    /// than into the download's own bookkeeping, where a STAT would seal empty
    /// files or inflate the PAR2 damage estimate.
    bool probeOnly = false;

    /// The pool had nothing to lease — every candidate blocked, excluded, or at
    /// its connection limit. A "try again shortly", not a failure, and explicitly
    /// flagged because the queue must not spend a retry on it: a server backing
    /// off for 60 s would otherwise burn an item's whole retry budget in a second.
    bool noServerAvailable = false;

    /// Failed because we tore the worker down (engine stop, settings save), not
    /// because the provider did anything. Statistics must not blame the server.
    bool aborted = false;

    /// Pipelined behind an article whose connection then died: never asked.
    /// Back at the cursor, no retry spent, nobody blamed.
    bool requeue = false;
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
    /// @p proxy routes every connection this worker's pool opens.
    void setServers(QList<eMule::NewsServer> servers, int retryIntervalSec,
                    QNetworkProxy proxy = QNetworkProxy(QNetworkProxy::NoProxy));

    /// This worker's share of the Usenet download budget, in bytes per second.
    /// 0 is unlimited, as everywhere else in eMuleQt. Split again across the
    /// worker's live sockets, so the sum stays within the share. The queue
    /// sizes it by liveSocketsChanged().
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

    /// Download sockets actually leased, probes excluded. The queue weights this
    /// worker's share of the rate limit by it.
    void liveSocketsChanged(int workerIndex, int live);

    /// Pipeline capacity: connections whose article is nearly done, free or
    /// already holding a follower. The queue dispatches that many beyond
    /// capacityChanged().
    /// @p followers of them already hold one, which is in flight but not a
    /// connection of its own.
    void pipelineSlotsChanged(int workerIndex, int slotCount, int followers);

private:
    struct Job;

    void startJob(Job* job);
    void finishJob(Job* job, NntpError error, const QString& text);
    void applyRateLimits();
    [[nodiscard]] int computeCapacity() const;
    void watchForFailure(Job* job);
    void onNearlyDone(Job* job);
    [[nodiscard]] NntpSocket* takeSlot(const UsenetFetchRequest& request);
    void reportSlots();

    int m_index = 0;
    std::unique_ptr<NntpServerPool> m_pool;
    ArticleFileCache m_files;                   ///< one handle per file across this worker's jobs
    QHash<NntpSocket*, Job*> m_jobsBySocket;   ///< the job whose response is being read
    QSet<NntpSocket*> m_slots;                 ///< nearly done, no follower yet
    QSet<QString> m_noPipeline;                ///< servers that broke on pipelining
    QList<Job*> m_jobs;
    qint64 m_rateLimit = 0;
    int m_reportedLive = -1;   ///< last liveSocketsChanged() value
    int m_reportedSlots = 0;   ///< last pipelineSlotsChanged() values
    int m_reportedFollowers = 0;
    bool m_shuttingDown = false;
};

} // namespace eMule::usenet

Q_DECLARE_METATYPE(eMule::usenet::UsenetFetchRequest)
Q_DECLARE_METATYPE(eMule::usenet::UsenetFetchResult)
