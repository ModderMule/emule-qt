#pragma once

/// @file GuardedNetworkAccessManager.h
/// @brief A QNetworkAccessManager that starts nothing while the bound interface is missing.
///
/// HTTP through Qt cannot be pinned to an interface, so it follows the default route.
/// When the selected interface (BindAddress.h) is gone that route is the one the user
/// wanted to avoid: every request then fails at once with a clear error instead.

#include <QNetworkAccessManager>
#include <QNetworkReply>

namespace eMule {

class GuardedNetworkAccessManager : public QNetworkAccessManager {
    Q_OBJECT

public:
    using QNetworkAccessManager::QNetworkAccessManager;

protected:
    QNetworkReply* createRequest(Operation op, const QNetworkRequest& request,
                                 QIODevice* outgoingData = nullptr) override;
};

/// The reply of a request that was not sent.
class HeldNetworkReply : public QNetworkReply {
    Q_OBJECT

public:
    HeldNetworkReply(QNetworkAccessManager::Operation op, const QNetworkRequest& request,
                     const QString& reason, QObject* parent);

    void abort() override {}

protected:
    qint64 readData(char*, qint64) override { return -1; }
};

} // namespace eMule
