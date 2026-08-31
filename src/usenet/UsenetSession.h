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

#include <QObject>
#include <QString>

#include <memory>

class QTimer;

namespace eMule::usenet {

class NntpServerPool;
class UsenetQueue;

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

    [[nodiscard]] NntpServerPool* pool() const { return m_pool.get(); }

    /// The download queue. Always present, even while the engine is stopped —
    /// the GUI can list a paused queue with the feature switched off.
    [[nodiscard]] UsenetQueue* queue() const { return m_queue.get(); }

signals:
    /// Forwarded straight from the queue. DaemonApp turns these into IPC pushes;
    /// UsenetQueue itself knows nothing about IPC, and core knows nothing about
    /// either — this signal is the whole of the seam.
    void itemChanged(const QString& id);
    void itemAdded(const QString& id);
    void itemRemoved(const QString& id);
    void itemFinished(const QString& id, bool success, const QString& message);

private:
    /// Recompute how the one global download budget is split between ED2K and
    /// Usenet, and publish both halves. Runs once a second.
    void updateBandwidthSplit();

    std::unique_ptr<NntpServerPool> m_pool;
    std::unique_ptr<UsenetQueue> m_queue;
    QTimer* m_bandwidthTimer = nullptr;
    bool m_running = false;
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
