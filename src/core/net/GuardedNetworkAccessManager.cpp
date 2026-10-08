#include "pch.h"
/// @file GuardedNetworkAccessManager.cpp
/// @brief A QNetworkAccessManager that starts nothing while the bound interface is missing.

#include "net/GuardedNetworkAccessManager.h"
#include "net/BindAddress.h"

namespace eMule {

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
