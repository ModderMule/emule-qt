#pragma once

/// @file UsenetSession.h
/// @brief Façade for the Usenet (NNTP) download engine — what the daemon owns.
///
/// Constructed unconditionally, like HttpCacheManager in CoreSession: the
/// preferences gate what it *does*, not whether it exists, so turning the
/// feature on at runtime needs no daemon restart. That also matches the
/// connect-gating convention already used for ED2K and Kad, where the enable
/// flag governs *automatic* activity and an explicit user action always works.
///
/// Lives in eMule::Usenet, which links eMule::Core. The reverse edge — core
/// reaching into usenet — must never be added, which is why DaemonApp owns this
/// object rather than AppContext.

#include "utils/Types.h"

#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QString>

#include <memory>

class QTimer;

namespace eMule::usenet {

class NntpServerPool;
class UsenetQueue;
class UsenetWatchFolder;

class UsenetSession : public QObject {
    Q_OBJECT

public:
    explicit UsenetSession(QObject* parent = nullptr);
    ~UsenetSession() override;

    /// Bring the engine up. Idempotent — a second call while running is a no-op.
    void start();

    /// Tear the engine down. Idempotent. Must complete before anything the
    /// engine references is destroyed.
    void stop();

    [[nodiscard]] bool isRunning() const { return m_running; }

    /// Re-read the news-server list and the retry interval from Preferences.
    /// Called at startup and whenever the Options page saves, so a credential
    /// change takes effect without a restart. Safe to call while running: the
    /// pool drops connections whose server changed and keeps the rest.
    void applyPreferences();

    /// Renumber every stored category index after the category list changed.
    ///
    /// Two paths, because the queue only loads its sidecars in `start()`: the
    /// live items when it is running, the `.nzbstate` files on disk when it is
    /// not. Without the second one, disabling Usenet, deleting a category and
    /// enabling it again files finished releases into a stranger's folder --
    /// ED2K has no equivalent exposure, since its queue is live for as long as
    /// the daemon is.
    void remapCategories(const QHash<uint32, uint32>& oldToNew);

    /// What one engine is asking of the shared download line.
    struct EngineDemand {
        /// Has work that wants bandwidth right now. Structural, never derived
        /// from the rate: an engine throttled to a trickle would otherwise read
        /// as uninterested and never get its share back.
        bool active = false;
        qint64 rateBytesPerSec = 0;     ///< measured, smoothed
    };

    /// maxDownload() divided between the two engines, as last published.
    struct DownloadSplit {
        uint32 ceilingKb = 0;           ///< maxDownload() it was cut from; 0 = unlimited
        qint64 usenetLimitBytes = 0;    ///< UsenetQueue::setRateLimit(); 0 = unlimited
        qint64 ed2kBudgetKb = -1;       ///< setEd2kDownloadBudget(); -1 = no split

        /// Effective caps in KB/s, sentinels resolved. 0 = unlimited.
        [[nodiscard]] qint64 usenetKb() const
        {
            return usenetLimitBytes > 0 ? usenetLimitBytes / 1024 : qint64(ceilingKb);
        }
        [[nodiscard]] qint64 ed2kKb() const
        {
            return ed2kBudgetKb >= 0 ? ed2kBudgetKb : qint64(ceilingKb);
        }
        /// Either engine held below the ceiling because the other is busy.
        [[nodiscard]] bool isThrottling() const
        {
            return ceilingKb > 0 && (usenetKb() < ceilingKb || ed2kKb() < ceilingKb);
        }

        bool operator==(const DownloadSplit&) const = default;
    };

    /// Split the one global download ceiling between ED2K and Usenet.
    ///
    /// The share is a floor while both engines are busy, never a cap on one whose
    /// counterpart is idle or under-using: each engine is held back only because
    /// the other is active, and only by what the other measurably reserves. Pure,
    /// so the arithmetic is testable without a queue.
    [[nodiscard]] static DownloadSplit computeDownloadSplit(uint32 ceilingKb,
                                                            int usenetSharePercent,
                                                            EngineDemand usenet,
                                                            EngineDemand ed2k);

    /// The split currently in force. Unthrottled while the engine is stopped.
    [[nodiscard]] DownloadSplit lastSplit() const { return m_split; }

    [[nodiscard]] NntpServerPool* pool() const { return m_pool.get(); }

    /// The download queue. Always present, even while the engine is stopped —
    /// the GUI can list a paused queue with the feature switched off.
    [[nodiscard]] UsenetQueue* queue() const { return m_queue.get(); }

    /// The .nzb intake folder. Present whether or not one is configured — an
    /// empty watchDir is the off state, the same way an empty account list is
    /// for indexers.
    [[nodiscard]] UsenetWatchFolder* watchFolder() const { return m_watchFolder.get(); }

signals:
    /// Forwarded straight from the queue. DaemonApp turns these into IPC pushes;
    /// UsenetQueue itself knows nothing about IPC, and core knows nothing about
    /// either — this signal is the whole of the seam.
    void itemChanged(const QString& id);
    void itemAdded(const QString& id);
    void itemRemoved(const QString& id);
    void itemFinished(const QString& id, bool success, const QString& message);
    void enginePausedChanged(bool paused);

private:
    /// Recompute how the one global download budget is split between ED2K and
    /// Usenet, and publish both halves. Runs every kBandwidthTickMs.
    void updateBandwidthSplit();

    /// One log line when the split moves materially; see the .cpp for the gate.
    void logSplitChange(const DownloadSplit& split);

    std::unique_ptr<NntpServerPool> m_pool;
    std::unique_ptr<UsenetQueue> m_queue;
    std::unique_ptr<UsenetWatchFolder> m_watchFolder;
    QTimer* m_bandwidthTimer = nullptr;
    DownloadSplit m_split;
    DownloadSplit m_loggedSplit;
    QElapsedTimer m_splitLogClock;
    bool m_running = false;

    /// Last preferences revision whose subject rules were compiled for the log.
    /// 0 is never a live revision, so the first call always reports.
    quint64 m_subjectPatternRevision = 0;
};

/// The daemon's engine, published for the IPC handlers.
///
/// The same shape as AppContext's pointers, and published for the same reason:
/// IpcClientHandler deliberately owns no back-pointer to DaemonApp. It cannot
/// live in AppContext itself, because that is core and core must never depend on
/// eMule::Usenet — which is the whole reason DaemonApp owns the session.
///
/// Set by DaemonApp on construction and cleared before teardown. Null means the
/// daemon has not started the engine yet; every handler must check.
extern UsenetSession* theUsenetSession;

} // namespace eMule::usenet
