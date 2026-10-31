#include "pch.h"
/// @file GeoIpUpdater.cpp
/// @brief Scheduled GeoLite2-Country / DB-IP Country Lite download.

#include "geo/GeoIpUpdater.h"
#include "net/GuardedNetworkAccessManager.h"
#include "geo/IP2Country.h"
#include "app/AppConfig.h"
#include "archive/ArchiveUnpack.h"
#include "net/HttpDefaults.h"
#include "prefs/Preferences.h"
#include "utils/Log.h"

#include <QCoreApplication>
#include <QLocale>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimeZone>

#include <filesystem>
#include <system_error>
#include <utility>

namespace eMule {

namespace {

constexpr auto kDownloadUrl =
    "https://download.maxmind.com/geoip/databases/GeoLite2-Country/download?suffix=tar.gz";
constexpr auto kDbIpUrlTemplate = "https://download.db-ip.com/free/dbip-country-lite-%1.mmdb.gz";
constexpr int kTickMs = 60 * 60 * 1000;
constexpr qint64 kRefreshSecs = 7 * 24 * 60 * 60;
constexpr qint64 kFailureBackoffSecs = 6 * 60 * 60;
constexpr int kTimeoutMs = 120000;
constexpr int kMaxRedirects = 5;

/// Longest a GeoLite2 tarball has ever been is ~5 MB; this only stops a runaway.
constexpr qint64 kMaxDownloadBytes = 64ll * 1024 * 1024;

[[nodiscard]] QByteArray httpDate(const QDateTime& dt)
{
    return QLocale::c()
        .toString(dt.toUTC(), QStringLiteral("ddd, dd MMM yyyy hh:mm:ss 'GMT'"))
        .toLatin1();
}

[[nodiscard]] GeoIpSource selectedSource()
{
    return thePrefs.geoIpSource() == static_cast<int>(GeoIpSource::DbIp) ? GeoIpSource::DbIp
                                                                          : GeoIpSource::MaxMind;
}

[[nodiscard]] QString sourceName(GeoIpSource source)
{
    return source == GeoIpSource::DbIp ? QStringLiteral("DB-IP Country Lite")
                                       : QStringLiteral("GeoLite2-Country");
}

[[nodiscard]] bool haveCredentials()
{
    return !thePrefs.geoIpAccountId().trimmed().isEmpty()
           && !thePrefs.geoIpLicenseKey().trimmed().isEmpty();
}

} // namespace

GeoIpUpdater::GeoIpUpdater(IP2Country* ip2Country, const QString& configDir, QObject* parent)
    : QObject(parent)
    , m_ip2Country(ip2Country)
    , m_configDir(configDir)
    , m_url(QString::fromLatin1(kDownloadUrl))
    , m_dbIpUrlTemplate(QString::fromLatin1(kDbIpUrlTemplate))
{
    m_timer.setInterval(kTickMs);
    connect(&m_timer, &QTimer::timeout, this, &GeoIpUpdater::checkSchedule);
}

GeoIpUpdater::~GeoIpUpdater()
{
    if (m_reply)
        m_reply->abort();
}

QString GeoIpUpdater::databasePath() const
{
    return databasePathFor(selectedSource());
}

QString GeoIpUpdater::databasePathFor(GeoIpSource source) const
{
    return QDir(m_configDir).filePath(QString::fromLatin1(
        source == GeoIpSource::DbIp ? kDbIpDatabaseFilename : kGeoIpDatabaseFilename));
}

void GeoIpUpdater::reloadForSource()
{
    m_lastFailure = {};
    if (selectedLoaded())
        return;
    const QString path = databasePath();
    if (QFileInfo::exists(path) && openDatabase(path))
        return;
    // Selected file missing: keep (or load) the other source until a download lands
    if (!m_ip2Country->isLoaded()) {
        const GeoIpSource other = selectedSource() == GeoIpSource::DbIp ? GeoIpSource::MaxMind
                                                                        : GeoIpSource::DbIp;
        if (const QString fallback = databasePathFor(other); QFileInfo::exists(fallback))
            openDatabase(fallback);
    }
}

void GeoIpUpdater::start()
{
    adoptBundledDatabase();
    reloadForSource();
    m_timer.start();
    QTimer::singleShot(0, this, &GeoIpUpdater::checkSchedule);
}

void GeoIpUpdater::stop()
{
    m_timer.stop();
    if (m_reply)
        m_reply->abort();
}

void GeoIpUpdater::updateNow(Callback done)
{
    if (done)
        m_waiting.push_back(std::move(done));
    if (m_reply)
        return;   // joins the running download

    m_activeSource = selectedSource();
    m_redirects = 0;
    if (m_activeSource == GeoIpSource::DbIp) {
        logInfo(QStringLiteral("IP2Country: checking for a DB-IP Country Lite update"));
        m_dbIpMonthsBack = 0;
        m_requestUrl = dbIpUrl(0);
        startRequest(m_requestUrl, false, true);
        return;
    }

    if (!haveCredentials()) {
        finish(false, QStringLiteral("no MaxMind account ID / license key configured"));
        return;
    }
    logInfo(QStringLiteral("IP2Country: checking for a GeoLite2-Country update"));
    m_requestUrl = m_url;
    startRequest(m_requestUrl, true, true);
}

void GeoIpUpdater::checkSchedule()
{
    if (m_reply || !thePrefs.geoIpAutoUpdate())
        return;
    if (selectedSource() == GeoIpSource::MaxMind && !haveCredentials())
        return;

    const QDateTime now = QDateTime::currentDateTimeUtc();
    if (m_lastFailure.isValid() && m_lastFailure.secsTo(now) < kFailureBackoffSecs)
        return;

    const bool haveDb = selectedLoaded();   // the other source's file doesn't count
    const qint64 lastCheck = thePrefs.geoIpLastCheck();
    if (haveDb && lastCheck > 0 && now.toSecsSinceEpoch() - lastCheck < kRefreshSecs)
        return;

    updateNow();
}

bool GeoIpUpdater::adoptBundledDatabase()
{
    if (!m_bundleDir) {
        m_bundleDir = QString();
        for (const QString& c : AppConfig::bundleCandidates(QCoreApplication::applicationDirPath()))
            if (QDir(c).exists()) {
                m_bundleDir = c;
                break;
            }
    }
    if (m_bundleDir->isEmpty())
        return false;

    const QString bundled = QDir(*m_bundleDir).filePath(QString::fromLatin1(kGeoIpDatabaseFilename));
    const QString target = databasePathFor(GeoIpSource::MaxMind);   // only GeoLite2 is bundled
    const QFileInfo bundledInfo(bundled);
    // Windows portable mode: the bundle is the config dir
    if (!bundledInfo.exists() || bundledInfo.canonicalFilePath() == QFileInfo(target).canonicalFilePath())
        return false;

    const QDateTime bundledBuilt = IP2Country::buildDateOf(bundled);
    if (!bundledBuilt.isValid()) {
        logWarning(QStringLiteral("IP2Country: bundled %1 is invalid, ignored").arg(bundled));
        return false;
    }
    // Newest build wins: never roll back a database the updater downloaded
    if (const QDateTime liveBuilt = IP2Country::buildDateOf(target);
        liveBuilt.isValid() && liveBuilt >= bundledBuilt)
        return false;

    QFile in(bundled);
    if (!in.open(QIODevice::ReadOnly)) {
        logWarning(QStringLiteral("IP2Country: cannot read %1").arg(bundled));
        return false;
    }
    QString error;
    m_activeSource = GeoIpSource::MaxMind;
    if (!installDatabase(in.readAll(), error)) {
        logWarning(QStringLiteral("IP2Country: cannot adopt the bundled database: %1").arg(error));
        return false;
    }
    logInfo(QStringLiteral("IP2Country: updated GeoLite2-Country from the bundle (built %1)")
                .arg(bundledBuilt.toString(Qt::ISODate)));
    emit databaseChanged();
    return true;
}

void GeoIpUpdater::startRequest(const QUrl& url, bool withCredentials, bool conditional)
{
    QNetworkRequest request = Http::makeRequest(url);
    request.setTransferTimeout(kTimeoutMs);
    // Redirects by hand, so the Authorization header never follows one off-host
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QVariant::fromValue(QNetworkRequest::ManualRedirectPolicy));
    if (withCredentials) {
        const QString credentials = thePrefs.geoIpAccountId().trimmed() + u':'
                                    + thePrefs.geoIpLicenseKey().trimmed();
        request.setRawHeader("Authorization", "Basic " + credentials.toUtf8().toBase64());
    }
    // Only skip the download when the file we would replace is the loaded one
    const QString target = databasePathFor(m_activeSource);
    if (conditional && m_ip2Country->path() == target && QFileInfo::exists(target))
        if (const QDateTime built = m_ip2Country->buildDate(); built.isValid())
            request.setRawHeader("If-Modified-Since", httpDate(built));

    if (!m_nam)
        m_nam = new GuardedNetworkAccessManager(this);
    m_reply = m_nam->get(request);
    connect(m_reply, &QNetworkReply::finished, this, &GeoIpUpdater::onReplyFinished);
}

void GeoIpUpdater::onReplyFinished()
{
    QNetworkReply* reply = m_reply;
    m_reply = nullptr;
    if (!reply)
        return;
    reply->deleteLater();

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
        const QUrl target = reply->url().resolved(
            reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl());
        if (++m_redirects > kMaxRedirects || !target.isValid()
            || (target.scheme() != u"https" && m_requestUrl.scheme() == u"https")) {
            finish(false, QStringLiteral("bad redirect from the download server (HTTP %1)").arg(status));
            return;
        }
        // Credentials stay with MaxMind: the file itself comes from a presigned
        // storage URL that refuses any Authorization header (HTTP 400)
        const bool sameHost = target.host() == m_requestUrl.host();
        const bool maxMind = m_activeSource == GeoIpSource::MaxMind;
        startRequest(target, maxMind && sameHost, !maxMind || sameHost);
        return;
    }
    if (status == 304) {
        thePrefs.setGeoIpLastCheck(QDateTime::currentSecsSinceEpoch());
        finish(true, QStringLiteral("%1 is up to date").arg(sourceName(m_activeSource)));
        return;
    }
    // This month's DB-IP file appears a few days in; take last month's meanwhile
    if (status == 404 && m_activeSource == GeoIpSource::DbIp && m_dbIpMonthsBack == 0) {
        m_dbIpMonthsBack = 1;
        m_redirects = 0;
        m_requestUrl = dbIpUrl(1);
        startRequest(m_requestUrl, false, true);
        return;
    }
    if (m_activeSource == GeoIpSource::MaxMind && (status == 401 || status == 403)) {
        finish(false, QStringLiteral("MaxMind rejected the account ID / license key (HTTP %1)")
                          .arg(status));
        return;
    }
    if (reply->error() != QNetworkReply::NoError) {
        // errorString() quotes the (presigned) URL; the status is what matters
        finish(false, status > 0 ? QStringLiteral("download failed (HTTP %1)").arg(status)
                                 : reply->errorString());
        return;
    }

    const QByteArray body = reply->readAll();
    if (body.size() > kMaxDownloadBytes) {
        finish(false, QStringLiteral("download too large"));
        return;
    }

    const UnwrapResult unwrapped = unwrapDownload(
        body, {QString::fromLatin1(m_activeSource == GeoIpSource::DbIp ? kDbIpDatabaseFilename
                                                                        : kGeoIpDatabaseFilename)});
    if (!unwrapped.error.isEmpty()) {
        finish(false, unwrapped.error);
        return;
    }

    QString error;
    if (!installDatabase(unwrapped.data, error)) {
        finish(false, error);
        return;
    }

    thePrefs.setGeoIpLastCheck(QDateTime::currentSecsSinceEpoch());
    const QDateTime built = m_ip2Country->buildDate();
    // Source switched while downloading: the new file waits for its turn
    if (m_activeSource != selectedSource())
        reloadForSource();
    emit databaseChanged();
    finish(true, QStringLiteral("%1 updated (built %2)")
                     .arg(sourceName(m_activeSource), built.toString(Qt::ISODate)));
}

bool GeoIpUpdater::installDatabase(const QByteArray& mmdb, QString& error)
{
    const QString target = databasePathFor(m_activeSource);
    const QString staging = target + QStringLiteral(".new");

    QFile out(staging);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)
        || out.write(mmdb) != mmdb.size() || !out.flush()) {
        error = QStringLiteral("cannot write %1: %2").arg(staging, out.errorString());
        out.close();
        QFile::remove(staging);
        return false;
    }
    out.close();

    if (!IP2Country::isValidDatabase(staging, &error)) {
        error = QStringLiteral("downloaded database is invalid: %1").arg(error);
        QFile::remove(staging);
        return false;
    }

    // Windows can't replace a mapped file, so let go of it first
    const QString previous = m_ip2Country->path();
    m_ip2Country->close();
    std::error_code ec;
    std::filesystem::rename(staging.toStdU16String(), target.toStdU16String(), ec);
    if (ec) {
        error = QStringLiteral("cannot replace %1: %2")
                    .arg(target, QString::fromStdString(ec.message()));
        QFile::remove(staging);
        if (!previous.isEmpty())
            m_ip2Country->open(previous);
        return false;
    }
    return m_ip2Country->open(target, &error);
}

void GeoIpUpdater::finish(bool ok, const QString& message)
{
    if (ok) {
        m_lastFailure = {};
        logInfo(QStringLiteral("IP2Country: %1").arg(message));
    } else {
        m_lastFailure = QDateTime::currentDateTimeUtc();
        logWarning(QStringLiteral("IP2Country: update failed: %1").arg(message));
    }

    auto waiting = std::exchange(m_waiting, {});
    for (auto& cb : waiting)
        cb(ok, message);
    emit updateFinished(ok, message);
}

bool GeoIpUpdater::openDatabase(const QString& path)
{
    QString error;
    if (!m_ip2Country->open(path, &error)) {
        logWarning(QStringLiteral("IP2Country: cannot open %1: %2").arg(path, error));
        return false;
    }
    logInfo(QStringLiteral("IP2Country: loaded %1 (built %2)")
                .arg(path, m_ip2Country->buildDate().toString(Qt::ISODate)));
    return true;
}

bool GeoIpUpdater::selectedLoaded() const
{
    return m_ip2Country->isLoaded() && m_ip2Country->path() == databasePath();
}

QUrl GeoIpUpdater::dbIpUrl(int monthsBack) const
{
    const QDate month = QDateTime::currentDateTimeUtc().date().addMonths(-monthsBack);
    return QUrl(m_dbIpUrlTemplate.arg(month.toString(QStringLiteral("yyyy-MM"))));
}

} // namespace eMule
