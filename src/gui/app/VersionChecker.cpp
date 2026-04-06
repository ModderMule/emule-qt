#include "pch.h"
/// @file VersionChecker.cpp
/// @brief HTTP-based version checker implementation.

#include "app/VersionChecker.h"
#include "core/app/AppConfig.h"
#include "prefs/Preferences.h"
#include "utils/Log.h"

#include <QApplication>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QVersionNumber>

namespace eMule {

VersionChecker::VersionChecker(QObject* parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
    connect(m_nam, &QNetworkAccessManager::finished,
            this, &VersionChecker::onReplyFinished);
}

void VersionChecker::check(bool manual)
{
    m_manual = manual;

    if (!manual) {
        if (!thePrefs.versionCheckEnabled())
            return;

        const int64_t lastCheck = thePrefs.lastVersionCheck();
        const int64_t intervalSecs = static_cast<int64_t>(thePrefs.versionCheckDays()) * 86400;
        const int64_t now = QDateTime::currentSecsSinceEpoch();

        if (lastCheck > 0 && (now - lastCheck) < intervalSecs)
            return;
    }

    QNetworkRequest req(QUrl(QStringLiteral("https://emule-qt.org/pub/emuleqt-version.json")));
    req.setHeader(QNetworkRequest::UserAgentHeader, eMule::kUserAgent);
    m_nam->get(req);
}

void VersionChecker::onReplyFinished(QNetworkReply* reply)
{
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        logWarning(QStringLiteral("Version check failed: %1").arg(reply->errorString()));
        emit checkFailed();
        return;
    }

    const QByteArray data = reply->readAll();
    const QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) {
        logWarning(QStringLiteral("Version check: invalid JSON response"));
        emit checkFailed();
        return;
    }

    const QJsonObject obj = doc.object();
    const QString remoteStr = obj.value(QStringLiteral("latest")).toString();
    if (remoteStr.isEmpty()) {
        logWarning(QStringLiteral("Version check: no 'latest' field in response"));
        emit checkFailed();
        return;
    }

    const QString downloadUrl = obj.value(QStringLiteral("downloadUrl")).toString();

    // Update last check timestamp
    thePrefs.setLastVersionCheck(QDateTime::currentSecsSinceEpoch());

    const QVersionNumber remote = QVersionNumber::fromString(remoteStr);
    const QVersionNumber local = QVersionNumber::fromString(QApplication::applicationVersion());

    if (remote > local) {
        logInfo(QStringLiteral("New version available: %1 (current: %2)")
                    .arg(remoteStr, QApplication::applicationVersion()));
        emit newVersionAvailable(remoteStr, downloadUrl);
    } else {
        if (m_manual)
            logInfo(QStringLiteral("eMule Qt is up to date (v%1)").arg(QApplication::applicationVersion()));
        emit upToDate();
    }
}

} // namespace eMule
