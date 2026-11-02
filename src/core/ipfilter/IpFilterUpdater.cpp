#include "pch.h"
/// @file IpFilterUpdater.cpp
/// @brief Scheduled, conditional ipfilter.dat download.

#include "ipfilter/IpFilterUpdater.h"
#include "ipfilter/IPFilter.h"
#include "prefs/Preferences.h"
#include "utils/Log.h"

#include <QDir>
#include <QFileInfo>
#include <QSaveFile>
#include <QUrl>

#include <utility>

namespace eMule {

namespace {

constexpr qint64 kDaySecs = 24 * 60 * 60;
constexpr int kTimeoutMs = 120000;

} // namespace

IpFilterUpdater::IpFilterUpdater(IPFilter* filter, const QString& configDir, QObject* parent)
    : ScheduledUpdater(parent)
    , m_filter(filter)
    , m_configDir(configDir)
{
}

void IpFilterUpdater::checkSchedule()
{
    if (isRunning() || !thePrefs.ipFilterAutoUpdate() || backingOff())
        return;

    const qint64 last = thePrefs.ipFilterLastUpdate();
    const qint64 every = static_cast<qint64>(std::max(1, thePrefs.ipFilterUpdateDays())) * kDaySecs;
    if (last > 0 && QDateTime::currentSecsSinceEpoch() - last < every)
        return;

    run(thePrefs.ipFilterUpdateUrl(), true);
}

void IpFilterUpdater::updateNow(const QString& url, Callback done)
{
    if (done)
        m_waiting.push_back(std::move(done));
    if (isRunning())
        return;   // joins the running download
    run(url.trimmed().isEmpty() ? thePrefs.ipFilterUpdateUrl() : url.trimmed(), false);
}

void IpFilterUpdater::run(const QString& source, bool conditional)
{
    const QUrl qurl(source);
    if (!qurl.isValid() || qurl.scheme().isEmpty()) {
        finish(false, 0, QStringLiteral("invalid IP filter URL: %1").arg(source));
        return;
    }

    // Most public lists ship compressed; the names are the ones MFC looks for inside
    // an archive (srchybrid/PPgSecurity.cpp:246-250)
    HttpFileDownload::Options opts;
    opts.preferredNames = {QStringLiteral("ipfilter.dat"),
                           QStringLiteral("guarding.p2p"),
                           QStringLiteral("guardian.p2p")};
    opts.timeoutMs = kTimeoutMs;
    // Only skip the download while the list it would replace is on disk
    if (conditional && QFileInfo::exists(QDir(m_configDir).filePath(QStringLiteral("ipfilter.dat"))))
        opts.validators = thePrefs.downloadValidators(QString::fromLatin1(kValidatorKey), source);

    logInfo(opts.validators.isEmpty()
                ? QStringLiteral("IP filter: downloading %1").arg(source)
                : QStringLiteral("IP filter: checking %1 for changes").arg(source));
    m_job = HttpFileDownload::fetch(this, qurl, opts,
        [this, source](const HttpFileDownload::Result& r) { onDownloaded(source, r); });
}

void IpFilterUpdater::onDownloaded(const QString& source, const HttpFileDownload::Result& r)
{
    if (!r.ok) {
        finish(false, 0, QStringLiteral("download failed: %1").arg(r.error));
        return;
    }
    if (r.notModified) {
        thePrefs.setIpFilterLastUpdate(QDateTime::currentSecsSinceEpoch());
        // No range count: at startup the list on disk may still be loading
        finish(true, m_filter ? m_filter->entryCount() : 0,
               QStringLiteral("IP filter is up to date"));
        return;
    }
    if (r.data.isEmpty()) {
        finish(false, 0, QStringLiteral("the downloaded list is empty"));
        return;
    }

    // Write beside, then replace: a half-written list must never be loaded
    QSaveFile file(QDir(m_configDir).filePath(QStringLiteral("ipfilter.dat")));
    if (!file.open(QIODevice::WriteOnly) || file.write(r.data) != r.data.size() || !file.commit()) {
        finish(false, 0, QStringLiteral("cannot save ipfilter.dat: %1").arg(file.errorString()));
        return;
    }

    const int entries = m_filter ? m_filter->loadFromDefaultFile(m_configDir) : 0;
    thePrefs.setIpFilterLastUpdate(QDateTime::currentSecsSinceEpoch());
    thePrefs.setDownloadValidators(QString::fromLatin1(kValidatorKey), source, r.validators);
    finish(true, entries,
           r.entryName.isEmpty()
               ? QStringLiteral("IP filter updated: %1 ranges").arg(entries)
               : QStringLiteral("IP filter updated: %1 ranges (unpacked \"%2\")")
                     .arg(entries).arg(r.entryName));
}

void IpFilterUpdater::finish(bool ok, int entries, const QString& message)
{
    noteOutcome(ok);
    if (ok)
        logInfo(message);
    else
        logWarning(QStringLiteral("IP filter: %1").arg(message));
    auto waiting = std::exchange(m_waiting, {});
    for (const auto& cb : waiting)
        cb(ok, entries, message);
    emit updateFinished(ok, entries, message);
}

} // namespace eMule
