#include "Ed3kFfmpegTool.h"
#include "utils/ArgStr.h"

#ifdef EMULE_HAVE_HOSTER_JD

#include "app/VersionManifest.h"
#include "archive/ArchiveReader.h"
#include "net/GuardedNetworkAccessManager.h"
#include "net/HttpFileDownload.h"
#include "net/HttpStreamDownload.h"
#include "utils/Log.h"
#include "utils/SafeRemove.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QProcess>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>

#include <utility>

namespace eMule::ed3k {

namespace {

constexpr auto kVersionFile = "version.txt";

QString ffmpegName()
{
    return Ed3kFfmpegTool::exe(QStringLiteral("ffmpeg"));
}

QString ffprobeName()
{
    return Ed3kFfmpegTool::exe(QStringLiteral("ffprobe"));
}

} // namespace

Ed3kFfmpegTool::Ed3kFfmpegTool(QString baseDir, QObject* parent)
    : QObject(parent)
    , m_baseDir(std::move(baseDir))
{
}

Ed3kFfmpegTool::~Ed3kFfmpegTool()
{
    abortTransfer();
    if (m_verify) {
        m_verify->disconnect(this);
        m_verify->kill();
        m_verify->waitForFinished(3000);
    }
    if (m_unpackThread) {
        m_unpackThread->disconnect(this);
        m_unpackThread->wait();
    }
}

QString Ed3kFfmpegTool::exe(const QString& name)
{
#ifdef Q_OS_WIN
    return name + QStringLiteral(".exe");
#else
    return name;
#endif
}

QString Ed3kFfmpegTool::toolDir() const
{
    return QDir(m_baseDir).filePath(QStringLiteral("ffmpeg"));
}

QStringList Ed3kFfmpegTool::candidateDirs(const QString& appDir) const
{
    QStringList dirs{toolDir()};
    if (!appDir.isEmpty()) {
        // a bundle that ships them: next to the binary, macOS Resources, Linux share
        dirs << appDir << QDir(appDir).filePath(QStringLiteral("tools"))
             << QDir(appDir).filePath(QStringLiteral("../Resources/ffmpeg"))
             << QDir(appDir).filePath(QStringLiteral("../share/emuleqt/ffmpeg"));
    }
    if (const QString onPath = QStandardPaths::findExecutable(ffmpegName()); !onPath.isEmpty())
        dirs << QFileInfo(onPath).absolutePath();
#if defined(Q_OS_MACOS)
    // Finder starts apps with PATH=/usr/bin:/bin:/usr/sbin:/sbin: Homebrew and MacPorts not on it
    dirs << QStringLiteral("/opt/homebrew/bin") << QStringLiteral("/usr/local/bin") << QStringLiteral("/opt/local/bin");
#elif defined(Q_OS_WIN)
    if (const QString local = qEnvironmentVariable("LOCALAPPDATA"); !local.isEmpty())
        dirs << QDir(local).filePath(QStringLiteral("Microsoft/WinGet/Links"));
    dirs << QDir::home().filePath(QStringLiteral("scoop/shims"));
    if (const QString pd = qEnvironmentVariable("ProgramData"); !pd.isEmpty())
        dirs << QDir(pd).filePath(QStringLiteral("chocolatey/bin"));
    if (const QString pf = qEnvironmentVariable("ProgramFiles"); !pf.isEmpty())
        dirs << QDir(pf).filePath(QStringLiteral("ffmpeg/bin"));
#else
    dirs << QStringLiteral("/usr/bin") << QStringLiteral("/usr/local/bin") << QStringLiteral("/snap/bin");
#endif
    for (QString& d : dirs)
        d = QDir::cleanPath(d);
    dirs.removeDuplicates();
    return dirs;
}

void Ed3kFfmpegTool::locate()
{
    if (busy())
        return;
    for (const QString& dir : candidateDirs(QCoreApplication::applicationDirPath())) {
        const QFileInfo f(QDir(dir).filePath(ffmpegName()));
        const QFileInfo p(QDir(dir).filePath(ffprobeName()));
        // both from one folder: a mixed pair may not match
        if (f.isFile() && f.isExecutable() && p.isFile() && p.isExecutable()) {
            const bool changed = m_ffmpeg != f.absoluteFilePath() || m_state != State::Ready;
            m_ffmpeg = f.absoluteFilePath();
            m_ffprobe = p.absoluteFilePath();
            if (changed)
                logHoster(QStringLiteral("hoster: ffmpeg at %1").arg(dir));
            setState(State::Ready);
            return;
        }
    }
    m_ffmpeg.clear();
    m_ffprobe.clear();
    // a failure stays one: download() would refuse anyway
    if (m_state != State::Failed)
        setState(State::Missing);
}

bool Ed3kFfmpegTool::download()
{
    if (busy())
        return true;
    if (!m_allowed) {
        m_error = tr("hoster downloads are off");
        return false;
    }
    if (m_failedAt.isValid() && m_failedAt.secsTo(QDateTime::currentDateTimeUtc()) < kRetrySecs)
        return false;   // m_error still says why
    m_progress = 0;
    setState(State::Downloading);
    const QUrl url = m_manifestUrl.isValid() ? m_manifestUrl : QUrl(VersionManifest::manifestUrl());
    logHoster(QStringLiteral("hoster: no ffmpeg found, looking for ours in the update manifest"));
    m_job = HttpFileDownload::fetch(this, url, {}, [this](const HttpFileDownload::Result& res) {
        // aborted (abortTransfer cleared m_job first): nothing to do
        if (!m_job || m_state != State::Downloading)
            return;
        m_job = nullptr;
        if (!res.ok) {
            fail(QStringLiteral("manifest: %1").arg(res.error));
            return;
        }
        onManifest(res.data);
    });
    return true;
}

void Ed3kFfmpegTool::setAllowed(bool allowed)
{
    if (m_allowed == allowed)
        return;
    m_allowed = allowed;
    if (allowed || !busy())
        return;
    abortTransfer();
    if (m_unpackThread) {
        // a zip of ~100 MB: seconds
        QThread* t = m_unpackThread;
        m_unpackThread = nullptr;
        t->disconnect(this);
        t->wait();
        t->deleteLater();
    }
    if (!m_zip.isEmpty())
        QFile::remove(std::exchange(m_zip, {}));
    removeDirTree(toolDir() + QStringLiteral(".tmp"));
    logHoster(QStringLiteral("hoster: ffmpeg download stopped (hoster downloads switched off)"));
    setState(State::Missing);
}

// -- private ------------------------------------------------------------------

void Ed3kFfmpegTool::onManifest(const QByteArray& json)
{
    const VersionManifestInfo info = VersionManifest::parse(json);
    if (!info.isValid()) {
        fail(QStringLiteral("manifest: %1").arg(info.error));
        return;
    }
    const VersionDownload d = info.ffmpeg.forPlatform();
    if (!d.isInstallable()) {
        fail(tr("no ffmpeg for %1 in the update manifest").arg(VersionManifest::platformKey()));
        return;
    }
    // https; plain http only on loopback (test servers)
    if (const QUrl u(d.url); u.scheme() != QLatin1StringView("https") && !QHostAddress(u.host()).isLoopback()) {
        fail(QStringLiteral("ffmpeg zip not on https"));
        return;
    }
    if (d.size > kMaxZipBytes) {
        fail(QStringLiteral("ffmpeg zip too large (%1 bytes)").arg(d.size));
        return;
    }
    m_version = info.ffmpeg.version;
    if (!QDir().mkpath(m_baseDir)) {
        fail(QStringLiteral("cannot create %1").arg(m_baseDir));
        return;
    }
    if (!m_nam)
        m_nam = std::make_unique<GuardedNetworkAccessManager>();
    m_nam->setProxy(m_proxy);
    m_dl = std::make_unique<HttpStreamDownload>(m_nam.get());
    connect(m_dl.get(), &HttpStreamDownload::progress, this, [this, total = d.size](qint64 got, qint64) {
        const int pct = total > 0 ? int(std::min<qint64>(100, got * 100 / total)) : 0;
        if (pct != m_progress) {
            m_progress = pct;
            emit stateChanged();
        }
    });
    connect(m_dl.get(), &HttpStreamDownload::finished, this, &Ed3kFfmpegTool::onZipDone);
    m_zip = QDir(m_baseDir).filePath(QStringLiteral("ffmpeg-download.zip"));
    logHoster(QStringLiteral("hoster: downloading ffmpeg %1 (%2 MB)")
                  .arg(argStr(m_version.isEmpty() ? QStringLiteral("?") : m_version),
                       argStr(d.size / (1024 * 1024))));
    if (!m_dl->start(QUrl(d.url), m_zip, {kMaxZipBytes, d.sha256, d.size})) {
        const QString err = m_dl->errorString();
        m_dl.reset();
        fail(QStringLiteral("cannot write %1: %2").arg(m_zip, err));
    }
}

void Ed3kFfmpegTool::onZipDone(bool ok)
{
    if (!m_dl)
        return;
    const QString err = m_dl->errorString();
    m_dl.release()->deleteLater();   // we are inside its signal
    if (!ok) {
        fail(QStringLiteral("ffmpeg zip: %1").arg(err));
        return;
    }
    unpack(m_zip);
}

void Ed3kFfmpegTool::unpack(const QString& zip)
{
    setState(State::Unpacking);
    const QString tmp = toolDir() + QStringLiteral(".tmp");
    removeDirTree(tmp);
    QDir().mkpath(tmp);
    // in-process on every OS: the zip is ours, exec bits are set after
    auto error = std::make_shared<QString>();
    QThread* t = QThread::create([zip, tmp, error] {
        ArchiveReader reader;
        if (!reader.open(zip) || !reader.extractAll(tmp))
            *error = reader.lastError().isEmpty() ? QStringLiteral("unreadable archive") : reader.lastError();
    });
    t->setParent(this);
    m_unpackThread = t;
    connect(t, &QThread::finished, this, [this, t, error] {
        t->deleteLater();
        m_unpackThread = nullptr;
        unpackDone(*error);
    });
    t->start();
}

void Ed3kFfmpegTool::unpackDone(const QString& error)
{
    const QString tmp = toolDir() + QStringLiteral(".tmp");
    QFile::remove(std::exchange(m_zip, {}));
    if (!error.isEmpty()) {
        removeDirTree(tmp);
        fail(QStringLiteral("ffmpeg zip: %1").arg(error));
        return;
    }
    const QString installError = installFrom(tmp);
    removeDirTree(tmp);
    if (!installError.isEmpty()) {
        fail(installError);
        return;
    }
    verify();
}

QString Ed3kFfmpegTool::installFrom(const QString& tree)
{
    QString ffmpeg;
    QString ffprobe;
    for (QDirIterator it(tree, QDir::Files, QDirIterator::Subdirectories); it.hasNext();) {
        const QString f = it.next();
        const QString name = it.fileName();
        if (ffmpeg.isEmpty() && name.compare(ffmpegName(), Qt::CaseInsensitive) == 0)
            ffmpeg = f;
        else if (ffprobe.isEmpty() && name.compare(ffprobeName(), Qt::CaseInsensitive) == 0)
            ffprobe = f;
    }
    if (ffmpeg.isEmpty() || ffprobe.isEmpty())
        return QStringLiteral("the ffmpeg zip lacks %1").arg(ffmpeg.isEmpty() ? ffmpegName() : ffprobeName());
    const QDir dir(toolDir());
    if (!QDir().mkpath(dir.path()))
        return QStringLiteral("cannot create %1").arg(dir.path());
    for (const auto& [from, name] : {std::pair{ffmpeg, ffmpegName()}, std::pair{ffprobe, ffprobeName()}}) {
        const QString to = dir.filePath(name);
        if (QFileInfo::exists(to)) {
            // moved aside, not deleted: Windows renames a running exe but cannot delete it
            const QString aside = to + QStringLiteral(".old-") + QString::number(QDateTime::currentMSecsSinceEpoch());
            if (!QFile::rename(to, aside))
                return QStringLiteral("%1 is in use and cannot be replaced").arg(to);
            QFile::remove(aside);
        }
        if (!QFile::rename(from, to) && !QFile::copy(from, to))
            return QStringLiteral("cannot write %1").arg(to);
        QFile::setPermissions(to, QFile::permissions(to) | QFile::ExeOwner | QFile::ExeUser | QFile::ExeGroup
                                      | QFile::ExeOther | QFile::ReadOwner | QFile::ReadUser);
    }
    for (const QString& old : dir.entryList({QStringLiteral("*.old-*")}, QDir::Files))
        QFile::remove(dir.filePath(old));
#ifdef Q_OS_MACOS
    // fetched by us, not a browser: no quarantine flag expected; drop one anyway
    QProcess::execute(QStringLiteral("xattr"), {QStringLiteral("-dr"), QStringLiteral("com.apple.quarantine"), dir.path()});
#endif
    if (QFile v(dir.filePath(QLatin1StringView(kVersionFile))); v.open(QIODevice::WriteOnly | QIODevice::Truncate))
        v.write(m_version.toUtf8() + '\n');
    return {};
}

void Ed3kFfmpegTool::verify()
{
    const QString ffmpeg = QDir(toolDir()).filePath(ffmpegName());
    auto* p = new QProcess(this);
    m_verify = p;
    p->setProcessChannelMode(QProcess::MergedChannels);
    const auto done = [this, p, ffmpeg](bool started) {
        const QByteArray out = started ? p->readAll() : QByteArray();
        const bool ok = started && p->exitStatus() == QProcess::NormalExit && p->exitCode() == 0
                        && out.startsWith("ffmpeg version");
        p->deleteLater();
        m_verify = nullptr;
        if (!ok) {
            fail(tr("the downloaded ffmpeg does not start (blocked by a virus scanner?)"));
            return;
        }
        logHoster(QStringLiteral("hoster: ffmpeg installed: %1").arg(QString::fromUtf8(out.left(out.indexOf('\n'))).trimmed()));
        m_failedAt = {};
        m_state = State::Unknown;
        locate();
    };
    connect(p, &QProcess::finished, this, [done] { done(true); });
    connect(p, &QProcess::errorOccurred, this, [done](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart)
            done(false);
    });
    p->start(ffmpeg, {QStringLiteral("-hide_banner"), QStringLiteral("-version")});
    QTimer::singleShot(kVerifyTimeoutMs, p, [p] { p->kill(); });
}

void Ed3kFfmpegTool::setState(State s, const QString& error)
{
    const bool same = s == m_state && error == m_error;
    m_state = s;
    m_error = error;
    if (!same)
        emit stateChanged();
}

void Ed3kFfmpegTool::fail(const QString& error)
{
    m_failedAt = QDateTime::currentDateTimeUtc();
    logHosterWarning(QStringLiteral("hoster: ffmpeg: %1").arg(error));
    setState(State::Failed, error);
}

void Ed3kFfmpegTool::abortTransfer()
{
    if (HttpDownloadJob* j = std::exchange(m_job, nullptr))
        j->abort();
    m_dl.reset();   // removes the part file
}

} // namespace eMule::ed3k

#endif // EMULE_HAVE_HOSTER_JD
