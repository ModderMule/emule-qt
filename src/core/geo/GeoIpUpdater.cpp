#include "pch.h"
/// @file GeoIpUpdater.cpp
/// @brief Scheduled GeoLite2-Country download.

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

} // namespace

GeoIpUpdater::GeoIpUpdater(IP2Country* ip2Country, const QString& configDir, QObject* parent)
    : QObject(parent)
    , m_ip2Country(ip2Country)
    , m_configDir(configDir)
    , m_url(QString::fromLatin1(kDownloadUrl))
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
    return QDir(m_configDir).filePath(QString::fromLatin1(kGeoIpDatabaseFilename));
}

void GeoIpUpdater::start()
{
    adoptBundledDatabase();
    const QString path = databasePath();
    if (!m_ip2Country->isLoaded() && QFileInfo::exists(path)) {
        QString error;
        if (m_ip2Country->open(path, &error))
            logInfo(QStringLiteral("IP2Country: loaded %1 (built %2)")
                        .arg(path, m_ip2Country->buildDate().toString(Qt::ISODate)));
        else
            logWarning(QStringLiteral("IP2Country: cannot open %1: %2").arg(path, error));
    }
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

    const QString accountId = thePrefs.geoIpAccountId().trimmed();
    const QString licenseKey = thePrefs.geoIpLicenseKey().trimmed();
    if (accountId.isEmpty() || licenseKey.isEmpty()) {
        finish(false, QStringLiteral("no MaxMind account ID / license key configured"));
        return;
    }

    logInfo(QStringLiteral("IP2Country: checking for a GeoLite2-Country update"));
    m_redirects = 0;
    startRequest(m_url, true);
}

void GeoIpUpdater::checkSchedule()
{
    if (m_reply || !thePrefs.geoIpAutoUpdate())
        return;
    if (thePrefs.geoIpAccountId().trimmed().isEmpty()
        || thePrefs.geoIpLicenseKey().trimmed().isEmpty())
        return;

    const QDateTime now = QDateTime::currentDateTimeUtc();
    if (m_lastFailure.isValid() && m_lastFailure.secsTo(now) < kFailureBackoffSecs)
        return;

    const bool haveDb = m_ip2Country->isLoaded();
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
    const QString target = databasePath();
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
    if (!installDatabase(in.readAll(), error)) {
        logWarning(QStringLiteral("IP2Country: cannot adopt the bundled database: %1").arg(error));
        return false;
    }
    logInfo(QStringLiteral("IP2Country: updated GeoLite2-Country from the bundle (built %1)")
                .arg(bundledBuilt.toString(Qt::ISODate)));
    emit databaseChanged();
    return true;
}

void GeoIpUpdater::startRequest(const QUrl& url, bool withCredentials)
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
        // Only skip the download when the file we would replace is really there
        if (m_ip2Country->isLoaded() && QFileInfo::exists(databasePath()))
            if (const QDateTime built = m_ip2Country->buildDate(); built.isValid())
                request.setRawHeader("If-Modified-Since", httpDate(built));
    }

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
            || (target.scheme() != u"https" && m_url.scheme() == u"https")) {
            finish(false, QStringLiteral("bad redirect from the download server (HTTP %1)").arg(status));
            return;
        }
        // Credentials stay with MaxMind: the file itself comes from a presigned
        // storage URL that refuses any Authorization header (HTTP 400)
        startRequest(target, target.host() == m_url.host());
        return;
    }
    if (status == 304) {
        thePrefs.setGeoIpLastCheck(QDateTime::currentSecsSinceEpoch());
        finish(true, QStringLiteral("GeoLite2-Country is up to date"));
        return;
    }
    if (status == 401 || status == 403) {
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

    const UnwrapResult unwrapped =
        unwrapDownload(body, {QString::fromLatin1(kGeoIpDatabaseFilename)});
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
    emit databaseChanged();
    finish(true, QStringLiteral("GeoLite2-Country updated (built %1)")
                     .arg(m_ip2Country->buildDate().toString(Qt::ISODate)));
}

bool GeoIpUpdater::installDatabase(const QByteArray& mmdb, QString& error)
{
    const QString target = databasePath();
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
    m_ip2Country->close();
    std::error_code ec;
    std::filesystem::rename(staging.toStdU16String(), target.toStdU16String(), ec);
    if (ec) {
        error = QStringLiteral("cannot replace %1: %2")
                    .arg(target, QString::fromStdString(ec.message()));
        QFile::remove(staging);
        if (QFileInfo::exists(target))
            m_ip2Country->open(target);
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

} // namespace eMule
