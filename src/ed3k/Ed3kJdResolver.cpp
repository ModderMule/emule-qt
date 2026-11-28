#include "Ed3kJdResolver.h"
#include "utils/ArgStr.h"

#ifdef EMULE_HAVE_HOSTER_JD

#include "archive/ArchiveReader.h"
#include "utils/Log.h"
#include "utils/PathUtils.h"
#include "utils/SafeRemove.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QRegularExpression>
#include <QRandomGenerator>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QThread>
#include <QUrl>

namespace eMule::ed3k {

namespace {

constexpr qsizetype kMaxLine = 16 * 1024 * 1024;   // a puzzle image is the largest event

QString instanceRoot(const QString& dataDir)
{
    return QDir(dataDir).filePath(QStringLiteral("plugins/resolver"));
}

/// JD's core packed, the plugins loose beside it (scripts/build-hoster-resolver.sh)
bool layoutOk(const QString& root)
{
    const QDir d(root);
    return QFileInfo(d.filePath(QStringLiteral("Core.jar"))).isFile()
           && QFileInfo(d.filePath(QStringLiteral("jd/plugins/hoster"))).isDir();
}

constexpr auto kLastUsed = ".lastused";

/// a version's folder name in the shared dir: the zip's content, not its path or mtime
QString zipKey(const QString& zip)
{
    QFile f(zip);
    QCryptographicHash h(QCryptographicHash::Sha1);
    if (!f.open(QIODevice::ReadOnly) || !h.addData(&f))
        return {};
    return QString::fromLatin1(h.result().toHex().left(12));
}

/// the JD revision from revision.txt that build-hoster-resolver.sh writes; empty: none
QString readRevision(const QString& root)
{
    if (root.isEmpty())
        return {};
    QFile f(QDir(root).filePath(QStringLiteral("revision.txt")));
    if (!f.open(QIODevice::ReadOnly))
        return {};
    const QString rev = QString::fromUtf8(f.readLine(64)).trimmed();
    return rev == u'?' ? QString() : rev;
}

void touchUsed(const QString& versionDir)
{
    QFile f(QDir(versionDir).filePath(QLatin1StringView(kLastUsed)));
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QDateTime::currentDateTimeUtc().toString(Qt::ISODate).toLatin1());
}

int readNumber(const QString& root, const QString& name)
{
    QFile f(QDir(root).filePath(name));
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readLine(64)).trimmed().toInt() : 0;
}

/// under the install lock: left-over tmp dirs, and versions nobody started for a while
void pruneShared(const QString& sharedDir, const QString& keep, int days)
{
    static const QRegularExpression version(QStringLiteral("^[0-9a-f]{12}(\\.tmp)?$"));
    const QDir dir(sharedDir);
    const QDateTime cutoff = QDateTime::currentDateTimeUtc().addDays(-days);
    for (const QString& v : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (v == keep || !version.match(v).hasMatch())
            continue;
        const QFileInfo used(dir.filePath(v + u'/' + QLatin1StringView(kLastUsed)));
        if ((v.endsWith(QStringLiteral(".tmp")) || !used.exists() || used.lastModified() < cutoff)
            && removeDirTree(dir.filePath(v)))
            logHoster(QStringLiteral("hoster: shared resolver plugins %1 removed (unused)").arg(v));
    }
}

/// path too: a fetched zip of the same size and time is still another one
QString zipStamp(const QString& zip)
{
    const QFileInfo fi(zip);
    return QStringLiteral("%1 %2 %3").arg(fi.size()).arg(fi.lastModified().toMSecsSinceEpoch()).arg(fi.absoluteFilePath());
}

QJsonObject accountJson(const std::optional<ResolverAccount>& a)
{
    if (!a || a->user.isEmpty())
        return {};
    QJsonObject o{{QStringLiteral("user"), a->user}, {QStringLiteral("password"), a->password}};
    if (!a->via.isEmpty())
        o.insert(QStringLiteral("host"), a->via);
    if (const QJsonDocument d = QJsonDocument::fromJson(a->state); d.isObject())
        o.insert(QStringLiteral("state"), d.object());
    return o;
}

/// "via" for a multi-hoster login, else "account"
QString accountKey(const std::optional<ResolverAccount>& a)
{
    return a && !a->via.isEmpty() ? QStringLiteral("via") : QStringLiteral("account");
}

} // namespace

Ed3kJdResolver::Ed3kJdResolver(QObject* parent)
    : Ed3kResolver(parent)
{
    m_idle.setSingleShot(true);
    m_idle.setInterval(kIdleStopMs);
    connect(&m_idle, &QTimer::timeout, this, &Ed3kJdResolver::onIdle);
    m_connectTimer.setSingleShot(true);
    m_connectTimer.setInterval(kConnectTimeoutMs);
    connect(&m_connectTimer, &QTimer::timeout, this, &Ed3kJdResolver::onConnectTimeout);
    connect(&m_sidecar, &SidecarProcess::lineRead, this, &Ed3kJdResolver::onLog);
    connect(&m_sidecar, &SidecarProcess::finished, this, &Ed3kJdResolver::onFinished);
    connect(&m_sidecar, &SidecarProcess::failedToStart, this, [this] {
        logHosterWarning(QStringLiteral("hoster: resolver did not start: %1").arg(m_config.java));
        onFinished(-1, false, false);
    });
}

Ed3kJdResolver::~Ed3kJdResolver()
{
    // an unpack in flight finishes: a half-moved plugins folder would break the next start
    if (m_installThread) {
        m_installThread->disconnect(this);
        m_installThread->wait();
    }
    m_stopping = true;
    closeChannel();
    // m_sidecar waits for java, then kills it
    m_sidecar.disconnect(this);
}

void Ed3kJdResolver::setConfig(const JdResolverConfig& config)
{
    JdResolverConfig next = config;
    const bool sameBase = next.enabled == m_config.enabled && next.java == m_config.java
                          && next.dataDir == m_config.dataDir && next.sharedDir == m_config.sharedDir;
    // a new zip alone waits while the sidecar works (a download, a check): swapped in when it ends
    const bool swap = sameBase && next.zip != m_config.zip && !m_root.isEmpty();
    if (swap && running()) {
        if (m_pendingZip != next.zip) {
            m_pendingZip = next.zip;
            const int rev = readZipInfo(next.zip).revision;
            m_pendingVersion = rev > 0 ? QStringLiteral("r%1").arg(rev) : QString();
            logHoster(QStringLiteral("hoster: new resolver plugins %1 wait until the resolver is idle")
                          .arg(m_pendingVersion.isEmpty() ? next.zip : m_pendingVersion));
            emit stateChanged();
        }
        next.zip = m_config.zip;
    } else if (!m_pendingZip.isEmpty()) {
        m_pendingZip.clear();
        m_pendingVersion.clear();
    }
    const bool restart = next.enabled != m_config.enabled || next.java != m_config.java || next.zip != m_config.zip
                         || next.dataDir != m_config.dataDir || next.sharedDir != m_config.sharedDir
                         || next.loopbackHosters != m_config.loopbackHosters;
    const bool proxy = next.proxy != m_config.proxy;
    const bool bind = next.bindAddress != m_config.bindAddress;
    const bool puzzles = next.askPuzzles != m_config.askPuzzles;
    const bool generic = next.genericHttp != m_config.genericHttp;
    const bool solvers = next.solvers != m_config.solvers;
    const bool tools = next.ffmpeg != m_config.ffmpeg || next.ffprobe != m_config.ffprobe;
    m_config = next;
    if (solvers)
        m_solverStates.clear();
    if (restart) {
        stop();
        if (installing())
            m_reinstall = true;   // after the running unpack, with this config
        else if (swap || m_fetching)   // the first fetched zip: links wait, unpack off the GUI thread
            installAsync();
        else
            installPlugins();
        emit stateChanged();
        return;
    }
    if (proxy)
        setProxy(m_config.proxy);
    if (bind)
        setBindAddress(m_config.bindAddress);
    if (puzzles)
        setAskPuzzles(m_config.askPuzzles);
    if (generic)
        setGenericHttp(m_config.genericHttp);
    // not running: sent with the next start
    if (solvers && running())
        sendSolvers();
    if (tools && running())
        sendTools();
}

void Ed3kJdResolver::toolsFailed(const QString& error)
{
    if (running())
        sendTools(error.isEmpty() ? QStringLiteral("not available") : error);
}

QString Ed3kJdResolver::findBundledZip()
{
    // a build tree: build/hoster-resolver/resolver.zip above the binary
    return findBundledFile("EMULE_RESOLVER_ZIP",
                           {QStringLiteral("resolver.zip"), QStringLiteral("../Resources/resolver.zip"),
                            QStringLiteral("../share/emuleqt/resolver.zip"), QStringLiteral("resolver/resolver.zip")},
                           QStringLiteral("hoster-resolver/resolver.zip"));
}

Ed3kJdResolver::ZipInfo Ed3kJdResolver::readZipInfo(const QString& zip)
{
    ZipInfo info;
    ArchiveReader reader;
    if (zip.isEmpty() || !reader.open(zip))
        return info;
    QTemporaryDir tmp;
    const auto number = [&](int index) {
        const QString to = tmp.filePath(QStringLiteral("v.txt"));
        QFile::remove(to);
        if (!tmp.isValid() || !reader.extractEntry(index, to))
            return 0;
        QFile f(to);
        return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readLine(64)).trimmed().toInt() : 0;
    };
    bool core = false;
    bool hoster = false;
    for (int i = 0; i < reader.entryCount(); ++i) {
        const QString name = reader.entryName(i);
        if (name == QLatin1StringView("resolver/revision.txt"))
            info.revision = number(i);
        else if (name == QLatin1StringView("resolver/api.txt"))
            info.api = number(i);
        else if (name == QLatin1StringView("resolver/Core.jar"))
            core = true;
        else if (name.startsWith(QLatin1StringView("resolver/jd/plugins/hoster/")))
            hoster = true;
    }
    info.layout = core && hoster;
    return info;
}

bool Ed3kJdResolver::install(const QString& zip, const QString& dataDir, QString* error)
{
    const QString plugins = QDir(dataDir).filePath(QStringLiteral("plugins"));
    const QString stampFile = QDir(plugins).filePath(QStringLiteral(".zipstamp"));
    QFile stamp(stampFile);
    if (stamp.open(QIODevice::ReadOnly) && QString::fromUtf8(stamp.readAll()) == zipStamp(zip)
        && layoutOk(instanceRoot(dataDir)))
        return true;
    stamp.close();
    const QString tmp = plugins + QStringLiteral(".tmp");
    removeDirTree(tmp);
    QDir().mkpath(tmp);
    ArchiveReader reader;
    if (!reader.open(zip) || !reader.extractAll(tmp)) {
        if (error)
            *error = reader.lastError();
        removeDirTree(tmp);
        return false;
    }
    if (!layoutOk(QDir(tmp).filePath(QStringLiteral("resolver")))) {
        if (error)
            *error = QStringLiteral("not a resolver archive");
        removeDirTree(tmp);
        return false;
    }
    removeDirTree(plugins);
    if (!QDir().rename(tmp, plugins)) {
        if (error)
            *error = QStringLiteral("cannot move %1").arg(tmp);
        return false;
    }
    QFile out(stampFile);
    if (out.open(QIODevice::WriteOnly | QIODevice::Truncate))
        out.write(zipStamp(zip).toUtf8());
    logHoster(QStringLiteral("hoster: resolver plugins installed from %1").arg(zip));
    return true;
}

QString Ed3kJdResolver::installShared(const QString& zip, const QString& sharedDir, QString* error)
{
    const auto fail = [error](const QString& why) {
        if (error)
            *error = why;
        return QString();
    };
    const QString key = zipKey(zip);
    if (key.isEmpty())
        return fail(QStringLiteral("cannot read %1").arg(zip));
    const QDir dir(sharedDir);
    const QString version = dir.filePath(key);
    const QString root = version + QStringLiteral("/resolver");
    if (layoutOk(root)) {
        touchUsed(version);
        return root;
    }
    if (!QDir().mkpath(sharedDir))
        return fail(QStringLiteral("cannot create %1").arg(sharedDir));
    // several instances start at once: one unpacks, the others wait and take it
    QLockFile lock(dir.filePath(QStringLiteral(".install.lock")));
    lock.setStaleLockTime(kSharedLockWaitMs);
    if (!lock.tryLock(kSharedLockWaitMs))
        return fail(lock.error() == QLockFile::LockFailedError ? QStringLiteral("%1 is locked by another instance").arg(sharedDir)
                                                               : QStringLiteral("%1 is not writable").arg(sharedDir));
    if (layoutOk(root)) {
        touchUsed(version);
        return root;
    }
    const QString tmp = version + QStringLiteral(".tmp");
    removeDirTree(tmp);
    QDir().mkpath(tmp);
    ArchiveReader reader;
    if (!reader.open(zip) || !reader.extractAll(tmp)) {
        removeDirTree(tmp);
        return fail(reader.lastError());
    }
    if (!layoutOk(QDir(tmp).filePath(QStringLiteral("resolver")))) {
        removeDirTree(tmp);
        return fail(QStringLiteral("not a resolver archive"));
    }
    removeDirTree(version);
    if (!QDir().rename(tmp, version))
        return fail(QStringLiteral("cannot move %1").arg(tmp));
    touchUsed(version);
    pruneShared(sharedDir, key, kSharedKeepDays);
    logHoster(QStringLiteral("hoster: shared resolver plugins installed into %1").arg(version));
    return root;
}

void Ed3kJdResolver::removePlugins(const QString& dataDir)
{
    if (dataDir.isEmpty())
        return;
    const QString plugins = QDir(dataDir).filePath(QStringLiteral("plugins"));
    removeDirTree(plugins + QStringLiteral(".tmp"));
    if (QFileInfo(plugins).isDir() && removeDirTree(plugins))
        logHoster(QStringLiteral("hoster: resolver plugins removed (hoster downloads off)"));
}

QString Ed3kJdResolver::sharedVersion(const QString& sharedDir)
{
    QString best;
    int bestRev = -1;
    QDateTime bestAt;
    const QDir dir(sharedDir);
    for (const QString& v : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString root = dir.filePath(v + QStringLiteral("/resolver"));
        if (v.endsWith(QStringLiteral(".tmp")) || !layoutOk(root))
            continue;
        // mixed app versions share the folder: never a version of another api
        if (const int api = readNumber(root, QStringLiteral("api.txt")); api != 0 && api != kProtocol)
            continue;
        const int rev = readNumber(root, QStringLiteral("revision.txt"));
        const QDateTime at = QFileInfo(dir.filePath(v + u'/' + QLatin1StringView(kLastUsed))).lastModified();
        if (rev > bestRev || (rev == bestRev && at > bestAt)) {
            best = root;
            bestRev = rev;
            bestAt = at;
        }
    }
    return best;
}

bool Ed3kJdResolver::dirWritable(const QString& dir)
{
    if (dir.isEmpty() || !QDir().mkpath(dir))
        return false;
    QTemporaryFile probe(QDir(dir).filePath(QStringLiteral(".probe-XXXXXX")));
    return probe.open();
}

QStringList Ed3kJdResolver::arguments() const
{
    const QDir root(m_root);
    const QString rev = m_revision.isEmpty() ? QStringLiteral("?") : m_revision;
    // the root itself for the loose jd/plugins classes
    const QString cp = root.filePath(QStringLiteral("Core.jar")) + QDir::listSeparator() + root.path() + QDir::listSeparator()
                       + root.filePath(QStringLiteral("libs/*"));
    QStringList args{QStringLiteral("-Xmx768m"),
                     QStringLiteral("-Djava.awt.headless=true"),
                     QStringLiteral("-Dfile.encoding=UTF-8"),
                     QStringLiteral("-DROOT=") + QDir(m_config.dataDir).filePath(QStringLiteral("jd")),
                     QStringLiteral("-Demuleqt.jdRevision=") + rev};
    if (m_config.loopbackHosters)
        args << QStringLiteral("-Demuleqt.loopbackHosters=true");
    // the daemon answers needTools (Ed3kFfmpegTool); paths known now go along
    args << QStringLiteral("-Demuleqt.tools=true");
    if (!m_config.ffmpeg.isEmpty() && !m_config.ffprobe.isEmpty())
        args << QStringLiteral("-Demuleqt.ffmpeg=") + QDir::toNativeSeparators(m_config.ffmpeg)
             << QStringLiteral("-Demuleqt.ffprobe=") + QDir::toNativeSeparators(m_config.ffprobe);
    if (m_server && m_server->isListening())
        args << QStringLiteral("-Demuleqt.port=") + QString::number(m_server->serverPort());
    args << QStringLiteral("-cp") << cp << QStringLiteral("org.emuleqt.resolver.ResolverMain");
    return args;
}

bool Ed3kJdResolver::available() const
{
    return unavailableReason().isEmpty();
}

QString Ed3kJdResolver::unavailableReason() const
{
    if (!m_config.enabled)
        return tr("Hoster downloads are off");
    if (installing())
        return tr("Updating resolver plugins");
    if (m_fetching)
        return tr("Downloading resolver plugins");
    if (m_config.java.isEmpty())
        return tr("No Java runtime");
    if (!m_installError.isEmpty())
        return tr("Resolver plugins not installed: %1").arg(m_installError);
    if (m_config.dataDir.isEmpty() || m_root.isEmpty() || !layoutOk(m_root))
        return tr("Resolver plugins missing");
    return {};
}

void Ed3kJdResolver::wantPlugins()
{
    if (m_config.enabled && m_config.zip.isEmpty() && (m_root.isEmpty() || !layoutOk(m_root)) && !m_fetching
        && !installing())
        emit pluginsNeeded();
}

void Ed3kJdResolver::setFetching(bool on)
{
    if (std::exchange(m_fetching, on) != on)
        emit stateChanged();
}

bool Ed3kJdResolver::running() const
{
    return m_sidecar.running();
}

Ed3kResolver::TaskId Ed3kJdResolver::match(const QString& url)
{
    return request({{QStringLiteral("op"), QStringLiteral("match")}, {QStringLiteral("url"), url}}, true);
}

Ed3kResolver::TaskId Ed3kJdResolver::check(const QString& url, const std::optional<ResolverAccount>& account)
{
    QJsonObject r{{QStringLiteral("op"), QStringLiteral("check")}, {QStringLiteral("url"), url}};
    if (const QJsonObject a = accountJson(account); !a.isEmpty())
        r.insert(QStringLiteral("account"), a);
    return request(r, true);
}

Ed3kResolver::TaskId Ed3kJdResolver::expand(const QString& url)
{
    return request({{QStringLiteral("op"), QStringLiteral("expand")}, {QStringLiteral("url"), url}}, true);
}

Ed3kResolver::TaskId Ed3kJdResolver::expandDeep(const QString& url)
{
    return request({{QStringLiteral("op"), QStringLiteral("expand")}, {QStringLiteral("url"), url}, {QStringLiteral("deep"), true}},
                   true);
}

Ed3kResolver::TaskId Ed3kJdResolver::openContainer(const QString& path)
{
    return request({{QStringLiteral("op"), QStringLiteral("container")}, {QStringLiteral("path"), path}}, true);
}

Ed3kResolver::TaskId Ed3kJdResolver::open(const QString& url, const std::optional<ResolverAccount>& account,
                                          const QString& dir, std::uint64_t have)
{
    QJsonObject r{{QStringLiteral("op"), QStringLiteral("open")},
                  {QStringLiteral("url"), url},
                  {QStringLiteral("path"), dir},
                  {QStringLiteral("have"), static_cast<qint64>(have)}};
    if (const QJsonObject a = accountJson(account); !a.isEmpty())
        r.insert(accountKey(account), a);
    return request(r, true);
}

Ed3kResolver::TaskId Ed3kJdResolver::accountCheck(const QString& host, const ResolverAccount& account)
{
    QJsonObject req{{QStringLiteral("op"), QStringLiteral("accountCheck")},
                    {QStringLiteral("host"), host},
                    {QStringLiteral("account"), accountJson(account)}};
    if (account.askLogin)
        req.insert(QStringLiteral("askLogin"), true);
    return request(req, true);
}

Ed3kResolver::TaskId Ed3kJdResolver::accountHosts()
{
    return request({{QStringLiteral("op"), QStringLiteral("accountHosts")}}, true);
}

Ed3kResolver::TaskId Ed3kJdResolver::favicon(const QString& host)
{
    return request({{QStringLiteral("op"), QStringLiteral("favicon")}, {QStringLiteral("host"), host}}, true);
}

Ed3kResolver::TaskId Ed3kJdResolver::sites(const QString& host)
{
    return request({{QStringLiteral("op"), QStringLiteral("sites")}, {QStringLiteral("host"), host}}, true);
}

Ed3kResolver::TaskId Ed3kJdResolver::setSolverAccounts(const QList<SolverAccount>& accounts)
{
    QJsonArray list;
    for (const SolverAccount& a : accounts)
        list.append(QJsonObject{{QStringLiteral("host"), a.host},
                                {QStringLiteral("user"), a.user},
                                {QStringLiteral("password"), a.password}});
    return request({{QStringLiteral("op"), QStringLiteral("solverAccounts")}, {QStringLiteral("accounts"), list}}, true);
}

Ed3kResolver::TaskId Ed3kJdResolver::solveTest(const QString& solver, const QString& type, const QByteArray& image)
{
    QJsonObject r{{QStringLiteral("op"), QStringLiteral("solveTest")},
                  {QStringLiteral("solver"), solver},
                  {QStringLiteral("type"), type}};
    if (!image.isEmpty())
        r.insert(QStringLiteral("image"), QString::fromLatin1(image.toBase64()));
    return request(r, true);
}

void Ed3kJdResolver::answerPuzzle(std::uint64_t puzzle, const QString& text)
{
    if (running())
        send({{QStringLiteral("op"), QStringLiteral("answer")},
              {QStringLiteral("puzzle"), static_cast<qint64>(puzzle)},
              {QStringLiteral("text"), text}});
}

void Ed3kJdResolver::answerCode(TaskId task, const QString& code)
{
    if (running())
        send({{QStringLiteral("op"), QStringLiteral("answerCode")},
              {QStringLiteral("task"), static_cast<qint64>(task)},
              {QStringLiteral("text"), code}});
}

void Ed3kJdResolver::cancel(TaskId task)
{
    if (std::erase_if(m_held, [task](const auto& h) { return h.first == task; }) > 0) {
        ResolverEvent e;
        e.kind = ResolverEvent::Kind::Failed;
        e.code = QStringLiteral("cancelled");
        QMetaObject::invokeMethod(this, [this, task, e] { emit taskEvent(task, e); }, Qt::QueuedConnection);
        return;
    }
    if (!m_pending.contains(task))
        return;
    if (running())
        send({{QStringLiteral("op"), QStringLiteral("cancel")}, {QStringLiteral("task"), static_cast<qint64>(task)}});
}

void Ed3kJdResolver::setAskPuzzles(bool on)
{
    m_config.askPuzzles = on;
    if (running())
        request({{QStringLiteral("op"), QStringLiteral("setPuzzles")},
                 {QStringLiteral("mode"), on ? QStringLiteral("ask") : QStringLiteral("never")}},
                false);
}

void Ed3kJdResolver::setGenericHttp(bool on)
{
    m_config.genericHttp = on;
    // an older sidecar answers "unknown op": it keeps refusing generic links
    if (running())
        request({{QStringLiteral("op"), QStringLiteral("setGeneric")}, {QStringLiteral("on"), on}}, false);
}

void Ed3kJdResolver::setProxy(const QString& url)
{
    m_config.proxy = url;
    if (running())
        request({{QStringLiteral("op"), QStringLiteral("setProxy")}, {QStringLiteral("url"), url}}, false);
}

void Ed3kJdResolver::setBindAddress(const QString& address)
{
    m_config.bindAddress = address;
    // an older sidecar answers "unknown op": harmless, it stays on the default route
    if (running())
        request({{QStringLiteral("op"), QStringLiteral("setBind")}, {QStringLiteral("address"), address}}, false);
}

std::optional<std::pair<Ed3kResolver::TaskId, ResolverEvent>> Ed3kJdResolver::parseEvent(const QByteArray& line)
{
    const QJsonObject o = QJsonDocument::fromJson(line).object();
    const QString ev = o.value(QStringLiteral("ev")).toString();
    const auto id = static_cast<TaskId>(o.value(QStringLiteral("id")).toInteger(0));
    if (ev.isEmpty())
        return std::nullopt;
    using K = ResolverEvent::Kind;
    static const std::map<QString, K> kinds{
        {QStringLiteral("match"), K::Matched},       {QStringLiteral("found"), K::Found},
        {QStringLiteral("folder"), K::Folder},       {QStringLiteral("links"), K::Links},
        {QStringLiteral("progress"), K::Progress},   {QStringLiteral("wait"), K::Wait},
        {QStringLiteral("puzzle"), K::Puzzle},       {QStringLiteral("done"), K::Done},
        {QStringLiteral("gone"), K::Gone},           {QStringLiteral("needsUser"), K::NeedsUser},
        {QStringLiteral("error"), K::Failed},        {QStringLiteral("cancelled"), K::Cancelled},
        {QStringLiteral("account"), K::Account},    {QStringLiteral("sites"), K::Sites},
        {QStringLiteral("status"), K::Status},       {QStringLiteral("solvers"), K::Solvers},
        {QStringLiteral("solved"), K::Solved},       {QStringLiteral("login"), K::Login},
        {QStringLiteral("accountHosts"), K::AccountHosts}, {QStringLiteral("favicon"), K::Icon},
        {QStringLiteral("code"), K::Code}};
    const auto k = kinds.find(ev);
    if (k == kinds.end())
        return std::nullopt;
    const auto u64 = [&o](const char* key) {
        return static_cast<std::uint64_t>(std::max<qint64>(0, o.value(QLatin1StringView(key)).toInteger(0)));
    };
    const auto str = [&o](const char* key) { return o.value(QLatin1StringView(key)).toString(); };
    ResolverEvent e;
    e.kind = k->second;
    e.host = str("host");
    e.name = str("name");
    e.path = str("path");
    e.code = str("code");
    e.text = e.kind == K::NeedsUser ? str("kind") : str("text");
    e.hashType = str("hashType");
    e.hash = str("hash");
    e.size = u64("size");
    e.bytes = u64("bytes");
    e.total = u64("total");
    if (o.contains(QStringLiteral("from")))
        e.from = u64("from");
    e.muxed = o.value(QLatin1StringView("muxed")).toBool();
    e.seconds = u64("seconds");
    e.puzzle = u64("puzzle");
    e.count = int(u64("count"));
    e.timeoutS = int(u64("timeout"));
    if (e.kind == K::Wait && e.text.isEmpty())
        e.text = str("reason");
    if (e.kind == K::Puzzle || e.kind == K::Icon)
        e.image = QByteArray::fromBase64(str("image").toLatin1());
    for (const QJsonValue& v : o.value(QStringLiteral("links")).toArray()) {
        const QJsonObject l = v.toObject();
        ResolverLink link{l.value(QStringLiteral("url")).toString(), l.value(QStringLiteral("name")).toString(),
                          l.value(QStringLiteral("host")).toString(),
                          static_cast<std::uint64_t>(std::max<qint64>(0, l.value(QStringLiteral("size")).toInteger(0)))};
        link.package = l.value(QStringLiteral("package")).toString();
        for (const QJsonValue& pw : l.value(QStringLiteral("passwords")).toArray())
            if (const QString t = pw.toString(); !t.isEmpty())
                link.passwords << t;
        e.links.append(link);
    }
    for (const QJsonValue& v : o.value(QStringLiteral("sites")).toArray())
        if (const QString d = v.toString().toLower(); !d.isEmpty())
            e.sites.append(d);
    if (e.kind == K::Matched)
        e.generic = str("generic");
    e.valid = o.value(QStringLiteral("valid")).toBool();
    e.premium = o.value(QStringLiteral("premium")).toBool();
    e.expires = o.value(QStringLiteral("expires")).toInteger(0);
    e.trafficLeft = o.contains(QStringLiteral("trafficLeft")) ? o.value(QStringLiteral("trafficLeft")).toInteger(-1) : -1;
    e.trafficMax = o.value(QStringLiteral("trafficMax")).toInteger(-1);
    e.unlimited = o.value(QStringLiteral("unlimited")).toBool();
    e.unusable = str("unusable");
    e.maxDownloads = int(o.value(QStringLiteral("maxDownloads")).toInteger(0));
    e.balance = o.value(QStringLiteral("balance")).toDouble(-1);
    e.currency = str("currency");
    if (e.kind == K::Solved) {
        e.host = str("solver");
        e.name = str("expected");
    }
    if (e.kind == K::Login)
        e.text = str("url");
    if (e.kind == K::Code) {
        e.name = str("title");
        e.user = str("user");
    }
    if (e.kind == K::AccountHosts) {
        for (const QJsonValue& v : o.value(QStringLiteral("hosts")).toArray()) {
            const QJsonObject h = v.toObject();
            AccountHost a{h.value(QStringLiteral("host")).toString().toLower(), h.value(QStringLiteral("name")).toString(),
                          h.value(QStringLiteral("multi")).toBool(), h.value(QStringLiteral("solver")).toBool(), {}};
            for (const QJsonValue& sub : h.value(QStringLiteral("subs")).toArray())
                if (const QString n = sub.toString().toLower(); !n.isEmpty() && a.subs.size() < 8)
                    a.subs.append(n);
            if (!a.host.isEmpty())
                e.accountHosts.append(a);
        }
    }
    if (e.kind == K::Account) {
        // the plugin's login error, when invalid
        if (!e.valid)
            e.text = str("error");
        // a browser login (OAuth) nobody was asked for: the startup check, a download
        if (!e.valid && e.unusable.isEmpty() && e.text == QLatin1StringView("browserLogin"))
            e.unusable = QStringLiteral("login");
        e.multi = o.value(QStringLiteral("multi")).toBool();
        for (const QJsonValue& v : o.value(QStringLiteral("hosts")).toArray()) {
            const QJsonObject h = v.toObject();
            const auto num = [&h](const char* key, qint64 def) { return h.value(QLatin1StringView(key)).toInteger(def); };
            DebridHost d;
            d.domain = h.value(QStringLiteral("domain")).toString().toLower();
            for (const QJsonValue& x : h.value(QStringLiteral("domains")).toArray())
                if (const QString s = x.toString().toLower(); !s.isEmpty())
                    d.domains << s;
            if (d.domain.isEmpty())
                continue;
            if (!d.domains.contains(d.domain))
                d.domains.prepend(d.domain);
            d.status = h.value(QStringLiteral("status")).toString(QStringLiteral("working"));
            d.linksLeft = num("linksLeft", -1);
            d.linksMax = num("linksMax", -1);
            d.trafficLeft = num("trafficLeft", -1);
            d.trafficMax = num("trafficMax", -1);
            d.factor = int(std::clamp<qint64>(num("factor", 100), 1, 100000));
            d.maxDownloads = int(num("maxDownloads", -1));
            d.unavailableS = std::max<qint64>(0, num("unavailableS", 0));
            e.hosts.append(d);
        }
        if (const QJsonObject st = o.value(QStringLiteral("state")).toObject(); !st.isEmpty())
            e.state = QJsonDocument(st).toJson(QJsonDocument::Compact);
    }
    for (const QJsonValue& v : o.value(QStringLiteral("accounts")).toArray()) {
        const QJsonObject a = v.toObject();
        e.solvers.append({a.value(QStringLiteral("host")).toString(), a.value(QStringLiteral("valid")).toBool(),
                          a.value(QStringLiteral("balance")).toDouble(), a.value(QStringLiteral("currency")).toString(),
                          a.value(QStringLiteral("code")).toString(), a.value(QStringLiteral("text")).toString()});
    }
    return std::pair{id, e};
}

Ed3kResolver::TaskId Ed3kJdResolver::request(QJsonObject req, bool task)
{
    const TaskId id = m_nextId++;
    // new plugins being unpacked: a few seconds, the task goes out after
    if (task && installing()) {
        m_held.emplace_back(id, req);
        return id;
    }
    if (task && !ensureStarted()) {
        // answered on the next turn, like a sidecar would
        ResolverEvent e;
        e.kind = ResolverEvent::Kind::Failed;
        e.code = QStringLiteral("unavailable");
        e.text = unavailableReason();
        QMetaObject::invokeMethod(this, [this, id, e] { emit taskEvent(id, e); }, Qt::QueuedConnection);
        return id;
    }
    req.insert(QStringLiteral("id"), static_cast<qint64>(id));
    if (task) {
        m_pending[id] = true;
        m_idle.stop();
    }
    send(req);
    return id;
}

void Ed3kJdResolver::send(const QJsonObject& req)
{
    if (!m_sidecar.running())
        return;
    // never logged whole: it may hold a password
    const QByteArray line = QJsonDocument(req).toJson(QJsonDocument::Compact) + '\n';
    const bool now = m_authed && m_sock;
    logHosterDebug([&]() -> QString { return QStringLiteral("hoster: -> %1%2").arg(describeRequest(req), now ? QString() : QStringLiteral(" (queued)")); });
    if (now)
        m_sock->write(line);
    else
        m_sendQueue += line;
}

Ed3kJdResolver::InstallResult Ed3kJdResolver::installFor(const JdResolverConfig& c)
{
    InstallResult r;
    if (c.sharedDir.isEmpty()) {
        if (!c.zip.isEmpty() && !install(c.zip, c.dataDir, &r.error))
            logHosterWarning(QStringLiteral("hoster: resolver plugins not installed: %1").arg(r.error));
        r.root = instanceRoot(c.dataDir);
    } else {
        // a per-instance copy of before is not needed any more
        removePlugins(c.dataDir);
        if (!c.zip.isEmpty() && (r.root = installShared(c.zip, c.sharedDir, &r.error)).isEmpty())
            logHosterWarning(QStringLiteral("hoster: shared resolver plugins not installed: %1").arg(r.error));
        // no zip, or it could not go in (read-only folder, disk full): what is there already
        if (r.root.isEmpty() && !(r.root = sharedVersion(c.sharedDir)).isEmpty())
            r.error.clear();
    }
    r.revision = readRevision(r.root);
    return r;
}

void Ed3kJdResolver::installPlugins()
{
    m_installError.clear();
    m_root.clear();
    m_revision.clear();
    if (!m_config.enabled) {
        // a shared copy stays: other instances may run from it
        if (m_config.sharedDir.isEmpty())
            removePlugins(m_config.dataDir);
        return;
    }
    if (m_config.dataDir.isEmpty())
        return;
    const InstallResult r = installFor(m_config);
    m_root = r.root;
    m_installError = r.error;
    m_revision = r.revision;
}

void Ed3kJdResolver::installAsync()
{
    if (!m_config.enabled || m_config.dataDir.isEmpty()) {
        installPlugins();
        return;
    }
    m_installError.clear();
    const JdResolverConfig cfg = m_config;
    auto result = std::make_shared<InstallResult>();
    QThread* t = QThread::create([cfg, result] { *result = installFor(cfg); });
    m_installThread = t;
    connect(t, &QThread::finished, this, [this, t, result] {
        t->deleteLater();
        m_installThread = nullptr;
        onInstalled(*result);
    });
    logHosterDebug([&]() -> QString { return QStringLiteral("hoster: unpacking resolver plugins from %1").arg(cfg.zip); });
    t->start(QThread::LowPriority);
}

void Ed3kJdResolver::onInstalled(const InstallResult& r)
{
    if (std::exchange(m_reinstall, false)) {
        // the config changed meanwhile: what it says now
        installPlugins();
    } else {
        m_root = r.root;
        m_installError = r.error;
        m_revision = r.revision;
        if (r.error.isEmpty())
            logHoster(QStringLiteral("hoster: resolver plugins updated to %1")
                          .arg(m_revision.isEmpty() ? QStringLiteral("?") : QStringLiteral("r") + m_revision));
    }
    m_description.clear();
    releaseHeld();
    emit stateChanged();
}

void Ed3kJdResolver::releaseHeld()
{
    for (auto& [id, req] : std::exchange(m_held, {})) {
        if (!ensureStarted()) {
            ResolverEvent e;
            e.kind = ResolverEvent::Kind::Failed;
            e.code = QStringLiteral("unavailable");
            e.text = unavailableReason();
            QMetaObject::invokeMethod(this, [this, id, e] { emit taskEvent(id, e); }, Qt::QueuedConnection);
            continue;
        }
        req.insert(QStringLiteral("id"), static_cast<qint64>(id));
        m_pending[id] = true;
        m_idle.stop();
        send(req);
    }
}

bool Ed3kJdResolver::ensureStarted()
{
    if (running())
        return true;
    if (installing())
        return false;
    // a shared version pruned while this instance idled: unpack it again
    if (m_config.enabled && !m_config.sharedDir.isEmpty() && !layoutOk(m_root))
        installPlugins();
    if (!available())
        return false;
    if (!m_config.sharedDir.isEmpty())
        touchUsed(QFileInfo(m_root).path());
    closeChannel();
    // loopback only, never the wildcard; one connection, the one with the token
    auto* server = new QTcpServer(this);
    if (!server->listen(QHostAddress::LocalHost, 0)) {
        logHosterWarning(QStringLiteral("hoster: resolver socket: %1").arg(server->errorString()));
        delete server;
        return false;
    }
    m_server = server;
    connect(server, &QTcpServer::newConnection, this, &Ed3kJdResolver::onNewConnection);
    QByteArray raw(32, Qt::Uninitialized);
    QRandomGenerator::system()->fillRange(reinterpret_cast<quint32*>(raw.data()), raw.size() / 4);
    m_token = raw.toHex();
    m_outBuf.clear();
    QDir().mkpath(QDir(m_config.dataDir).filePath(QStringLiteral("jd")));
    // stdout is no protocol any more: both are JD's log
    m_sidecar.start(m_config.java, arguments(), m_config.dataDir);
    // the token goes in through stdin, then stdin closes: console reads in JD get EOF
    m_sidecar.write(m_token + '\n');
    m_sidecar.closeWriteChannel();
    m_connectTimer.start();
    m_progressLogged.clear();
    logHosterDebug([&]() -> QString { return QStringLiteral("hoster: resolver starting: %1, plugins %2, listening on 127.0.0.1:%3")
                       .arg(argStr(m_config.java), argStr(m_revision.isEmpty() ? QStringLiteral("?") : QStringLiteral("r") + m_revision),
                            argStr(m_server->serverPort())); });
    m_stopping = false;
    request({{QStringLiteral("op"), QStringLiteral("hello")}}, false);
    if (!m_config.proxy.isEmpty())
        request({{QStringLiteral("op"), QStringLiteral("setProxy")}, {QStringLiteral("url"), m_config.proxy}}, false);
    if (!m_config.bindAddress.isEmpty())
        request({{QStringLiteral("op"), QStringLiteral("setBind")}, {QStringLiteral("address"), m_config.bindAddress}}, false);
    if (!m_config.askPuzzles)
        request({{QStringLiteral("op"), QStringLiteral("setPuzzles")}, {QStringLiteral("mode"), QStringLiteral("never")}}, false);
    if (m_config.genericHttp)
        request({{QStringLiteral("op"), QStringLiteral("setGeneric")}, {QStringLiteral("on"), true}}, false);
    if (!m_config.solvers.isEmpty())
        sendSolvers();
    return true;
}

void Ed3kJdResolver::sendSolvers()
{
    QJsonArray list;
    for (const SolverAccount& a : m_config.solvers)
        list.append(QJsonObject{{QStringLiteral("host"), a.host},
                                {QStringLiteral("user"), a.user},
                                {QStringLiteral("password"), a.password}});
    // a task: its answer carries the balances
    m_solverTask = m_nextId++;
    m_pending[m_solverTask] = true;
    m_idle.stop();
    send({{QStringLiteral("op"), QStringLiteral("solverAccounts")},
          {QStringLiteral("id"), static_cast<qint64>(m_solverTask)},
          {QStringLiteral("accounts"), list}});
}

void Ed3kJdResolver::sendTools(const QString& error)
{
    // an older sidecar answers "unknown op": its task fails with FFMPEG_MISSING as before
    QJsonObject req{{QStringLiteral("op"), QStringLiteral("setTools")},
                    {QStringLiteral("ffmpeg"), QDir::toNativeSeparators(m_config.ffmpeg)},
                    {QStringLiteral("ffprobe"), QDir::toNativeSeparators(m_config.ffprobe)}};
    if (!error.isEmpty())
        req.insert(QStringLiteral("error"), error);
    request(req, false);
}

void Ed3kJdResolver::stop()
{
    m_idle.stop();
    if (!m_sidecar.running())
        return;
    m_stopping = true;
    closeChannel();
    // closing the channel ends java; killed when it does not
    m_sidecar.stop(3000, false);
}

void Ed3kJdResolver::onLog(const QByteArray& line)
{
    // JD's own log may hold page content: only the driver's lines
    if (line.startsWith("resolver: "))
        logHosterDebug([&]() -> QString { return QStringLiteral("hoster: ") + QString::fromUtf8(line.mid(10)); });
    else if (line.startsWith("resolver+ "))   // shown without debug
        logHoster(QStringLiteral("hoster: ") + QString::fromUtf8(line.mid(10)).trimmed());
    else if (line.startsWith("resolver! "))
        logHosterWarning(QStringLiteral("hoster: ") + QString::fromUtf8(line.mid(10)).trimmed());
}

void Ed3kJdResolver::onFinished(int exitCode, bool crashed, bool stopped)
{
    const bool expected = m_stopping || stopped;
    logHosterDebug([&]() -> QString { return QStringLiteral("hoster: resolver exited, code %1%2, after %3 s")
                       .arg(argStr(exitCode),
                            argStr(crashed ? QStringLiteral(" (crashed)") : QString()),
                            argStr(m_sidecar.uptimeMs() / 1000)); });
    closeChannel();
    m_stopping = false;
    if (!expected)
        logHosterWarning(QStringLiteral("hoster: resolver ended"));
    failPending(QStringLiteral("io"));
    // idle now: new plugins that waited go in
    if (!m_pendingZip.isEmpty()) {
        JdResolverConfig c = m_config;
        c.zip = m_pendingZip;
        setConfig(c);   // emits stateChanged
        return;
    }
    emit stateChanged();
}

void Ed3kJdResolver::onIdle()
{
    if (m_pending.empty()) {
        logHosterDebug([&]() -> QString { return QStringLiteral("hoster: resolver idle for %1 min, stopping it (up %2 s)")
                           .arg(kIdleStopMs / 60000)
                           .arg(m_sidecar.uptimeMs() / 1000); });
        stop();
    }
}

void Ed3kJdResolver::failPending(const QString& code)
{
    const auto pending = std::exchange(m_pending, {});
    for (const auto& [id, _] : pending) {
        ResolverEvent e;
        e.kind = ResolverEvent::Kind::Failed;
        e.code = code;
        emit taskEvent(id, e);
    }
}

void Ed3kJdResolver::handleLine(const QByteArray& line)
{
    const QJsonObject o = QJsonDocument::fromJson(line).object();
    if (o.value(QStringLiteral("ev")).toString() == QLatin1StringView("hello")) {
        const int proto = o.value(QStringLiteral("protocol")).toInt();
        if (proto != kProtocol) {
            logHosterWarning(QStringLiteral("hoster: resolver speaks protocol %1, not %2").arg(proto).arg(kProtocol));
            m_installError = tr("Resolver plugins of another version");
            stop();
            return;
        }
        if (const QString rev = o.value(QStringLiteral("jdRevision")).toString(); !rev.isEmpty() && rev != u'?')
            m_revision = rev;
        m_deep = o.value(QStringLiteral("deep")).toBool();
        m_debrid = o.value(QStringLiteral("debrid")).toBool();
        logHosterDebug([&]() -> QString { return QStringLiteral("hoster: <- hello, protocol %1, JD r%2, %3 hosters, %4 decrypters")
                           .arg(argStr(proto),
                                argStr(o.value(QStringLiteral("jdRevision")).toString()),
                                argStr(o.value(QStringLiteral("hosts")).toInt()),
                                argStr(o.value(QStringLiteral("crawlers")).toInt())); });
        m_description = tr("JDownloader r%1, %2 hosters, %3 decrypters")
                            .arg(argStr(o.value(QStringLiteral("jdRevision")).toString()),
                                 argStr(o.value(QStringLiteral("hosts")).toInt()),
                                 argStr(o.value(QStringLiteral("crawlers")).toInt()));
        emit stateChanged();
        return;
    }
    if (o.value(QStringLiteral("ev")).toString() == QLatin1StringView("needTools")) {
        // not a task's: one ask for every waiting task
        logHoster(QStringLiteral("hoster: the resolver needs ffmpeg + ffprobe (HLS/DASH)"));
        emit toolsNeeded();
        return;
    }
    auto parsed = parseEvent(line);
    if (!parsed) {
        // the name only: the line may hold page content
        logHosterDebug([&]() -> QString { return QStringLiteral("hoster: <- unknown event '%1', dropped")
                           .arg(o.value(QStringLiteral("ev")).toString().left(32)); });
        return;
    }
    const auto& [id, ev] = *parsed;
    // a puzzle of a link no open task names (a check) has id 0
    if (id == 0 && ev.kind != ResolverEvent::Kind::Puzzle)
        return;
    if (id != 0 && !m_pending.contains(id)) {
        logHosterDebug([&]() -> QString { return QStringLiteral("hoster: <- %1 for task %2, not open any more, dropped")
                           .arg(argStr(o.value(QStringLiteral("ev")).toString()), argStr(id)); });
        return;
    }
    logEvent(id, o.value(QStringLiteral("ev")).toString(), ev);
    if (id == m_solverTask && ev.isFinal()) {
        m_solverStates = ev.solvers;
        m_solverTask = 0;
    }
    if (ev.isFinal()) {
        m_progressLogged.erase(id);
        m_pending.erase(id);
        if (m_pending.empty())
            m_idle.start();
    }
    emit taskEvent(id, ev);
}

void Ed3kJdResolver::closeChannel()
{
    m_connectTimer.stop();
    m_authed = false;
    m_sendQueue.clear();
    m_outBuf.clear();
    m_token.clear();
    if (m_sock) {
        m_sock->disconnect(this);
        m_sock->abort();
        m_sock->deleteLater();
    }
    m_sock = nullptr;
    if (m_server) {
        m_server->disconnect(this);
        m_server->close();
        m_server->deleteLater();
    }
    m_server = nullptr;
}

void Ed3kJdResolver::onNewConnection()
{
    while (m_server && m_server->hasPendingConnections()) {
        QTcpSocket* s = m_server->nextPendingConnection();
        if (m_sock) {
            // one candidate at a time; the sidecar retries nothing, so a second is not ours
            s->abort();
            s->deleteLater();
            continue;
        }
        s->setParent(this);   // the server is deleted after auth, its children with it
        m_sock = s;
        logHosterDebug([&]() -> QString { return QStringLiteral("hoster: resolver connected from port %1, waiting for its token")
                           .arg(s->peerPort()); });
        m_outBuf.clear();
        connect(s, &QTcpSocket::readyRead, this, &Ed3kJdResolver::onSocketRead);
        connect(s, &QTcpSocket::disconnected, this, &Ed3kJdResolver::onSocketGone);
    }
}

void Ed3kJdResolver::onSocketRead()
{
    if (!m_sock)
        return;
    m_outBuf += m_sock->readAll();
    qsizetype nl;
    while (m_sock && (nl = m_outBuf.indexOf('\n')) >= 0) {
        const QByteArray line = m_outBuf.left(nl);
        m_outBuf.remove(0, nl + 1);
        if (m_authed) {
            handleLine(line);
            continue;
        }
        const QJsonObject o = QJsonDocument::fromJson(line).object();
        const QByteArray token = o.value(QStringLiteral("token")).toString().toLatin1();
        if (o.value(QStringLiteral("ev")).toString() != QLatin1StringView("auth") || m_token.isEmpty() || token != m_token) {
            logHosterWarning(QStringLiteral("hoster: resolver socket: a connection without the token, dropped"));
            m_sock->disconnect(this);
            m_sock->abort();
            m_sock->deleteLater();
            m_sock = nullptr;
            m_outBuf.clear();
            return;
        }
        m_authed = true;
        m_connectTimer.stop();
        if (m_server) {
            m_server->disconnect(this);
            m_server->close();
            m_server->deleteLater();
            m_server = nullptr;
        }
        m_sock->setSocketOption(QAbstractSocket::LowDelayOption, 1);
        logHosterDebug([&]() -> QString { return QStringLiteral("hoster: resolver authenticated after %1 ms, %2 queued request(s) sent")
                           .arg(m_sidecar.uptimeMs())
                           .arg(m_sendQueue.count('\n')); });
        m_sock->write(std::exchange(m_sendQueue, {}));
    }
    if (m_outBuf.size() > kMaxLine) {
        logHosterWarning(QStringLiteral("hoster: resolver line too long, stopping it"));
        m_outBuf.clear();
        stop();
    }
}

void Ed3kJdResolver::onSocketGone()
{
    const bool authed = m_authed;
    if (m_sock) {
        m_sock->disconnect(this);
        m_sock->deleteLater();
    }
    m_sock = nullptr;
    m_outBuf.clear();
    if (!authed)
        return;   // an unauthenticated one; keep listening
    m_authed = false;
    // the sidecar closed its end: it is ending or broken either way
    if (!m_stopping)
        logHosterWarning(QStringLiteral("hoster: resolver closed its connection"));
    stop();
}

void Ed3kJdResolver::onConnectTimeout()
{
    if (m_authed || !m_sidecar.running())
        return;
    logHosterWarning(QStringLiteral("hoster: resolver did not connect"));
    stop();
}

QString Ed3kJdResolver::describeRequest(const QJsonObject& req)
{
    const QString op = req.value(QStringLiteral("op")).toString();
    QString out = op;
    if (const qint64 id = req.value(QStringLiteral("id")).toInteger(0); id)
        out += QStringLiteral(" task %1").arg(id);
    // a link's host only: path and query may carry a file id or a session
    if (op == QLatin1StringView("setProxy")) {
        const QUrl u(req.value(QStringLiteral("url")).toString());
        out += u.isEmpty() ? QStringLiteral(" (none)") : QStringLiteral(" %1://%2").arg(u.scheme(), u.host());
    } else if (const QString url = req.value(QStringLiteral("url")).toString(); !url.isEmpty())
        out += u' ' + QUrl(url).host();
    if (const QString host = req.value(QStringLiteral("host")).toString(); !host.isEmpty())
        out += u' ' + host;
    if (req.contains(QStringLiteral("account")))
        out += QStringLiteral(" with account");
    if (const QJsonObject via = req.value(QStringLiteral("via")).toObject(); !via.isEmpty())
        out += QStringLiteral(" via ") + via.value(QStringLiteral("host")).toString();
    if (const QJsonArray accs = req.value(QStringLiteral("accounts")).toArray(); !accs.isEmpty()) {
        QStringList hosts;
        for (const QJsonValue& v : accs)
            hosts << v.toObject().value(QStringLiteral("host")).toString();
        out += u' ' + hosts.join(u',');
    }
    if (const QString solver = req.value(QStringLiteral("solver")).toString(); !solver.isEmpty())
        out += u' ' + solver;
    if (const QString type = req.value(QStringLiteral("type")).toString(); !type.isEmpty())
        out += u' ' + type;
    if (const qint64 have = req.value(QStringLiteral("have")).toInteger(0); have > 0)
        out += QStringLiteral(", resume at %1 B").arg(have);
    if (const QString mode = req.value(QStringLiteral("mode")).toString(); !mode.isEmpty())
        out += u' ' + mode;
    if (const qint64 task = req.value(QStringLiteral("task")).toInteger(0); task)
        out += QStringLiteral(" task %1").arg(task);
    if (const qint64 puzzle = req.value(QStringLiteral("puzzle")).toInteger(0); puzzle)
        out += QStringLiteral(" puzzle %1").arg(puzzle);   // the answer text stays out
    return out;
}

void Ed3kJdResolver::logEvent(TaskId id, const QString& ev, const ResolverEvent& e)
{
    using K = ResolverEvent::Kind;
    if (e.kind == K::Progress) {
        // once in 10 s per task
        constexpr qint64 kProgressEveryMs = 10'000;
        const qint64 now = m_sidecar.uptimeMs();
        auto [it, fresh] = m_progressLogged.try_emplace(id, now);
        if (!fresh && now - it->second < kProgressEveryMs)
            return;
        it->second = now;
    }
    QString d;
    switch (e.kind) {
    case K::Matched:
    case K::Sites:
        d = e.host.isEmpty() ? QStringLiteral("no plugin") : QStringLiteral("%1, %2 site(s)").arg(argStr(e.host), argStr(e.sites.size()));
        break;
    case K::Found:
        d = QStringLiteral("'%1', %2 B%3").arg(argStr(e.name), argStr(e.size),
                     argStr(e.hashType.isEmpty() ? QString() : QStringLiteral(", ") + e.hashType));
        break;
    case K::Folder:
        d = QStringLiteral("%1 link(s)").arg(e.count);
        break;
    case K::Links:
        d = QStringLiteral("%1 link(s)").arg(e.links.size());
        break;
    case K::Progress:
        d = e.total ? QStringLiteral("%1 of %2 B").arg(e.bytes).arg(e.total) : QStringLiteral("%1 B").arg(e.bytes);
        if (e.from)
            d += QStringLiteral(", from %1").arg(*e.from);
        break;
    case K::Wait:
        d = QStringLiteral("%1 s, %2").arg(e.seconds).arg(e.text);
        break;
    case K::Puzzle:
        d = QStringLiteral("puzzle %1 of %2, %3 s to answer").arg(argStr(e.puzzle), argStr(e.host), argStr(e.timeoutS));
        break;
    case K::Login:
        d = QStringLiteral("browser login at %1, %2 s").arg(argStr(e.host), argStr(e.timeoutS));
        break;
    case K::Code:
        d = QStringLiteral("confirmation code, %1 s").arg(e.timeoutS);   // the site's words stay out
        break;
    case K::Done:
        d = QStringLiteral("'%1', %2 B").arg(argStr(e.name), argStr(e.size));
        break;
    case K::NeedsUser:
    case K::Status:
        d = e.text;
        break;
    case K::Failed:
        d = e.text.isEmpty() ? e.code : QStringLiteral("%1: %2").arg(e.code, e.text);
        break;
    case K::Account:
        d = QStringLiteral("valid %1, premium %2%3%4%5").arg(argStr(e.valid ? QStringLiteral("yes") : QStringLiteral("no")), argStr(e.premium ? QStringLiteral("yes") : QStringLiteral("no")),
                     argStr(e.expires ? QStringLiteral(", expires ") + QDateTime::fromSecsSinceEpoch(e.expires).toString(Qt::ISODate) : QString()),
                     argStr(e.trafficLeft >= 0 ? QStringLiteral(", %1 B traffic left").arg(e.trafficLeft) : QString()),
                     argStr(e.maxDownloads > 0    ? QStringLiteral(", %1 downloads at once").arg(e.maxDownloads)
                     : e.maxDownloads < 0 ? QStringLiteral(", downloads at once unlimited")
                                          : QString()));
        if (e.multi)
            d += QStringLiteral(", multi-hoster for %1 hosts").arg(e.hosts.size());
        if (!e.valid && !e.text.isEmpty())
            d += QStringLiteral(" (%1)").arg(e.text);
        break;
    case K::Solvers: {
        QStringList parts;
        for (const SolverAccountState& a : e.solvers)
            parts << (a.valid ? QStringLiteral("%1 %2 %3").arg(argStr(a.host), argStr(a.balance), argStr(a.currency)).trimmed()
                              : QStringLiteral("%1 %2").arg(a.host, a.code));
        d = parts.join(QStringLiteral(", "));
        break;
    }
    case K::Solved:
        d = QStringLiteral("by %1").arg(e.host);   // the answer stays out
        break;
    case K::AccountHosts:
        d = QStringLiteral("%1 hoster(s)").arg(e.accountHosts.size());
        break;
    case K::Icon:
        d = QStringLiteral("%1, %2 B").arg(argStr(e.host), argStr(e.image.size()));
        break;
    case K::Gone:
    case K::Cancelled:
        break;
    }
    logHosterDebug([&]() -> QString { return QStringLiteral("hoster: <- task %1 %2%3").arg(argStr(id), argStr(ev), argStr(d.isEmpty() ? QString() : QStringLiteral(": ") + d)); });
}

} // namespace eMule::ed3k

#endif // EMULE_HAVE_HOSTER_JD
