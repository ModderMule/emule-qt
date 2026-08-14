#include "pch.h"
/// @file VersionChecker.cpp
/// @brief HTTP-based version checker implementation.

#include "app/VersionChecker.h"
#include "app/AppConfig.h"   // src/core/app — src/gui/app has no AppConfig.h
#include "prefs/Preferences.h"
#include "utils/Log.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
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

/// Where the manifest lives when nothing overrides it.
constexpr QLatin1StringView kDefaultManifestUrl{"https://emule-qt.org/pub/emuleqt-version.json"};

/// How often the periodic timer wakes up to ask whether a check is due. The interval
/// itself is 1-14 days, so hourly is plenty; the comparison is against wall-clock
/// seconds, so a suspended machine simply finds the check overdue when it wakes.
constexpr int kPeriodicTickMs = 60 * 60 * 1000;

/// Pick this platform's download URL out of the manifest's "downloads" object.
///
/// Returns an empty string whenever the manifest has nothing usable to offer:
/// no "downloads" object, no entry for this platform, a placeholder entry with
/// size 0, or a URL that is not plain http(s). None of those are errors — the
/// manifest legitimately advertises a new version before the binaries for every
/// platform exist (all six entries are currently not-ready:// stubs with size 0),
/// so they are skipped silently rather than logged.
QString downloadUrlForThisPlatform(const QJsonObject& manifest)
{
    const QString key = VersionChecker::platformKey();
    if (key.isEmpty())
        return {};

    const QJsonObject downloads = manifest.value(QStringLiteral("downloads")).toObject();
    const QJsonObject entry = downloads.value(key).toObject();
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
}

} // namespace

// ---------------------------------------------------------------------------
// Pure helpers — no network, no preferences, no widgets (see tst_VersionChecker)
// ---------------------------------------------------------------------------

VersionChecker::Manifest VersionChecker::parse(const QByteArray& json)
{
    Manifest info;

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (!doc.isObject()) {
        info.error = parseError.error == QJsonParseError::NoError
                         ? tr("the version manifest is not a JSON object")
                         : tr("invalid JSON response: %1").arg(parseError.errorString());
        return info;
    }

    const QJsonObject obj = doc.object();
    info.latest = obj.value(QStringLiteral("latest")).toString();
    if (info.latest.isEmpty()) {
        info.error = tr("the version manifest has no 'latest' field");
        return info;
    }

    // Optional from here on — a manifest missing any of these is still usable.
    info.date = QDateTime::fromString(obj.value(QStringLiteral("date")).toString(),
                                      Qt::ISODate).date();
    info.releaseNotes = obj.value(QStringLiteral("releaseNotes")).toString();
    info.minVersion   = obj.value(QStringLiteral("minVersion")).toString();
    info.downloadUrl  = downloadUrlForThisPlatform(obj);
    return info;
}

bool VersionChecker::isNewer(const QString& remote, const QString& local)
{
    const QVersionNumber r = QVersionNumber::fromString(remote);
    const QVersionNumber l = QVersionNumber::fromString(local);
    if (r.isNull() || l.isNull())
        return false;
    return r > l;
}

QString VersionChecker::platformKey()
{
#ifdef EMULE_MANIFEST_PLATFORM
    return QStringLiteral(EMULE_MANIFEST_PLATFORM);
#else
    return {};
#endif
}

QString VersionChecker::manifestUrl()
{
    const QString override = qEnvironmentVariable("EMULEQT_VERSION_MANIFEST_URL");
    return override.isEmpty() ? QString(kDefaultManifestUrl) : override;
}

// ---------------------------------------------------------------------------

VersionChecker::VersionChecker(QObject* parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
    connect(m_nam, &QNetworkAccessManager::finished,
            this, &VersionChecker::onReplyFinished);
}

void VersionChecker::checkNow()
{
    start(true);
}

void VersionChecker::checkIfDue()
{
    if (!thePrefs.versionCheckEnabled())
        return;

    const int64_t now = QDateTime::currentSecsSinceEpoch();
    const int64_t intervalSecs = static_cast<int64_t>(thePrefs.versionCheckDays()) * 86400;

    // m_lastCheck > now means the clock moved backwards (or the file was edited);
    // treat that as due rather than never checking again.
    if (m_lastCheck > 0 && m_lastCheck <= now && (now - m_lastCheck) < intervalSecs)
        return;

    start(false);
}

void VersionChecker::startPeriodicChecks()
{
    if (m_periodicTimer)
        return;   // already running — this is called again on every IPC reconnect

    m_periodicTimer = new QTimer(this);
    m_periodicTimer->setInterval(kPeriodicTickMs);
    connect(m_periodicTimer, &QTimer::timeout, this, &VersionChecker::checkIfDue);
    m_periodicTimer->start();

    checkIfDue();
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

void VersionChecker::start(bool manual)
{
    if (m_inFlight)
        return;   // an hourly tick must not stack requests behind a slow reply

    QNetworkRequest req{QUrl(manifestUrl())};
    req.setHeader(QNetworkRequest::UserAgentHeader, eMule::kUserAgent);

    QNetworkReply* reply = m_nam->get(req);
    // Per-reply, not a member: a menu check firing while an hourly one is in flight
    // would otherwise relabel it and pop a dialog nobody asked for.
    reply->setProperty("emuleManualCheck", manual);
    m_inFlight = true;
}

void VersionChecker::onReplyFinished(QNetworkReply* reply)
{
    reply->deleteLater();
    m_inFlight = false;

    const bool manual = reply->property("emuleManualCheck").toBool();

    if (reply->error() != QNetworkReply::NoError) {
        logWarning(QStringLiteral("Version check failed: %1").arg(reply->errorString()));
        emit checkFailed(reply->errorString(), manual);
        return;
    }

    const Manifest info = parse(reply->readAll());
    if (!info.isValid()) {
        logWarning(QStringLiteral("Version check: %1").arg(info.error));
        emit checkFailed(info.error, manual);
        return;
    }

    // Only a usable manifest resets the interval. A network failure deliberately does
    // not, so an offline session retries on the next tick.
    m_lastCheck = QDateTime::currentSecsSinceEpoch();
    emit checkCompleted(m_lastCheck);

    const QString current = QCoreApplication::applicationVersion();
    if (isNewer(info.latest, current)) {
        logInfo(QStringLiteral("New version available: %1 (current: %2)")
                    .arg(info.latest, current));
        emit newVersionAvailable(info, manual);
    } else {
        if (manual)
            logInfo(QStringLiteral("eMule Qt is up to date (v%1)").arg(current));
        emit upToDate(current, manual);
    }
}

} // namespace eMule
