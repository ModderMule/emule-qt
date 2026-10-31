#include "Ed3kJavaRuntime.h"

#ifdef EMULE_HAVE_HOSTER_JD

#include "net/HttpDefaults.h"
#include "net/HttpStreamDownload.h"
#include "utils/Log.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTimer>

namespace eMule::ed3k {

namespace {

constexpr int kProbeTimeoutMs = 15000;
/// A JRE is about 45 MB; anything far above that is not one
constexpr qint64 kMaxPackage = 400LL * 1024 * 1024;

QString exe(const QString& name)
{
#ifdef Q_OS_WIN
    return name + QStringLiteral(".exe");
#else
    return name;
#endif
}

} // namespace

Ed3kJavaRuntime::Ed3kJavaRuntime(QString baseDir, QString legacyDir, QObject* parent)
    : QObject(parent)
    , m_baseDir(std::move(baseDir))
    , m_legacyDir(std::move(legacyDir))
{
}

Ed3kJavaRuntime::~Ed3kJavaRuntime()
{
    abortTransfer();
    if (m_probe)
        m_probe->kill();
    if (m_tar)
        m_tar->kill();
}

QString Ed3kJavaRuntime::runtimeDir() const
{
    return QDir(m_baseDir).filePath(QStringLiteral("java/jre"));
}

void Ed3kJavaRuntime::locate(const QString& prefPath)
{
    if (m_state == State::Probing || m_state == State::Downloading || m_state == State::Unpacking)
        return;
    adoptLegacy();
    m_info.reset();
    m_queue = candidates(prefPath);
    setState(State::Probing);
    probeNext();
}

QStringList Ed3kJavaRuntime::candidates(const QString& prefPath) const
{
    QStringList out;
    const auto add = [&out](const QString& path) {
        if (path.isEmpty())
            return;
        const QString clean = QDir::cleanPath(path);
        if (QFileInfo(clean).isFile() && !out.contains(clean))
            out << clean;
    };
    add(prefPath);
    add(javaIn(runtimeDir()));
    const QString javaHome = QProcessEnvironment::systemEnvironment().value(QStringLiteral("JAVA_HOME"));
    if (!javaHome.isEmpty())
        add(QDir(javaHome).filePath(QStringLiteral("bin/") + exe(QStringLiteral("java"))));
#ifdef Q_OS_MACOS
    // java_home answers with the best installed JDK of a version range
    {
        QProcess p;
        p.start(QStringLiteral("/usr/libexec/java_home"), {QStringLiteral("-v"), QStringLiteral("%1+").arg(kMinFeature)});
        if (p.waitForFinished(3000) && p.exitCode() == 0)
            add(QDir(QString::fromLocal8Bit(p.readAllStandardOutput()).trimmed()).filePath(QStringLiteral("bin/java")));
    }
#endif
    add(QStandardPaths::findExecutable(QStringLiteral("java")));
    QStringList roots;
#if defined(Q_OS_MACOS)
    roots << QStringLiteral("/Library/Java/JavaVirtualMachines")
          << QDir::home().filePath(QStringLiteral("Library/Java/JavaVirtualMachines"))
          << QStringLiteral("/opt/homebrew/opt") << QStringLiteral("/usr/local/opt");
#elif defined(Q_OS_WIN)
    for (const char* var : {"ProgramFiles", "ProgramW6432"}) {
        const QString pf = qEnvironmentVariable(var);
        if (pf.isEmpty())
            continue;
        for (const char* vendor : {"Java", "Eclipse Adoptium", "Microsoft", "Zulu", "Amazon Corretto", "BellSoft"})
            roots << QDir(pf).filePath(QString::fromLatin1(vendor));
    }
#else
    roots << QStringLiteral("/usr/lib/jvm") << QStringLiteral("/usr/java") << QStringLiteral("/opt/java");
#endif
    for (const QString& root : roots) {
        const QDir dir(root);
        // newest name first: jdk-26 before jdk-17
        QStringList names = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::Reversed);
        for (const QString& n : names)
            add(javaIn(dir.filePath(n)));
    }
    return out;
}

std::optional<JavaInfo> Ed3kJavaRuntime::parseProperties(const QByteArray& out, const QString& java)
{
    static const QRegularExpression line(QStringLiteral(R"(^\s*([\w.]+)\s*=\s*(.*?)\s*$)"),
                                         QRegularExpression::MultilineOption);
    JavaInfo info;
    info.java = java;
    QString spec;
    for (auto it = line.globalMatch(QString::fromLocal8Bit(out)); it.hasNext();) {
        const auto m = it.next();
        const QString key = m.captured(1);
        if (key == QLatin1StringView("java.specification.version"))
            spec = m.captured(2);
        else if (key == QLatin1StringView("os.arch"))
            info.arch = normaliseArch(m.captured(2));
        else if (key == QLatin1StringView("java.vendor"))
            info.vendor = m.captured(2);
    }
    // "1.8" before Java 9, "17" after
    const QStringList parts = spec.split(QLatin1Char('.'));
    bool ok = false;
    info.feature = parts.value(parts.value(0) == QLatin1StringView("1") ? 1 : 0).toInt(&ok);
    if (!ok || info.arch.isEmpty())
        return std::nullopt;
    return info;
}

QString Ed3kJavaRuntime::normaliseArch(const QString& arch)
{
    const QString a = arch.trimmed().toLower();
    if (a == QLatin1StringView("aarch64") || a == QLatin1StringView("arm64"))
        return QStringLiteral("arm64");
    if (a == QLatin1StringView("amd64") || a == QLatin1StringView("x86_64") || a == QLatin1StringView("x64"))
        return QStringLiteral("x86_64");
    return a;
}

QString Ed3kJavaRuntime::hostArch()
{
    return normaliseArch(QSysInfo::currentCpuArchitecture());
}

QString Ed3kJavaRuntime::adoptiumUrl()
{
#if defined(Q_OS_MACOS)
    const QString os = QStringLiteral("mac");
#elif defined(Q_OS_WIN)
    const QString os = QStringLiteral("windows");
#else
    const QString os = QStringLiteral("linux");
#endif
    const QString arch = hostArch() == QLatin1StringView("arm64") ? QStringLiteral("aarch64") : QStringLiteral("x64");
    return QStringLiteral("https://api.adoptium.net/v3/assets/latest/%1/hotspot?architecture=%2&image_type=jre&os=%3&vendor=eclipse")
        .arg(kDownloadFeature)
        .arg(arch, os);
}

std::optional<Ed3kJavaRuntime::Package> Ed3kJavaRuntime::parseAdoptium(const QByteArray& json)
{
    const QJsonArray list = QJsonDocument::fromJson(json).array();
    for (const QJsonValue& v : list) {
        const QJsonObject pkg = v.toObject().value(QStringLiteral("binary")).toObject().value(QStringLiteral("package")).toObject();
        Package p{pkg.value(QStringLiteral("link")).toString(), pkg.value(QStringLiteral("checksum")).toString().toLower(),
                  pkg.value(QStringLiteral("name")).toString(), pkg.value(QStringLiteral("size")).toInteger()};
        static const QRegularExpression hex64(QStringLiteral("^[0-9a-f]{64}$"));
        if (p.link.startsWith(QLatin1StringView("https://")) && hex64.match(p.checksum).hasMatch() && !p.name.isEmpty()
            && !p.name.contains(QLatin1Char('/')) && !p.name.contains(QLatin1Char('\\')))
            return p;
    }
    return std::nullopt;
}

bool Ed3kJavaRuntime::download()
{
    if (!m_allowed)
        return false;
    if (m_state == State::Downloading || m_state == State::Unpacking || m_state == State::Probing)
        return m_state != State::Probing;
    m_progress = 0;
    if (!m_nam)
        m_nam = std::make_unique<QNetworkAccessManager>();
    m_nam->setProxy(m_proxy);
    setState(State::Downloading);
    logHoster(QStringLiteral("hoster: no Java %1+ found, downloading Temurin %2").arg(kMinFeature).arg(kDownloadFeature));
    m_reply = m_nam->get(Http::makeRequest(QUrl(adoptiumUrl())));
    connect(m_reply, &QNetworkReply::finished, this, &Ed3kJavaRuntime::onMetadata);
    return true;
}

void Ed3kJavaRuntime::setAllowed(bool allowed)
{
    if (m_allowed == allowed)
        return;
    m_allowed = allowed;
    if (allowed || (m_state != State::Downloading && m_state != State::Unpacking))
        return;
    // switched off while fetching: stop and leave nothing behind
    abortTransfer();
    if (m_tar) {
        QProcess* p = m_tar;
        const QString archive = p->property("archive").toString();
        m_tar = nullptr;
        p->disconnect(this);
        p->kill();
        p->deleteLater();
        QFile::remove(archive);
    }
    QDir(runtimeDir() + QStringLiteral(".tmp")).removeRecursively();
    logHoster(QStringLiteral("hoster: Java runtime download stopped (hoster downloads switched off)"));
    setState(State::Missing);
}

void Ed3kJavaRuntime::probeNext()
{
    if (m_queue.isEmpty()) {
        setState(State::Missing);
        return;
    }
    const QString java = m_queue.takeFirst();
    auto* p = new QProcess(this);
    m_probe = p;
    p->setProcessChannelMode(QProcess::MergedChannels);
    connect(p, &QProcess::finished, this, [this, p, java] {
        const QByteArray out = p->readAll();
        p->deleteLater();
        probeFinished(java, out);
    });
    connect(p, &QProcess::errorOccurred, this, [this, p, java](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart)
            return;
        p->deleteLater();
        probeFinished(java, {});
    });
    p->start(java, {QStringLiteral("-XshowSettings:properties"), QStringLiteral("-version")});
    QTimer::singleShot(kProbeTimeoutMs, p, [p] { p->kill(); });
}

void Ed3kJavaRuntime::probeFinished(const QString& java, const QByteArray& out)
{
    const auto info = parseProperties(out, java);
    if (!info) {
        logHosterDebug(QStringLiteral("hoster: %1 is no Java runtime").arg(java));
    } else if (info->feature < kMinFeature) {
        logHosterDebug(QStringLiteral("hoster: %1 is Java %2, %3 or newer is needed").arg(java).arg(info->feature).arg(kMinFeature));
    } else if (info->arch != hostArch()) {
        logHosterDebug(QStringLiteral("hoster: %1 is for %2, not %3").arg(java, info->arch, hostArch()));
    } else {
        m_info = info;
        logHoster(QStringLiteral("hoster: Java %1 (%2) at %3").arg(info->feature).arg(info->vendor, java));
        setState(State::Ready);
        return;
    }
    probeNext();
}

void Ed3kJavaRuntime::onMetadata()
{
    QNetworkReply* r = m_reply;
    if (!r)
        return;
    r->deleteLater();
    m_reply = nullptr;
    if (r->error() != QNetworkReply::NoError) {
        setState(State::Failed, r->errorString());
        return;
    }
    const auto pkg = parseAdoptium(r->read(1024 * 1024));
    if (!pkg || pkg->size > kMaxPackage) {
        setState(State::Failed, tr("No Java runtime is offered for this system"));
        return;
    }
    m_package = *pkg;
    QDir().mkpath(QDir(m_baseDir).filePath(QStringLiteral("java")));
    m_dl = std::make_unique<HttpStreamDownload>(m_nam.get());
    connect(m_dl.get(), &HttpStreamDownload::progress, this, [this](qint64 got, qint64 total) {
        const qint64 all = total > 0 ? total : m_package.size;
        const int pct = all > 0 ? int(std::min<qint64>(100, got * 100 / all)) : 0;
        if (pct != m_progress) {
            m_progress = pct;
            emit stateChanged();
        }
    });
    connect(m_dl.get(), &HttpStreamDownload::finished, this, &Ed3kJavaRuntime::onPackageDone);
    const QString archive = QDir(m_baseDir).filePath(QStringLiteral("java/") + m_package.name);
    if (!m_dl->start(QUrl(m_package.link), archive, {kMaxPackage, m_package.checksum, 0})) {
        const QString err = m_dl->errorString();
        m_dl.reset();
        setState(State::Failed, err);
    }
}

void Ed3kJavaRuntime::onPackageDone(bool ok)
{
    if (!m_dl)
        return;
    const HttpStreamDownload::Error err = m_dl->error();
    const QString errText = m_dl->errorString();
    m_dl.release()->deleteLater();   // we are inside its signal
    if (!ok) {
        switch (err) {
        case HttpStreamDownload::Error::HashMismatch:
            setState(State::Failed, tr("The Java runtime download is damaged (SHA-256 mismatch)"));
            break;
        case HttpStreamDownload::Error::Network:
            setState(State::Failed, errText);
            break;
        default:
            setState(State::Failed, tr("Could not write the Java runtime"));
            break;
        }
        return;
    }
    unpack(QDir(m_baseDir).filePath(QStringLiteral("java/") + m_package.name));
}

void Ed3kJavaRuntime::adoptLegacy()
{
    if (m_legacyDir.isEmpty() || !QFileInfo(m_legacyDir).isDir() || QFileInfo::exists(runtimeDir()))
        return;
    QDir().mkpath(QFileInfo(runtimeDir()).absolutePath());
    if (!QDir().rename(m_legacyDir, runtimeDir())) {
        logHosterWarning(QStringLiteral("hoster: cannot move the Java runtime %1 to %2").arg(m_legacyDir, runtimeDir()));
        return;
    }
    logHoster(QStringLiteral("hoster: Java runtime moved to %1").arg(runtimeDir()));
    // <eMuleQt>/runtime: gone once nothing else is left in it
    const QString parent = QFileInfo(m_legacyDir).absolutePath();
    QDir().rmdir(parent);
}

void Ed3kJavaRuntime::unpack(const QString& archive)
{
    setState(State::Unpacking);
    const QString tmp = runtimeDir() + QStringLiteral(".tmp");
    QDir(tmp).removeRecursively();
    QDir().mkpath(tmp);
    auto* p = new QProcess(this);
    m_tar = p;
    p->setProcessChannelMode(QProcess::MergedChannels);
    connect(p, &QProcess::finished, this, [this, p](int code) {
        p->deleteLater();
        unpackDone(code);
    });
    connect(p, &QProcess::errorOccurred, this, [this, p](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart)
            return;
        p->deleteLater();
        unpackDone(-1);
    });
    p->setProperty("archive", archive);
    // bsdtar (macOS, Windows 10+) reads zip and tar.gz; GNU tar tar.gz
    p->start(QStringLiteral("tar"), {QStringLiteral("-xf"), archive, QStringLiteral("-C"), tmp});
}

void Ed3kJavaRuntime::unpackDone(int exitCode)
{
    const QString archive = m_tar ? m_tar->property("archive").toString() : QString();
    const QString tmp = runtimeDir() + QStringLiteral(".tmp");
    QFile::remove(archive);
    if (exitCode != 0) {
        QDir(tmp).removeRecursively();
        setState(State::Failed, tr("Could not unpack the Java runtime"));
        return;
    }
    // the archive holds one folder, e.g. jdk-21.0.5+11-jre
    const QStringList top = QDir(tmp).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    const QString home = top.size() == 1 ? QDir(tmp).filePath(top.first()) : tmp;
    if (javaIn(home).isEmpty()) {
        QDir(tmp).removeRecursively();
        setState(State::Failed, tr("The Java runtime archive has no java program"));
        return;
    }
    QDir(runtimeDir()).removeRecursively();
    if (!QDir().rename(home, runtimeDir())) {
        QDir(tmp).removeRecursively();
        setState(State::Failed, tr("Could not install the Java runtime"));
        return;
    }
    QDir(tmp).removeRecursively();
#ifdef Q_OS_MACOS
    // downloaded by us, not a browser: no quarantine flag is expected; drop one anyway
    QProcess::execute(QStringLiteral("xattr"), {QStringLiteral("-dr"), QStringLiteral("com.apple.quarantine"), runtimeDir()});
#endif
    logHoster(QStringLiteral("hoster: Java runtime installed at %1").arg(runtimeDir()));
    setState(State::Unknown);
    locate({});
}

QString Ed3kJavaRuntime::javaIn(const QString& home)
{
    for (const QString& sub : {QStringLiteral("Contents/Home/bin/"), QStringLiteral("bin/")}) {
        const QString j = QDir(home).filePath(sub + exe(QStringLiteral("java")));
        if (QFileInfo(j).isFile())
            return j;
    }
    return {};
}

void Ed3kJavaRuntime::setState(State s, const QString& error)
{
    m_state = s;
    m_error = error;
    if (s == State::Failed)
        logHosterWarning(QStringLiteral("hoster: Java runtime: %1").arg(error));
    emit stateChanged();
}

void Ed3kJavaRuntime::abortTransfer()
{
    if (m_reply) {
        QNetworkReply* r = m_reply;
        m_reply = nullptr;
        r->disconnect(this);
        r->abort();
        r->deleteLater();
    }
    m_dl.reset();   // removes the part file
}

} // namespace eMule::ed3k

#endif // EMULE_HAVE_HOSTER_JD
