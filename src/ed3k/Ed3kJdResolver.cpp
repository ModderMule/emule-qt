#include "Ed3kJdResolver.h"

#ifdef EMULE_HAVE_HOSTER_JD

#include "archive/ArchiveReader.h"
#include "utils/Log.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>

namespace eMule::ed3k {

namespace {

constexpr qsizetype kMaxLine = 16 * 1024 * 1024;   // a puzzle image is the largest event

QString pluginRoot(const QString& dataDir)
{
    return QDir(dataDir).filePath(QStringLiteral("plugins/resolver"));
}

QString zipStamp(const QString& zip)
{
    const QFileInfo fi(zip);
    return QStringLiteral("%1 %2").arg(fi.size()).arg(fi.lastModified().toMSecsSinceEpoch());
}

QJsonObject accountJson(const std::optional<ResolverAccount>& a)
{
    if (!a || a->user.isEmpty())
        return {};
    return QJsonObject{{QStringLiteral("user"), a->user}, {QStringLiteral("password"), a->password}};
}

} // namespace

Ed3kJdResolver::Ed3kJdResolver(QObject* parent)
    : Ed3kResolver(parent)
{
    m_idle.setSingleShot(true);
    m_idle.setInterval(kIdleStopMs);
    connect(&m_idle, &QTimer::timeout, this, &Ed3kJdResolver::onIdle);
}

Ed3kJdResolver::~Ed3kJdResolver()
{
    m_stopping = true;
    if (m_proc) {
        m_proc->disconnect(this);
        m_proc->closeWriteChannel();
        if (!m_proc->waitForFinished(2000))
            m_proc->kill();
        delete m_proc;
    }
}

void Ed3kJdResolver::setConfig(const JdResolverConfig& config)
{
    const bool restart = config.java != m_config.java || config.zip != m_config.zip || config.dataDir != m_config.dataDir;
    const bool proxy = config.proxy != m_config.proxy;
    const bool puzzles = config.askPuzzles != m_config.askPuzzles;
    m_config = config;
    if (restart) {
        stop();
        m_installError.clear();
        if (!m_config.zip.isEmpty() && !m_config.dataDir.isEmpty() && !install(m_config.zip, m_config.dataDir, &m_installError))
            logEd3kWarning(QStringLiteral("hoster: resolver plugins not installed: %1").arg(m_installError));
        emit stateChanged();
        return;
    }
    if (proxy)
        setProxy(m_config.proxy);
    if (puzzles)
        setAskPuzzles(m_config.askPuzzles);
}

QString Ed3kJdResolver::findBundledZip()
{
    const QString env = qEnvironmentVariable("EMULE_RESOLVER_ZIP");
    if (!env.isEmpty() && QFileInfo(env).isFile())
        return env;
    const QDir app(QCoreApplication::applicationDirPath());
    for (const QString& rel : {QStringLiteral("resolver.zip"), QStringLiteral("../Resources/resolver.zip"),
                               QStringLiteral("../share/emuleqt/resolver.zip"), QStringLiteral("resolver/resolver.zip")}) {
        const QString p = app.filePath(rel);
        if (QFileInfo(p).isFile())
            return QDir::cleanPath(p);
    }
    // a build tree: build/hoster-resolver/resolver.zip above the binary
    QDir up = app;
    for (int i = 0; i < 6 && up.cdUp(); ++i) {
        const QString p = up.filePath(QStringLiteral("hoster-resolver/resolver.zip"));
        if (QFileInfo(p).isFile())
            return QDir::cleanPath(p);
    }
    return {};
}

bool Ed3kJdResolver::install(const QString& zip, const QString& dataDir, QString* error)
{
    const QString plugins = QDir(dataDir).filePath(QStringLiteral("plugins"));
    const QString stampFile = QDir(plugins).filePath(QStringLiteral(".zipstamp"));
    QFile stamp(stampFile);
    if (stamp.open(QIODevice::ReadOnly) && QString::fromUtf8(stamp.readAll()) == zipStamp(zip)
        && QFileInfo(QDir(pluginRoot(dataDir)).filePath(QStringLiteral("classes"))).isDir())
        return true;
    stamp.close();
    const QString tmp = plugins + QStringLiteral(".tmp");
    QDir(tmp).removeRecursively();
    QDir().mkpath(tmp);
    ArchiveReader reader;
    if (!reader.open(zip) || !reader.extractAll(tmp)) {
        if (error)
            *error = reader.lastError();
        QDir(tmp).removeRecursively();
        return false;
    }
    if (!QFileInfo(QDir(tmp).filePath(QStringLiteral("resolver/classes/org/emuleqt/resolver/ResolverMain.class"))).isFile()) {
        if (error)
            *error = QStringLiteral("not a resolver archive");
        QDir(tmp).removeRecursively();
        return false;
    }
    QDir(plugins).removeRecursively();
    if (!QDir().rename(tmp, plugins)) {
        if (error)
            *error = QStringLiteral("cannot move %1").arg(tmp);
        return false;
    }
    QFile out(stampFile);
    if (out.open(QIODevice::WriteOnly | QIODevice::Truncate))
        out.write(zipStamp(zip).toUtf8());
    logEd3k(QStringLiteral("hoster: resolver plugins installed from %1").arg(zip));
    return true;
}

QStringList Ed3kJdResolver::arguments() const
{
    const QDir root(pluginRoot(m_config.dataDir));
    QFile revFile(root.filePath(QStringLiteral("revision.txt")));
    QString rev = QStringLiteral("?");
    if (revFile.open(QIODevice::ReadOnly))
        rev = QString::fromUtf8(revFile.readAll()).trimmed();
    const QString cp = root.filePath(QStringLiteral("classes")) + QDir::listSeparator() + root.filePath(QStringLiteral("libs/*"));
    return {QStringLiteral("-Xmx768m"),
            QStringLiteral("-Djava.awt.headless=true"),
            QStringLiteral("-Dfile.encoding=UTF-8"),
            QStringLiteral("-DROOT=") + QDir(m_config.dataDir).filePath(QStringLiteral("jd")),
            QStringLiteral("-Demuleqt.jdRevision=") + rev,
            QStringLiteral("-cp"),
            cp,
            QStringLiteral("org.emuleqt.resolver.ResolverMain")};
}

bool Ed3kJdResolver::available() const
{
    return unavailableReason().isEmpty();
}

QString Ed3kJdResolver::unavailableReason() const
{
    if (m_config.java.isEmpty())
        return tr("No Java runtime");
    if (!m_installError.isEmpty())
        return tr("Resolver plugins not installed: %1").arg(m_installError);
    if (m_config.dataDir.isEmpty()
        || !QFileInfo(QDir(pluginRoot(m_config.dataDir)).filePath(QStringLiteral("classes"))).isDir())
        return tr("Resolver plugins missing");
    return {};
}

bool Ed3kJdResolver::running() const
{
    return m_proc && m_proc->state() != QProcess::NotRunning;
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

Ed3kResolver::TaskId Ed3kJdResolver::open(const QString& url, const std::optional<ResolverAccount>& account,
                                          const QString& dir, std::uint64_t have)
{
    QJsonObject r{{QStringLiteral("op"), QStringLiteral("open")},
                  {QStringLiteral("url"), url},
                  {QStringLiteral("path"), dir},
                  {QStringLiteral("have"), static_cast<qint64>(have)}};
    if (const QJsonObject a = accountJson(account); !a.isEmpty())
        r.insert(QStringLiteral("account"), a);
    return request(r, true);
}

Ed3kResolver::TaskId Ed3kJdResolver::accountCheck(const QString& host, const ResolverAccount& account)
{
    return request({{QStringLiteral("op"), QStringLiteral("accountCheck")},
                    {QStringLiteral("host"), host},
                    {QStringLiteral("account"), accountJson(account)}},
                   true);
}

void Ed3kJdResolver::answerPuzzle(std::uint64_t puzzle, const QString& text)
{
    if (running())
        send({{QStringLiteral("op"), QStringLiteral("answer")},
              {QStringLiteral("puzzle"), static_cast<qint64>(puzzle)},
              {QStringLiteral("text"), text}});
}

void Ed3kJdResolver::cancel(TaskId task)
{
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

void Ed3kJdResolver::setProxy(const QString& url)
{
    m_config.proxy = url;
    if (running())
        request({{QStringLiteral("op"), QStringLiteral("setProxy")}, {QStringLiteral("url"), url}}, false);
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
        {QStringLiteral("account"), K::Account}};
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
    e.seconds = u64("seconds");
    e.puzzle = u64("puzzle");
    e.count = int(u64("count"));
    e.timeoutS = int(u64("timeout"));
    if (e.kind == K::Wait && e.text.isEmpty())
        e.text = str("reason");
    if (e.kind == K::Puzzle)
        e.image = QByteArray::fromBase64(str("image").toLatin1());
    for (const QJsonValue& v : o.value(QStringLiteral("links")).toArray()) {
        const QJsonObject l = v.toObject();
        e.links.append({l.value(QStringLiteral("url")).toString(), l.value(QStringLiteral("name")).toString(),
                        l.value(QStringLiteral("host")).toString(),
                        static_cast<std::uint64_t>(std::max<qint64>(0, l.value(QStringLiteral("size")).toInteger(0)))});
    }
    e.valid = o.value(QStringLiteral("valid")).toBool();
    e.premium = o.value(QStringLiteral("premium")).toBool();
    e.expires = o.value(QStringLiteral("expires")).toInteger(0);
    e.trafficLeft = o.contains(QStringLiteral("trafficLeft")) ? o.value(QStringLiteral("trafficLeft")).toInteger(-1) : -1;
    return std::pair{id, e};
}

Ed3kResolver::TaskId Ed3kJdResolver::request(QJsonObject req, bool task)
{
    const TaskId id = m_nextId++;
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
    if (!m_proc)
        return;
    // never logged: it may hold a password
    m_proc->write(QJsonDocument(req).toJson(QJsonDocument::Compact) + '\n');
}

bool Ed3kJdResolver::ensureStarted()
{
    if (running())
        return true;
    if (!available())
        return false;
    auto* p = new QProcess(this);
    m_proc = p;
    m_outBuf.clear();
    m_errBuf.clear();
    QDir().mkpath(QDir(m_config.dataDir).filePath(QStringLiteral("jd")));
    p->setWorkingDirectory(m_config.dataDir);
    connect(p, &QProcess::readyReadStandardOutput, this, &Ed3kJdResolver::onStdout);
    connect(p, &QProcess::readyReadStandardError, this, &Ed3kJdResolver::onStderr);
    connect(p, &QProcess::finished, this, &Ed3kJdResolver::onFinished);
    connect(p, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) {
            logEd3kWarning(QStringLiteral("hoster: resolver did not start: %1").arg(m_config.java));
            onFinished();
        }
    });
    p->start(m_config.java, arguments());
    logEd3kDebug(QStringLiteral("hoster: resolver starting"));
    m_stopping = false;
    request({{QStringLiteral("op"), QStringLiteral("hello")}}, false);
    if (!m_config.proxy.isEmpty())
        request({{QStringLiteral("op"), QStringLiteral("setProxy")}, {QStringLiteral("url"), m_config.proxy}}, false);
    if (!m_config.askPuzzles)
        request({{QStringLiteral("op"), QStringLiteral("setPuzzles")}, {QStringLiteral("mode"), QStringLiteral("never")}}, false);
    return true;
}

void Ed3kJdResolver::stop()
{
    m_idle.stop();
    if (!m_proc)
        return;
    m_stopping = true;
    QProcess* p = m_proc;
    p->closeWriteChannel();
    QTimer::singleShot(3000, p, [p] { p->kill(); });
}

void Ed3kJdResolver::onStdout()
{
    if (!m_proc)
        return;
    m_outBuf += m_proc->readAllStandardOutput();
    qsizetype nl;
    while ((nl = m_outBuf.indexOf('\n')) >= 0) {
        const QByteArray line = m_outBuf.left(nl);
        m_outBuf.remove(0, nl + 1);
        handleLine(line);
    }
    if (m_outBuf.size() > kMaxLine) {
        logEd3kWarning(QStringLiteral("hoster: resolver line too long, stopping it"));
        m_outBuf.clear();
        stop();
    }
}

void Ed3kJdResolver::onStderr()
{
    if (!m_proc)
        return;
    m_errBuf += m_proc->readAllStandardError();
    qsizetype nl;
    while ((nl = m_errBuf.indexOf('\n')) >= 0) {
        const QByteArray line = m_errBuf.left(nl).trimmed();
        m_errBuf.remove(0, nl + 1);
        // JD's own log may hold page content: only the driver's lines
        if (line.startsWith("resolver: "))
            logEd3kDebug(QStringLiteral("hoster: ") + QString::fromUtf8(line.mid(10)));
    }
    if (m_errBuf.size() > 64 * 1024)
        m_errBuf.clear();
}

void Ed3kJdResolver::onFinished()
{
    if (!m_proc)
        return;
    const bool expected = m_stopping;
    m_proc->deleteLater();
    m_proc = nullptr;
    m_stopping = false;
    if (!expected)
        logEd3kWarning(QStringLiteral("hoster: resolver ended"));
    failPending(QStringLiteral("io"));
    emit stateChanged();
}

void Ed3kJdResolver::onIdle()
{
    if (m_pending.empty()) {
        logEd3kDebug(QStringLiteral("hoster: resolver idle, stopping it"));
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
            logEd3kWarning(QStringLiteral("hoster: resolver speaks protocol %1, not %2").arg(proto).arg(kProtocol));
            m_installError = tr("Resolver plugins of another version");
            stop();
            return;
        }
        m_description = tr("JDownloader r%1, %2 hosters, %3 decrypters")
                            .arg(o.value(QStringLiteral("jdRevision")).toString())
                            .arg(o.value(QStringLiteral("hosts")).toInt())
                            .arg(o.value(QStringLiteral("crawlers")).toInt());
        emit stateChanged();
        return;
    }
    auto parsed = parseEvent(line);
    if (!parsed)
        return;
    const auto& [id, ev] = *parsed;
    // a puzzle of a link no open task names (a check) has id 0
    if (id == 0 && ev.kind != ResolverEvent::Kind::Puzzle)
        return;
    if (id != 0 && !m_pending.contains(id))
        return;
    if (ev.isFinal()) {
        m_pending.erase(id);
        if (m_pending.empty())
            m_idle.start();
    }
    emit taskEvent(id, ev);
}

} // namespace eMule::ed3k

#endif // EMULE_HAVE_HOSTER_JD
