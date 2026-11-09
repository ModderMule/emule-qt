#include "pch.h"
/// @file GuardedNetworkAccessManager.cpp
/// @brief A QNetworkAccessManager that starts nothing while the bound interface is missing.

#include "net/GuardedNetworkAccessManager.h"
#include "net/BindAddress.h"
#include "net/ProxySettings.h"
#include "prefs/Preferences.h"

#include <QNetworkProxyFactory>

namespace eMule {

namespace {

/// The Proxy page's proxy, read at each request: a change applies to the next one.
class PrefsProxyFactory : public QNetworkProxyFactory {
public:
    QList<QNetworkProxy> queryProxy(const QNetworkProxyQuery& query) override
    {
        return {toNetworkProxy(thePrefs.proxySettings(), query.peerHostName())};
    }
};

} // namespace

GuardedNetworkAccessManager::GuardedNetworkAccessManager(QObject* parent)
    : QNetworkAccessManager(parent)
{
    setProxyFactory(new PrefsProxyFactory);   // owned by the manager
}

QNetworkReply* GuardedNetworkAccessManager::createRequest(Operation op, const QNetworkRequest& request,
                                                          QIODevice* outgoingData)
{
    if (!BindAddress::outboundAllowed()) {
        return new HeldNetworkReply(op, request,
                                    QStringLiteral("Not sent: %1").arg(BindAddress::current().reason),
                                    this);
    }
    return QNetworkAccessManager::createRequest(op, request, outgoingData);
}

HeldNetworkReply::HeldNetworkReply(QNetworkAccessManager::Operation op, const QNetworkRequest& request,
                                   const QString& reason, QObject* parent)
    : QNetworkReply(parent)
{
    setOperation(op);
    setRequest(request);
    setUrl(request.url());
    open(QIODevice::ReadOnly);   // before setError(): opening clears the error text
    setError(QNetworkReply::NetworkSessionFailedError, reason);
    // Queued: callers connect their handlers after get() / post() returns.
    QMetaObject::invokeMethod(this, [this] {
        setFinished(true);
        emit errorOccurred(error());
        emit finished();
    }, Qt::QueuedConnection);
}

} // namespace eMule
