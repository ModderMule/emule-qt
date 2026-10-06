#include "pch.h"
/// @file HttpCacheReach.cpp
/// @brief Who can reach a cache host — implementation.

#include "httpcache/HttpCacheReach.h"

#include <QHostAddress>

#include <algorithm>

namespace eMule {

CacheReach classifyCacheAddress(const Address& addr)
{
    if (addr.isNull())
        return CacheReach::Unknown;

    if (addr.toQHostAddress().isLoopback())
        return CacheReach::Loopback;

    return addr.isLan() ? CacheReach::Lan : CacheReach::Public;
}

CacheReach classifyCacheHostLiteral(const QString& host)
{
    if (host.compare(QLatin1String("localhost"), Qt::CaseInsensitive) == 0
        || host.endsWith(QLatin1String(".localhost"), Qt::CaseInsensitive)) {
        return CacheReach::Loopback;
    }

    QHostAddress parsed;
    if (!parsed.setAddress(host))
        return CacheReach::Unknown;   // a name

    // fromQHostAddress() folds ::ffff:a.b.c.d to IPv4; the wildcard comes back null.
    return classifyCacheAddress(Address::fromQHostAddress(parsed));
}

CacheReach strictestCacheReach(CacheReach a, CacheReach b)
{
    return std::max(a, b);
}

bool cacheReachAllowsPeer(CacheReach reach, const Address& peer, bool lanMode)
{
    switch (reach) {
    case CacheReach::Public:
        return true;
    case CacheReach::Lan:
        // isLan() covers loopback too: a peer on this machine reaches our LAN.
        return lanMode && peer.isLan();
    case CacheReach::Loopback:
        return lanMode && classifyCacheAddress(peer) == CacheReach::Loopback;
    case CacheReach::Unknown:
        break;
    }
    return false;
}

} // namespace eMule
