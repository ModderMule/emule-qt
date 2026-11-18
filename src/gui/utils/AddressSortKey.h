#pragma once

/// @file AddressSortKey.h
/// @brief Display text and fixed-width sort key for an address column.

#include <QHostAddress>
#include <QString>

#include <cstdint>

namespace eMule {

/// A fixed-width sort key for an address, because the displayed form sorts by
/// leading digit: "192.168.1.10" lands before "192.168.1.9", "213.x" among
/// IPv6 "2a00:…", and "2001:b07::" before "2001:ac8::".
///
/// IPv4 as 8 hex digits, IPv6 as all 32 (groups padded, "::" expanded), each
/// behind its family so every IPv4 row sorts ahead of every IPv6 one. A name
/// (no literal) sorts after both. Then the port. Stays a QString, so the
/// column keeps a single type.
[[nodiscard]] inline QString addressSortKey(const QString& ip, uint16_t port)
{
    QString host;
    const QHostAddress a(ip);
    bool v4 = false;
    const quint32 v4Addr = a.toIPv4Address(&v4);   // also takes ::ffff:a.b.c.d
    if (v4) {
        host = QStringLiteral("4:%1").arg(v4Addr, 8, 16, QLatin1Char('0'));
    } else if (a.protocol() == QAbstractSocket::IPv6Protocol) {
        const Q_IPV6ADDR v6 = a.toIPv6Address();
        host = QStringLiteral("6:")
               + QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(v6.c), 16).toHex());
    } else {
        host = QStringLiteral("9:") + ip;
    }
    return QStringLiteral("%1:%2").arg(host).arg(port, 5, 10, QLatin1Char('0'));
}

/// Host and port of a "host:port" / "[v6]:port" / bare-address text (port 0: none).
struct AddressPort {
    QString host;
    uint16_t port = 0;
};

[[nodiscard]] inline AddressPort splitEndpoint(const QString& endpoint)
{
    AddressPort e{endpoint};
    if (endpoint.startsWith(QLatin1Char('['))) {
        const qsizetype close = endpoint.indexOf(QLatin1Char(']'));
        if (close > 0) {
            e.host = endpoint.mid(1, close - 1);
            if (endpoint.mid(close + 1).startsWith(QLatin1Char(':')))
                e.port = static_cast<uint16_t>(endpoint.mid(close + 2).toUInt());
        }
    } else if (endpoint.count(QLatin1Char(':')) == 1) {
        const qsizetype colon = endpoint.indexOf(QLatin1Char(':'));
        e.host = endpoint.left(colon);
        e.port = static_cast<uint16_t>(endpoint.mid(colon + 1).toUInt());
    }
    return e;
}

/// addressSortKey of a "host:port" / "[v6]:port" / bare-address text; anything
/// else (I2P, hidden) after the addresses, by its text.
[[nodiscard]] inline QString endpointSortKey(const QString& endpoint)
{
    const AddressPort e = splitEndpoint(endpoint);
    return addressSortKey(e.host, e.port);
}

/// "host:port", bracketing an IPv6 literal.
[[nodiscard]] inline QString addressWithPort(const QString& ip, uint16_t port)
{
    return ip.contains(QLatin1Char(':')) ? QStringLiteral("[%1]:%2").arg(ip).arg(port)
                                         : QStringLiteral("%1:%2").arg(ip).arg(port);
}

} // namespace eMule
