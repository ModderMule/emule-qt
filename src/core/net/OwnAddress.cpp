#include "pch.h"
#include "net/OwnAddress.h"
#include "net/LocalIPv6.h"

#include <QHash>
#include <QHostInfo>
#include <QMutex>
#include <QNetworkInterface>

#include <algorithm>
#include <chrono>
#include <future>
#include <memory>
#include <thread>

namespace eMule::OwnAddress {

namespace {
QMutex g_mutex;
Lookup g_lookup;
QHash<QString, QList<QHostAddress>> g_cache;   ///< normalized name -> addresses

std::optional<QList<QHostAddress>> lookupBounded(const QString& name, int timeoutMs);
} // namespace

bool isOwn(QHostAddress addr)
{
    if (addr.isNull())
        return false;
    if (addr.isLoopback() || addr == QHostAddress::AnyIPv4 || addr == QHostAddress::AnyIPv6
        || addr == QHostAddress::Any)
        return true;
    addr.setScopeId({});
    for (QHostAddress own : QNetworkInterface::allAddresses()) {
        own.setScopeId({});
        if (own.isEqual(addr, QHostAddress::TolerantConversion))
            return true;
    }
    return false;
}

QString normalizedName(const QString& name)
{
    QString n = name.trimmed().toLower();
    if (n.endsWith(u'.'))
        n.chop(1);
    return n;
}

bool isLocalhostName(const QString& name)
{
    return name == u"localhost" || name.endsWith(u".localhost");
}

bool isSelfName(const QString& name)
{
    const QString self = QHostInfo::localHostName().toLower();
    return !self.isEmpty()
        && (name == self || name == self + u".local" || name == self.section(u'.', 0, 0));
}

std::optional<QList<QHostAddress>> resolve(const QString& name)
{
    const QString n = normalizedName(name);
    Lookup lookup;
    {
        const QMutexLocker lock(&g_mutex);
        if (const auto it = g_cache.constFind(n); it != g_cache.cend())
            return *it;
        lookup = g_lookup;
    }
    auto addrs = lookup ? lookup(n, kLookupTimeoutMs) : lookupBounded(n, kLookupTimeoutMs);
    if (addrs) {
        const QMutexLocker lock(&g_mutex);
        g_cache.insert(n, *addrs);
    }
    return addrs;
}

void refresh(const QString& name, const QObject* context, std::function<void(bool answered)> done)
{
    const QString n = normalizedName(name);
    Lookup lookup;
    {
        const QMutexLocker lock(&g_mutex);
        lookup = g_lookup;
    }
    if (lookup) {
        const auto addrs = lookup(n, kLookupTimeoutMs);
        if (addrs) {
            const QMutexLocker lock(&g_mutex);
            g_cache.insert(n, *addrs);
        }
        done(addrs.has_value());
        return;
    }
    QHostInfo::lookupHost(n, context, [n, done = std::move(done)](const QHostInfo& info) {
        // HostNotFound is an answer too; anything else may be a passing outage
        const bool answered = info.error() != QHostInfo::UnknownError;
        if (answered) {
            const QMutexLocker lock(&g_mutex);
            g_cache.insert(n, info.addresses());
        }
        done(answered);
    });
}

std::optional<QList<QHostAddress>> ownBindAddressesOf(const QString& name, bool withTemporary)
{
    const QString n = normalizedName(name);
    if (isLocalhostName(n))
        return QList<QHostAddress>{};
    const auto addrs = resolve(n);
    if (!addrs)
        return std::nullopt;
    const QList<QHostAddress> temporary = withTemporary ? QList<QHostAddress>{} : temporaryIPv6();
    QList<QHostAddress> result;
    for (QHostAddress addr : *addrs) {
        // fe80:: binds only with its interface's scope id; loopback the caller adds
        if (addr.isLoopback() || addr.isLinkLocal() || !isOwn(addr))
            continue;
        addr.setScopeId({});
        if (temporary.contains(addr))   // rotates: a listener there soon goes stale
            continue;
        if (!result.contains(addr))   // DNS may repeat one
            result.append(addr);
    }
    return result;
}

QList<QHostAddress> temporaryIPv6()
{
    QList<QHostAddress> result;
    for (const LocalIPv6Address& local : scanLocalIPv6().addresses) {
        if (local.kind == IPv6AddressKind::Temporary)
            result.append(local.address.toQHostAddress());
    }
    return result;
}

void setLookup(Lookup lookup)
{
    const QMutexLocker lock(&g_mutex);
    g_lookup = std::move(lookup);
    g_cache.clear();
}

namespace {

/// QHostInfo::fromName on its own thread: it has no timeout of its own
std::optional<QList<QHostAddress>> lookupBounded(const QString& name, int timeoutMs)
{
    auto task = std::make_shared<std::packaged_task<QList<QHostAddress>()>>(
        [name] { return QHostInfo::fromName(name).addresses(); });
    auto result = task->get_future();
    std::thread([task] { (*task)(); }).detach();   // a late answer is dropped
    if (result.wait_for(std::chrono::milliseconds(timeoutMs)) != std::future_status::ready)
        return std::nullopt;
    return result.get();
}

} // namespace

} // namespace eMule::OwnAddress
