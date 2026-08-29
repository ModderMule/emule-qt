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

#include <memory>

namespace eMule::usenet {

class NntpServerPool;

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

private:
    std::unique_ptr<NntpServerPool> m_pool;
    bool m_running = false;
};

} // namespace eMule::usenet
