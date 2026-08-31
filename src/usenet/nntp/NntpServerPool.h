#pragma once

/// @file NntpServerPool.h
/// @brief Connection leasing across priority levels, with per-server backoff.
///
/// Semantics ported from NZBGet's `daemon/nntp/ServerPool.h`. The shape that
/// matters is `acquire(level, ignoreServers)`:
///
///   - Servers are grouped into **levels**. Level 0 is tried first; a higher
///     level is only reached when every server below reported the article
///     *missing* (430), never when one merely timed out. That is the difference
///     between a fill server earning its money and a fill server being hammered
///     because the main provider was briefly busy.
///   - Within a level, servers are equals and rotate, so no single account
///     absorbs the whole queue.
///   - `ignoreServers` carries the accounts already asked for this article, so
///     an escalation never asks the same one twice.
///   - A server that fails at the transport level is **blocked** for a retry
///     interval rather than removed, because "too many connections" is the most
///     common failure on Usenet and it cures itself.
///
/// Connections are created lazily and kept: the TLS handshake and the AUTHINFO
/// round trip cost more than the article fetch that follows, so a pool that
/// reconnects per article spends most of its time in setup.

#include "nntp/NewsServer.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include <memory>
#include <vector>

namespace eMule::usenet {

class NntpSocket;

class NntpServerPool : public QObject {
    Q_OBJECT

public:
    explicit NntpServerPool(QObject* parent = nullptr);
    ~NntpServerPool() override;

    /// Replace the configured servers. Connections to servers that are gone or
    /// whose parameters changed are dropped; the generation counter is bumped
    /// so an in-flight download can notice its plan is stale.
    void setServers(QList<NewsServer> servers);
    [[nodiscard]] const QList<NewsServer>& servers() const { return m_servers; }

    /// Highest normalized level, i.e. the last rung of the failover ladder.
    /// User-visible levels are arbitrary integers (0, 5, 10 is a common way to
    /// leave room to insert); they are normalized to 0..n here so a caller can
    /// simply increment.
    [[nodiscard]] int maxLevel() const { return m_maxLevel; }

    /// Normalized level of one server, or -1 if it is not configured.
    [[nodiscard]] int levelOf(const QString& serverKey) const;

    /// Lease a connection on @p level, skipping disabled and blocked servers and
    /// any whose key() appears in @p ignoreServers. Returns nullptr when every
    /// candidate is blocked, excluded, or at its connection limit — a "try again
    /// shortly", not an error.
    ///
    /// The returned socket may still be connecting: wait for its ready() or
    /// failed(). Leasing before the handshake completes is what lets the pool
    /// overlap connection setup with the queue's scheduling.
    [[nodiscard]] NntpSocket* acquire(int level, const QStringList& ignoreServers = {});

    /// Return a lease. @p reusable false drops the connection instead of
    /// pooling it — a socket that failed is not reusable, and handing it out
    /// again is how one dead provider stalls a whole queue.
    void release(NntpSocket* socket, bool reusable = true);

    /// Back this server off for the retry interval.
    void blockServer(const QString& serverKey);
    [[nodiscard]] bool isServerBlocked(const QString& serverKey) const;

    /// Seconds a blocked server stays out of rotation. 0 disables blocking.
    void setRetryInterval(int seconds) { m_retryIntervalSec = seconds; }
    [[nodiscard]] int retryInterval() const { return m_retryIntervalSec; }

    /// Bumped whenever the server list changes.
    [[nodiscard]] int generation() const { return m_generation; }

    /// Close and drop every pooled connection that is not currently leased.
    void closeIdleConnections();

    [[nodiscard]] int busyCount() const;
    [[nodiscard]] int totalCount() const { return static_cast<int>(m_connections.size()); }

private:
    struct Lease {
        std::unique_ptr<NntpSocket> socket;
        QString serverKey;
        int level = 0;
        bool inUse = false;
    };

    /// Abort @p lease's socket and hand it to the event loop to destroy.
    ///
    /// Never `delete` a pooled socket directly. release() is routinely reached
    /// from *inside* that socket's own readyRead handler — drain() ->
    /// handleBodyLine() -> commandFinished -> ArticleFetcher::finished ->
    /// UsenetWorker::finishJob -> release() — and destroying it there leaves
    /// QAbstractSocketPrivate::canReadNotification() reading freed memory the
    /// moment the stack unwinds. deleteLater() is the same answer UsenetWorker
    /// already uses for the ArticleFetcher, for the same reason.
    static void retire(Lease& lease);

    [[nodiscard]] int connectionsFor(const QString& serverKey) const;
    [[nodiscard]] qint64 nowSeconds() const;
    void normalizeLevels();
    void dropConnections(const QString& serverKey);

    QList<NewsServer> m_servers;
    QHash<QString, int> m_normalizedLevel;   ///< server key -> 0..maxLevel
    QHash<QString, qint64> m_blockedUntil;   ///< server key -> epoch seconds

    std::vector<Lease> m_connections;

    /// Where the next acquire() on each level starts looking, so servers on one
    /// level rotate instead of the first one absorbing everything.
    QHash<int, int> m_rotation;

    int m_maxLevel = 0;
    int m_retryIntervalSec = 60;
    int m_generation = 0;
};

} // namespace eMule::usenet
