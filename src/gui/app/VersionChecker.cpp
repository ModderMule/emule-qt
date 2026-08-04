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
#include <QUrl>
#include <QVersionNumber>

// Key this build uses to look itself up in the manifest's "downloads" map
// (windows-x64, mac-arm64, linux-x64, ...). Left undefined on OS/CPU combinations
// the manifest has no entry for, in which case no download URL is ever offered.
#if defined(Q_OS_WIN)
#  define EMULE_MANIFEST_OS "windows"
#elif defined(Q_OS_MACOS)
#  define EMULE_MANIFEST_OS "mac"
#elif defined(Q_OS_LINUX)
#  define EMULE_MANIFEST_OS "linux"
#endif

#if defined(Q_PROCESSOR_X86_64)
#  define EMULE_MANIFEST_ARCH "x64"
#elif defined(Q_PROCESSOR_ARM_64)
#  define EMULE_MANIFEST_ARCH "arm64"
#endif

#if defined(EMULE_MANIFEST_OS) && defined(EMULE_MANIFEST_ARCH)
#  define EMULE_MANIFEST_PLATFORM EMULE_MANIFEST_OS "-" EMULE_MANIFEST_ARCH
#endif

namespace eMule {

namespace {

/// Pick this platform's download URL out of the manifest's "downloads" object.
///
/// Returns an empty string whenever the manifest has nothing usable to offer:
/// no "downloads" object, no entry for this platform, a placeholder entry with
/// size 0, or a URL that is not plain http(s). None of those are errors — the
/// manifest legitimately advertises a new version before the binaries for every
/// platform exist (all six entries are currently not-ready:// stubs with size 0),
/// so they are skipped silently rather than logged. The caller falls back to the
/// website when the URL is empty.
QString downloadUrlForThisPlatform(const QJsonObject& manifest)
{
#ifdef EMULE_MANIFEST_PLATFORM
    const QJsonObject downloads = manifest.value(QStringLiteral("downloads")).toObject();
    const QJsonObject entry =
        downloads.value(QStringLiteral(EMULE_MANIFEST_PLATFORM)).toObject();
    if (entry.isEmpty())
        return {};

    // size 0 (or missing/non-numeric, which also yields 0) marks a placeholder
    if (entry.value(QStringLiteral("size")).toInteger(0) <= 0)
        return {};

    const QUrl url(entry.value(QStringLiteral("url")).toString(), QUrl::StrictMode);
    if (!url.isValid() || url.host().isEmpty())
        return {};
    if (url.scheme() != QStringLiteral("https") && url.scheme() != QStringLiteral("http"))
        return {};

    return url.toString();
#else
    Q_UNUSED(manifest);
    return {};
#endif
}

} // namespace

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

    const QString downloadUrl = downloadUrlForThisPlatform(obj);

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
