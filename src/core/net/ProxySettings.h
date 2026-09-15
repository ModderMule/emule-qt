#pragma once

/// @file ProxySettings.h
/// @brief The user's proxy, and the one QNetworkProxy every socket builds from it.
///
/// Lives apart from EMSocket.h so a socket that is not eMule's -- NntpSocket --
/// can use it without pulling in packet framing, obfuscation and the throttler.

#include "utils/Opcodes.h"
#include "utils/Types.h"

#include <QNetworkProxy>
#include <QString>

namespace eMule {

/// Proxy settings as the Proxy page stores them.
struct ProxySettings {
    bool useProxy = false;
    int type = PROXYTYPE_NOPROXY;   ///< PROXYTYPE_* from Opcodes.h.
    QString host;
    uint16 port = 0;
    bool enablePassword = false;
    QString user;
    QString password;
};

/// NoProxy when the proxy is off or its type is unknown.
///
/// SOCKS4 and SOCKS4a come back as Socks5: Qt has no SOCKS4 client, so a proxy
/// that speaks only SOCKS4 refuses the handshake. Credentials only when
/// authentication is enabled.
[[nodiscard]] QNetworkProxy toNetworkProxy(const ProxySettings& settings);

/// A type Qt can only approximate, see toNetworkProxy().
[[nodiscard]] constexpr bool isSocks4ProxyType(int type)
{
    return type == PROXYTYPE_SOCKS4 || type == PROXYTYPE_SOCKS4A;
}

} // namespace eMule
