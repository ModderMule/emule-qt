#include "pch.h"
/// @file IpFilterUpdater.cpp
/// @brief Scheduled ipfilter.dat download.

#include "ipfilter/IpFilterUpdater.h"
#include "ipfilter/IPFilter.h"
#include "net/HttpFileDownload.h"
#include "prefs/Preferences.h"
#include "utils/Log.h"

#include <QDir>
#include <QSaveFile>
#include <QUrl>

#include <utility>

namespace eMule {

namespace {

constexpr int kTickMs = 60 * 60 * 1000;
constexpr qint64 kFailureBackoffSecs = 6 * 60 * 60;
constexpr qint64 kDaySecs = 24 * 60 * 60;
constexpr int kTimeoutMs = 120000;

} // namespace

IpFilterUpdater::IpFilterUpdater(IPFilter* filter, const QString& configDir, QObject* parent)
    : QObject(parent)
    , m_filter(filter)
    , m_configDir(configDir)
{
    m_timer.setInterval(kTickMs);
    connect(&m_timer, &QTimer::timeout, this, &IpFilterUpdater::checkSchedule);
}

void IpFilterUpdater::start()
{
    m_timer.start();
    QTimer::singleShot(0, this, &IpFilterUpdater::checkSchedule);
}

void IpFilterUpdater::stop()
{
    m_timer.stop();
}

void IpFilterUpdater::checkSchedule()
{
    if (m_running || !thePrefs.ipFilterAutoUpdate())
        return;

    const QDateTime now = QDateTime::currentDateTimeUtc();
    if (m_lastFailure.isValid() && m_lastFailure.secsTo(now) < kFailureBackoffSecs)
        return;

    const qint64 last = thePrefs.ipFilterLastUpdate();
    const qint64 every = static_cast<qint64>(std::max(1, thePrefs.ipFilterUpdateDays())) * kDaySecs;
    if (last > 0 && now.toSecsSinceEpoch() - last < every)
        return;

    updateNow();
}

void IpFilterUpdater::updateNow(const QString& url, Callback done)
{
    if (done)
        m_waiting.push_back(std::move(done));
    if (m_running)
        return;   // joins the running download

    const QString source = url.trimmed().isEmpty() ? thePrefs.ipFilterUpdateUrl() : url.trimmed();
    const QUrl qurl(source);
    if (!qurl.isValid() || qurl.scheme().isEmpty()) {
        finish(false, 0, QStringLiteral("invalid IP filter URL: %1").arg(source));
        return;
    }

    m_running = true;
    logInfo(QStringLiteral("IP filter: downloading %1").arg(source));

    // Most public lists ship compressed; the names are the ones MFC looks for inside
    // an archive (srchybrid/PPgSecurity.cpp:246-250)
    HttpFileDownload::Options opts;
    opts.preferredNames = {QStringLiteral("ipfilter.dat"),
                           QStringLiteral("guarding.p2p"),
                           QStringLiteral("guardian.p2p")};
    opts.timeoutMs = kTimeoutMs;

    HttpFileDownload::get(this, qurl, opts,
        [this](bool ok, const QByteArray& data, const QString& entryName, const QString& error) {
            if (!ok) {
                finish(false, 0, QStringLiteral("download failed: %1").arg(error));
                return;
            }
            if (data.isEmpty()) {
                finish(false, 0, QStringLiteral("the downloaded list is empty"));
                return;
            }

            // Write beside, then replace: a half-written list must never be loaded
            QSaveFile file(QDir(m_configDir).filePath(QStringLiteral("ipfilter.dat")));
            if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
                finish(false, 0, QStringLiteral("cannot save ipfilter.dat: %1").arg(file.errorString()));
                return;
            }

            const int entries = m_filter ? m_filter->loadFromDefaultFile(m_configDir) : 0;
            thePrefs.setIpFilterLastUpdate(QDateTime::currentSecsSinceEpoch());
            finish(true, entries,
                   entryName.isEmpty()
                       ? QStringLiteral("IP filter updated: %1 ranges").arg(entries)
                       : QStringLiteral("IP filter updated: %1 ranges (unpacked \"%2\")")
                             .arg(entries).arg(entryName));
        });
}

void IpFilterUpdater::finish(bool ok, int entries, const QString& message)
{
    m_running = false;
    if (ok) {
        m_lastFailure = {};
        logInfo(message);
    } else {
        m_lastFailure = QDateTime::currentDateTimeUtc();
        logWarning(QStringLiteral("IP filter: %1").arg(message));
    }
    auto waiting = std::exchange(m_waiting, {});
    for (const auto& cb : waiting)
        cb(ok, entries, message);
    emit updateFinished(ok, entries, message);
}

} // namespace eMule
