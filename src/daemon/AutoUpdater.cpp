/// @file AutoUpdater.cpp
/// @brief `emulecored --autoupdate[=hours]` — implementation.

#include "AutoUpdater.h"

#include "app/AppConfig.h"
#include "app/VersionManifest.h"
#include "net/GuardedNetworkAccessManager.h"
#include "net/HttpDefaults.h"
#include "net/HttpStreamDownload.h"
#include "prefs/Preferences.h"
#include "utils/Log.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QProcess>
#include <QSaveFile>
#include <QTimer>
#include <QUrl>

#ifdef Q_OS_WIN
#  include <windows.h>
#endif

#include <QDeadlineTimer>

namespace eMule {

namespace {

constexpr int kTickMs = 60 * 60 * 1000;     // hourly due check
constexpr qint64 kRetrySecs = 60 * 60;      // after a failed fetch/download
constexpr int kMaxFailures = 3;             // per version
constexpr qint64 kSlackBytes = 1024 * 1024; // download cap above the manifest size
constexpr int kGuiWaitMs = 15'000;          // for GUIs to quit after the push
constexpr int kMaxHours = 720;

const QString kStateFile = QStringLiteral("autoupdate.json");
const QString kResultFile = QStringLiteral("result.json");
const QString kLogFile = QStringLiteral("updater.log");

QString exeName(const QString& base)
{
#ifdef Q_OS_WIN
    return base + QStringLiteral(".exe");
#else
    return base;
#endif
}

bool isNumber(const QString& s)
{
    bool ok = false;
    s.toInt(&ok);
    return ok;
}

} // namespace

AutoUpdater::AutoUpdater(QString stateDir, int hoursOverride, QObject* parent)
    : QObject(parent)
    , m_stateDir(std::move(stateDir))
    , m_hoursOverride(clampHours(hoursOverride))
    , m_currentVersion(QCoreApplication::applicationVersion())
    , m_appDir(QCoreApplication::applicationDirPath())
    , m_coreArgv(QCoreApplication::arguments())
{
    QDir().mkpath(m_stateDir);
}

AutoUpdater::~AutoUpdater()
{
    if (m_reply)
        m_reply->abort();
}

void AutoUpdater::start()
{
    loadState();
    readResult();
    if (m_hoursOverride > 0)
        logInfo(QStringLiteral("Auto-update on, every %1 h").arg(m_hoursOverride));
    else
        logInfo(QStringLiteral("Auto-update on, every %1 day(s)").arg(thePrefs.versionCheckDays()));

    if (!m_tick) {
        m_tick = new QTimer(this);
        m_tick->setInterval(kTickMs);
        connect(m_tick, &QTimer::timeout, this, &AutoUpdater::checkIfDue);
        m_tick->start();
    }
    QTimer::singleShot(m_firstDelayMs, this, &AutoUpdater::checkIfDue);
}

void AutoUpdater::checkNow()
{
    if (m_busy)
        return;
    fetchManifest();
}

// ---------------------------------------------------------------------------
// Pure helpers
// ---------------------------------------------------------------------------

QStringList AutoUpdater::normalizeArgs(const QStringList& args)
{
    static const QString flag = QStringLiteral("--autoupdate");
    static const QString hours = QStringLiteral("--autoupdate-hours");
    QStringList out;
    out.reserve(args.size() + 2);
    for (qsizetype i = 0; i < args.size(); ++i) {
        const QString& a = args.at(i);
        if (a.startsWith(flag + QLatin1Char('='))) {
            out << flag << hours << a.mid(flag.size() + 1);
        } else if (a == flag && i + 1 < args.size() && isNumber(args.at(i + 1))) {
            out << flag << hours << args.at(++i);
        } else {
            out << a;
        }
    }
    return out;
}

QStringList AutoUpdater::relaunchArgs(const QStringList& argv)
{
    QStringList out = argv.mid(1);
    out.removeAll(QStringLiteral("--hold-connect"));
    return out;
}

int AutoUpdater::clampHours(int hours)
{
    return hours >= 1 && hours <= kMaxHours ? hours : 0;
}

bool AutoUpdater::isDue(qint64 lastCheck, qint64 now, qint64 intervalSecs)
{
    // lastCheck in the future = the clock moved back: due rather than never
    return lastCheck <= 0 || lastCheck > now || now - lastCheck >= intervalSecs;
}

QString AutoUpdater::packageKey(const QString& platformKey, bool innoInstall)
{
    if (platformKey.isEmpty())
        return {};
    if (innoInstall && platformKey.startsWith(QStringLiteral("windows-")))
        return platformKey + QStringLiteral("-installer");
    return platformKey;
}

QString AutoUpdater::packageFormat(const QString& fileName)
{
    const QString f = fileName.toLower();
    if (f.endsWith(QStringLiteral(".dmg")))
        return QStringLiteral("dmg");
    if (f.endsWith(QStringLiteral(".exe")))
        return QStringLiteral("inno");
    if (f.endsWith(QStringLiteral(".zip")) || f.endsWith(QStringLiteral(".tar.gz")) || f.endsWith(QStringLiteral(".tgz")))
        return QStringLiteral("archive");
    return {};
}

QString AutoUpdater::installRoot(const QString& appDir)
{
    const QDir dir(appDir);
#ifdef Q_OS_MACOS
    // <X>.app/Contents/MacOS
    const QString bundle = QDir::cleanPath(dir.absoluteFilePath(QStringLiteral("../..")));
    if (bundle.endsWith(QStringLiteral(".app")))
        return bundle;
#endif
    return QDir::cleanPath(dir.absolutePath());
}

QStringList AutoUpdater::configKeepArgs(const QString& root, const QStringList& configDirs,
                                        const QStringList& userFiles)
{
#ifdef Q_OS_WIN
    constexpr auto cs = Qt::CaseInsensitive;
#else
    constexpr auto cs = Qt::CaseSensitive;
#endif
    const QString base = QDir::cleanPath(QFileInfo(root).absoluteFilePath());
    const QString bundle = base + QStringLiteral("/config");
    QStringList out, seen;
    for (const QString& d : configDirs) {
        if (d.isEmpty())
            continue;
        const QString dir = QDir::cleanPath(QFileInfo(d).absoluteFilePath());
        if (seen.contains(dir, cs) || !dir.startsWith(base + QLatin1Char('/'), cs))
            continue;
        seen << dir;
        const QString rel = dir.mid(base.size() + 1);
        if (dir.compare(bundle, cs) == 0) {
            out << QStringLiteral("--keep-edited") << rel;
            for (const QString& f : userFiles)
                out << QStringLiteral("--keep-existing") << rel + QLatin1Char('/') + f;
        } else {
            out << QStringLiteral("--keep-existing") << rel;
        }
    }
    return out;
}

bool AutoUpdater::inContainer()
{
#ifdef Q_OS_LINUX
    return QFile::exists(QStringLiteral("/.dockerenv")) || QFile::exists(QStringLiteral("/run/.containerenv"));
#else
    return false;
#endif
}

QString AutoUpdater::helperName()
{
    return exeName(QStringLiteral("emuleqt-updater"));
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

void AutoUpdater::checkIfDue()
{
    if (m_busy)
        return;
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    if (m_retryAfter > now)
        return;
    if (!isDue(m_lastCheck, now, intervalSecs()))
        return;
    fetchManifest();
}

void AutoUpdater::fetchManifest()
{
    if (!m_nam) {
        m_ownNam = std::make_unique<GuardedNetworkAccessManager>();
        m_nam = m_ownNam.get();
    }
    m_busy = true;
    setStage(QStringLiteral("checking"));
    QNetworkRequest req = Http::makeRequest(QUrl(VersionManifest::manifestUrl()));
    req.setTransferTimeout(30'000);
    m_reply = m_nam->get(req);
    connect(m_reply, &QNetworkReply::finished, this, [this, r = m_reply.data()] { onManifest(r); });
}

void AutoUpdater::onManifest(QNetworkReply* reply)
{
    reply->deleteLater();
    m_reply = nullptr;
    const qint64 now = QDateTime::currentSecsSinceEpoch();

    if (reply->error() != QNetworkReply::NoError) {
        m_busy = false;
        m_retryAfter = now + kRetrySecs;
        logWarning(QStringLiteral("Auto-update: version check failed: %1").arg(reply->errorString()));
        setStage(QStringLiteral("failed"), reply->errorString());
        return;
    }
    const VersionManifestInfo info = VersionManifest::parse(reply->readAll());
    if (!info.isValid()) {
        m_busy = false;
        m_retryAfter = now + kRetrySecs;
        logWarning(QStringLiteral("Auto-update: %1").arg(info.error));
        setStage(QStringLiteral("failed"), info.error);
        return;
    }
    // only a usable manifest moves the interval
    m_lastCheck = now;
    saveState();

    if (!VersionManifest::isNewer(info.latest, m_currentVersion)) {
        m_busy = false;
        logDebug(QStringLiteral("Auto-update: v%1 is current").arg(m_currentVersion));
        setStage(QStringLiteral("uptodate"), m_currentVersion);
        return;
    }
    if (info.latest == m_failedVersion && m_failedCount >= kMaxFailures) {
        m_busy = false;
        logDebug(QStringLiteral("Auto-update: v%1 failed %2 times, waiting for a newer release")
                     .arg(info.latest).arg(m_failedCount));
        setStage(QStringLiteral("skipped"), info.latest);
        return;
    }

    const bool inno = QFile::exists(QDir(m_appDir).filePath(QStringLiteral("unins000.exe")));
    const QString key = packageKey(VersionManifest::platformKey(), inno);
    const VersionDownload d = info.downloadFor(key);
    const QString fileName = QFileInfo(QUrl(d.url).path()).fileName();
    if (key.isEmpty() || !d.isInstallable() || packageFormat(fileName).isEmpty()) {
        m_busy = false;
        logWarning(QStringLiteral("Auto-update: v%1 is out, but has no package for %2")
                       .arg(info.latest, key.isEmpty() ? QStringLiteral("this platform") : key));
        setStage(QStringLiteral("skipped"), key);
        return;
    }

    m_version = info.latest;
    m_package = QDir(m_stateDir).filePath(fileName);
    logInfo(QStringLiteral("Auto-update: downloading v%1 (%2 bytes)").arg(m_version).arg(d.size));
    setStage(QStringLiteral("downloading"), d.url);
    m_download = std::make_unique<HttpStreamDownload>(m_nam);
    connect(m_download.get(), &HttpStreamDownload::finished, this, &AutoUpdater::onDownloaded);
    if (!m_download->start(QUrl(d.url), m_package, {d.size + kSlackBytes, d.sha256, d.size})) {
        const QString err = m_download->errorString();
        m_download.reset();
        m_busy = false;
        m_retryAfter = now + kRetrySecs;
        recordFailure(m_version, err);
    }
}

void AutoUpdater::onDownloaded(bool ok)
{
    const QString err = m_download ? m_download->errorString() : QString();
    if (m_download)
        m_download.release()->deleteLater();   // inside its signal
    if (!ok) {
        m_busy = false;
        m_retryAfter = QDateTime::currentSecsSinceEpoch() + kRetrySecs;
        recordFailure(m_version, err);
        return;
    }
    logInfo(QStringLiteral("Auto-update: v%1 downloaded and verified").arg(m_version));
    handOver();
}

void AutoUpdater::handOver()
{
    // run the helper from a copy: the install dir is replaced underneath it
    const QString helperSrc = QDir(m_appDir).filePath(helperName());
    const QString helper = QDir(m_stateDir).filePath(helperName());
    QFile::remove(helper);
    if (!QFile::copy(helperSrc, helper)) {
        m_busy = false;
        recordFailure(m_version, QStringLiteral("cannot copy %1").arg(helperSrc));
        QFile::remove(m_package);
        return;
    }
    QFile::setPermissions(helper, QFile::permissions(helper) | QFileDevice::ExeOwner | QFileDevice::ExeUser);

    // GUIs before the push: once they quit they are gone from the list
    const std::vector<Gui> guis = m_hooks.localGuis ? m_hooks.localGuis() : std::vector<Gui>{};
    const QString root = installRoot(m_appDir);

    QStringList args{QStringLiteral("--package"), m_package,
                     QStringLiteral("--format"), packageFormat(m_package),
                     QStringLiteral("--target"), root,
                     QStringLiteral("--version"), m_version,
                     QStringLiteral("--log"), QDir(m_stateDir).filePath(kLogFile),
                     QStringLiteral("--result"), QDir(m_stateDir).filePath(kResultFile),
                     QStringLiteral("--wait-pid"), QString::number(QCoreApplication::applicationPid())};
    for (const Gui& g : guis) {
        if (g.pid > 0)
            args << QStringLiteral("--wait-pid") << QString::number(g.pid);
    }
    // portable config inside the install dir: never overwrite the user's files.
    // Both dirs: a moved portable folder leaves a stale absolute path in the prefs
    args << configKeepArgs(root, {thePrefs.configDir(), AppConfig::configDir()}, AppConfig::userDataFiles());

    // the core first (same arguments, so --autoupdate stays), then the GUIs
    args << QStringLiteral("--launch") << QDir(m_appDir).filePath(exeName(QStringLiteral("emulecored")))
         << relaunchArgs(m_coreArgv) << QStringLiteral("--end");
    for (const Gui& g : guis) {
        if (!g.relaunch.isEmpty() && QFileInfo(g.relaunch.first()).isAbsolute())
            args << QStringLiteral("--launch") << g.relaunch << QStringLiteral("--end");
    }

    m_pendingPackage = m_package;
    saveState();

    if (m_hooks.announce)
        m_hooks.announce(m_version);
    setStage(QStringLiteral("launching"), m_version);

    // give the GUIs a moment to quit by themselves, then hand over
    auto deadline = std::make_shared<QDeadlineTimer>(kGuiWaitMs);
    auto* poll = new QTimer(this);
    poll->setInterval(250);
    connect(poll, &QTimer::timeout, this, [this, poll, deadline, helper, args] {
        const bool guisGone = !m_hooks.localGuis || m_hooks.localGuis().empty();
        if (!guisGone && !deadline->hasExpired())
            return;
        poll->deleteLater();
        const auto launch = m_hooks.launch ? m_hooks.launch : [](const QString& p, const QStringList& a) {
            QProcess proc;
            proc.setProgram(p);
            proc.setArguments(a);
#ifdef Q_OS_WIN
            // a console program: no window flashing up
            proc.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args) {
                args->flags &= ~static_cast<DWORD>(CREATE_NEW_CONSOLE);
                args->flags |= DETACHED_PROCESS;
            });
#endif
            return proc.startDetached();
        };
        if (!launch(helper, args)) {
            m_busy = false;
            m_pendingPackage.clear();
            saveState();
            recordFailure(m_version, QStringLiteral("cannot start %1").arg(helper));
            QFile::remove(m_package);
            return;
        }
        logInfo(QStringLiteral("Auto-update: installing v%1, the core restarts").arg(m_version));
        if (m_hooks.quit)
            m_hooks.quit();
        else
            QMetaObject::invokeMethod(QCoreApplication::instance(), &QCoreApplication::quit, Qt::QueuedConnection);
    });
    poll->start();
}

void AutoUpdater::readResult()
{
    if (!m_pendingPackage.isEmpty()) {
        QFile::remove(m_pendingPackage);
        m_pendingPackage.clear();
    }
    QFile::remove(QDir(m_stateDir).filePath(helperName()));

    const QString path = QDir(m_stateDir).filePath(kResultFile);
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        saveState();
        return;
    }
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    f.close();
    QFile::remove(path);

    const QString version = o.value(QStringLiteral("version")).toString();
    if (o.value(QStringLiteral("ok")).toBool() && version == m_currentVersion) {
        logInfo(QStringLiteral("Auto-update: now running v%1").arg(version));
        m_failedVersion.clear();
        m_failedCount = 0;
        saveState();
    } else {
        QString why = o.value(QStringLiteral("error")).toString();
        if (why.isEmpty())
            why = QStringLiteral("still v%1 after the install").arg(m_currentVersion);
        if (o.value(QStringLiteral("rolledBack")).toBool())
            why += QStringLiteral(" (rolled back)");
        recordFailure(version, why);
    }
}

void AutoUpdater::recordFailure(const QString& version, const QString& why)
{
    if (version == m_failedVersion)
        ++m_failedCount;
    else {
        m_failedVersion = version;
        m_failedCount = 1;
    }
    saveState();
    logWarning(QStringLiteral("Auto-update to v%1 failed (%2/%3): %4")
                   .arg(version).arg(m_failedCount).arg(kMaxFailures).arg(why));
    setStage(QStringLiteral("failed"), why);
}

void AutoUpdater::loadState()
{
    QFile f(statePath());
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    m_lastCheck = o.value(QStringLiteral("lastCheck")).toInteger();
    m_failedVersion = o.value(QStringLiteral("failedVersion")).toString();
    m_failedCount = o.value(QStringLiteral("failedCount")).toInt();
    m_pendingPackage = o.value(QStringLiteral("pendingPackage")).toString();
}

void AutoUpdater::saveState() const
{
    QJsonObject o{{QStringLiteral("lastCheck"), m_lastCheck},
                  {QStringLiteral("failedVersion"), m_failedVersion},
                  {QStringLiteral("failedCount"), m_failedCount}};
    if (!m_pendingPackage.isEmpty())
        o.insert(QStringLiteral("pendingPackage"), m_pendingPackage);
    QSaveFile f(statePath());
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(o).toJson());
        f.commit();
    }
}

qint64 AutoUpdater::intervalSecs() const
{
    if (m_hoursOverride > 0)
        return qint64(m_hoursOverride) * 3600;
    return qint64(thePrefs.versionCheckDays()) * 86400;
}

QString AutoUpdater::statePath() const
{
    return QDir(m_stateDir).filePath(kStateFile);
}

void AutoUpdater::setStage(const QString& stage, const QString& detail)
{
    emit stageChanged(stage, detail);
}

} // namespace eMule
