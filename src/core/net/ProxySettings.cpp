#include "pch.h"

#include "net/ProxySettings.h"

#include <QHostAddress>

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

bool bypassesProxy(const QString& host)
{
    if (host.compare(QLatin1StringView("localhost"), Qt::CaseInsensitive) == 0)
        return true;
    QString literal = host;
    if (literal.startsWith(u'[') && literal.endsWith(u']'))
        literal = literal.mid(1, literal.size() - 2);
    const QHostAddress addr(literal);
    if (addr.isNull())
        return false;
    return addr.isLoopback() || addr.isPrivateUse() || addr.isLinkLocal();
}

QNetworkProxy toNetworkProxy(const ProxySettings& settings, const QString& host)
{
    if (bypassesProxy(host))
        return QNetworkProxy(QNetworkProxy::NoProxy);
    return toNetworkProxy(settings);
}

} // namespace eMule
