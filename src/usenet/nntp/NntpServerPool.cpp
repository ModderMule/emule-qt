#include "nntp/NntpServerPool.h"

#include "nntp/NntpSocket.h"
#include "utils/Log.h"

#include <QDateTime>
#include <QTimer>

#include <algorithm>
#include <limits>
#include <set>

namespace eMule::usenet {

QList<int> nntpLevelLadder(const QList<NewsServer>& servers)
{
    std::set<int> distinct;
    for (const auto& s : servers) {
        if (s.enabled && s.isValid())
            distinct.insert(s.level);
    }
    return QList<int>(distinct.cbegin(), distinct.cend());
}

QHash<QString, NntpConnectionBucket> nntpConnectionBuckets(const QList<NewsServer>& servers)
{
    // Two passes: the group limits have to be known before a member can be
    // pointed at one, and a member's own limit is only the answer when it is
    // alone in its bucket.
    QHash<int, int> groupLimit;
    for (const auto& s : servers) {
        if (!s.enabled || !s.isValid() || s.group <= 0)
            continue;
        const auto it = groupLimit.constFind(s.group);
        groupLimit.insert(s.group, it == groupLimit.cend()
                                       ? s.maxConnections
                                       : std::min(*it, s.maxConnections));
    }

    QHash<QString, NntpConnectionBucket> out;
    for (const auto& s : servers) {
        if (!s.enabled || !s.isValid())
            continue;
        if (s.group > 0) {
            out.insert(s.key(), {QStringLiteral("g%1").arg(s.group),
                                 groupLimit.value(s.group)});
        } else {
            out.insert(s.key(), {QStringLiteral("s:") + s.key(), s.maxConnections});
        }
    }
    return out;
}

NntpServerPool::NntpServerPool(QObject* parent)
    : QObject(parent)
{
}

NntpServerPool::~NntpServerPool() = default;

void NntpServerPool::setServers(QList<NewsServer> servers)
{
    // Drop connections to servers that are gone or whose parameters changed.
    // Comparing the whole record, not just the key, matters: a password change
    // keeps the key identical while invalidating every authenticated socket.
    QHash<QString, NewsServer> incoming;
    for (const auto& s : servers)
        incoming.insert(s.key(), s);

    for (const auto& old : m_servers) {
        const auto it = incoming.constFind(old.key());
        const bool gone = it == incoming.cend();
        const bool changed = !gone
            && (it->pass != old.pass || it->user != old.user
                || it->tlsMode != old.tlsMode || it->certVerification != old.certVerification
                || !it->enabled);
        if (gone || changed)
            dropConnections(old.key());
    }

    m_servers = std::move(servers);
    // New configuration, fresh start: a cap measured against the old limits
    // says nothing about the new ones.
    m_caps.clear();
    rebuildServerIndex();
    ++m_generation;
}

int NntpServerPool::levelOf(const QString& serverKey) const
{
    return m_normalizedLevel.value(serverKey, -1);
}

NntpSocket* NntpServerPool::acquire(int level, const QStringList& ignoreServers)
{
    // Build the candidate list for this level, in configuration order.
    QList<const NewsServer*> candidates;
    for (const auto& s : m_servers) {
        if (!s.enabled || !s.isValid())
            continue;
        if (m_normalizedLevel.value(s.key(), -1) != level)
            continue;
        if (ignoreServers.contains(s.key()))
            continue;
        if (isServerBlocked(s.key()))
            continue;
        candidates.append(&s);
    }
    if (candidates.isEmpty())
        return nullptr;

    // Rotate the starting point so one account on a level does not absorb the
    // whole queue while its neighbours sit idle.
    const int start = m_rotation.value(level, 0) % candidates.size();

    for (int i = 0; i < candidates.size(); ++i) {
        const NewsServer* server = candidates.at((start + i) % candidates.size());
        const QString key = server->key();

        // An idle pooled connection is far cheaper than a new one: TLS plus
        // AUTHINFO costs more round trips than the article fetch that follows.
        for (auto& lease : m_connections) {
            if (lease.inUse || lease.serverKey != key)
                continue;
            if (!lease.socket->isReady())
                continue;
            lease.inUse = true;
            m_rotation[level] = (start + i + 1) % candidates.size();
            return lease.socket.get();
        }

        // Grouped accounts share one budget, so the count is per bucket rather
        // than per key. No std::max(1, ...) floor: UsenetQueue divides
        // maxConnections across workers, and a slice that divided down to zero
        // opening one connection anyway is exactly the replication the split
        // exists to prevent. A limit of 0 means this pool may not lease this
        // account at all.
        const NntpConnectionBucket bucket = m_buckets.value(key);
        const int limit = effectiveLimit(bucket);
        if (limit <= 0 || connectionsInBucket(bucket.id) >= limit)
            continue;

        auto socket = std::make_unique<NntpSocket>();
        NntpSocket* raw = socket.get();
        m_connections.push_back(Lease{std::move(socket), key, bucket.id, level, true});
        raw->setProxy(m_proxy);
        raw->connectToServer(*server);
        m_rotation[level] = (start + i + 1) % candidates.size();
        return raw;
    }

    // Every server on this level is at its connection limit. Not an error and
    // not a reason to escalate — the article is still here, just wait.
    return nullptr;
}

void NntpServerPool::release(NntpSocket* socket, bool reusable)
{
    if (!socket)
        return;

    const auto it = std::ranges::find_if(m_connections, [socket](const Lease& l) {
        return l.socket.get() == socket;
    });
    if (it == m_connections.end())
        return;

    // retireOnRelease: the server was backed off or reconfigured while this
    // connection was out. It could not be dropped then without stranding the
    // article on it, so this is where it goes.
    if (reusable && socket->isReady() && !it->retireOnRelease) {
        it->inUse = false;
        return;
    }

    retire(*it);
    m_connections.erase(it);
}

void NntpServerPool::retire(Lease& lease)
{
    if (!lease.socket)
        return;

    lease.socket->abort();

    // Ownership goes to the event loop. A pending deleteLater is flushed when the
    // thread's event loop exits, so this does not leak at shutdown either.
    lease.socket.release()->deleteLater();
}

void NntpServerPool::blockServer(const QString& serverKey)
{
    if (m_retryIntervalSec <= 0)
        return;

    // Every sibling article that was in flight on a dead server fails too, and
    // each one lands here. Say it once per block, not once per article.
    const bool alreadyBlocked = isServerBlocked(serverKey);
    m_blockedUntil.insert(serverKey, nowSeconds() + m_retryIntervalSec);
    if (!alreadyBlocked) {
        logWarning(QStringLiteral("Usenet: %1 backed off for %2 s")
                       .arg(serverKey)
                       .arg(m_retryIntervalSec));
    }

    // A blocked server's pooled connections are not merely unused, they are
    // suspect — the block exists because one of them failed.
    dropConnections(serverKey);
}

int NntpServerPool::limitConnections(const QString& serverKey)
{
    if (m_retryIntervalSec <= 0)
        return -1;
    const auto bucketIt = m_buckets.constFind(serverKey);
    if (bucketIt == m_buckets.cend())
        return -1;
    const NntpConnectionBucket& bucket = *bucketIt;

    // What is open now is what the provider accepted. Still-connecting leases
    // count: most get in, and one that does not lowers the cap again.
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    int limit = std::min(connectionsInBucket(bucket.id), bucket.limit);

    const auto capIt = m_caps.constFind(bucket.id);
    const bool active = capIt != m_caps.cend() && capIt->untilMs > now;
    if (active)
        limit = std::min(limit, capIt->limit);
    const bool lowered = !active || limit < capIt->limit;

    // A refusal during the hold restarts it: the limit is still being hit.
    m_caps.insert(bucket.id, {limit, now + qint64(m_retryIntervalSec) * 1000});
    armCapTimer();

    if (!lowered)
        return -1;
    emit capacityChanged();
    return limit;
}

bool NntpServerPool::isServerBlocked(const QString& serverKey) const
{
    const auto it = m_blockedUntil.constFind(serverKey);
    if (it == m_blockedUntil.cend())
        return false;
    return *it > nowSeconds();
}

void NntpServerPool::closeIdleConnections()
{
    std::erase_if(m_connections, [](Lease& lease) {
        if (lease.inUse)
            return false;
        retire(lease);
        return true;
    });
}

int NntpServerPool::capacity() const
{
    // Distinct buckets, not rows: two hostnames of one grouped account share a
    // budget, so counting both would let the queue dispatch more work than this
    // pool can ever lease and every excess job would come back "no server".
    QHash<QString, int> seen;
    for (auto it = m_buckets.cbegin(); it != m_buckets.cend(); ++it)
        seen.insert(it->id, effectiveLimit(*it));

    int total = 0;
    for (const int limit : std::as_const(seen))
        total += std::max(0, limit);
    return total;
}

int NntpServerPool::busyCount() const
{
    return static_cast<int>(std::ranges::count_if(m_connections,
                                                  [](const Lease& l) { return l.inUse; }));
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

int NntpServerPool::connectionsInBucket(const QString& bucket) const
{
    return static_cast<int>(std::ranges::count_if(
        m_connections, [&bucket](const Lease& l) { return l.bucket == bucket; }));
}

int NntpServerPool::effectiveLimit(const NntpConnectionBucket& bucket) const
{
    const auto it = m_caps.constFind(bucket.id);
    if (it == m_caps.cend() || it->untilMs <= QDateTime::currentMSecsSinceEpoch())
        return bucket.limit;
    return std::min(bucket.limit, it->limit);
}

void NntpServerPool::armCapTimer()
{
    if (m_caps.isEmpty()) {
        if (m_capTimer)
            m_capTimer->stop();
        return;
    }
    if (!m_capTimer) {
        m_capTimer = new QTimer(this);
        m_capTimer->setSingleShot(true);
        connect(m_capTimer, &QTimer::timeout, this, &NntpServerPool::onCapTimer);
    }

    qint64 next = std::numeric_limits<qint64>::max();
    for (const Cap& cap : std::as_const(m_caps))
        next = std::min(next, cap.untilMs);
    const qint64 delay = std::max<qint64>(0, next - QDateTime::currentMSecsSinceEpoch());
    m_capTimer->start(int(std::min<qint64>(delay, std::numeric_limits<int>::max())));
}

void NntpServerPool::onCapTimer()
{
    // The hold is over: back to the configured limit. Connections past what the
    // provider allows are refused again and cap it again. That costs one failed
    // connection per probe, never the ones already working.
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    bool lifted = false;
    for (auto it = m_caps.begin(); it != m_caps.end();) {
        if (it->untilMs <= now) {
            it = m_caps.erase(it);
            lifted = true;
        } else {
            ++it;
        }
    }
    armCapTimer();
    if (lifted)
        emit capacityChanged();
}

qint64 NntpServerPool::nowSeconds() const
{
    return QDateTime::currentSecsSinceEpoch();
}

void NntpServerPool::rebuildServerIndex()
{
    m_normalizedLevel.clear();
    m_rotation.clear();
    m_maxLevel = 0;
    m_buckets = nntpConnectionBuckets(m_servers);

    // The ladder comes from the shared function, not from a second copy of the
    // rule: UsenetQueue decides when the ladder is exhausted and this decides who
    // is on each rung, so the two numbering the rungs differently is a silent
    // routing bug.
    const QList<int> ladder = nntpLevelLadder(m_servers);
    if (ladder.isEmpty())
        return;

    for (const auto& s : m_servers) {
        if (s.enabled && s.isValid())
            m_normalizedLevel.insert(s.key(), int(ladder.indexOf(s.level)));
    }
    m_maxLevel = int(ladder.size()) - 1;
}

void NntpServerPool::dropConnections(const QString& serverKey)
{
    std::erase_if(m_connections, [&serverKey](Lease& lease) {
        if (lease.serverKey != serverKey)
            return false;

        // A leased socket is mid-article. abort() sets Disconnected before it
        // touches the QTcpSocket, so onSocketDisconnected() raises no failed()
        // and the article on it would simply never finish: its in-flight slot
        // stays taken, its item stalls until the next restart, and the job's
        // pointer to this socket dangles into UsenetWorker::shutdown(). Let the
        // holder finish and drop the lease in release() instead.
        if (lease.inUse) {
            lease.retireOnRelease = true;
            return false;
        }

        retire(lease);
        return true;
    });
}

} // namespace eMule::usenet
