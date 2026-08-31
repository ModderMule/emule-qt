#include "nntp/NntpServerPool.h"

#include "nntp/NntpSocket.h"
#include "utils/Log.h"

#include <QDateTime>

#include <algorithm>
#include <set>

namespace eMule::usenet {

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
    normalizeLevels();
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

        if (connectionsFor(key) >= std::max(1, server->maxConnections))
            continue;

        auto socket = std::make_unique<NntpSocket>();
        NntpSocket* raw = socket.get();
        m_connections.push_back(Lease{std::move(socket), key, level, true});
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

    if (reusable && socket->isReady()) {
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

    m_blockedUntil.insert(serverKey, nowSeconds() + m_retryIntervalSec);
    logWarning(QStringLiteral("Usenet: %1 backed off for %2 s")
                   .arg(serverKey)
                   .arg(m_retryIntervalSec));

    // A blocked server's pooled connections are not merely unused, they are
    // suspect — the block exists because one of them failed.
    dropConnections(serverKey);
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

int NntpServerPool::busyCount() const
{
    return static_cast<int>(std::ranges::count_if(m_connections,
                                                  [](const Lease& l) { return l.inUse; }));
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

int NntpServerPool::connectionsFor(const QString& serverKey) const
{
    return static_cast<int>(std::ranges::count_if(
        m_connections, [&serverKey](const Lease& l) { return l.serverKey == serverKey; }));
}

qint64 NntpServerPool::nowSeconds() const
{
    return QDateTime::currentSecsSinceEpoch();
}

void NntpServerPool::normalizeLevels()
{
    m_normalizedLevel.clear();
    m_rotation.clear();
    m_maxLevel = 0;

    // Users write levels as 0/5/10 to leave room to insert later. Collapse them
    // to 0..n so the failover ladder is just "level + 1" and no rung is empty —
    // an empty rung would end the escalation early, silently.
    std::set<int> distinct;
    for (const auto& s : m_servers) {
        if (s.enabled && s.isValid())
            distinct.insert(s.level);
    }
    if (distinct.empty())
        return;

    QHash<int, int> map;
    int next = 0;
    for (int level : distinct)
        map.insert(level, next++);

    for (const auto& s : m_servers) {
        if (s.enabled && s.isValid())
            m_normalizedLevel.insert(s.key(), map.value(s.level));
    }
    m_maxLevel = next - 1;
}

void NntpServerPool::dropConnections(const QString& serverKey)
{
    std::erase_if(m_connections, [&serverKey](Lease& lease) {
        if (lease.serverKey != serverKey)
            return false;
        retire(lease);
        return true;
    });
}

} // namespace eMule::usenet
