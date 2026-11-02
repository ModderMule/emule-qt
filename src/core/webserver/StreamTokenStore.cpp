#include "pch.h"
/// @file StreamTokenStore.cpp
/// @brief Scoped, rotating stream tokens.

#include "webserver/StreamTokenStore.h"

#include "crypto/SecretCompare.h"

#include <QDateTime>
#include <QMutexLocker>
#include <QRandomGenerator>

namespace eMule {

StreamTokenStore::StreamTokenStore(Clock clock)
    : m_clock(std::move(clock))
{
}

QString StreamTokenStore::mint(const QString& scope, std::chrono::milliseconds idleTtl)
{
    QByteArray bytes(16, Qt::Uninitialized);
    QRandomGenerator::system()->fillRange(reinterpret_cast<quint32*>(bytes.data()),
                                          bytes.size() / qsizetype(sizeof(quint32)));
    const QString token = QString::fromLatin1(bytes.toHex());

    const QMutexLocker lock(&m_mutex);
    const qint64 nowMs = now();
    dropExpired(nowMs);
    QList<Entry>& list = m_byScope[scope];
    while (list.size() >= kMaxPerScope)
        list.removeFirst();   // oldest first
    list.append({token, nowMs, static_cast<qint64>(idleTtl.count())});
    return token;
}

std::optional<QString> StreamTokenStore::check(QStringView token)
{
    if (token.isEmpty())
        return std::nullopt;
    const QByteArray wanted = token.toLatin1();

    const QMutexLocker lock(&m_mutex);
    const qint64 nowMs = now();
    dropExpired(nowMs);

    // Compare against every token, no early exit, so timing says nothing.
    std::optional<QString> scope;
    Entry* hit = nullptr;
    for (auto it = m_byScope.begin(); it != m_byScope.end(); ++it) {
        for (Entry& e : *it) {
            if (constantTimeEquals(wanted, e.token.toLatin1()) && !hit) {
                hit = &e;
                scope = it.key();
            }
        }
    }
    if (hit)
        hit->lastUseMs = nowMs;
    return scope;
}

void StreamTokenStore::touchScope(const QString& scope)
{
    const QMutexLocker lock(&m_mutex);
    const qint64 nowMs = now();
    dropExpired(nowMs);
    if (const auto it = m_byScope.find(scope); it != m_byScope.end()) {
        for (Entry& e : *it)
            e.lastUseMs = nowMs;
    }
}

void StreamTokenStore::revokeScope(const QString& scope)
{
    const QMutexLocker lock(&m_mutex);
    m_byScope.remove(scope);
}

void StreamTokenStore::clear()
{
    const QMutexLocker lock(&m_mutex);
    m_byScope.clear();
}

qsizetype StreamTokenStore::count(const QString& scope) const
{
    const QMutexLocker lock(&m_mutex);
    const auto it = m_byScope.constFind(scope);
    return it == m_byScope.constEnd() ? 0 : it->size();
}

qint64 StreamTokenStore::now() const
{
    return m_clock ? m_clock() : QDateTime::currentMSecsSinceEpoch();
}

void StreamTokenStore::dropExpired(qint64 nowMs)
{
    for (auto it = m_byScope.begin(); it != m_byScope.end(); ) {
        it->removeIf([nowMs](const Entry& e) {
            return e.idleMs > 0 && nowMs - e.lastUseMs > e.idleMs;
        });
        if (it->isEmpty())
            it = m_byScope.erase(it);
        else
            ++it;
    }
}

} // namespace eMule
