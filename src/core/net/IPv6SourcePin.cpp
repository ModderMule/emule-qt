#include "pch.h"
/// @file IPv6SourcePin.cpp
/// @brief Per-socket IPv6 source selection — see IPv6SourcePin.h.

#include "net/IPv6SourcePin.h"
#include "net/ProxySettings.h"
#include "prefs/Preferences.h"
#include "utils/Log.h"

#include <QAbstractSocket>
#include <QNetworkDatagram>

#include <mutex>

namespace eMule::IPv6SourcePin {

namespace {
std::mutex s_lock;
Address s_pin;
} // namespace

void setPinAddress(const Address& addr)
{
    std::lock_guard lock(s_lock);
    s_pin = addr.isIPv6() ? addr : Address();
}

Address pinAddress()
{
    std::lock_guard lock(s_lock);
    return s_pin;
}

Address sourceFor(const Address& dest)
{
    if (!dest.isIPv6() || thePrefs.ipv6UsePrivacyAddress() || thePrefs.proxySettings().useProxy)
        return {};
    return pinAddress();
}

void bindForConnect(QAbstractSocket& socket, const Address& dest)
{
    const Address source = sourceFor(dest);
    if (source.isNull() || socket.state() != QAbstractSocket::UnconnectedState)
        return;
    if (!socket.bind(source.toQHostAddress(), 0))
        logDebug(QStringLiteral("IPv6: cannot bind outgoing TCP to %1 (%2) — OS picks the source")
                     .arg(source.toString(), socket.errorString()));
}

void applyToDatagram(QNetworkDatagram& datagram, const Address& dest)
{
    if (const Address source = sourceFor(dest); !source.isNull())
        datagram.setSender(source.toQHostAddress());
}

} // namespace eMule::IPv6SourcePin
