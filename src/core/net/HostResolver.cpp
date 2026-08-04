#include "pch.h"
/// @file HostResolver.cpp
/// @brief Implementation of the shared asynchronous host resolver.

#include "net/HostResolver.h"

#include "utils/Log.h"

#include <QHostInfo>
#include <QTimer>

#include <algorithm>
#include <utility>

namespace eMule {

namespace {

constexpr qint64 kPositiveCacheMs = 300'000;   // 5 min — DNS TTLs we cannot see anyway
constexpr qint64 kNegativeCacheMs = 60'000;    // 1 min — keeps a link paste from re-storming

bool isWantedFamily(const Address& addr, HostResolver::Preference pref)
{
    switch (pref) {
    case HostResolver::Preference::IPv4Only:
        return addr.isIPv4();
    case HostResolver::Preference::IPv6Only:
        return addr.isIPv6();
    default:
        return !addr.isNull();
    }
}

} // namespace

// ---------------------------------------------------------------------------
// Result
// ---------------------------------------------------------------------------

Address HostResolver::Result::first() const
{
    return addresses.empty() ? Address() : addresses.front();
}

Address HostResolver::Result::firstIPv4() const
{
    for (const auto& addr : addresses)
        if (addr.isIPv4())
            return addr;
    return {};
}

Address HostResolver::Result::firstIPv6() const
{
    for (const auto& addr : addresses)
        if (addr.isIPv6())
            return addr;
    return {};
}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

HostResolver::HostResolver(QObject* parent)
    : QObject(parent)
{
    m_clock.start();
}

HostResolver::~HostResolver()
{
    cancelAll();
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

int HostResolver::resolve(const QString& host, Preference pref, QObject* context,
                          Callback cb, int timeoutMs)
{
    const QString name = host.trimmed();
    if (name.isEmpty() || !cb)
        return 0;

    const int token = m_nextToken++;

    Pending pending;
    pending.host = name;
    pending.pref = pref;
    pending.cb = std::move(cb);
    if (context) {
        pending.contextConnection =
            connect(context, &QObject::destroyed, this, [this, token] { cancel(token); });
    }

    // An IP literal needs no resolver. Deliver it on the event loop anyway, so every
    // caller sees the same "callback arrives later" invariant.
    if (const Address literal = Address::fromString(name); !literal.isNull()) {
        m_pending.emplace(token, std::move(pending));
        QTimer::singleShot(0, this, [this, token, literal] {
            finish(token, {literal}, {});
        });
        return token;
    }

    if (const CacheEntry* entry = cachedEntry(name)) {
        const std::vector<Address> addresses = entry->addresses;
        const QString error = entry->errorString;
        m_pending.emplace(token, std::move(pending));
        QTimer::singleShot(0, this, [this, token, addresses, error] {
            finish(token, addresses, error);
        });
        return token;
    }

    if (timeoutMs > 0) {
        pending.timeout = std::make_unique<QTimer>();
        pending.timeout->setSingleShot(true);
        pending.timeout->setInterval(timeoutMs);
        connect(pending.timeout.get(), &QTimer::timeout, this, [this, token] {
            finish(token, {}, QStringLiteral("DNS lookup timed out"));
        });
        pending.timeout->start();
    }

    m_pending.emplace(token, std::move(pending));

    auto& waiters = m_waiters[name];
    waiters.push_back(token);
    if (waiters.size() == 1)
        startLookup(name);

    return token;
}

int HostResolver::resolve(const QString& host, Preference pref, Callback cb, int timeoutMs)
{
    return resolve(host, pref, nullptr, std::move(cb), timeoutMs);
}

void HostResolver::cancel(int token)
{
    const auto it = m_pending.find(token);
    if (it == m_pending.end())
        return;

    Pending pending = std::move(it->second);
    m_pending.erase(it);

    if (pending.contextConnection)
        disconnect(pending.contextConnection);
    if (pending.timeout)
        pending.timeout->stop();

    detachFromLookup(token, pending.host);
}

void HostResolver::cancelAll()
{
    for (auto& [token, pending] : m_pending) {
        if (pending.contextConnection)
            disconnect(pending.contextConnection);
        if (pending.timeout)
            pending.timeout->stop();
    }
    m_pending.clear();
    m_waiters.clear();

    for (const auto& [host, lookupId] : m_lookupIds)
        QHostInfo::abortHostLookup(lookupId);
    m_lookupIds.clear();
}

void HostResolver::clearCache()
{
    m_cache.clear();
}

std::vector<Address> HostResolver::orderByPreference(std::vector<Address> addresses,
                                                     Preference pref)
{
    std::vector<Address> out;
    out.reserve(addresses.size());
    for (const auto& addr : addresses) {
        if (!isWantedFamily(addr, pref))
            continue;
        if (std::find(out.begin(), out.end(), addr) == out.end())
            out.push_back(addr);
    }

    if (pref == Preference::PreferIPv4) {
        std::stable_partition(out.begin(), out.end(),
                              [](const Address& a) { return a.isIPv4(); });
    } else if (pref == Preference::PreferIPv6) {
        std::stable_partition(out.begin(), out.end(),
                              [](const Address& a) { return a.isIPv6(); });
    }

    return out;
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

void HostResolver::startLookup(const QString& host)
{
    // QHostInfo returns A and AAAA records together; the family choice is applied
    // afterwards per request, so two callers wanting different families still share
    // a single query.
    const int lookupId = QHostInfo::lookupHost(host, this, [this, host](const QHostInfo& info) {
        m_lookupIds.erase(host);

        std::vector<Address> addresses;
        addresses.reserve(static_cast<size_t>(info.addresses().size()));
        for (const auto& qaddr : info.addresses()) {
            const Address addr = Address::fromQHostAddress(qaddr);
            if (!addr.isNull())
                addresses.push_back(addr);
        }

        QString error;
        if (addresses.empty()) {
            error = info.error() == QHostInfo::NoError
                        ? QStringLiteral("no address records")
                        : info.errorString();
        }

        logDebug(QStringLiteral("HostResolver: %1 -> %2 address(es)%3")
                     .arg(host)
                     .arg(addresses.size())
                     .arg(error.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(error)));

        cacheResult(host, addresses, error);
        completeHost(host, addresses, error);
    });

    m_lookupIds[host] = lookupId;
}

void HostResolver::completeHost(const QString& host, const std::vector<Address>& addresses,
                                const QString& errorString)
{
    const auto it = m_waiters.find(host);
    if (it == m_waiters.end())
        return;

    // Detach the waiter list first: finish() must not try to abort a lookup that has
    // already completed, and a callback may start a new request for the same host.
    const std::vector<int> tokens = std::move(it->second);
    m_waiters.erase(it);

    for (const int token : tokens)
        finish(token, addresses, errorString);
}

void HostResolver::finish(int token, const std::vector<Address>& addresses,
                          const QString& errorString)
{
    const auto it = m_pending.find(token);
    if (it == m_pending.end())
        return;   // cancelled, or already delivered

    Pending pending = std::move(it->second);
    m_pending.erase(it);

    if (pending.contextConnection)
        disconnect(pending.contextConnection);
    if (pending.timeout)
        pending.timeout->stop();

    detachFromLookup(token, pending.host);

    Result result;
    result.host = pending.host;
    result.addresses = orderByPreference(addresses, pending.pref);
    result.errorString = errorString;
    if (result.addresses.empty() && result.errorString.isEmpty() && !addresses.empty())
        result.errorString = QStringLiteral("no address of the requested family");

    pending.cb(result);
}

void HostResolver::detachFromLookup(int token, const QString& host)
{
    const auto it = m_waiters.find(host);
    if (it == m_waiters.end())
        return;

    auto& tokens = it->second;
    tokens.erase(std::remove(tokens.begin(), tokens.end(), token), tokens.end());
    if (!tokens.empty())
        return;

    m_waiters.erase(it);
    if (const auto lookupIt = m_lookupIds.find(host); lookupIt != m_lookupIds.end()) {
        QHostInfo::abortHostLookup(lookupIt->second);
        m_lookupIds.erase(lookupIt);
    }
}

const HostResolver::CacheEntry* HostResolver::cachedEntry(const QString& host) const
{
    const auto it = m_cache.find(host);
    if (it == m_cache.end())
        return nullptr;
    if (it->second.expiresAtMs <= m_clock.elapsed())
        return nullptr;
    return &it->second;
}

void HostResolver::cacheResult(const QString& host, const std::vector<Address>& addresses,
                               const QString& errorString)
{
    CacheEntry entry;
    entry.addresses = addresses;
    entry.errorString = errorString;
    entry.expiresAtMs = m_clock.elapsed() + (addresses.empty() ? kNegativeCacheMs : kPositiveCacheMs);
    m_cache[host] = std::move(entry);
}

} // namespace eMule
