#include "Ed3kResolverUpdater.h"

#ifdef EMULE_HAVE_HOSTER_JD

#include "Ed3kJdResolver.h"
#include "net/GuardedNetworkAccessManager.h"
#include "net/HttpFileDownload.h"
#include "net/HttpStreamDownload.h"
#include "utils/BackgroundFileWriter.h"
#include "utils/Log.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

namespace eMule::ed3k {

namespace {

constexpr auto kStateFile = "update.json";
constexpr auto kFetchLock = ".fetch.lock";

QString zipName(int revision)
{
    return QStringLiteral("resolver-r%1.zip").arg(revision);
}

} // namespace

Ed3kResolverUpdater::Ed3kResolverUpdater(QString dir, QObject* parent)
    : ScheduledUpdater(parent)
    , m_dir(std::move(dir))
{
    m_lockRetry.setSingleShot(true);
    m_lockRetry.setInterval(kLockRetryMs);
    connect(&m_lockRetry, &QTimer::timeout, this, &Ed3kResolverUpdater::fetchManifest);
    loadState();
}

Ed3kResolverUpdater::~Ed3kResolverUpdater()
{
    if (m_dl)
        m_dl->abort();
}

void Ed3kResolverUpdater::setDir(const QString& dir)
{
    if (dir == m_dir)
        return;
    const bool wasBusy = busy();
    stop();
    const QString oldDir = std::exchange(m_dir, dir);
    const QString oldZip = m_fetched.isEmpty() ? QString() : QDir(oldDir).filePath(m_fetched);
    const int oldRev = m_fetchedApi == Ed3kJdResolver::kProtocol ? m_fetchedRevision : 0;
    const QString oldSha = m_fetchedSha;
    m_checked = {};
    clearFetched();
    loadState();
    // the zip fetched before goes along when the new folder has nothing as new
    QLockFile lock(QDir(m_dir).filePath(QLatin1StringView(kFetchLock)));
    lock.setStaleLockTime(0);
    if (oldRev > haveRevision() && QDir().mkpath(m_dir) && lock.tryLock(5000) && (reloadState(), oldRev > haveRevision())) {
        const QString name = zipName(oldRev);
        const QString to = QDir(m_dir).filePath(name);
        const QString part = to + QStringLiteral(".part");
        QFile::remove(part);
        // copied: the old folder may be the shared one other instances still read
        if (QFile::copy(oldZip, part) && (QFile::remove(to), QFile::rename(part, to))) {
            m_fetched = name;
            m_fetchedRevision = oldRev;
            m_fetchedApi = Ed3kJdResolver::kProtocol;
            m_fetchedSha = oldSha;
            saveState();
        }
        QFile::remove(part);
    }
    lock.unlock();
    logHosterDebug([&]() -> QString { return QStringLiteral("hoster: resolver plugin updates in %1").arg(m_dir); });
    // a first fetch a link waits for goes on in the new folder
    if (wasBusy && zipToUse().isEmpty())
        fetchManifest();
}

void Ed3kResolverUpdater::setSchedule(bool on, int hours)
{
    m_hours = std::max(1, hours);
    m_on = on;
    if (on && !m_started) {
        m_started = true;
        start();
    } else if (!on && m_started) {
        m_started = false;
        stop();
    }
}

void Ed3kResolverUpdater::setBundledZip(const QString& zip)
{
    if (zip == m_bundled)
        return;
    m_bundled = zip;
    m_bundledRevision = Ed3kJdResolver::readZipInfo(zip).revision;
    // a fetched zip the application caught up with is not needed any more
    if (!m_fetched.isEmpty() && zipToUse() != fetchedZip()) {
        logHosterDebug([&]() -> QString { return QStringLiteral("hoster: fetched resolver plugins r%1 replaced by bundled r%2")
                           .arg(m_fetchedRevision).arg(m_bundledRevision); });
        QFile::remove(fetchedZip());
        m_fetched.clear();
        m_fetchedRevision = m_fetchedApi = 0;
        m_fetchedSha.clear();
        saveState();
    }
}

QString Ed3kResolverUpdater::fetchedZip() const
{
    return m_fetched.isEmpty() ? QString() : QDir(m_dir).filePath(m_fetched);
}

QString Ed3kResolverUpdater::zipToUse() const
{
    const QString fetched = fetchedZip();
    const bool usable = !fetched.isEmpty() && m_fetchedApi == Ed3kJdResolver::kProtocol && QFileInfo(fetched).isFile();
    if (usable && (m_bundled.isEmpty() || m_fetchedRevision > m_bundledRevision))
        return fetched;
    return m_bundled;
}

bool Ed3kResolverUpdater::busy() const
{
    return isRunning() || (m_dl && m_dl->isRunning()) || m_lockRetry.isActive();
}

void Ed3kResolverUpdater::checkSchedule()
{
    // shared: another instance may have fetched or checked meanwhile
    if (!busy() && reloadState())
        emit zipReady();
    // no zip at all (bundles ship none): fetchFirst() once a link wants one
    if (!m_on || busy() || backingOff() || zipToUse().isEmpty())
        return;
    if (m_checked.isValid() && m_checked.secsTo(QDateTime::currentDateTimeUtc()) < qint64(m_hours) * 3600)
        return;
    fetchManifest();
}

bool Ed3kResolverUpdater::fetchFirst()
{
    if (busy())
        return true;
    if (reloadState())
        emit zipReady();
    if (!zipToUse().isEmpty())
        return false;
    // links come in bursts: one try per kFirstRetrySecs
    const QDateTime now = QDateTime::currentDateTimeUtc();
    if (m_firstTried.isValid() && m_firstTried.secsTo(now) < kFirstRetrySecs)
        return false;
    m_firstTried = now;
    logHoster(QStringLiteral("hoster: a link needs the resolver plugins, fetching them"));
    fetchManifest();
    return true;
}

void Ed3kResolverUpdater::checkNow()
{
    if (!m_on || busy())
        return;
    clearBackoff();
    fetchManifest();
}

void Ed3kResolverUpdater::stop()
{
    ScheduledUpdater::stop();
    m_lockRetry.stop();
    if (m_dl && m_dl->isRunning()) {
        m_dl->abort();
        logHosterDebug([] { return QStringLiteral("hoster: resolver plugin download stopped"); });
    }
    m_fetchLock.reset();
}

bool Ed3kResolverUpdater::wanted(const HosterResolverRelease& r, int haveRevision, QString* why)
{
    const auto no = [why](const QString& text) {
        if (why)
            *why = text;
        return false;
    };
    if (!r.file.isInstallable())
        return no(QStringLiteral("no usable resolver.zip entry"));
    // https; plain http only on loopback (test servers)
    if (const QUrl u(r.file.url); u.scheme() != QLatin1StringView("https") && !QHostAddress(u.host()).isLoopback())
        return no(QStringLiteral("resolver.zip not on https"));
    if (r.file.size > kMaxZipBytes)
        return no(QStringLiteral("resolver.zip too large"));
    if (r.api != Ed3kJdResolver::kProtocol)
        return no(r.api == 0 ? QStringLiteral("resolver.zip has no api version")
                             : QStringLiteral("resolver.zip is api %1, this version speaks %2")
                                   .arg(r.api).arg(Ed3kJdResolver::kProtocol));
    if (r.jdRevision <= haveRevision)
        return no(QStringLiteral("r%1 is not newer than r%2").arg(r.jdRevision).arg(haveRevision));
    return true;
}

// -- private ------------------------------------------------------------------

void Ed3kResolverUpdater::fetchManifest()
{
    if (!takeFetchLock())
        return;
    // the other instance's fetch ended: its zip, no second download
    if (reloadState()) {
        emit zipReady();
        finish(true, QStringLiteral("resolver plugins r%1 fetched by another instance").arg(m_fetchedRevision));
        return;
    }
    const QUrl url = m_manifestUrl.isValid() ? m_manifestUrl : QUrl(VersionManifest::manifestUrl());
    logHosterDebug([] { return QStringLiteral("hoster: checking for new resolver plugins"); });
    m_job = HttpFileDownload::fetch(this, url, {}, [this](const HttpFileDownload::Result& res) {
        if (!res.ok) {
            finish(false, QStringLiteral("manifest: %1").arg(res.error));
            return;
        }
        onManifest(res.data);
    });
}

void Ed3kResolverUpdater::onManifest(const QByteArray& json)
{
    const VersionManifestInfo info = VersionManifest::parse(json);
    if (!info.isValid()) {
        finish(false, QStringLiteral("manifest: %1").arg(info.error));
        return;
    }
    QString why;
    if (!wanted(info.hosterResolver, haveRevision(), &why)) {
        finish(true, why);
        return;
    }
    startZip(info.hosterResolver);
}

void Ed3kResolverUpdater::startZip(const HosterResolverRelease& r)
{
    if (!QDir().mkpath(m_dir)) {
        finish(false, QStringLiteral("cannot create %1").arg(m_dir));
        return;
    }
    if (!m_nam)
        m_nam = std::make_unique<GuardedNetworkAccessManager>();
    m_nam->setProxy(m_proxy);
    m_wanted = r;
    m_dl = std::make_unique<HttpStreamDownload>(m_nam.get());
    connect(m_dl.get(), &HttpStreamDownload::finished, this, &Ed3kResolverUpdater::onZipDone);
    HttpStreamDownload::Options opts;
    opts.maxBytes = kMaxZipBytes;
    opts.sha256 = r.file.sha256;
    opts.expectedSize = r.file.size;
    logHoster(QStringLiteral("hoster: downloading resolver plugins r%1 (%2 MB)")
                  .arg(r.jdRevision).arg(r.file.size / (1024 * 1024)));
    if (!m_dl->start(QUrl(r.file.url), QDir(m_dir).filePath(zipName(r.jdRevision)), opts))
        finish(false, QStringLiteral("cannot write into %1").arg(m_dir));
}

void Ed3kResolverUpdater::onZipDone(bool ok)
{
    const QString path = QDir(m_dir).filePath(zipName(m_wanted.jdRevision));
    if (!ok) {
        finish(false, QStringLiteral("resolver.zip: %1").arg(m_dl->errorString()));
        return;
    }
    // the manifest may lie about what is inside: the zip's own word decides
    const Ed3kJdResolver::ZipInfo zi = Ed3kJdResolver::readZipInfo(path);
    if (!zi.layout || zi.api != Ed3kJdResolver::kProtocol || zi.revision != m_wanted.jdRevision) {
        QFile::remove(path);
        finish(false, QStringLiteral("resolver.zip is not r%1 of api %2 (has r%3, api %4)")
                          .arg(m_wanted.jdRevision).arg(Ed3kJdResolver::kProtocol).arg(zi.revision).arg(zi.api));
        return;
    }
    pruneZips(zipName(zi.revision));
    m_fetched = zipName(zi.revision);
    m_fetchedRevision = zi.revision;
    m_fetchedApi = zi.api;
    m_fetchedSha = m_wanted.file.sha256;
    logHoster(QStringLiteral("hoster: resolver plugins r%1 downloaded").arg(zi.revision));
    // before checked(): the resolver takes the zip while it still shows "downloading"
    emit zipReady();
    finish(true, QStringLiteral("resolver plugins r%1 downloaded").arg(zi.revision));
}

void Ed3kResolverUpdater::finish(bool ok, const QString& message)
{
    noteOutcome(ok);
    if (ok)
        m_checked = QDateTime::currentDateTimeUtc();
    saveState();
    m_fetchLock.reset();
    if (ok)
        logHosterDebug([&]() -> QString { return QStringLiteral("hoster: plugin update check: %1").arg(message); });
    else
        logHosterWarning(QStringLiteral("hoster: plugin update failed: %1").arg(message));
    emit checked(ok, message);
}

void Ed3kResolverUpdater::loadState()
{
    const auto raw = BackgroundFileWriter::readFile(QDir(m_dir).filePath(QLatin1StringView(kStateFile)));
    if (!raw)
        return;
    const QJsonObject o = QJsonDocument::fromJson(*raw).object();
    m_checked = QDateTime::fromString(o.value(QStringLiteral("checked")).toString(), Qt::ISODate);
    // only a name of ours: the file can be edited by hand
    static const QRegularExpression name(QStringLiteral("^resolver-r[0-9]+\\.zip$"));
    const QString file = o.value(QStringLiteral("file")).toString();
    if (!name.match(file).hasMatch() || !QFileInfo(QDir(m_dir).filePath(file)).isFile())
        return;
    m_fetched = file;
    m_fetchedRevision = o.value(QStringLiteral("revision")).toInt();
    m_fetchedApi = o.value(QStringLiteral("api")).toInt();
    m_fetchedSha = o.value(QStringLiteral("sha256")).toString();
}

bool Ed3kResolverUpdater::reloadState()
{
    const QString before = zipToUse();
    const QString fetched = m_fetched;
    const int revision = m_fetchedRevision;
    m_checked = {};
    clearFetched();
    loadState();
    return zipToUse() != before || m_fetched != fetched || m_fetchedRevision != revision;
}

bool Ed3kResolverUpdater::takeFetchLock()
{
    if (m_fetchLock)
        return true;
    if (!QDir().mkpath(m_dir)) {
        finish(false, QStringLiteral("cannot create %1").arg(m_dir));
        return false;
    }
    auto lock = std::make_unique<QLockFile>(QDir(m_dir).filePath(QLatin1StringView(kFetchLock)));
    lock->setStaleLockTime(0);   // a fetch can take long; only a dead holder frees it
    if (!lock->tryLock(0)) {
        if (lock->error() != QLockFile::LockFailedError) {
            finish(false, QStringLiteral("cannot lock %1").arg(m_dir));
            return false;
        }
        logHosterDebug([] { return QStringLiteral("hoster: another instance fetches resolver plugins, waiting"); });
        m_lockRetry.start();
        return false;
    }
    m_fetchLock = std::move(lock);
    return true;
}

void Ed3kResolverUpdater::clearFetched()
{
    m_fetched.clear();
    m_fetchedRevision = m_fetchedApi = 0;
    m_fetchedSha.clear();
}

void Ed3kResolverUpdater::saveState() const
{
    QJsonObject o{{QStringLiteral("checked"), m_checked.toString(Qt::ISODate)}};
    if (!m_fetched.isEmpty()) {
        o.insert(QStringLiteral("file"), m_fetched);
        o.insert(QStringLiteral("revision"), m_fetchedRevision);
        o.insert(QStringLiteral("api"), m_fetchedApi);
        o.insert(QStringLiteral("sha256"), m_fetchedSha);
    }
    // off the main thread (report 43); the writer makes the folder
    BackgroundFileWriter::instance().write(QDir(m_dir).filePath(QLatin1StringView(kStateFile)), QJsonDocument(o).toJson(), {});
}

void Ed3kResolverUpdater::pruneZips(const QString& keep) const
{
    const QDir dir(m_dir);
    for (const QString& f : dir.entryList({QStringLiteral("resolver-r*.zip")}, QDir::Files))
        if (f != keep && f != m_fetched)
            QFile::remove(dir.filePath(f));
}

int Ed3kResolverUpdater::haveRevision() const
{
    const int fetched = m_fetchedApi == Ed3kJdResolver::kProtocol ? m_fetchedRevision : 0;
    return std::max(fetched, m_bundledRevision);
}

} // namespace eMule::ed3k

#endif // EMULE_HAVE_HOSTER_JD
