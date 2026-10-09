#include "pch.h"
/// @file ListenConflict.cpp
/// @brief Finds local addresses another program already listens on at a port.

#include "net/ListenConflict.h"

#include <QNetworkInterface>
#include <QStringList>
#include <QTcpServer>
#include <QUdpSocket>

namespace eMule {

QList<QHostAddress> heldLocalAddresses(const QHostAddress& listenAddr, quint16 port)
{
    QList<QHostAddress> held;
    if (port == 0)
        return held;

    const bool any = listenAddr == QHostAddress(QHostAddress::Any);
    const bool v4 = any || listenAddr == QHostAddress(QHostAddress::AnyIPv4);
    const bool v6 = any || listenAddr == QHostAddress(QHostAddress::AnyIPv6);
    if (!v4 && !v6)
        return held;    // a specific address: listen() fails by itself

    const auto locals = QNetworkInterface::allAddresses();
    for (const QHostAddress& local : locals) {
        const auto proto = local.protocol();
        if (!(proto == QAbstractSocket::IPv4Protocol && v4)
            && !(proto == QAbstractSocket::IPv6Protocol && v6))
            continue;
        // Same address twice is refused even where wildcard + specific is not.
        // Only "in use" counts: a tentative v6 address fails for other reasons.
        QTcpServer probe;
        if (!probe.listen(local, port) && probe.serverError() == QAbstractSocket::AddressInUseError)
            held.append(local);
    }
    return held;
}

bool ipv4WildcardHeld(quint16 port, QAbstractSocket::SocketType type)
{
    if (port == 0)
        return false;
    const QHostAddress v4(QHostAddress::AnyIPv4);
    if (type == QAbstractSocket::UdpSocket) {
        QUdpSocket probe;
        return !probe.bind(v4, port) && probe.error() == QAbstractSocket::AddressInUseError;
    }
    QTcpServer probe;
    return !probe.listen(v4, port) && probe.serverError() == QAbstractSocket::AddressInUseError;
}

QString addressListText(const QList<QHostAddress>& addresses)
{
    QStringList parts;
    for (const QHostAddress& a : addresses)
        parts.append(a.toString());
    return parts.join(QStringLiteral(", "));
}

} // namespace eMule
