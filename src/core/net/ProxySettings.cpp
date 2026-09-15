#include "pch.h"

#include "net/ProxySettings.h"

namespace eMule {

QNetworkProxy toNetworkProxy(const ProxySettings& settings)
{
    if (!settings.useProxy)
        return QNetworkProxy(QNetworkProxy::NoProxy);

    QNetworkProxy proxy;
    switch (settings.type) {
    case PROXYTYPE_SOCKS4:
    case PROXYTYPE_SOCKS4A:
    case PROXYTYPE_SOCKS5:
        proxy.setType(QNetworkProxy::Socks5Proxy);
        break;
    case PROXYTYPE_HTTP10:
    case PROXYTYPE_HTTP11:
        proxy.setType(QNetworkProxy::HttpProxy);
        break;
    default:
        return QNetworkProxy(QNetworkProxy::NoProxy);
    }

    proxy.setHostName(settings.host);
    proxy.setPort(settings.port);
    if (settings.enablePassword) {
        proxy.setUser(settings.user);
        proxy.setPassword(settings.password);
    }
    return proxy;
}

} // namespace eMule
