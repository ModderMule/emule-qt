#pragma once

/// @file UsenetQueue.h
/// @brief The Usenet download queue: scheduling, failover and completion.
///
/// Lives on the daemon thread and owns every policy decision. The workers own the
/// sockets; this owns *what to ask for and what to do when the answer is no*.
/// That split is the same one ArticleFetcher already documents ("It holds no
/// policy: which server to ask and what to do about a failure belong to the pool
/// and the queue respectively") — extended one level up.
///
/// The failover rule is the whole of Usenet fault tolerance, and it is one line
/// of judgement that must not be re-derived anywhere else:
///
///   - `escalatesToNextLevel(error)` — the server does not have the article (430)
///     — retry at `level + 1`, with that account added to `ignoreServers` so the
///     escalation never asks it twice.
///   - anything else is a *connection* fault. Retry the **same** level. The
///     worker has already backed the server off; a different account on the same
///     rung picks the article up.
///
/// Getting that backwards means either hammering a fill server every time the
/// main provider is briefly busy, or paying for a block account that never gets
/// used.
///
/// **Completion is a cycle, not a line.** Since phase 4 the plan deliberately
/// leaves out the PAR2 recovery volumes, so "every planned segment resolved"
/// means the *download* phase is done, not the item. Post-processing then
/// verifies, and a verify that comes up short sends the item back to
/// downloading for exactly the recovery volumes it turned out to need. Every
/// path out of that loop has to terminate: requestPar2Volumes() returning false
/// — nothing further to ask for — is what ends it, and kMaxPar2Rounds is the
/// backstop.

#include "nntp/NewsServer.h"
#include "post/UsenetPostProcessor.h"
#include "queue/UsenetQueueItem.h"
#include "queue/UsenetWorker.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>

#include <memory>
#include <vector>

class QThread;
class QTimer;

namespace eMule::usenet {

class UsenetQueue : public QObject {
    Q_OBJECT

public:
    explicit UsenetQueue(QObject* parent = nullptr);
    ~UsenetQueue() override;

    /// Load persisted items and spin the worker threads up. Idempotent.
    void start();

    /// Stop dispatching, join every worker thread, flush state. Idempotent, and
    /// must complete before anything the queue references is destroyed.
    void stop();

    [[nodiscard]] bool isRunning() const { return m_running; }

    /// Re-slice the server list across the workers. Safe while running.
    void applyServers(const QList<NewsServer>& servers, int retryIntervalSec);

    // -- Queue operations ---------------------------------------------------

    /// Parse @p data and queue it. Returns the new item id, or an empty string
    /// with @p error set.
    QString addNzb(const QByteArray& data, const QString& name, QString& error);

    bool removeItem(const QString& id, bool deleteFiles);
    bool pauseItem(const QString& id);
    bool resumeItem(const QString& id);
    bool setItemPriority(const QString& id, int priority);

    [[nodiscard]] QList<const UsenetQueueItem*> items() const;
    [[nodiscard]] const UsenetQueueItem* findItem(const QString& id) const;

    /// Bytes per second this engine may use in total. 0 is unlimited, as
    /// everywhere else in eMuleQt. Divided across workers, then across their
    /// sockets.
    void setRateLimit(qint64 bytesPerSecond);

    /// Post-processing settings, refreshed from preferences at each job. Kept
    /// here rather than read inside the pipeline so a job carries a consistent
    /// snapshot across the thread boundary.
    void setPostProcessingOptions(bool par2, bool rename, bool unpack, bool cleanup);

    /// Decoded bytes per second, measured over the last tick. Feeds the
    /// ED2K/Usenet budget split.
    [[nodiscard]] qint64 currentRate() const { return m_currentRate; }

    /// Whether anything is actually downloading, i.e. whether Usenet needs a
    /// share of the budget at all.
    [[nodiscard]] bool hasActiveDownloads() const;

signals:
    /// Progress or state moved. Coalesced by the daemon before it reaches a GUI.
    void itemChanged(const QString& id);

    void itemAdded(const QString& id);
    void itemRemoved(const QString& id);

    /// A terminal outcome. Deliberately separate from itemChanged so the daemon
    /// can broadcast it *uncoalesced*: this is a transition, not a latest value,
    /// and collapsing it inside a coalescing window loses it entirely.
    void itemFinished(const QString& id, bool success, const QString& message);

private:
    struct SegmentKey {
        int fileIndex = -1;
        int segmentIndex = -1;
        [[nodiscard]] quint64 packed() const
        {
            return (quint64(quint32(fileIndex)) << 32) | quint32(segmentIndex);
        }
    };

    /// Failover position of a segment that has already failed at least once.
    /// Segments with no entry are at level 0 with nothing excluded, which is the
    /// overwhelmingly common case — so this stays small even for a huge release.
    struct SegmentAttempt {
        int level = 0;
        QStringList tried;
        int transportRetries = 0;
    };

    struct ItemRuntime {
        std::unique_ptr<UsenetQueueItem> item;
        QHash<quint64, SegmentAttempt> attempts;
        QSet<quint64> inFlight;
        /// Order segments are handed out in: the index PAR2 last, because it is
        /// only worth having if something else came up short. Recovery volumes
        /// are not in here at all until requestPar2Volumes() adds them.
        QList<quint64> plan;
        int planCursor = 0;
        bool dirty = false;

        /// Guards the download → verify → download cycle. Incremented per round
        /// trip, never reset, and checked against kMaxPar2Rounds.
        int par2Rounds = 0;

        /// A job is with the post-processing thread. Nothing may dispatch for
        /// this item, and no second job may start, until it comes back.
        bool postRunning = false;
    };

    void startWorkers();
    void stopWorkers();
    void startPostProcessor();
    void stopPostProcessor();
    void rebuildPlan(ItemRuntime& rt);
    void dispatch();
    void onSegmentFinished(const UsenetFetchResult& result);
    void onCapacityChanged(int workerIndex, int capacity);
    void onTick();

    void markSegmentDone(ItemRuntime& rt, const UsenetFetchResult& result);
    void handleSegmentFailure(ItemRuntime& rt, const UsenetFetchResult& result);
    void checkFileCompletion(ItemRuntime& rt, int fileIndex);

    /// Close a finished file off in the item's scratch directory: pad it out to
    /// its declared length so PAR2 sees the holes rather than a short file, and
    /// mark it done. It does **not** publish — that is post-processing's last
    /// step now.
    void sealFile(ItemRuntime& rt, int fileIndex);

    void checkItemCompletion(ItemRuntime& rt);

    /// Whether @p fileIndex takes part in the current download round. False for
    /// a recovery volume nobody has asked for.
    [[nodiscard]] static bool isPlanned(const ItemRuntime& rt, int fileIndex);

    void beginPostProcessing(ItemRuntime& rt);
    void onPostStage(const QString& itemId, int stage, int percent, const QString& detail);
    void onPostFinished(const eMule::usenet::UsenetPostResult& result);

    /// Add the smallest set of unrequested recovery volumes covering @p blocks.
    /// False when there is nothing left to add, which is what ends the cycle.
    bool requestPar2Volumes(ItemRuntime& rt, int blocks);

    /// Rename the staged files into place and offer them to the share.
    void publishStaged(ItemRuntime& rt, const eMule::usenet::UsenetPostResult& result);

    void failItem(ItemRuntime& rt, const QString& message);
    void persist(ItemRuntime& rt);

    [[nodiscard]] ItemRuntime* runtimeFor(const QString& id);
    [[nodiscard]] int maxFailoverLevel() const { return m_maxLevel; }

    // std::vector, not QList: QList requires copyable elements and ItemRuntime
    // holds a unique_ptr.
    std::vector<std::unique_ptr<ItemRuntime>> m_items;

    QList<QThread*> m_threads;
    QList<UsenetWorker*> m_workers;
    QList<int> m_workerCapacity;
    QList<int> m_workerInFlight;

    QList<NewsServer> m_servers;
    int m_retryIntervalSec = 60;
    int m_maxLevel = 0;

    QThread* m_postThread = nullptr;
    UsenetPostProcessor* m_postProcessor = nullptr;

    bool m_par2Enabled    = true;
    bool m_renameEnabled  = true;
    bool m_unpackEnabled  = true;
    bool m_cleanupEnabled = true;

    QTimer* m_tickTimer = nullptr;
    qint64 m_rateLimit = 0;
    qint64 m_currentRate = 0;
    qint64 m_bytesThisTick = 0;

    /// Set when a dispatch round found every server blocked or busy. Cleared on
    /// the next tick, which is what turns a spin into a 250 ms retry.
    bool m_starved = false;

    bool m_running = false;
};

} // namespace eMule::usenet
