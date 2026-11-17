#include "pch.h"
/// @file CdnRanges.cpp
/// @brief CDN address ranges — sorted, binary search.

#include "geo/CdnRanges.h"

#include <QList>

#include <algorithm>
#include <utility>

namespace eMule {

namespace {

constexpr const char* kRanges[] = {
#include "geo/CdnRanges.inc"
};

using V6 = std::pair<quint64, quint64>;   // hi, lo

V6 v6Of(const Q_IPV6ADDR& a)
{
    quint64 hi = 0;
    quint64 lo = 0;
    for (int i = 0; i < 8; ++i) {
        hi = hi << 8 | a[i];
        lo = lo << 8 | a[i + 8];
    }
    return {hi, lo};
}

struct Table {
    QList<std::pair<quint32, quint32>> v4;   // first, last; sorted, no overlap
    QList<std::pair<V6, V6>> v6;

    Table()
    {
        for (const char* r : kRanges) {
            const auto [net, len] = QHostAddress::parseSubnet(QString::fromLatin1(r));
            if (net.protocol() == QAbstractSocket::IPv4Protocol) {
                const quint32 first = net.toIPv4Address();
                const quint32 mask = len == 0 ? 0 : ~quint32(0) << (32 - len);
                v4.append({first & mask, (first & mask) | ~mask});
            } else if (net.protocol() == QAbstractSocket::IPv6Protocol) {
                const V6 a = v6Of(net.toIPv6Address());
                const quint64 hiMask = len >= 64 ? ~quint64(0) : len == 0 ? 0 : ~quint64(0) << (64 - len);
                const quint64 loMask = len <= 64 ? 0 : len == 128 ? ~quint64(0) : ~quint64(0) << (128 - len);
                const V6 first{a.first & hiMask, a.second & loMask};
                v6.append({first, {first.first | ~hiMask, first.second | ~loMask}});
            }
        }
        std::ranges::sort(v4);
        std::ranges::sort(v6);
    }
};

template <typename List, typename Key>
bool inRanges(const List& list, const Key& key)
{
    // the last range starting at or before key
    auto it = std::ranges::upper_bound(list, key, {}, &List::value_type::first);
    return it != list.begin() && key <= std::prev(it)->second;
}

} // namespace

bool isCdnAddress(const QHostAddress& address)
{
    static const Table table;
    bool ok = false;
    if (const quint32 v4 = address.toIPv4Address(&ok); ok)   // ::ffff:a.b.c.d too
        return inRanges(table.v4, v4);
    if (address.protocol() != QAbstractSocket::IPv6Protocol)
        return false;
    return inRanges(table.v6, v6Of(address.toIPv6Address()));
}

} // namespace eMule
