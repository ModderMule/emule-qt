#include "Ed3kJavaRuntime.h"

#ifdef EMULE_HAVE_HOSTER_JD

#include "net/HttpDefaults.h"
#include "utils/Log.h"

#include <QCryptographicHash>
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

Ed3kJavaRuntime::Ed3kJavaRuntime(QString baseDir, QObject* parent)
    : QObject(parent)
    , m_baseDir(std::move(baseDir))
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
    return QDir(m_baseDir).filePath(QStringLiteral("runtime/jre"));
}

void Ed3kJavaRuntime::locate(const QString& prefPath)
{
    if (m_state == State::Probing || m_state == State::Downloading || m_state == State::Unpacking)
        return;
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
    logEd3k(QStringLiteral("hoster: no Java %1+ found, downloading Temurin %2").arg(kMinFeature).arg(kDownloadFeature));
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
    logEd3k(QStringLiteral("hoster: Java runtime download stopped (hoster downloads switched off)"));
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
        logEd3kDebug(QStringLiteral("hoster: %1 is no Java runtime").arg(java));
    } else if (info->feature < kMinFeature) {
        logEd3kDebug(QStringLiteral("hoster: %1 is Java %2, %3 or newer is needed").arg(java).arg(info->feature).arg(kMinFeature));
    } else if (info->arch != hostArch()) {
        logEd3kDebug(QStringLiteral("hoster: %1 is for %2, not %3").arg(java, info->arch, hostArch()));
    } else {
        m_info = info;
        logEd3k(QStringLiteral("hoster: Java %1 (%2) at %3").arg(info->feature).arg(info->vendor, java));
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
    QDir().mkpath(QDir(m_baseDir).filePath(QStringLiteral("runtime")));
    m_file = std::make_unique<QFile>(QDir(m_baseDir).filePath(QStringLiteral("runtime/") + m_package.name + QStringLiteral(".part")));
    if (!m_file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        setState(State::Failed, m_file->errorString());
        m_file.reset();
        return;
    }
    m_sha = std::make_unique<QCryptographicHash>(QCryptographicHash::Sha256);
    m_reply = m_nam->get(Http::makeRequest(QUrl(m_package.link)));
    connect(m_reply, &QNetworkReply::readyRead, this, &Ed3kJavaRuntime::onPackageData);
    connect(m_reply, &QNetworkReply::downloadProgress, this, [this](qint64 got, qint64 total) {
        const qint64 all = total > 0 ? total : m_package.size;
        const int pct = all > 0 ? int(std::min<qint64>(100, got * 100 / all)) : 0;
        if (pct != m_progress) {
            m_progress = pct;
            emit stateChanged();
        }
    });
    connect(m_reply, &QNetworkReply::finished, this, &Ed3kJavaRuntime::onPackageDone);
}

void Ed3kJavaRuntime::onPackageData()
{
    if (!m_reply || !m_file)
        return;
    const QByteArray chunk = m_reply->readAll();
    if (m_file->size() + chunk.size() > kMaxPackage || m_file->write(chunk) != chunk.size()) {
        abortTransfer();
        setState(State::Failed, tr("Could not write the Java runtime"));
        return;
    }
    m_sha->addData(chunk);
}

void Ed3kJavaRuntime::onPackageDone()
{
    QNetworkReply* r = m_reply;
    if (!r || !m_file)
        return;
    onPackageData();
    if (!m_file)
        return;
    r->deleteLater();
    m_reply = nullptr;
    const QString part = m_file->fileName();
    m_file->close();
    m_file.reset();
    if (r->error() != QNetworkReply::NoError) {
        QFile::remove(part);
        setState(State::Failed, r->errorString());
        return;
    }
    if (QString::fromLatin1(m_sha->result().toHex()) != m_package.checksum) {
        QFile::remove(part);
        setState(State::Failed, tr("The Java runtime download is damaged (SHA-256 mismatch)"));
        return;
    }
    const QString archive = part.chopped(5);
    QFile::remove(archive);
    QFile::rename(part, archive);
    unpack(archive);
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
    logEd3k(QStringLiteral("hoster: Java runtime installed at %1").arg(runtimeDir()));
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
        logEd3kWarning(QStringLiteral("hoster: Java runtime: %1").arg(error));
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
    if (m_file) {
        const QString part = m_file->fileName();
        m_file.reset();
        QFile::remove(part);
    }
}

} // namespace eMule::ed3k

#endif // EMULE_HAVE_HOSTER_JD
