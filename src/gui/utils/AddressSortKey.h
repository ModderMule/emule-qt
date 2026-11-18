#pragma once

/// @file AddressSortKey.h
/// @brief Display text and fixed-width sort key for an address column.

#include <QString>
#include <QStringList>

#include <cstdint>

namespace eMule {

/// A fixed-width sort key for an address, because the displayed form sorts by
/// leading digit: "192.168.1.10" lands before "192.168.1.9".
///
/// An IPv6 literal has no octets to pad and is left as it is. The key stays a
/// QString either way, so the column keeps a single type and every IPv4 row
/// sorts ahead of every IPv6 one — arbitrary, but stable and grouped.
[[nodiscard]] inline QString addressSortKey(const QString& ip, uint16_t port)
{
    QString host = ip;
    const QStringList octets = ip.split(QLatin1Char('.'));
    if (octets.size() == 4) {
        QStringList padded;
        padded.reserve(4);
        for (const QString& octet : octets)
            padded << QStringLiteral("%1").arg(octet.toUInt(), 3, 10, QLatin1Char('0'));
        host = padded.join(QLatin1Char('.'));
    }
    return QStringLiteral("%1:%2").arg(host).arg(port, 5, 10, QLatin1Char('0'));
}

/// "host:port", bracketing an IPv6 literal.
[[nodiscard]] inline QString addressWithPort(const QString& ip, uint16_t port)
{
    return ip.contains(QLatin1Char(':')) ? QStringLiteral("[%1]:%2").arg(ip).arg(port)
                                         : QStringLiteral("%1:%2").arg(ip).arg(port);
}

} // namespace eMule
