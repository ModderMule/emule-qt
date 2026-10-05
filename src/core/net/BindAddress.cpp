#include "pch.h"
/// @file BindAddress.cpp
/// @brief The "bind address" preference applied to every P2P socket.

#include "net/BindAddress.h"
#include "prefs/Preferences.h"
#include "utils/Log.h"

#include <mutex>

namespace eMule::BindAddress {

namespace {

struct Parsed {
    QString text;           // the preference value this was parsed from
    QHostAddress address;   // null when unusable
};

// The UDP sockets ask per datagram and from their own thread, so parse once per value.
Parsed current()
{
    static std::mutex lock;
    static Parsed cache;

    const QString text = thePrefs.bindAddress().trimmed();
    std::lock_guard guard(lock);
    if (text != cache.text) {
        cache.text = text;
        cache.address = text.isEmpty() ? QHostAddress() : QHostAddress(text);
        const auto proto = cache.address.protocol();
        if (!text.isEmpty()
            && (cache.address.isNull()
                || (proto != QAbstractSocket::IPv4Protocol && proto != QAbstractSocket::IPv6Protocol)))
        {
            cache.address = QHostAddress();
            logError(QStringLiteral("Bind address \"%1\" is not an IPv4 or IPv6 address — "
                                    "no P2P socket will be opened until it is corrected")
                         .arg(text));
        } else if (proto == QAbstractSocket::IPv6Protocol) {
            logWarning(QStringLiteral("Bound to IPv6 address %1: IPv4-only servers and peers "
                                      "are unreachable").arg(text));
        }
    }
    return cache;
}

} // namespace

bool isConfigured()
{
    return !current().text.isEmpty();
}

std::optional<QHostAddress> listenAddress()
{
    const Parsed p = current();
    if (p.text.isEmpty())
        return QHostAddress(QHostAddress::Any);
    if (p.address.isNull())
        return std::nullopt;
    return p.address;
}

bool canReach(const Address& dest)
{
    const Parsed p = current();
    if (p.text.isEmpty())
        return true;
    if (p.address.isNull())
        return false;
    return p.address.protocol() == QAbstractSocket::IPv4Protocol ? dest.isIPv4() : dest.isIPv6();
}

QString ipv4Literal()
{
    const Parsed p = current();
    return p.address.protocol() == QAbstractSocket::IPv4Protocol ? p.address.toString() : QString();
}

bool isIPv4Only()
{
    return current().address.protocol() == QAbstractSocket::IPv4Protocol;
}

} // namespace eMule::BindAddress
