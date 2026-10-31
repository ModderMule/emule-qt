#include "pch.h"
/// @file VersionChecker.cpp
/// @brief HTTP-based version checker implementation.

#include "app/VersionChecker.h"
#include "net/HttpDefaults.h"   // src/core/net
#include "prefs/Preferences.h"
#include "utils/Log.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

namespace eMule {

namespace {

/// How often the periodic timer wakes up to ask whether a check is due. The interval
/// itself is 1-14 days, so hourly is plenty; the comparison is against wall-clock
/// seconds, so a suspended machine simply finds the check overdue when it wakes.
constexpr int kPeriodicTickMs = 60 * 60 * 1000;

} // namespace

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

    QNetworkRequest req = eMule::Http::makeRequest(QUrl(manifestUrl()));

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
