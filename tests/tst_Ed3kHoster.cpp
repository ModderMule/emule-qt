/// @file tst_Ed3kHoster.cpp
/// @brief Hoster links (hoster.md) through the hoster join (src/ed3k/Ed3kHosterJoin) on a real
/// ed3k engine, with a fake resolver that writes its part file the way the JDownloader
/// sidecar does: a link without a root downloaded whole (§4.1) and moved to incoming, the
/// user's switch (§3), no plugin, a removed file. And the pure parts of the JD resolver and
/// the Java runtime lookup.

#include "utils/BackgroundFileWriter.h"
#include "TestHelpers.h"

#include "Ed3kHosterJoin.h"
#include "Ed3kResolver.h"
#include "Ed3kSession.h"
#ifdef EMULE_HAVE_HOSTER_JD
#include "Ed3kJavaRuntime.h"
#include "Ed3kJdResolver.h"
#include "Ed3kResolverUpdater.h"
#include "FakeHttpServer.h"
#include "archive/ArchiveReader.h"
#endif

#include <QCryptographicHash>
#include <QDir>
#include <QDateTime>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFileInfo>
#include <QHash>
#include <QLockFile>
#include <QScopeGuard>
#include <QSet>
#include <QSettings>
#include <QSignalSpy>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QStandardPaths>
#include <QProcess>

#include <array>
#include <QTimer>
#include <QUrl>

#include <functional>

#ifdef EMULE_HAVE_HOSTER_JD
#include <archive.h>
#ifndef Q_OS_WIN
#include <csignal>
#include <sys/types.h>
#endif
#include <archive_entry.h>
#endif

using namespace eMule;
using namespace eMule::ed3k;

namespace {

Ed3kSettings loopback(const QString& dir)
{
    Ed3kSettings s;
    s.configDir = dir;
    s.udpPort = 0;
    s.bindAddress = QHostAddress::LocalHost;
    s.network = QStringLiteral("ed3k test");
    s.forceOpen = true;
    return s;
}

QByteArray content(int size)
{
    QByteArray b(size, Qt::Uninitialized);
    for (int i = 0; i < size; ++i)
        b[i] = char((i * 31 + i / 7) & 0xff);
    return b;
}

/// One Ed3kHosterJoin::containerOpened().
struct Opened {
    quint64 token = 0;
    QString fileName;
    QList<ResolverLink> links;
    QString code;
    QString text;
};

/// A slot that keeps the first containerOpened() in @p out.
auto opened(std::optional<Opened>& out)
{
    return [&out](quint64 token, const QString& fileName, const QList<ResolverLink>& links, const QString& code,
                  const QString& text) {
        if (!out)
            out = Opened{token, fileName, links, code, text};
    };
}

/// Takes links of fake.example; writes <dir>/data.part in steps, then renames it to data.
class FakeResolver : public Ed3kResolver {
public:
    QByteArray data = content(700 * 1024 + 123);
    QString fileName = QStringLiteral("movie.bin");   ///< the name check() and the done file give
    bool gone = false;
    QSet<QString> goneUrls;      ///< check() says Gone for these
    QSet<QString> quotaUrls;     ///< open() says a quota wait of an hour for these
    QHash<QString, std::pair<QString, QString>> hashes;   ///< check() tells this checksum (type, hex)
    QSet<QString> hashFailUrls;  ///< open() fails like JD's own checksum check (code "hash")
    QStringList opened;          ///< urls open() was asked for
    int opens = 0;
    int checks = 0;
    int accountChecks = 0;
    int checkDelayMs = 0;        ///< check() answers this much later
    QSet<QString> hosts{QStringLiteral("fake.example")};   ///< hosters match() takes
    QSet<QString> premiumHosts;  ///< accountCheck() says premium for these
    QHash<QString, int> maxDownloads;   ///< accountCheck() tells JD's downloads at once
    QList<QStringList> siteGroups;   ///< one hoster's domains, main first (match() and sites())
    QHash<QString, int> runningChecks;   ///< check() by url host, until it answers
    QHash<QString, int> maxRunningChecks;
    int maxRunningAll = 0;
    /// resume: the first open stops (a hoster busy wait) once this many bytes are written
    qsizetype breakAt = 0;
    /// resume: the hoster cannot resume, JD starts over from 0
    bool refuseResume = false;
    std::uint64_t lastHave = 0;
    QByteArray lastPrefill;   ///< the front of the part file a resumed open found

    bool ready = true;           ///< false: like JD before the Java probe ends
    QStringList openSteps;       ///< open() sends these status events first
    bool holdAfterSteps = false; ///< open() stops after its steps (a plugin stuck on a captcha)
    QSet<QString> puzzleUrls;    ///< open() asks an image puzzle; no answer ends needsUser{skipped}
    std::map<std::uint64_t, TaskId> puzzles;   ///< asked, not answered yet
    std::uint64_t nextPuzzle = 100;
    int puzzlesAsked = 0;

    bool available() const override { return ready; }
    QString unavailableReason() const override { return ready ? QString() : QStringLiteral("No Java runtime"); }
    QString description() const override { return QStringLiteral("fake"); }
    void setReady(bool r)
    {
        ready = r;
        emit stateChanged();
    }

    TaskId match(const QString& url) override
    {
        const TaskId t = next++;
        ResolverEvent e;
        e.kind = ResolverEvent::Kind::Matched;
        const QString h = QUrl(url).host();
        if (hosts.contains(h)) {
            e.sites = group(h);
            e.host = e.sites.isEmpty() ? h : e.sites.first();
        }
        later(t, e);
        return t;
    }
    TaskId sites(const QString& host) override
    {
        const TaskId t = next++;
        ResolverEvent e;
        e.kind = ResolverEvent::Kind::Sites;
        e.sites = group(host);
        if (!e.sites.isEmpty())
            e.host = e.sites.first();
        later(t, e);
        return t;
    }
    TaskId check(const QString& url, const std::optional<ResolverAccount>&) override
    {
        ++checks;
        const TaskId t = next++;
        ResolverEvent e;
        e.kind = gone || goneUrls.contains(url) ? ResolverEvent::Kind::Gone : ResolverEvent::Kind::Found;
        e.name = fileName;
        e.size = std::uint64_t(data.size());
        if (const auto hs = hashes.find(url); hs != hashes.end())
            std::tie(e.hashType, e.hash) = *hs;
        const QString h = QUrl(url).host();
        maxRunningChecks[h] = std::max(maxRunningChecks.value(h), ++runningChecks[h]);
        int all = 0;
        for (int n : std::as_const(runningChecks))
            all += n;
        maxRunningAll = std::max(maxRunningAll, all);
        QTimer::singleShot(checkDelayMs, this, [this, t, e, h] {
            --runningChecks[h];
            emit taskEvent(t, e);
        });
        return t;
    }
    QStringList expanded;   ///< expand() urls, in order
    TaskId expand(const QString& url) override
    {
        const TaskId t = next++;
        expanded << url;
        ResolverEvent e;
        e.kind = ResolverEvent::Kind::Links;
        // a DummyCNL link with "bad" in it: nothing decrypts, as with a wrong key
        if (!url.contains(QStringLiteral("dummycnl")) || !QString::fromLatin1(QByteArray::fromHex(url.section(u'#', 1).toLatin1())).contains(QStringLiteral("bad")))
            e.links = {{QStringLiteral("https://fake.example/f/a"), QStringLiteral("a"), QStringLiteral("fake.example"), 1}};
        later(t, e);
        return t;
    }
    QByteArray containerBytes;   ///< what the last openContainer() file held
    QString containerPath;
    /// openContainer(): "limit" in the file fails like the DLC service; else two packages
    TaskId openContainer(const QString& path) override
    {
        const TaskId t = next++;
        containerPath = path;
        QFile f(path);
        containerBytes = f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
        ResolverEvent e;
        if (containerBytes == "limit") {
            e.kind = ResolverEvent::Kind::Failed;
            e.code = QStringLiteral("limit");
            e.text = QStringLiteral("Server claims: DLC Limit reached.");
        } else {
            e.kind = ResolverEvent::Kind::Links;
            ResolverLink a{QStringLiteral("https://fake.example/c/1"), QStringLiteral("one.rar"), {}, 0};
            a.package = QStringLiteral("Pkg A");
            a.passwords = {QStringLiteral("pw1"), QStringLiteral("pw2")};
            ResolverLink b{QStringLiteral("https://fake.example/c/2"), {}, {}, 0};
            b.package = QStringLiteral("Pkg B");
            e.links = {a, b};
        }
        later(t, e);
        return t;
    }
    TaskId open(const QString& url, const std::optional<ResolverAccount>&, const QString& dir, std::uint64_t have) override
    {
        ++opens;
        opened << url;
        const TaskId t = next++;
        if (puzzleUrls.contains(url)) {
            ++puzzlesAsked;
            ResolverEvent pz;
            pz.kind = ResolverEvent::Kind::Puzzle;
            pz.puzzle = nextPuzzle++;
            pz.host = QUrl(url).host();
            pz.image = QByteArray("img");
            pz.timeoutS = 300;
            puzzles[pz.puzzle] = t;
            later(t, pz);
            return t;
        }
        if (hashFailUrls.contains(url)) {
            ResolverEvent f;
            f.kind = ResolverEvent::Kind::Failed;
            f.code = QStringLiteral("hash");
            f.text = QStringLiteral("md5");
            later(t, f);
            return t;
        }
        if (quotaUrls.contains(url)) {
            ResolverEvent w;
            w.kind = ResolverEvent::Kind::Wait;
            w.text = QStringLiteral("quota");
            w.seconds = 3600;
            later(t, w);
            return t;
        }
        for (const QString& step : std::as_const(openSteps)) {
            ResolverEvent st;
            st.kind = ResolverEvent::Kind::Status;
            st.text = step;
            later(t, st);
        }
        if (holdAfterSteps)
            return t;
        const QString part = QDir(dir).filePath(QStringLiteral("data.part"));
        lastHave = have;
        lastPrefill.clear();
        if (have > 0) {
            QFile f(part);
            if (f.open(QIODevice::ReadOnly))
                lastPrefill = f.readAll();
        } else {
            QFile::remove(part);
        }
        const qsizetype from = have > 0 && !refuseResume ? qsizetype(have) : 0;
        writeStep(t, part, from, have > 0 ? std::optional<std::uint64_t>(std::uint64_t(from)) : std::nullopt);
        return t;
    }
    TaskId accountCheck(const QString& host, const ResolverAccount&) override
    {
        ++accountChecks;
        const TaskId t = next++;
        ResolverEvent e;
        e.kind = ResolverEvent::Kind::Account;
        e.valid = true;
        e.premium = premiumHosts.contains(host);
        e.maxDownloads = maxDownloads.value(host);
        later(t, e);
        return t;
    }
    void answerPuzzle(std::uint64_t puzzle, const QString& text) override
    {
        const auto it = puzzles.find(puzzle);
        if (it == puzzles.end())
            return;
        ResolverEvent e;
        e.kind = ResolverEvent::Kind::NeedsUser;
        e.text = text.isEmpty() ? QStringLiteral("skipped") : QStringLiteral("wrong");
        later(it->second, e);
        puzzles.erase(it);
    }
    TaskId setSolverAccounts(const QList<SolverAccount>&) override
    {
        const TaskId t = next++;
        ResolverEvent e;
        e.kind = ResolverEvent::Kind::Solvers;
        later(t, e);
        return t;
    }
    TaskId solveTest(const QString&, const QString&, const QByteArray&) override
    {
        const TaskId t = next++;
        ResolverEvent e;
        e.code = QStringLiteral("noSolver");
        later(t, e);
        return t;
    }
    void cancel(TaskId) override {}
    void setAskPuzzles(bool) override {}
    void setProxy(const QString&) override {}

private:
    TaskId next = 1;

    [[nodiscard]] QStringList group(const QString& host) const
    {
        for (const QStringList& g : siteGroups)
            if (g.contains(host))
                return g;
        return {};
    }

    void later(TaskId t, const ResolverEvent& e, int delayMs = 0)
    {
        QTimer::singleShot(delayMs, this, [this, t, e] { emit taskEvent(t, e); });
    }
    /// 300 KiB per step, a progress event each, then done (the part renamed, as JD does);
    /// @p from: told with the first progress of a resumed open
    void writeStep(TaskId t, const QString& part, qsizetype at, std::optional<std::uint64_t> from = std::nullopt)
    {
        QTimer::singleShot(5, this, [this, t, part, at, from] {
            const qsizetype n = std::min<qsizetype>(300 * 1024, data.size() - at);
            QFile f(part);
            if (!f.open(QIODevice::ReadWrite) || !f.seek(at) || f.write(data.constData() + at, n) != n)
                return;
            f.close();
            ResolverEvent e;
            e.kind = ResolverEvent::Kind::Progress;
            e.bytes = std::uint64_t(at + n);
            e.total = std::uint64_t(data.size());
            e.path = part;
            e.from = from;
            emit taskEvent(t, e);
            if (opens == 1 && breakAt > 0 && at + n >= breakAt) {
                // the join reads what is on disk first, then the hoster says busy
                QTimer::singleShot(500, this, [this, t] {
                    ResolverEvent w;
                    w.kind = ResolverEvent::Kind::Wait;
                    w.text = QStringLiteral("busy");
                    w.seconds = 1;
                    emit taskEvent(t, w);
                });
                return;
            }
            if (at + n < data.size()) {
                writeStep(t, part, at + n);
                return;
            }
            const QString done = part.chopped(5);
            QFile::rename(part, done);
            ResolverEvent d;
            d.kind = ResolverEvent::Kind::Done;
            d.size = std::uint64_t(data.size());
            d.path = done;
            d.name = fileName;
            emit taskEvent(t, d);
        });
    }
};

/// The item of @p url; 0: none.
quint32 idOf(const Ed3kHosterJoin& join, const QString& url)
{
    for (const auto& it : join.items())
        if (it.url == url)
            return it.id;
    return 0;
}

/// An unfinished rooted item as a stopped node leaves it: the first @p blocks blocks of
/// @p data in <dir>/<key>.hoster.part, its state beside it.
bool writeHeld(const QString& dir, const QString& key, const QString& url, const QByteArray& data, int blocks,
               bool paused = false)
{
    QDir().mkpath(dir);
    QFile part(QDir(dir).filePath(key + QStringLiteral(".hoster.part")));
    if (!part.open(QIODevice::WriteOnly) || part.write(data.left(blocks * 256 * 1024)) < 0)
        return false;
    part.close();
    // append, not QJsonArray{QJsonArray{..}}: MSVC takes that as a copy, giving [0,n]
    QJsonArray have;
    have.append(QJsonArray{0, blocks});
    const QJsonObject o{{QStringLiteral("format"), QStringLiteral("ed3k-hoster-met")},
                        {QStringLiteral("v"), 1},
                        {QStringLiteral("url"), url},
                        {QStringLiteral("host"), QStringLiteral("fake.example")},
                        {QStringLiteral("name"), QStringLiteral("held.bin")},
                        {QStringLiteral("size"), qint64(data.size())},
                        {QStringLiteral("root"), QString(64, QLatin1Char('a'))},
                        {QStringLiteral("state"), int(paused ? Ed3kHosterJoin::State::Paused : Ed3kHosterJoin::State::Downloading)},
                        {QStringLiteral("mirrors"), QJsonArray{QString(url + QStringLiteral("-mirror"))}},
                        {QStringLiteral("pkgName"), QStringLiteral("Pack")},
                        {QStringLiteral("have"), have}};
    QFile met(part.fileName() + QStringLiteral(".met"));
    return met.open(QIODevice::WriteOnly) && met.write(QJsonDocument(o).toJson()) > 0;
}

struct Rig {
    QTemporaryDir tmp;
    Ed3kSession session;
    FakeResolver resolver;
    std::unique_ptr<Ed3kHosterJoin> join;
    Ed3kHosterPrefs prefs;

    bool start(bool enabled)
    {
        if (!session.start(loopback(tmp.filePath(QStringLiteral("cfg")))))
            return false;
        join = std::make_unique<Ed3kHosterJoin>(session, tmp.filePath(QStringLiteral("cfg")));
        join->setResolver(&resolver);
        prefs.enabled = enabled;
        join->setConfig(prefs, tmp.filePath(QStringLiteral("temp")), tmp.filePath(QStringLiteral("incoming")));
        join->start();
        return true;
    }
    [[nodiscard]] Ed3kHosterJoin::State state(quint32 id) const { return join->item(id)->state; }

    /// A complete item whose own link was vouched for.
    quint32 completeOne(const QString& url)
    {
        const auto ids = join->addLinks(url);
        if (ids.size() != 1)
            return 0;
        if (!QTest::qWaitFor([&] { return state(ids[0]) == Ed3kHosterJoin::State::Complete; }, 60000))
            return 0;
        if (!QTest::qWaitFor([&] { return !join->sharedLinks(ids[0]).isEmpty(); }, 10000))
            return 0;
        return ids[0];
    }
    /// Stops the join, lets @p edit change each saved link, starts a new one with prefs.
    bool restart(const std::function<void(QJsonObject&)>& edit = {})
    {
        join->stop();
        join.reset();
        eMule::BackgroundFileWriter::instance().flush();   // items.json is queued (report 43)
        QString itemsPath;
        for (QDirIterator it(tmp.path(), {QStringLiteral("items.json")}, QDir::Files, QDirIterator::Subdirectories); it.hasNext();)
            itemsPath = it.next();
        if (itemsPath.isEmpty())
            return false;
        if (edit) {
            QFile items(itemsPath);
            if (!items.open(QIODevice::ReadOnly))
                return false;
            QJsonArray list = QJsonDocument::fromJson(items.readAll()).array();
            items.close();
            for (int i = 0; i < list.size(); ++i) {
                QJsonObject o = list.at(i).toObject();
                QJsonArray links = o.value(QStringLiteral("links")).toArray();
                for (int k = 0; k < links.size(); ++k) {
                    QJsonObject l = links.at(k).toObject();
                    edit(l);
                    links[k] = l;
                }
                o[QStringLiteral("links")] = links;
                list[i] = o;
            }
            if (!items.open(QIODevice::WriteOnly | QIODevice::Truncate))
                return false;
            items.write(QJsonDocument(list).toJson());
        }
        join = std::make_unique<Ed3kHosterJoin>(session, tmp.filePath(QStringLiteral("cfg")));
        join->setResolver(&resolver);
        join->setConfig(prefs, tmp.filePath(QStringLiteral("temp")), tmp.filePath(QStringLiteral("incoming")));
        join->start();
        return true;
    }
};

#ifdef EMULE_HAVE_HOSTER_JD
/// A zip of @p members (name, data), as resolver.zip is
bool writeZip(const QString& path, const QList<QPair<QString, QByteArray>>& members)
{
    auto* a = archive_write_new();
    archive_write_set_format_zip(a);
    if (archive_write_open_filename(a, path.toUtf8().constData()) != ARCHIVE_OK) {
        archive_write_free(a);
        return false;
    }
    bool ok = true;
    for (const auto& [name, data] : members) {
        auto* entry = archive_entry_new();
        archive_entry_set_pathname(entry, name.toUtf8().constData());
        archive_entry_set_size(entry, data.size());
        archive_entry_set_filetype(entry, AE_IFREG);
        archive_entry_set_perm(entry, 0644);
        ok = ok && archive_write_header(a, entry) == ARCHIVE_OK
             && archive_write_data(a, data.constData(), size_t(data.size())) == data.size();
        archive_entry_free(entry);
    }
    archive_write_close(a);
    archive_write_free(a);
    return ok;
}

/// A resolver.zip of the shipped layout, with stand-in contents
bool writeResolverZip(const QString& path, const QByteArray& revision = "53779", const QByteArray& api = {})
{
    QList<QPair<QString, QByteArray>> members{{QStringLiteral("resolver/Core.jar"), "PK"},
                                              {QStringLiteral("resolver/jd/plugins/hoster/ArchiveOrg.class"), "cafe"},
                                              {QStringLiteral("resolver/libs/js.jar"), "PK"},
                                              {QStringLiteral("resolver/revision.txt"), revision}};
    if (!api.isEmpty())
        members.append({QStringLiteral("resolver/api.txt"), api});
    return writeZip(path, members);
}

QByteArray fileSha256(const QString& path)
{
    QFile f(path);
    QCryptographicHash h(QCryptographicHash::Sha256);
    return f.open(QIODevice::ReadOnly) && h.addData(&f) ? h.result().toHex() : QByteArray();
}
#endif

/// A saved link last checked 100 h ago: past the default 72 h.
/// A downloader @p node in site 10.0.@p site.0/24 reports @p code for @p url, handed out with
/// item @p id's file; site 0: none (a virtual connection).
void report(Rig& r, quint32 id, const QString& url, quint8 node, std::uint32_t code, int site = -1)
{
    ::ed3k::event::HttpResults ev;
    const QByteArray root = QByteArray::fromHex(r.join->item(id)->rootHex.toLatin1());
    std::copy_n(reinterpret_cast<const std::uint8_t*>(root.constData()), std::min<qsizetype>(root.size(), 32), ev.root.bytes.begin());
    ev.node.bytes[0] = node;
    if (site < 0)
        site = node;
    if (site > 0)
        ev.site = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff, 10, 0, std::uint8_t(site), 0};
    const auto uid = ::ed3k::linkUrlId(url.toStdString());
    for (const auto b : uid)
        ev.results.push_back(std::byte(b));
    for (int i = 0; i < 4; ++i)
        ev.results.push_back(std::byte((code >> (8 * i)) & 0xff));
    for (int i = 0; i < 8; ++i)
        ev.results.push_back(std::byte(0));
    emit r.session.eventPolled(::ed3k::Event{ev}, nullptr);
}

void stale(QJsonObject& l)
{
    l[QStringLiteral("checkedAt")] = QDateTime::currentMSecsSinceEpoch() - 100LL * 3600 * 1000;
}

/// linkChecked() results by url.
struct CheckSpy {
    QHash<QString, Ed3kHosterJoin::LinkCheck> last;
    int count = 0;
    explicit CheckSpy(Ed3kHosterJoin& join)
    {
        QObject::connect(&join, &Ed3kHosterJoin::linkChecked, &join,
                         [this](const QString& url, const Ed3kHosterJoin::LinkCheck& c) {
                             last[url] = c;
                             ++count;
                         });
    }
    [[nodiscard]] Ed3kHosterJoin::LinkStatus status(const QString& url) const
    {
        return last.contains(url) ? last[url].status : Ed3kHosterJoin::LinkStatus::Checking;
    }
};

} // namespace

class tst_Ed3kHoster : public QObject {
    Q_OBJECT

private slots:
    /// a container's bytes go to a scratch file the resolver reads; the links come back, the file goes
    void container_opensLinksAndDropsScratch()
    {
        Rig r;
        QVERIFY(r.start(true));
        std::optional<Opened> got;
        connect(r.join.get(), &Ed3kHosterJoin::containerOpened, this, opened(got));
        const quint64 token = r.join->openContainer(QByteArray("dlc bytes"), QStringLiteral("../evil/My Set.dlc"));
        QVERIFY(token != 0);
        QTRY_VERIFY(got);
        QCOMPARE(got->token, token);
        QCOMPARE(got->fileName, QStringLiteral("My Set.dlc"));
        QCOMPARE(got->links.size(), 2);
        QCOMPARE(got->links[0].package, QStringLiteral("Pkg A"));
        QCOMPARE(got->links[0].passwords, (QStringList{QStringLiteral("pw1"), QStringLiteral("pw2")}));
        QVERIFY(got->code.isEmpty());
        QCOMPARE(r.resolver.containerBytes, QByteArray("dlc bytes"));
        // the plugin goes by the extension; no path from the GUI's name gets through
        QVERIFY(r.resolver.containerPath.endsWith(QStringLiteral("My Set.dlc")));
        QVERIFY(r.resolver.containerPath.startsWith(r.tmp.filePath(QStringLiteral("temp"))));
        QVERIFY(!QFileInfo::exists(r.resolver.containerPath));
    }

    void container_failureCarriesCode()
    {
        Rig r;
        QVERIFY(r.start(true));
        std::optional<Opened> got;
        connect(r.join.get(), &Ed3kHosterJoin::containerOpened, this, opened(got));
        QVERIFY(r.join->openContainer(QByteArray("limit"), QStringLiteral("x.dlc")) != 0);
        QTRY_VERIFY(got);
        QVERIFY(got->links.isEmpty());
        QCOMPARE(got->code, QStringLiteral("limit"));
        QVERIFY(got->text.contains(QStringLiteral("Limit")));
    }

    /// Click'n'Load: the encrypted list goes to JD's DummyCNL as its JSON in hex; its links
    /// come back after the plain ones, with the post's package and passwords
    void clickNLoad_decryptsThroughDummyCnl()
    {
        Rig r;
        QVERIFY(r.start(true));
        std::optional<Opened> got;
        connect(r.join.get(), &Ed3kHosterJoin::clickNLoadOpened, this, opened(got));
        Ed3kHosterJoin::ClickNLoad post;
        post.urls = QStringLiteral("https://fake.example/f/plain\r\nnot a link");
        post.crypted = QStringLiteral("AAAA");
        post.jk = QStringLiteral("function f(){ return '31323334353637383930313233343536';}");
        post.package = QStringLiteral("Set");
        post.passwords = {QStringLiteral("pw"), QStringLiteral("pw"), QStringLiteral(" ")};
        post.source = QStringLiteral("https://links.example/folder/1");
        const quint64 token = r.join->openClickNLoad(post);
        QVERIFY(token != 0);
        QTRY_VERIFY(got);
        QCOMPARE(got->token, token);
        QCOMPARE(got->fileName, post.source);
        QVERIFY(got->code.isEmpty());
        QCOMPARE(got->links.size(), 2);
        QCOMPARE(got->links[0].url, QStringLiteral("https://fake.example/f/plain"));
        QCOMPARE(got->links[1].url, QStringLiteral("https://fake.example/f/a"));
        for (const ResolverLink& l : std::as_const(got->links)) {
            QCOMPARE(l.package, QStringLiteral("Set"));
            QCOMPARE(l.passwords, QStringList{QStringLiteral("pw")});
        }
        QCOMPARE(r.resolver.expanded.size(), 1);
        const QString url = r.resolver.expanded.first();
        QVERIFY(url.startsWith(QStringLiteral("https://dummycnl.jdownloader.org/#")));
        const QJsonObject o = QJsonDocument::fromJson(QByteArray::fromHex(url.section(u'#', 1).toLatin1())).object();
        QCOMPARE(o.value(QStringLiteral("crypted")).toString(), post.crypted);
        QCOMPARE(o.value(QStringLiteral("jk")).toString(), post.jk);
        QCOMPARE(o.value(QStringLiteral("source")).toString(), post.source);
        QVERIFY(!o.contains(QStringLiteral("k")));
        // no item yet: the user picks in the GUI
        QVERIFY(r.join->items().isEmpty());
    }

    /// plain links only: no resolver task, the answer still after the token
    void clickNLoad_plainLinksOnly()
    {
        Rig r;
        QVERIFY(r.start(true));
        std::optional<Opened> got;
        connect(r.join.get(), &Ed3kHosterJoin::clickNLoadOpened, this, opened(got));
        Ed3kHosterJoin::ClickNLoad post;
        post.urls = QStringLiteral("https://fake.example/f/1 https://fake.example/f/2");
        QVERIFY(r.join->openClickNLoad(post) != 0);
        QVERIFY(!got);
        QTRY_VERIFY(got);
        QCOMPARE(got->links.size(), 2);
        QVERIFY(r.resolver.expanded.isEmpty());
    }

    /// a wrong key decrypts nothing: "invalid", unless plain links came along
    void clickNLoad_nothingDecrypted()
    {
        Rig r;
        QVERIFY(r.start(true));
        std::optional<Opened> got;
        connect(r.join.get(), &Ed3kHosterJoin::clickNLoadOpened, this, opened(got));
        Ed3kHosterJoin::ClickNLoad post;
        post.crypted = QStringLiteral("bad");
        post.k = QStringLiteral("00");
        QVERIFY(r.join->openClickNLoad(post) != 0);
        QTRY_VERIFY(got);
        QVERIFY(got->links.isEmpty());
        QCOMPARE(got->code, QStringLiteral("invalid"));
        got.reset();
        post.urls = QStringLiteral("https://fake.example/f/9");
        QVERIFY(r.join->openClickNLoad(post) != 0);
        QTRY_VERIFY(got);
        QCOMPARE(got->links.size(), 1);
        QVERIFY(got->code.isEmpty());
    }

    /// hosters off, nothing in it, or a page posting over and over: refused at once
    void clickNLoad_refused()
    {
        Rig off;
        QVERIFY(off.start(false));
        Ed3kHosterJoin::ClickNLoad post;
        post.urls = QStringLiteral("https://fake.example/f/1");
        QCOMPARE(off.join->openClickNLoad(post), quint64(0));
        Rig r;
        QVERIFY(r.start(true));
        QCOMPARE(r.join->openClickNLoad({}), quint64(0));
        Ed3kHosterJoin::ClickNLoad noKey;
        noKey.crypted = QStringLiteral("AAAA");
        QCOMPARE(r.join->openClickNLoad(noKey), quint64(0));
        Ed3kHosterJoin::ClickNLoad enc;
        enc.crypted = QStringLiteral("AAAA");
        enc.k = QStringLiteral("00");
        for (int i = 0; i < 4; ++i)
            QVERIFY(r.join->openClickNLoad(enc) != 0);
        QCOMPARE(r.join->openClickNLoad(enc), quint64(0));
    }

    /// hosters off, no bytes or too many: refused at once, nothing follows
    void container_refused()
    {
        Rig r;
        QVERIFY(r.start(false));
        QCOMPARE(r.join->openContainer(QByteArray("x"), QStringLiteral("x.dlc")), quint64(0));
        Rig on;
        QVERIFY(on.start(true));
        QCOMPARE(on.join->openContainer({}, QStringLiteral("x.dlc")), quint64(0));
        QCOMPARE(on.join->openContainer(QByteArray(5 * 1024 * 1024, 'a'), QStringLiteral("x.dlc")), quint64(0));
    }

    /// a container's archive passwords stay with its items, across a restart
    void addMirrored_keepsPasswords()
    {
        Rig r;
        QVERIFY(r.start(true));
        r.resolver.hosts.clear();   // no plugin: the items stay, nothing downloads
        const QStringList pws{QStringLiteral("pw1"), QStringLiteral("pw2")};
        const auto ids = r.join->addMirrored({{QStringLiteral("https://nohost.example/f/1")}}, QStringLiteral("Pkg"), 0, pws);
        QCOMPARE(ids.size(), 1);
        QCOMPARE(r.join->item(ids[0])->passwords, pws);
        QCOMPARE(r.join->item(ids[0])->package, QStringLiteral("Pkg"));
        r.join->stop();
        r.join = std::make_unique<Ed3kHosterJoin>(r.session, r.tmp.filePath(QStringLiteral("cfg")));
        r.join->setResolver(&r.resolver);
        r.join->setConfig(r.prefs, r.tmp.filePath(QStringLiteral("temp")), r.tmp.filePath(QStringLiteral("incoming")));
        r.join->start();
        const auto items = r.join->items();
        QCOMPARE(items.size(), 1);
        QCOMPARE(items.first().passwords, pws);
    }

    void linksIn_takesHttpLinksOnly()
    {
        const QStringList l = Ed3kHosterJoin::linksIn(QStringLiteral(
            "see https://a.example/x and\nhttp://b.example/y?z=1\nftp://c.example/f https://a.example/x"));
        QCOMPARE(l, (QStringList{QStringLiteral("https://a.example/x"), QStringLiteral("http://b.example/y?z=1")}));
    }

    /// §4.1: no ALIAS record, the link downloaded whole, its root computed, the file in incoming
    void rootless_downloadsWholeAndCompletes()
    {
        Rig r;
        QVERIFY(r.start(true));
        const auto ids = r.join->addLinks(QStringLiteral("https://fake.example/f/1"));
        QCOMPARE(ids.size(), 1);
        QTRY_COMPARE_WITH_TIMEOUT(r.state(ids[0]), Ed3kHosterJoin::State::Complete, 60000);
        const auto item = *r.join->item(ids[0]);
        QCOMPARE(item.name, QStringLiteral("movie.bin"));
        QCOMPARE(item.size, std::uint64_t(r.resolver.data.size()));
        QVERIFY(!item.rootHex.isEmpty());
        QVERIFY(item.file.startsWith(r.tmp.filePath(QStringLiteral("incoming"))));
        QFile f(item.file);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), r.resolver.data);
        QCOMPARE(r.resolver.checks, 1);
        QCOMPARE(r.resolver.opens, 1);
        // done: nothing of it left in Temp
        QTRY_VERIFY(QDir(r.tmp.filePath(QStringLiteral("temp"))).entryList({QStringLiteral("*.hoster.part*")}, QDir::Files).isEmpty());
        // the same link again is not a second item
        QVERIFY(r.join->addLinks(QStringLiteral("https://fake.example/f/1")).isEmpty());
    }

    /// the digests the hoster checksum check computes ("123456789" check values)
    void fileDigest_knownTypes()
    {
        QTemporaryDir tmp;
        const QString p = tmp.filePath(QStringLiteral("v"));
        QFile f(p);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("123456789");
        f.close();
        QCOMPARE(Ed3kHosterJoin::fileDigest(p, QStringLiteral("crc32")), QStringLiteral("cbf43926"));
        QCOMPARE(Ed3kHosterJoin::fileDigest(p, QStringLiteral("crc32c")), QStringLiteral("e3069283"));
        QCOMPARE(Ed3kHosterJoin::fileDigest(p, QStringLiteral("md5")), QStringLiteral("25f9e794323b453885f5181f1b624d0b"));
        QCOMPARE(Ed3kHosterJoin::fileDigest(p, QStringLiteral("sha1")), QStringLiteral("f7c3bc1d808e04732adf679965ccc34ca7ae3441"));
        QVERIFY(Ed3kHosterJoin::fileDigest(p, QStringLiteral("whirlpool")).isEmpty());
        QVERIFY(Ed3kHosterJoin::fileDigest(tmp.filePath(QStringLiteral("none")), QStringLiteral("md5")).isEmpty());
    }

    /// the hoster's checksum matches the whole file: complete and vouched as before
    void hash_matchCompletes()
    {
        Rig r;
        const QString u = QStringLiteral("https://fake.example/h/ok");
        r.resolver.hashes[u] = {QStringLiteral("sha256"),
                                QString::fromLatin1(QCryptographicHash::hash(r.resolver.data, QCryptographicHash::Sha256).toHex().toUpper())};
        QVERIFY(r.start(true));
        const quint32 id = r.completeOne(u);
        QVERIFY(id);
        QCOMPARE(r.join->item(id)->reason, Ed3kHosterJoin::Reason::None);
        QCOMPARE(r.join->sharedLinks(id).size(), 1);
    }

    /// a CRC32 told without its leading zeros still matches
    void hash_crcWithoutLeadingZeros()
    {
        Rig r;
        const QString u = QStringLiteral("https://fake.example/h/crc");
        QTemporaryDir tmp;
        QFile f(tmp.filePath(QStringLiteral("d")));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(r.resolver.data);
        f.close();
        QString crc = Ed3kHosterJoin::fileDigest(f.fileName(), QStringLiteral("crc32"));
        // force a leading zero case: told as the number, not 8 digits
        crc = QString::number(crc.toUInt(nullptr, 16), 16);
        r.resolver.hashes[u] = {QStringLiteral("crc32"), crc};
        QVERIFY(r.start(true));
        QVERIFY(r.completeOne(u));
    }

    /// the file is not what the hoster's checksum says: dropped, not vouched, the next mirror
    void hash_mismatchTakesNextMirror()
    {
        Rig r;
        const QString m1 = QStringLiteral("https://fake.example/h/bad");
        const QString m2 = QStringLiteral("https://fake.example/h/good");
        r.resolver.hashes[m1] = {QStringLiteral("md5"), QStringLiteral("00000000000000000000000000000000")};
        QVERIFY(r.start(true));
        const auto ids = r.join->addMirrored({{m1, m2}});
        QCOMPARE(ids.size(), 1);
        QTRY_COMPARE_WITH_TIMEOUT(r.state(ids[0]), Ed3kHosterJoin::State::Complete, 60000);
        QCOMPARE(r.join->item(ids[0])->url, m2);
        QTRY_VERIFY(!r.join->sharedLinks(ids[0]).isEmpty());
        const auto links = r.join->sharedLinks(ids[0]);
        QCOMPARE(links.size(), 1);
        QCOMPARE(links[0].url, m2);
        QFile f(r.join->item(ids[0])->file);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), r.resolver.data);
    }

    /// no mirror left: Failed with HashMismatch, the bytes gone, nothing in incoming
    void hash_mismatchFails()
    {
        Rig r;
        const QString u = QStringLiteral("https://fake.example/h/bad1");
        r.resolver.hashes[u] = {QStringLiteral("sha1"), QStringLiteral("0000000000000000000000000000000000000000")};
        QVERIFY(r.start(true));
        const auto ids = r.join->addLinks(u);
        QCOMPARE(ids.size(), 1);
        QTRY_COMPARE_WITH_TIMEOUT(r.state(ids[0]), Ed3kHosterJoin::State::Failed, 60000);
        const auto item = *r.join->item(ids[0]);
        QCOMPARE(item.reason, Ed3kHosterJoin::Reason::HashMismatch);
        QCOMPARE(item.reasonArg, QStringLiteral("sha1"));
        QCOMPARE(item.done, std::uint64_t{0});
        QVERIFY(item.rootHex.isEmpty());
        QVERIFY(!QFile::exists(item.file));
        QVERIFY(r.join->sharedLinks(ids[0]).isEmpty());
        QVERIFY(QDir(r.tmp.filePath(QStringLiteral("incoming"))).entryList(QDir::Files).isEmpty());
    }

    /// JD's own checksum check failed (code "hash"): the same as a mismatch here
    void hash_resolverCheckFails()
    {
        Rig r;
        const QString u = QStringLiteral("https://fake.example/h/jd");
        r.resolver.hashFailUrls = {u};
        QVERIFY(r.start(true));
        const auto ids = r.join->addLinks(u);
        QCOMPARE(ids.size(), 1);
        QTRY_COMPARE_WITH_TIMEOUT(r.state(ids[0]), Ed3kHosterJoin::State::Failed, 60000);
        QCOMPARE(r.join->item(ids[0])->reason, Ed3kHosterJoin::Reason::HashMismatch);
        QCOMPARE(r.resolver.opens, 1);
    }

    /// the ed3k network never finds a peer: no ALIAS lookup, straight to the hoster (#88)
    void noPeers_skipsLookupAndCompletes()
    {
        Rig r;
        QVERIFY(r.start(true));
        QCOMPARE(r.session.contactCount(), std::uint64_t{0});
        QSet<int> seen;
        QObject::connect(r.join.get(), &Ed3kHosterJoin::itemChanged, r.join.get(),
                         [&](quint32 id) { seen.insert(int(r.state(id))); });
        const auto ids = r.join->addLinks(QStringLiteral("https://fake.example/f/np"));
        QCOMPARE(ids.size(), 1);
        QTRY_COMPARE_WITH_TIMEOUT(r.state(ids[0]), Ed3kHosterJoin::State::Complete, 60000);
        QVERIFY(!seen.contains(int(Ed3kHosterJoin::State::Looking)));
        QVERIFY(!r.join->item(ids[0])->rootHex.isEmpty());
    }

    /// §3: no puzzle, timer or account without the user's switch: nothing is opened
    void switchedOff_needsUser()
    {
        Rig r;
        QVERIFY(r.start(false));
        const auto ids = r.join->addLinks(QStringLiteral("https://fake.example/f/2"));
        QTRY_COMPARE_WITH_TIMEOUT(r.state(ids[0]), Ed3kHosterJoin::State::NeedsUser, 10000);
        QCOMPARE(r.resolver.opens, 0);
        // switched on: it goes on by itself
        r.prefs.enabled = true;
        r.join->setConfig(r.prefs, r.tmp.filePath(QStringLiteral("temp")), r.tmp.filePath(QStringLiteral("incoming")));
        QTRY_COMPARE_WITH_TIMEOUT(r.state(ids[0]), Ed3kHosterJoin::State::Complete, 60000);
    }

    /// resolver not ready yet (Java still probed): the item goes on once it is, also after a restart
    /// the resolver's step before bytes flow shows as the reason of Downloading
    void status_stepShownWhileDownloading()
    {
        Rig r;
        r.resolver.openSteps = {QStringLiteral("link"), QStringLiteral("puzzle")};
        r.resolver.holdAfterSteps = true;
        QVERIFY(r.start(true));
        const auto ids = r.join->addLinks(QStringLiteral("https://fake.example/f/st"));
        QTRY_COMPARE_WITH_TIMEOUT(r.join->item(ids[0])->reasonArg, QStringLiteral("puzzle"), 60000);
        QCOMPARE(r.state(ids[0]), Ed3kHosterJoin::State::Downloading);
        QCOMPARE(r.join->item(ids[0])->reason, Ed3kHosterJoin::Reason::HosterStep);
    }

    /// "transfer" clears the step; the download completes
    void status_transferClearsStep()
    {
        Rig r;
        r.resolver.openSteps = {QStringLiteral("hoster"), QStringLiteral("transfer")};
        QVERIFY(r.start(true));
        QStringList steps;
        QObject::connect(r.join.get(), &Ed3kHosterJoin::itemChanged, r.join.get(), [&](quint32 id) {
            if (const auto it = r.join->item(id); it && it->reason == Ed3kHosterJoin::Reason::HosterStep)
                steps << it->reasonArg;
        });
        const auto ids = r.join->addLinks(QStringLiteral("https://fake.example/f/tr"));
        QTRY_COMPARE_WITH_TIMEOUT(r.state(ids[0]), Ed3kHosterJoin::State::Complete, 60000);
        QVERIFY(steps.contains(QStringLiteral("hoster")));
        QCOMPARE(r.join->item(ids[0])->reason, Ed3kHosterJoin::Reason::None);
    }

    void resolverNotReady_retriedWhenReady()
    {
        Rig r;
        r.resolver.ready = false;
        QVERIFY(r.start(true));
        const auto ids = r.join->addLinks(QStringLiteral("https://fake.example/f/nr"));
        QCOMPARE(r.state(ids[0]), Ed3kHosterJoin::State::NoResolver);
        QCOMPARE(r.join->item(ids[0])->reason, Ed3kHosterJoin::Reason::ResolverNotReady);
        // a signal while still not ready changes nothing
        r.resolver.setReady(false);
        QCOMPARE(r.state(ids[0]), Ed3kHosterJoin::State::NoResolver);
        r.resolver.setReady(true);
        QTRY_COMPARE_WITH_TIMEOUT(r.state(ids[0]), Ed3kHosterJoin::State::Complete, 60000);
    }

    /// saved as not ready, the resolver ready by the next start: start() takes it up
    void resolverNotReady_retriedOnStart()
    {
        Rig r;
        r.resolver.ready = false;
        QVERIFY(r.start(true));
        const auto ids = r.join->addLinks(QStringLiteral("https://fake.example/f/nr2"));
        QCOMPARE(r.join->item(ids[0])->reason, Ed3kHosterJoin::Reason::ResolverNotReady);
        r.join->stop();
        r.resolver.ready = true;   // no signal: ready before the join starts
        r.join->start();
        QTRY_COMPARE_WITH_TIMEOUT(r.state(ids[0]), Ed3kHosterJoin::State::Complete, 60000);
    }

    void unknownHoster_noResolver()
    {
        Rig r;
        QVERIFY(r.start(true));
        const auto ids = r.join->addLinks(QStringLiteral("https://other.example/f/3"));
        QTRY_COMPARE_WITH_TIMEOUT(r.state(ids[0]), Ed3kHosterJoin::State::NoResolver, 10000);
    }

    void removedFile_gone()
    {
        Rig r;
        r.resolver.gone = true;
        QVERIFY(r.start(true));
        const auto ids = r.join->addLinks(QStringLiteral("https://fake.example/f/4"));
        QTRY_COMPARE_WITH_TIMEOUT(r.state(ids[0]), Ed3kHosterJoin::State::Gone, 30000);
        QCOMPARE(r.resolver.opens, 0);
    }

    /// items survive a restart; an unfinished one starts again
    void items_savedAndLoaded()
    {
        Rig r;
        QVERIFY(r.start(true));
        const auto ids = r.join->addLinks(QStringLiteral("https://fake.example/f/5"));
        QTRY_COMPARE_WITH_TIMEOUT(r.state(ids[0]), Ed3kHosterJoin::State::Complete, 60000);
        r.join->stop();
        Ed3kHosterJoin again(r.session, r.tmp.filePath(QStringLiteral("cfg")));
        QCOMPARE(again.items().size(), 1);
        QCOMPARE(again.items().first().state, Ed3kHosterJoin::State::Complete);
        QCOMPARE(again.items().first().url, QStringLiteral("https://fake.example/f/5"));
    }

    /// a package: one id and name for its items, its own folder under incoming, kept over a
    /// restart; joining by id, never nested; the folder goes with its last file
    void packages_groupAndFolder()
    {
        Rig r;
        QVERIFY(r.start(true));
        const auto a = r.join->addLinks(QStringLiteral("https://fake.example/p/1"), QStringLiteral("Pack/One"));
        QCOMPARE(a.size(), 1);
        const quint32 pkg = r.join->item(a[0])->packageId;
        QVERIFY(pkg != 0);
        QCOMPARE(r.join->item(a[0])->package, QStringLiteral("Pack_One"));   // a folder name, not a path

        // by id: the same package
        const auto b = r.join->addMirrored({{QStringLiteral("https://fake.example/p/2")}}, {}, pkg);
        QCOMPARE(r.join->item(b[0])->packageId, pkg);
        QCOMPARE(r.join->item(b[0])->package, QStringLiteral("Pack_One"));
        // the same name again is a new package; no name is none
        const auto c = r.join->addMirrored({{QStringLiteral("https://fake.example/p/3")}}, QStringLiteral("Pack/One"));
        QVERIFY(r.join->item(c[0])->packageId != pkg);
        const auto d = r.join->addLinks(QStringLiteral("https://fake.example/p/4"));
        QCOMPARE(r.join->item(d[0])->packageId, quint32(0));
        QVERIFY(r.join->item(d[0])->package.isEmpty());

        QTRY_COMPARE_WITH_TIMEOUT(r.state(a[0]), Ed3kHosterJoin::State::Complete, 60000);
        const QString incoming = r.tmp.filePath(QStringLiteral("incoming"));
        const QString file = r.join->item(a[0])->file;
        QCOMPARE(QFileInfo(file).absolutePath(), QDir(incoming).filePath(QStringLiteral("Pack_One")));
        QTRY_COMPARE_WITH_TIMEOUT(r.state(d[0]), Ed3kHosterJoin::State::Complete, 60000);
        QCOMPARE(QFileInfo(r.join->item(d[0])->file).absolutePath(), QDir(incoming).absolutePath());

        // kept over a restart; a new package after it gets a fresh id
        const quint32 pkgC = r.join->item(c[0])->packageId;
        QVERIFY(r.restart());
        QCOMPARE(r.join->item(a[0])->packageId, pkg);
        QCOMPARE(r.join->item(b[0])->package, QStringLiteral("Pack_One"));
        QCOMPARE(r.join->item(d[0])->packageId, quint32(0));
        const auto e = r.join->addLinks(QStringLiteral("https://fake.example/p/5"), QStringLiteral("Next"));
        QVERIFY(r.join->item(e[0])->packageId > std::max(pkg, pkgC));

        // the folder goes with its last file, not before (c: another package, the same name)
        QTRY_COMPARE_WITH_TIMEOUT(r.state(b[0]), Ed3kHosterJoin::State::Complete, 60000);
        QTRY_COMPARE_WITH_TIMEOUT(r.state(c[0]), Ed3kHosterJoin::State::Complete, 60000);
        QCOMPARE(QFileInfo(r.join->item(c[0])->file).absolutePath(), QDir(incoming).filePath(QStringLiteral("Pack_One")));
        r.join->remove(a[0], true);
        r.join->remove(c[0], true);
        QVERIFY(QFileInfo::exists(QDir(incoming).filePath(QStringLiteral("Pack_One"))));
        r.join->remove(b[0], true);
        QVERIFY(!QFileInfo::exists(QDir(incoming).filePath(QStringLiteral("Pack_One"))));
    }

    /// an archive waits in Temp, what it holds goes to incoming/<package>/ and is handed to
    /// sharing; the archive stays unless deleteArchives
    void extract_packageArchiveIntoIncoming()
    {
        Rig r;
        const QString zip = r.tmp.filePath(QStringLiteral("src.zip"));
        QVERIFY(writeZip(zip, {{QStringLiteral("a.txt"), QByteArray("alpha")},
                               {QStringLiteral("sub/b.bin"), content(300 * 1024)}}));
        QFile zf(zip);
        QVERIFY(zf.open(QIODevice::ReadOnly));
        r.resolver.data = zf.readAll();
        r.resolver.fileName = QStringLiteral("release.zip");
        QVERIFY(r.start(true));
        QStringList shared;
        r.join->setPublishedHook([&shared](const QStringList& f) { shared << f; });
        const auto ids = r.join->addLinks(QStringLiteral("https://fake.example/z/1"), QStringLiteral("Rel"));
        QCOMPARE(ids.size(), 1);
        QTRY_COMPARE_WITH_TIMEOUT(r.join->item(ids[0])->reason, Ed3kHosterJoin::Reason::Unpacked, 60000);
        const QString dest = QDir(r.tmp.filePath(QStringLiteral("incoming"))).filePath(QStringLiteral("Rel"));
        QCOMPARE(r.join->item(ids[0])->reasonArg, dest);
        QFile a(QDir(dest).filePath(QStringLiteral("a.txt")));
        QVERIFY(a.open(QIODevice::ReadOnly));
        QCOMPARE(a.readAll(), QByteArray("alpha"));
        QVERIFY(QFileInfo::exists(QDir(dest).filePath(QStringLiteral("sub/b.bin"))));
        QVERIFY(!QFileInfo::exists(QDir(dest).filePath(QStringLiteral("release.zip"))));   // no archive in incoming
        QCOMPARE(shared.size(), 2);
        // kept in Temp
        const QString kept = r.join->item(ids[0])->file;
        QVERIFY(kept.startsWith(r.tmp.filePath(QStringLiteral("temp"))));
        QVERIFY(QFileInfo::exists(kept));
        QVERIFY(!QFileInfo::exists(QDir(QFileInfo(kept).absolutePath()).filePath(QStringLiteral(".unpack"))));
        // a restart keeps the verdict
        QVERIFY(r.restart());
        QCOMPARE(r.join->item(ids[0])->reason, Ed3kHosterJoin::Reason::Unpacked);
    }

    void extract_deleteArchivesAfterwards()
    {
        Rig r;
        const QString zip = r.tmp.filePath(QStringLiteral("src.zip"));
        QVERIFY(writeZip(zip, {{QStringLiteral("only.txt"), QByteArray("x")}}));
        QFile zf(zip);
        QVERIFY(zf.open(QIODevice::ReadOnly));
        r.resolver.data = zf.readAll();
        r.resolver.fileName = QStringLiteral("one.zip");
        r.prefs.deleteArchives = true;
        QVERIFY(r.start(true));
        const auto ids = r.join->addLinks(QStringLiteral("https://fake.example/z/2"));
        QTRY_COMPARE_WITH_TIMEOUT(r.join->item(ids[0])->reason, Ed3kHosterJoin::Reason::Unpacked, 60000);
        QVERIFY(QFileInfo::exists(QDir(r.tmp.filePath(QStringLiteral("incoming"))).filePath(QStringLiteral("only.txt"))));
        QVERIFY(!QFileInfo::exists(r.join->item(ids[0])->file));
    }

    /// an encrypted zip: the package's passwords first (a container's); none opens it ->
    /// UnpackPassword; a typed one goes first in the package and the extraction runs again
    void extract_passwordFromPackageThenTyped()
    {
        const QString zipTool = QStandardPaths::findExecutable(QStringLiteral("zip"));
        const bool unpacker = std::ranges::any_of(std::array{"7zz", "7z", "unrar", "7za"}, [](const char* n) {
            return !QStandardPaths::findExecutable(QString::fromLatin1(n)).isEmpty();
        });
        if (zipTool.isEmpty() || !unpacker)
            QSKIP("needs zip and 7-Zip");
        Rig r;
        const QString src = r.tmp.filePath(QStringLiteral("enc"));
        QVERIFY(QDir().mkpath(src));
        QFile plain(QDir(src).filePath(QStringLiteral("secret.txt")));
        QVERIFY(plain.open(QIODevice::WriteOnly));
        plain.write("inside");
        plain.close();
        QProcess zip;
        zip.setWorkingDirectory(src);
        zip.start(zipTool, {QStringLiteral("-q"), QStringLiteral("-P"), QStringLiteral("right"),
                            QStringLiteral("enc.zip"), QStringLiteral("secret.txt")});
        QVERIFY(zip.waitForFinished(30000) && zip.exitCode() == 0);
        QFile zf(QDir(src).filePath(QStringLiteral("enc.zip")));
        QVERIFY(zf.open(QIODevice::ReadOnly));
        r.resolver.data = zf.readAll();
        r.resolver.fileName = QStringLiteral("enc.zip");
        QVERIFY(r.start(true));
        const QString incoming = r.tmp.filePath(QStringLiteral("incoming"));

        // the container's right one among others: no question
        const auto a = r.join->addMirrored({{QStringLiteral("https://fake.example/e/1")}}, QStringLiteral("A"), 0,
                                           {QStringLiteral("nope"), QStringLiteral("right")});
        QTRY_COMPARE_WITH_TIMEOUT(r.join->item(a[0])->reason, Ed3kHosterJoin::Reason::Unpacked, 60000);
        QVERIFY(QFileInfo::exists(QDir(incoming).filePath(QStringLiteral("A/secret.txt"))));
        QVERIFY(!r.join->setArchivePassword(a[0], QStringLiteral("right")));   // wants none

        // wrong ones only: it waits for the user
        const auto b = r.join->addMirrored({{QStringLiteral("https://fake.example/e/2")}}, QStringLiteral("B"), 0,
                                           {QStringLiteral("nope")});
        QTRY_COMPARE_WITH_TIMEOUT(r.join->item(b[0])->reason, Ed3kHosterJoin::Reason::UnpackPassword, 60000);
        QVERIFY(!r.join->setArchivePassword(b[0], QString()));
        QVERIFY(r.join->setArchivePassword(b[0], QStringLiteral("right")));
        QCOMPARE(r.join->item(b[0])->passwords, (QStringList{QStringLiteral("right"), QStringLiteral("nope")}));
        QTRY_COMPARE_WITH_TIMEOUT(r.join->item(b[0])->reason, Ed3kHosterJoin::Reason::Unpacked, 60000);
        QFile out(QDir(incoming).filePath(QStringLiteral("B/secret.txt")));
        QVERIFY(out.open(QIODevice::ReadOnly));
        QCOMPARE(out.readAll(), QByteArray("inside"));
        // a wrong typed one asks again
        const auto c = r.join->addMirrored({{QStringLiteral("https://fake.example/e/3")}}, QStringLiteral("C"));
        QTRY_COMPARE_WITH_TIMEOUT(r.join->item(c[0])->reason, Ed3kHosterJoin::Reason::UnpackPassword, 60000);
        QVERIFY(r.join->setArchivePassword(c[0], QStringLiteral("guess")));
        QTRY_COMPARE_WITH_TIMEOUT(r.join->item(c[0])->reason, Ed3kHosterJoin::Reason::UnpackPassword, 60000);
    }

    /// extraction off: the archive goes to incoming as it is; a plain file is always shared
    void extract_offMovesArchiveToIncoming()
    {
        Rig r;
        r.resolver.fileName = QStringLiteral("raw.zip");   // not a zip inside: never opened
        r.prefs.extract = false;
        QVERIFY(r.start(true));
        QStringList shared;
        r.join->setPublishedHook([&shared](const QStringList& f) { shared << f; });
        const auto ids = r.join->addLinks(QStringLiteral("https://fake.example/z/3"), QStringLiteral("P"));
        QTRY_COMPARE_WITH_TIMEOUT(r.state(ids[0]), Ed3kHosterJoin::State::Complete, 60000);
        QCOMPARE(r.join->item(ids[0])->reason, Ed3kHosterJoin::Reason::None);
        const QString file = r.join->item(ids[0])->file;
        QCOMPARE(QFileInfo(file).absolutePath(),
                 QDir(r.tmp.filePath(QStringLiteral("incoming"))).filePath(QStringLiteral("P")));
        QCOMPARE(shared, QStringList{file});
    }

    /// a broken archive: failed, kept in Temp; switching extraction off releases it to incoming
    void extract_failedThenReleased()
    {
        Rig r;
        r.resolver.fileName = QStringLiteral("broken.zip");
        QVERIFY(r.start(true));
        const auto ids = r.join->addLinks(QStringLiteral("https://fake.example/z/4"));
        QTRY_COMPARE_WITH_TIMEOUT(r.join->item(ids[0])->reason, Ed3kHosterJoin::Reason::UnpackFailed, 60000);
        QVERIFY(r.join->item(ids[0])->file.startsWith(r.tmp.filePath(QStringLiteral("temp"))));
        r.prefs.extract = false;
        r.join->setConfig(r.prefs, r.tmp.filePath(QStringLiteral("temp")), r.tmp.filePath(QStringLiteral("incoming")));
        QCOMPARE(r.join->item(ids[0])->reason, Ed3kHosterJoin::Reason::None);
        QCOMPARE(QFileInfo(r.join->item(ids[0])->file).absolutePath(),
                 QDir(r.tmp.filePath(QStringLiteral("incoming"))).absolutePath());
    }

    /// §4.1: the downloaded link is vouched for, kept with its check time over a restart
    void shared_vouchedLinkSaved()
    {
        Rig r;
        QVERIFY(r.start(true));
        const QString u = QStringLiteral("https://fake.example/s/1");
        const quint32 id = r.completeOne(u);
        QVERIFY(id);
        auto links = r.join->sharedLinks(id);
        QCOMPARE(links.size(), 1);
        QCOMPARE(links[0].url, u);
        QVERIFY(links[0].checkedAt > 0);
        QVERIFY(!links[0].offline);
        const qint64 at = links[0].checkedAt;
        const int checks = r.resolver.checks;
        QVERIFY(r.restart());
        links = r.join->sharedLinks(id);
        QCOMPARE(links.size(), 1);
        QCOMPARE(links[0].checkedAt, at);
        // fresh: no recheck
        QTest::qWait(1500);
        QCOMPARE(r.resolver.checks, checks);
    }

    /// recheckHours: a stale published link is asked again, though its item is complete
    void recheck_due_online()
    {
        Rig r;
        QVERIFY(r.start(true));
        const QString u = QStringLiteral("https://fake.example/s/2");
        const quint32 id = r.completeOne(u);
        QVERIFY(id);
        const int checks = r.resolver.checks;
        QVERIFY(r.restart(stale));
        QTRY_COMPARE_WITH_TIMEOUT(r.resolver.checks, checks + 1, 20000);
        QTRY_VERIFY_WITH_TIMEOUT(QDateTime::currentMSecsSinceEpoch() - r.join->sharedLinks(id)[0].checkedAt < 60000, 10000);
        QVERIFY(!r.join->sharedLinks(id)[0].offline);
        QCOMPARE(r.join->item(id)->reason, Ed3kHosterJoin::Reason::None);
    }

    /// a recheck finds the link gone: withdrawn, the item says so, a links dialog sees it offline
    void recheck_gone_withdraws()
    {
        Rig r;
        QVERIFY(r.start(true));
        const QString u = QStringLiteral("https://fake.example/s/3");
        const quint32 id = r.completeOne(u);
        QVERIFY(id);
        r.resolver.goneUrls << u;
        QVERIFY(r.restart(stale));
        QTRY_VERIFY_WITH_TIMEOUT(r.join->sharedLinks(id)[0].offline, 20000);
        QCOMPARE(r.state(id), Ed3kHosterJoin::State::Complete);
        QCOMPARE(r.join->item(id)->reason, Ed3kHosterJoin::Reason::LinkOffline);
        CheckSpy spy(*r.join);
        QVERIFY(r.join->checkLinks({u}));
        QTRY_COMPARE_WITH_TIMEOUT(spy.status(u), Ed3kHosterJoin::LinkStatus::Offline, 5000);
        // kept offline over a restart
        QVERIFY(r.restart());
        QVERIFY(r.join->sharedLinks(id)[0].offline);
    }

    /// hosters off: nobody is asked; the stale link is kept as unknown, not withdrawn
    void recheck_hostersOff_keeps()
    {
        Rig r;
        QVERIFY(r.start(true));
        const quint32 id = r.completeOne(QStringLiteral("https://fake.example/s/4"));
        QVERIFY(id);
        const int checks = r.resolver.checks;
        r.prefs.enabled = false;
        QVERIFY(r.restart(stale));
        QTRY_VERIFY_WITH_TIMEOUT(r.join->sharedLinks(id)[0].unknown, 5000);
        QVERIFY(!r.join->sharedLinks(id)[0].offline);
        QCOMPARE(r.resolver.checks, checks);
    }

    /// recheckHours 0: never asked again
    void recheck_zero_never()
    {
        Rig r;
        QVERIFY(r.start(true));
        const quint32 id = r.completeOne(QStringLiteral("https://fake.example/s/5"));
        QVERIFY(id);
        const int checks = r.resolver.checks;
        r.prefs.recheckHours = 0;
        QVERIFY(r.restart(stale));
        QTest::qWait(2500);
        QCOMPARE(r.resolver.checks, checks);
        QVERIFY(!r.join->sharedLinks(id)[0].unknown);
    }

    /// rechecks wait for the check limits as the links dialog's do
    void recheck_respectsLimits()
    {
        Rig r;
        r.prefs.checkMaxPerHost = 1;
        QVERIFY(r.start(true));
        QList<quint32> ids;
        for (int i = 0; i < 3; ++i) {
            const quint32 id = r.completeOne(QStringLiteral("https://fake.example/s/l%1").arg(i));
            QVERIFY(id);
            ids << id;
        }
        r.resolver.checkDelayMs = 200;
        r.resolver.maxRunningChecks.clear();
        const int checks = r.resolver.checks;
        QVERIFY(r.restart(stale));
        QTRY_COMPARE_WITH_TIMEOUT(r.resolver.checks, checks + 3, 30000);
        QCOMPARE(r.resolver.maxRunningChecks.value(QStringLiteral("fake.example")), 1);
    }

    /// hoster.md §2: downloaders in 3 sites report the link gone: checked at once, withdrawn when it is
    void report_threeDead_checksNow()
    {
        Rig r;
        QVERIFY(r.start(true));
        const QString u = QStringLiteral("https://fake.example/s/r1");
        const quint32 id = r.completeOne(u);
        QVERIFY(id);
        r.resolver.goneUrls << u;
        // last checked 7 h ago: past the 6 h floor, far from recheckHours
        QVERIFY(r.restart([](QJsonObject& l) { l[QStringLiteral("checkedAt")] = QDateTime::currentMSecsSinceEpoch() - 7 * 3600 * 1000LL; }));
        QTest::qWait(1500);   // the start publishes the link
        const int checks = r.resolver.checks;
        report(r, id, u, 1, ED3K_HTTP_GONE);
        report(r, id, u, 2, ED3K_HTTP_CORRUPT);
        report(r, id, u, 2, ED3K_HTTP_GONE);         // the same downloader again
        report(r, id, u, 3, ED3K_HTTP_HTTP_FAILED);  // says nothing about the link
        report(r, id, u, 4, ED3K_HTTP_GONE, 1);      // another node in a site that reported
        report(r, id, u, 5, ED3K_HTTP_GONE, 0);      // no site: not counted
        QTest::qWait(1000);
        QCOMPARE(r.resolver.checks, checks);
        QCOMPARE(r.join->sharedLinks(id)[0].reporters.size(), std::size_t(2));
        report(r, id, u, 3, ED3K_HTTP_SIZE_MISMATCH);
        QTRY_VERIFY_WITH_TIMEOUT(r.join->sharedLinks(id)[0].offline, 20000);
        QCOMPARE(r.resolver.checks, checks + 1);
        QCOMPARE(r.join->item(id)->reason, Ed3kHosterJoin::Reason::LinkOffline);
        QVERIFY(r.join->sharedLinks(id)[0].reportCheckAt > 0);
    }

    /// reportRecheckHours: reports within it of the last check wait; it holds over a restart
    void report_floor_holds()
    {
        Rig r;
        QVERIFY(r.start(true));
        const QString u = QStringLiteral("https://fake.example/s/r2");
        const quint32 id = r.completeOne(u);
        QVERIFY(id);
        QTest::qWait(500);
        const int checks = r.resolver.checks;
        // just vouched for: the floor counts from that check
        for (quint8 n = 1; n <= 3; ++n)
            report(r, id, u, n, ED3K_HTTP_GONE);
        QTest::qWait(1000);
        QCOMPARE(r.resolver.checks, checks);
        // a report check 1 h ago, the vouch long before: still held
        QVERIFY(r.restart([](QJsonObject& l) {
            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            l[QStringLiteral("checkedAt")] = now - 10 * 3600 * 1000LL;
            l[QStringLiteral("reportCheckAt")] = now - 3600 * 1000LL;
        }));
        QTest::qWait(1500);
        for (quint8 n = 1; n <= 3; ++n)
            report(r, id, u, n, ED3K_HTTP_GONE);
        QTest::qWait(1000);
        QCOMPARE(r.resolver.checks, checks);
        // online after the floor: kept, reports start over
        QVERIFY(r.restart([](QJsonObject& l) {
            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            l[QStringLiteral("checkedAt")] = now - 10 * 3600 * 1000LL;
            l[QStringLiteral("reportCheckAt")] = now - 7 * 3600 * 1000LL;
        }));
        QTest::qWait(1500);
        for (quint8 n = 1; n <= 3; ++n)
            report(r, id, u, n, ED3K_HTTP_GONE);
        QTRY_COMPARE_WITH_TIMEOUT(r.resolver.checks, checks + 1, 20000);
        QTRY_VERIFY_WITH_TIMEOUT(QDateTime::currentMSecsSinceEpoch() - r.join->sharedLinks(id)[0].checkedAt < 60000, 10000);
        QVERIFY(!r.join->sharedLinks(id)[0].offline);
        QVERIFY(r.join->sharedLinks(id)[0].reporters.empty());
    }

    /// the first mirror's file is gone: the item goes on with the second
    void mirror_goneFirst_takesNext()
    {
        Rig r;
        const QString m1 = QStringLiteral("https://fake.example/m/1");
        const QString m2 = QStringLiteral("https://fake.example/m/2");
        r.resolver.goneUrls = {m1};
        QVERIFY(r.start(true));
        const auto ids = r.join->addMirrored({{m1, m2}});
        QCOMPARE(ids.size(), 1);
        QTRY_COMPARE_WITH_TIMEOUT(r.state(ids[0]), Ed3kHosterJoin::State::Complete, 60000);
        const auto item = *r.join->item(ids[0]);
        QCOMPARE(item.url, m2);
        QCOMPARE(item.mirrorNo, 1);
        QVERIFY(item.mirrors.isEmpty());
        QCOMPARE(r.resolver.opened, QStringList{m2});
    }

    /// no plugin for the first mirror's hoster: the next one at a known hoster
    void mirror_unknownHoster_takesNext()
    {
        Rig r;
        QVERIFY(r.start(true));
        const auto ids = r.join->addMirrored({{QStringLiteral("https://other.example/x"), QStringLiteral("https://fake.example/m/3")}});
        QTRY_COMPARE_WITH_TIMEOUT(r.state(ids[0]), Ed3kHosterJoin::State::Complete, 60000);
        QCOMPARE(r.join->item(ids[0])->url, QStringLiteral("https://fake.example/m/3"));
    }

    /// an hour's quota at the first mirror is stuck: the next one goes now
    void mirror_longWait_takesNext()
    {
        Rig r;
        const QString m1 = QStringLiteral("https://fake.example/m/4");
        const QString m2 = QStringLiteral("https://fake.example/m/5");
        r.resolver.quotaUrls = {m1};
        QVERIFY(r.start(true));
        const auto ids = r.join->addMirrored({{m1, m2}});
        QTRY_COMPARE_WITH_TIMEOUT(r.state(ids[0]), Ed3kHosterJoin::State::Complete, 60000);
        QCOMPARE(r.join->item(ids[0])->url, m2);
        QCOMPARE(r.resolver.opened, (QStringList{m1, m2}));
    }

    /// every mirror gone: one item, Gone, nothing left to try
    void mirror_allGone_staysGone()
    {
        Rig r;
        r.resolver.gone = true;
        QVERIFY(r.start(true));
        const auto ids = r.join->addMirrored({{QStringLiteral("https://fake.example/m/6"), QStringLiteral("https://fake.example/m/7")}});
        QTRY_COMPARE_WITH_TIMEOUT(r.join->item(ids[0])->mirrorNo, 1, 30000);
        QTRY_COMPARE_WITH_TIMEOUT(r.state(ids[0]), Ed3kHosterJoin::State::Gone, 30000);
        QVERIFY(r.join->item(ids[0])->mirrors.isEmpty());
        QCOMPARE(r.join->items().size(), 1);
        QCOMPARE(r.resolver.opens, 0);
    }

    /// Skip ends as PuzzleSkipped and the next mirror asks; "Skip hoster" ends the last one as
    /// HosterSkipped; retry() starts at the first link and asks again
    void puzzle_skipMirrorsThenRetry()
    {
        Rig r;
        r.resolver.hosts << QStringLiteral("other.example");
        const QStringList urls{QStringLiteral("https://fake.example/pz/1"), QStringLiteral("https://other.example/pz/1")};
        r.resolver.puzzleUrls = {urls[0], urls[1]};
        QVERIFY(r.start(true));
        QList<quint64> asked;
        connect(r.join.get(), &Ed3kHosterJoin::puzzleAsked, r.join.get(),
                [&asked](quint32, quint64 p, const QByteArray&, const QString&, const QString&, int) { asked << p; });
        const auto ids = r.join->addMirrored({urls});
        QTRY_COMPARE_WITH_TIMEOUT(asked.size(), 1, 10000);
        QCOMPARE(r.state(ids[0]), Ed3kHosterJoin::State::Puzzle);
        r.join->answerPuzzle(asked[0], {});
        // the next mirror asks its own
        QTRY_COMPARE_WITH_TIMEOUT(asked.size(), 2, 10000);
        QCOMPARE(r.join->item(ids[0])->mirrorNo, 1);
        QCOMPARE(r.join->item(ids[0])->tried, QStringList{urls[0]});
        r.join->answerPuzzle(asked[1], {}, true);
        QTRY_COMPARE_WITH_TIMEOUT(r.state(ids[0]), Ed3kHosterJoin::State::NeedsUser, 10000);
        QCOMPARE(r.join->item(ids[0])->reason, Ed3kHosterJoin::Reason::HosterSkipped);
        QCOMPARE(r.join->item(ids[0])->reasonArg, QStringLiteral("other.example"));
        r.join->retry(ids[0]);
        QTRY_COMPARE_WITH_TIMEOUT(asked.size(), 3, 10000);
        QCOMPARE(r.join->item(ids[0])->url, urls[0]);
        QCOMPARE(r.join->item(ids[0])->mirrors, QStringList{urls[1]});
        QCOMPARE(r.join->item(ids[0])->mirrorNo, 0);
        QVERIFY(r.join->item(ids[0])->tried.isEmpty());
        // a wrong answer, then the skipped hoster asks again after the retry
        r.join->answerPuzzle(asked[2], QStringLiteral("abc"));
        QTRY_COMPARE_WITH_TIMEOUT(asked.size(), 4, 10000);
        r.join->answerPuzzle(asked[3], QStringLiteral("abc"));
        QTRY_COMPARE_WITH_TIMEOUT(r.state(ids[0]), Ed3kHosterJoin::State::NeedsUser, 10000);
        QCOMPARE(r.join->item(ids[0])->reason, Ed3kHosterJoin::Reason::PuzzleWrong);
    }

    /// after "Skip hoster" a later puzzle of that hoster is not shown, until a retry
    void puzzle_skipHosterSilencesLaterOnes()
    {
        Rig r;
        r.resolver.puzzleUrls = {QStringLiteral("https://fake.example/pz/a"), QStringLiteral("https://fake.example/pz/b")};
        QVERIFY(r.start(true));
        QList<quint64> asked;
        connect(r.join.get(), &Ed3kHosterJoin::puzzleAsked, r.join.get(),
                [&asked](quint32, quint64 p, const QByteArray&, const QString&, const QString&, int) { asked << p; });
        const auto a = r.join->addLinks(QStringLiteral("https://fake.example/pz/a"));
        QTRY_COMPARE_WITH_TIMEOUT(asked.size(), 1, 10000);
        r.join->answerPuzzle(asked[0], {}, true);
        QTRY_COMPARE_WITH_TIMEOUT(r.state(a[0]), Ed3kHosterJoin::State::NeedsUser, 10000);
        QCOMPARE(r.join->item(a[0])->reason, Ed3kHosterJoin::Reason::HosterSkipped);
        const auto b = r.join->addLinks(QStringLiteral("https://fake.example/pz/b"));
        QTRY_COMPARE_WITH_TIMEOUT(r.state(b[0]), Ed3kHosterJoin::State::NeedsUser, 10000);
        QCOMPARE(r.join->item(b[0])->reason, Ed3kHosterJoin::Reason::HosterSkipped);
        QCOMPARE(asked.size(), 1);
        QCOMPARE(r.resolver.puzzlesAsked, 2);
        r.join->retry(b[0]);
        QTRY_COMPARE_WITH_TIMEOUT(asked.size(), 2, 10000);
        r.join->answerPuzzle(asked[1], {});
        QTRY_COMPARE_WITH_TIMEOUT(r.state(b[0]), Ed3kHosterJoin::State::NeedsUser, 10000);
        QCOMPARE(r.join->item(b[0])->reason, Ed3kHosterJoin::Reason::PuzzleSkipped);
    }

    /// the mirrors not tried yet survive a restart
    void mirror_savedAndLoaded()
    {
        Rig r;
        QVERIFY(r.start(false));   // hosters off: the item waits on its first link
        const QStringList urls{QStringLiteral("https://fake.example/m/8"), QStringLiteral("https://fake.example/m/9"),
                               QStringLiteral("https://other.example/m/9")};
        const auto ids = r.join->addMirrored({urls});
        QTRY_COMPARE_WITH_TIMEOUT(r.state(ids[0]), Ed3kHosterJoin::State::NeedsUser, 10000);
        QCOMPARE(r.join->item(ids[0])->mirrorNo, 0);   // hosters off is no reason to switch
        r.join->stop();
        Ed3kHosterJoin again(r.session, r.tmp.filePath(QStringLiteral("cfg")));
        // unfinished: taken up from its .met in Temp
        again.setConfig(r.prefs, r.tmp.filePath(QStringLiteral("temp")), r.tmp.filePath(QStringLiteral("incoming")));
        QCOMPARE(again.items().size(), 1);
        QCOMPARE(again.items().first().url, urls[0]);
        QCOMPARE(again.items().first().mirrors, urls.mid(1));
    }

    /// a file with any link already an item is not added again
    void mirror_duplicateGroupSkipped()
    {
        Rig r;
        QVERIFY(r.start(false));
        QCOMPARE(r.join->addLinks(QStringLiteral("https://fake.example/m/10")).size(), 1);
        QVERIFY(r.join->addMirrored({{QStringLiteral("https://fake.example/m/11"), QStringLiteral("https://fake.example/m/10")}}).isEmpty());
        QVERIFY(r.join->addLinks(QStringLiteral("https://fake.example/m/12")).size() == 1);
        QVERIFY(r.join->addMirrored({{QStringLiteral("https://fake.example/m/12")}}).isEmpty());
        QCOMPARE(r.join->items().size(), 2);
    }

    /// a links dialog's check: online, name and size, nothing opened; asked again, the result at once
    void probe_found_online()
    {
        Rig r;
        QVERIFY(r.start(true));
        CheckSpy spy(*r.join);
        const QString u = QStringLiteral("https://fake.example/p/1");
        QVERIFY(r.join->checkLinks({u}));
        QTRY_COMPARE_WITH_TIMEOUT(spy.status(u), Ed3kHosterJoin::LinkStatus::Online, 10000);
        QCOMPARE(spy.last[u].host, QStringLiteral("fake.example"));
        QCOMPARE(spy.last[u].name, QStringLiteral("movie.bin"));
        QCOMPARE(spy.last[u].size, std::uint64_t(r.resolver.data.size()));
        QCOMPARE(r.resolver.checks, 1);
        QCOMPARE(r.resolver.opens, 0);
        QVERIFY(r.join->checkLinks({u}));
        QCOMPARE(spy.count, 2);
        QCOMPARE(r.resolver.checks, 1);
    }

    void probe_gone_offline()
    {
        Rig r;
        const QString u = QStringLiteral("https://fake.example/p/2");
        r.resolver.goneUrls = {u};
        QVERIFY(r.start(true));
        CheckSpy spy(*r.join);
        QVERIFY(r.join->checkLinks({u}));
        QTRY_COMPARE_WITH_TIMEOUT(spy.status(u), Ed3kHosterJoin::LinkStatus::Offline, 10000);
    }

    /// ed3k node off (not connected): the resolver checks the links itself
    void probe_noNode_resolverChecks()
    {
        QTemporaryDir tmp;
        Ed3kSession session;   // never started: no engine
        FakeResolver resolver;
        const QString online = QStringLiteral("https://fake.example/p/10");
        const QString gone = QStringLiteral("https://fake.example/p/11");
        resolver.goneUrls = {gone};
        Ed3kHosterJoin join(session, tmp.filePath(QStringLiteral("cfg")));
        join.setResolver(&resolver);
        Ed3kHosterPrefs prefs;
        prefs.enabled = true;
        join.setConfig(prefs, tmp.filePath(QStringLiteral("temp")), tmp.filePath(QStringLiteral("incoming")));
        CheckSpy spy(join);
        QVERIFY(join.checkLinks({online, gone}));
        QTRY_COMPARE_WITH_TIMEOUT(spy.status(online), Ed3kHosterJoin::LinkStatus::Online, 10000);
        QTRY_COMPARE_WITH_TIMEOUT(spy.status(gone), Ed3kHosterJoin::LinkStatus::Offline, 10000);
        QCOMPARE(spy.last[online].name, QStringLiteral("movie.bin"));
        QCOMPARE(resolver.checks, 2);
        QCOMPARE(resolver.opens, 0);
    }

    void probe_unknownHoster_unknown()
    {
        Rig r;
        QVERIFY(r.start(true));
        CheckSpy spy(*r.join);
        const QString u = QStringLiteral("https://other.example/p/3");
        QVERIFY(r.join->checkLinks({u}));
        QTRY_COMPARE_WITH_TIMEOUT(spy.status(u), Ed3kHosterJoin::LinkStatus::Unknown, 10000);
        QVERIFY(spy.last[u].host.isEmpty());
        QCOMPARE(r.resolver.checks, 0);
    }

    /// §3: no hoster is asked anything without the user's switch
    void probe_hostersOff_none()
    {
        Rig r;
        QVERIFY(r.start(false));
        QVERIFY(!r.join->checkLinks({QStringLiteral("https://fake.example/p/4")}));
        QCOMPARE(r.resolver.checks, 0);
        QCOMPARE(r.resolver.accountChecks, 0);
    }

    /// an item's link is never checked beside it: its state answers
    void probe_itemUrl_notProbed()
    {
        Rig r;
        QVERIFY(r.start(true));
        const QString u = QStringLiteral("https://fake.example/p/5");
        const auto ids = r.join->addLinks(u);
        QTRY_COMPARE_WITH_TIMEOUT(r.state(ids[0]), Ed3kHosterJoin::State::Complete, 60000);
        CheckSpy spy(*r.join);
        QVERIFY(r.join->checkLinks({u}));
        QCOMPARE(spy.status(u), Ed3kHosterJoin::LinkStatus::Online);
        QCOMPARE(r.resolver.checks, 1);
    }

    /// an unknown result is not kept: the next dialog asks again
    void probe_unknown_askedAgain()
    {
        Rig r;
        QVERIFY(r.start(true));
        CheckSpy spy(*r.join);
        const QString u = QStringLiteral("https://other.example/p/3b");
        QVERIFY(r.join->checkLinks({u}));
        QTRY_COMPARE_WITH_TIMEOUT(spy.count, 1, 10000);
        QVERIFY(r.join->checkLinks({u}));
        QCOMPARE(spy.count, 1);   // not answered from the cache
        QTRY_COMPARE_WITH_TIMEOUT(spy.count, 2, 10000);
        QCOMPARE(spy.status(u), Ed3kHosterJoin::LinkStatus::Unknown);
    }

    /// a paused item never checked holds no core link: the dialog's check runs, not "unknown"
    void probe_pausedItem_checked()
    {
        Rig r;
        QVERIFY(r.start(true));
        const QString u = QStringLiteral("https://fake.example/p/5b");
        const auto ids = r.join->addLinks(u);
        r.join->pause(ids[0]);
        QCOMPARE(r.state(ids[0]), Ed3kHosterJoin::State::Paused);
        CheckSpy spy(*r.join);
        QVERIFY(r.join->checkLinks({u}));
        QTRY_COMPARE_WITH_TIMEOUT(spy.status(u), Ed3kHosterJoin::LinkStatus::Online, 10000);
    }

    /// Download while the dialog's check runs: the item goes on from that check, no second one
    void probe_adoptedWhileRunning()
    {
        Rig r;
        r.resolver.checkDelayMs = 1500;
        QVERIFY(r.start(true));
        const QString u = QStringLiteral("https://fake.example/p/6");
        QVERIFY(r.join->checkLinks({u}));
        QTRY_COMPARE_WITH_TIMEOUT(r.resolver.checks, 1, 10000);
        const auto ids = r.join->addLinks(u);
        QCOMPARE(ids.size(), 1);
        QTRY_COMPARE_WITH_TIMEOUT(r.state(ids[0]), Ed3kHosterJoin::State::Complete, 60000);
        QCOMPARE(r.resolver.checks, 1);
        QCOMPARE(r.resolver.opens, 1);
    }

    /// checked first: the offline mirror goes last, the core keeps it dead
    void probe_thenDownload_offlineLast()
    {
        Rig r;
        const QString m1 = QStringLiteral("https://fake.example/p/7");
        const QString m2 = QStringLiteral("https://fake.example/p/8");
        r.resolver.goneUrls = {m1};
        QVERIFY(r.start(true));
        CheckSpy spy(*r.join);
        QVERIFY(r.join->checkLinks({m1, m2}));
        QTRY_COMPARE_WITH_TIMEOUT(spy.count, 2, 10000);
        QCOMPARE(r.join->rankMirrors({m1, m2}), (QStringList{m2, m1}));
        const auto ids = r.join->addMirrored({{m1, m2}});
        QCOMPARE(r.join->item(ids[0])->url, m2);
        QCOMPARE(r.join->item(ids[0])->mirrors, QStringList{m1});
        QTRY_COMPARE_WITH_TIMEOUT(r.state(ids[0]), Ed3kHosterJoin::State::Complete, 60000);
        QCOMPARE(r.resolver.opened, QStringList{m2});
    }

    /// a link the dialog found dead: its download is refused by the core, no second check
    void probe_deadLink_goneAtOnce()
    {
        Rig r;
        const QString u = QStringLiteral("https://fake.example/p/9");
        r.resolver.goneUrls = {u};
        QVERIFY(r.start(true));
        CheckSpy spy(*r.join);
        QVERIFY(r.join->checkLinks({u}));
        QTRY_COMPARE_WITH_TIMEOUT(spy.status(u), Ed3kHosterJoin::LinkStatus::Offline, 10000);
        const auto ids = r.join->addMirrored({{u}});
        QTRY_COMPARE_WITH_TIMEOUT(r.state(ids[0]), Ed3kHosterJoin::State::Gone, 30000);
        QCOMPARE(r.resolver.checks, 1);
    }

    /// premium before a free account before none; unchecked links keep their order
    void rank_premiumOverFreeOverNone()
    {
        Rig r;
        r.resolver.hosts += {QStringLiteral("free.example"), QStringLiteral("prem.example")};
        r.resolver.premiumHosts = {QStringLiteral("prem.example")};
        r.prefs.accounts = {{QStringLiteral("free.example"), QStringLiteral("u"), QStringLiteral("p")},
                            {QStringLiteral("prem.example"), QStringLiteral("u"), QStringLiteral("p")}};
        QVERIFY(r.start(true));
        QTRY_COMPARE_WITH_TIMEOUT(r.join->accountTier(QStringLiteral("prem.example")), 2, 10000);
        QCOMPARE(r.join->accountTier(QStringLiteral("free.example")), 1);
        QCOMPARE(r.join->accountTier(QStringLiteral("www.free.example")), 1);
        QCOMPARE(r.join->accountTier(QStringLiteral("fake.example")), 0);
        QCOMPARE(r.resolver.accountChecks, 2);
        const QString none = QStringLiteral("https://fake.example/r/1");
        const QString free = QStringLiteral("https://free.example/r/1");
        const QString prem = QStringLiteral("https://prem.example/r/1");
        QCOMPARE(r.join->rankMirrors({none, free, prem}), (QStringList{prem, free, none}));
        const QString none2 = QStringLiteral("https://fake.example/r/2");
        QCOMPARE(r.join->rankMirrors({none2, none}), (QStringList{none2, none}));
        // checked again only after hours, not on every dialog
        QVERIFY(r.join->checkLinks({none}));
        QCOMPARE(r.resolver.accountChecks, 2);
    }

    /// streams per hoster: a premium account JD's limit (capped by maxStreams), a free one 1
    void streams_premiumAccountLimit()
    {
        Rig r;
        r.resolver.hosts += {QStringLiteral("free.example"), QStringLiteral("prem.example"), QStringLiteral("open.example")};
        r.resolver.premiumHosts = {QStringLiteral("prem.example"), QStringLiteral("open.example")};
        r.resolver.maxDownloads = {{QStringLiteral("prem.example"), 3}, {QStringLiteral("open.example"), -1}};
        r.resolver.openSteps = {QStringLiteral("hoster")};
        r.resolver.holdAfterSteps = true;
        r.prefs.maxStreams = 16;
        r.prefs.accounts = {{QStringLiteral("free.example"), QStringLiteral("u"), QStringLiteral("p")},
                            {QStringLiteral("prem.example"), QStringLiteral("u"), QStringLiteral("p")},
                            {QStringLiteral("open.example"), QStringLiteral("u"), QStringLiteral("p")}};
        QVERIFY(r.start(true));
        QTRY_COMPARE_WITH_TIMEOUT(r.join->accountTier(QStringLiteral("open.example")), 2, 10000);
        QTRY_COMPARE_WITH_TIMEOUT(r.join->accountTier(QStringLiteral("prem.example")), 2, 10000);
        QCOMPARE(r.join->streamsAt(QStringLiteral("prem.example")), 3);
        QCOMPARE(r.join->streamsAt(QStringLiteral("www.prem.example")), 3);
        QCOMPARE(r.join->streamsAt(QStringLiteral("open.example")), 16);   // unlimited: maxStreams
        QCOMPARE(r.join->streamsAt(QStringLiteral("free.example")), 1);
        QCOMPARE(r.join->streamsAt(QStringLiteral("fake.example")), 1);
        QString links;
        for (int i = 0; i < 4; ++i)
            links += QStringLiteral("https://prem.example/s/%1 https://free.example/s/%1 ").arg(i);
        const auto ids = r.join->addLinks(links);
        QCOMPARE(ids.size(), 8);
        const auto openedAt = [&r](const QString& host) {
            return int(std::ranges::count_if(r.resolver.opened, [&host](const QString& u) { return QUrl(u).host() == host; }));
        };
        QTRY_COMPARE_WITH_TIMEOUT(openedAt(QStringLiteral("prem.example")), 3, 60000);
        QTRY_COMPARE_WITH_TIMEOUT(openedAt(QStringLiteral("free.example")), 1, 60000);
        QTest::qWait(1000);
        QCOMPARE(openedAt(QStringLiteral("prem.example")), 3);
        QCOMPARE(openedAt(QStringLiteral("free.example")), 1);
        // maxStreams over all hosters still caps a premium limit
        r.prefs.maxStreams = 2;
        r.join->setConfig(r.prefs, r.tmp.filePath(QStringLiteral("temp")), r.tmp.filePath(QStringLiteral("incoming")));
        QCOMPARE(r.join->streamsAt(QStringLiteral("prem.example")), 2);
    }

    /// a login stored under one domain of a hoster counts for its others (JD's aliases)
    void rank_accountOnAlias()
    {
        Rig r;
        r.resolver.hosts += {QStringLiteral("rapidgator.net"), QStringLiteral("rg.to")};
        r.resolver.siteGroups = {{QStringLiteral("rapidgator.net"), QStringLiteral("rg.to")}};
        r.resolver.premiumHosts = {QStringLiteral("rg.to")};
        r.prefs.accounts = {{QStringLiteral("rg.to"), QStringLiteral("u"), QStringLiteral("p")}};
        QVERIFY(r.start(true));
        QTRY_COMPARE_WITH_TIMEOUT(r.join->accountTier(QStringLiteral("rapidgator.net")), 2, 10000);
        QCOMPARE(r.join->accountTier(QStringLiteral("www.rapidgator.net")), 2);
        QCOMPARE(r.join->accountTier(QStringLiteral("rg.to")), 2);
        QCOMPARE(r.join->accountTier(QStringLiteral("fake.example")), 0);
        // unchecked mirrors: the URL's own domain is enough
        const QString none = QStringLiteral("https://fake.example/a/1");
        const QString rg = QStringLiteral("https://rapidgator.net/file/1");
        QCOMPARE(r.join->rankMirrors({none, rg}), (QStringList{rg, none}));
    }

    /// link checks: at most checkMaxPerHost at one hoster, others go on beside them
    void probe_limits_perHost()
    {
        Rig r;
        r.resolver.hosts += {QStringLiteral("a.example"), QStringLiteral("b.example")};
        r.resolver.checkDelayMs = 300;
        r.prefs.checkMaxParallel = 30;
        r.prefs.checkMaxPerHost = 2;
        QVERIFY(r.start(true));
        CheckSpy spy(*r.join);
        QStringList urls;
        for (int i = 0; i < 5; ++i)
            urls << QStringLiteral("https://a.example/l/%1").arg(i) << QStringLiteral("https://b.example/l/%1").arg(i);
        QVERIFY(r.join->checkLinks(urls));
        for (const QString& u : std::as_const(urls))
            QTRY_COMPARE_WITH_TIMEOUT(spy.status(u), Ed3kHosterJoin::LinkStatus::Online, 30000);
        QCOMPARE(r.resolver.maxRunningChecks.value(QStringLiteral("a.example")), 2);
        QCOMPARE(r.resolver.maxRunningChecks.value(QStringLiteral("b.example")), 2);
        QCOMPARE(r.resolver.maxRunningAll, 4);
    }

    /// link checks: at most checkMaxParallel in all
    void probe_limits_parallel()
    {
        Rig r;
        r.resolver.checkDelayMs = 300;
        r.prefs.checkMaxParallel = 3;
        r.prefs.checkMaxPerHost = 5;
        QVERIFY(r.start(true));
        CheckSpy spy(*r.join);
        QStringList urls;
        for (int i = 0; i < 7; ++i)
            urls << QStringLiteral("https://fake.example/m/%1").arg(i);
        QVERIFY(r.join->checkLinks(urls));
        for (const QString& u : std::as_const(urls))
            QTRY_COMPARE_WITH_TIMEOUT(spy.status(u), Ed3kHosterJoin::LinkStatus::Online, 30000);
        QCOMPARE(r.resolver.maxRunningAll, 3);
    }

    /// a stream that broke goes on where the core's hash stands: the part file the resolver
    /// gets holds the real bytes up to there (JD checks the file's hash at the end)
    void resume_rootless_goesOnFromHash()
    {
        Rig r;
        r.resolver.data = content(3 * 1024 * 1024 + 77);
        r.resolver.breakAt = 1800 * 1024;
        QVERIFY(r.start(true));
        const auto ids = r.join->addLinks(QStringLiteral("https://fake.example/f/r1"));
        QTRY_COMPARE_WITH_TIMEOUT(r.state(ids[0]), Ed3kHosterJoin::State::Complete, 60000);
        QCOMPARE(r.resolver.opens, 2);
        QVERIFY2(r.resolver.lastHave >= 1024 * 1024, qPrintable(QString::number(r.resolver.lastHave)));
        QCOMPARE(r.resolver.lastPrefill, r.resolver.data.left(qsizetype(r.resolver.lastHave)));
        QFile f(r.join->item(ids[0])->file);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), r.resolver.data);
    }

    /// a hoster that cannot resume: the bytes come again from 0 and the core hashes again
    void resume_refused_startsOver()
    {
        Rig r;
        r.resolver.data = content(3 * 1024 * 1024 + 77);
        r.resolver.breakAt = 1800 * 1024;
        r.resolver.refuseResume = true;
        QVERIFY(r.start(true));
        const auto ids = r.join->addLinks(QStringLiteral("https://fake.example/f/r2"));
        QTRY_COMPARE_WITH_TIMEOUT(r.state(ids[0]), Ed3kHosterJoin::State::Complete, 60000);
        QCOMPARE(r.resolver.opens, 2);
        QVERIFY(r.resolver.lastHave > 0);
        QFile f(r.join->item(ids[0])->file);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), r.resolver.data);
    }

    /// a rooted item saved with verified blocks goes on after them, without a new lookup
    void resume_rooted_afterBlocksHeld()
    {
        Rig r;
        // as if stopped with 6 blocks of 256 KiB held under the root: part file and .met in Temp
        const QByteArray data = content(3 * 1024 * 1024 + 77);
        const QString url = QStringLiteral("https://fake.example/f/r3");
        QVERIFY(writeHeld(r.tmp.filePath(QStringLiteral("temp")), QStringLiteral("00000000000000a3"), url, data, 6));
        QVERIFY(r.start(true));
        const quint32 id = idOf(*r.join, url);
        QVERIFY(id != 0);
        QCOMPARE(r.join->item(id)->done, std::uint64_t(6 * 256 * 1024));
        QTRY_COMPARE_WITH_TIMEOUT(r.resolver.opens, 1, 20000);
        QCOMPARE(r.resolver.lastHave, std::uint64_t(6 * 256 * 1024));
        QCOMPARE(r.resolver.lastPrefill, data.left(6 * 256 * 1024));
        QVERIFY(r.join->item(id)->verified);
    }

    /// an unfinished item is its part file and .met in Temp: copied to another node's temp dir
    /// (any of them) it goes on there; removed, both go
    void copiedItem_goesOnElsewhere()
    {
        const QByteArray data = content(3 * 1024 * 1024 + 77);
        const QString url = QStringLiteral("https://fake.example/f/cp");
        Rig a;
        QVERIFY(writeHeld(a.tmp.filePath(QStringLiteral("temp")), QStringLiteral("00000000000000c1"), url, data, 6, true));
        QVERIFY(a.start(true));
        const quint32 id = idOf(*a.join, url);
        QVERIFY(id != 0);
        QCOMPARE(a.state(id), Ed3kHosterJoin::State::Paused);
        a.join->stop();
        eMule::BackgroundFileWriter::instance().flush();   // the .met is queued (report 43)
        const QString part = a.join->item(id)->file;
        QVERIFY(part.endsWith(QStringLiteral("00000000000000c1.hoster.part")));
        QVERIFY(QFileInfo::exists(part + QStringLiteral(".met")));

        Rig b;
        const QString temp2 = b.tmp.filePath(QStringLiteral("temp2"));
        QVERIFY(QDir().mkpath(temp2));
        QVERIFY(QFile::copy(part, QDir(temp2).filePath(QFileInfo(part).fileName())));
        QVERIFY(QFile::copy(part + QStringLiteral(".met"), QDir(temp2).filePath(QFileInfo(part).fileName() + QStringLiteral(".met"))));
        QVERIFY(b.session.start(loopback(b.tmp.filePath(QStringLiteral("cfg")))));
        b.join = std::make_unique<Ed3kHosterJoin>(b.session, b.tmp.filePath(QStringLiteral("cfg")));
        b.join->setResolver(&b.resolver);
        b.prefs.enabled = true;
        b.join->setConfig(b.prefs, QStringList{b.tmp.filePath(QStringLiteral("temp")), temp2},
                          b.tmp.filePath(QStringLiteral("incoming")));
        b.join->start();
        const quint32 bid = idOf(*b.join, url);
        QVERIFY(bid != 0);
        const auto it = *b.join->item(bid);
        QCOMPARE(it.state, Ed3kHosterJoin::State::Paused);
        QCOMPARE(it.done, std::uint64_t(6 * 256 * 1024));
        QCOMPARE(it.name, QStringLiteral("held.bin"));
        QCOMPARE(it.mirrors, QStringList{QStringLiteral("https://fake.example/f/cp-mirror")});
        QCOMPARE(it.package, QStringLiteral("Pack"));
        QVERIFY(it.packageId != 0);
        QCOMPARE(it.file, QDir(temp2).filePath(QFileInfo(part).fileName()));
        b.join->resume(bid);
        QTRY_COMPARE_WITH_TIMEOUT(b.resolver.opens, 1, 20000);
        QCOMPARE(b.resolver.lastHave, std::uint64_t(6 * 256 * 1024));
        // a copy already taken up is not a second item
        b.join->setConfig(b.prefs, QStringList{b.tmp.filePath(QStringLiteral("temp")), temp2},
                          b.tmp.filePath(QStringLiteral("incoming")));
        QCOMPARE(b.join->items().size(), 1);
        b.join->remove(bid, false);
        QVERIFY(!QFileInfo::exists(it.file));
        QVERIFY(!QFileInfo::exists(it.file + QStringLiteral(".met")));
    }

    /// "Move to" (#187): a paused item's part and .met go to the new folder and stay paused;
    /// a restart with that folder among the temp dirs finds it, and it goes on from its blocks
    void move_pausedItemGoesOnInNewFolder()
    {
        Rig r;
        r.resolver.data = content(3 * 1024 * 1024 + 77);
        const QString url = QStringLiteral("https://fake.example/f/mv1");
        const QString temp = r.tmp.filePath(QStringLiteral("temp"));
        QVERIFY(writeHeld(temp, QStringLiteral("00000000000000d1"), url, r.resolver.data, 6, true));
        QVERIFY(r.start(true));
        const quint32 id = idOf(*r.join, url);
        QVERIFY(id != 0);
        QCOMPARE(r.state(id), Ed3kHosterJoin::State::Paused);
        const QString oldPart = r.join->item(id)->file;
        const QString disk2 = r.tmp.filePath(QStringLiteral("disk2"));
        const QString newPart = QDir(disk2).filePath(QFileInfo(oldPart).fileName());

        QCOMPARE(r.join->moveTo(id, disk2), QString());
        QCOMPARE(r.join->moveTo(id, disk2), QStringLiteral("busy"));   // one move at a time
        QTRY_COMPARE_WITH_TIMEOUT(r.join->item(id)->file, newPart, 10000);
        eMule::BackgroundFileWriter::instance().flush();
        QVERIFY(QFileInfo::exists(newPart + QStringLiteral(".met")));
        QVERIFY(!QFileInfo::exists(oldPart));
        QVERIFY(!QFileInfo::exists(oldPart + QStringLiteral(".met")));
        QCOMPARE(r.state(id), Ed3kHosterJoin::State::Paused);
        {
            QFile f(newPart);
            QVERIFY(f.open(QIODevice::ReadOnly));
            QCOMPARE(f.readAll(), r.resolver.data.left(6 * 256 * 1024));
        }

        // restarted with the folder as a second temp dir, as the daemon keeps it
        r.join->stop();
        r.join.reset();
        eMule::BackgroundFileWriter::instance().flush();
        r.join = std::make_unique<Ed3kHosterJoin>(r.session, r.tmp.filePath(QStringLiteral("cfg")));
        r.join->setResolver(&r.resolver);
        r.join->setConfig(r.prefs, QStringList{temp, disk2}, r.tmp.filePath(QStringLiteral("incoming")));
        r.join->start();
        const quint32 again = idOf(*r.join, url);
        QVERIFY(again != 0);
        QCOMPARE(r.join->item(again)->file, newPart);
        QCOMPARE(r.join->item(again)->done, std::uint64_t(6 * 256 * 1024));
        r.join->resume(again);
        QTRY_COMPARE_WITH_TIMEOUT(r.state(again), Ed3kHosterJoin::State::Complete, 60000);
        QCOMPARE(r.resolver.lastHave, std::uint64_t(6 * 256 * 1024));
        QFile f(r.join->item(again)->file);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), r.resolver.data);
    }

    /// a running item pauses for the move and goes on in the new folder by itself
    void move_runningItemGoesOnThere()
    {
        Rig r;
        r.resolver.data = content(3 * 1024 * 1024 + 77);
        r.resolver.breakAt = 1800 * 1024;   // the hoster says busy once: time to move
        QVERIFY(r.start(true));
        const auto ids = r.join->addLinks(QStringLiteral("https://fake.example/f/mv2"));
        QCOMPARE(ids.size(), 1);
        const quint32 id = ids[0];
        QTRY_VERIFY_WITH_TIMEOUT(r.join->item(id)->done >= 1024 * 1024, 20000);
        QVERIFY(r.state(id) != Ed3kHosterJoin::State::Complete);
        const QString disk2 = r.tmp.filePath(QStringLiteral("disk2"));

        QCOMPARE(r.join->moveTo(id, disk2), QString());
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo(r.join->item(id)->file).absolutePath() == QDir::cleanPath(disk2)
                                     || r.state(id) == Ed3kHosterJoin::State::Complete, 10000);
        QTRY_COMPARE_WITH_TIMEOUT(r.state(id), Ed3kHosterJoin::State::Complete, 60000);
        QVERIFY(r.resolver.opens >= 2);
        QFile f(r.join->item(id)->file);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), r.resolver.data);
        QVERIFY(QDir(disk2).entryList(QDir::Files).filter(QStringLiteral(".hoster.part")).isEmpty());
    }

    /// done items, unknown ids and relative folders are refused
    void move_refusals()
    {
        Rig r;
        r.resolver.data = content(512 * 1024);
        QVERIFY(r.start(true));
        QVERIFY(!r.join->moveTo(4242, r.tmp.filePath(QStringLiteral("x"))).isEmpty());
        const auto ids = r.join->addLinks(QStringLiteral("https://fake.example/f/mv3"));
        QTRY_COMPARE_WITH_TIMEOUT(r.state(ids[0]), Ed3kHosterJoin::State::Complete, 60000);
        QCOMPARE(r.join->moveTo(ids[0], r.tmp.filePath(QStringLiteral("x"))), QStringLiteral("already complete"));
    }

    /// no resolver at all (EMULE_HOSTER_JD off): the item says so
    void noResolver_built()
    {
        QTemporaryDir tmp;
        Ed3kSession session;
        QVERIFY(session.start(loopback(tmp.filePath(QStringLiteral("cfg")))));
        Ed3kHosterJoin join(session, tmp.filePath(QStringLiteral("cfg")));
        join.start();
        const auto ids = join.addLinks(QStringLiteral("https://fake.example/f/6"));
        QCOMPARE(join.item(ids[0])->state, Ed3kHosterJoin::State::NoResolver);
    }

#ifdef EMULE_HAVE_HOSTER_JD
    void jdResolver_parsesEvents()
    {
        using K = ResolverEvent::Kind;
        auto e = Ed3kJdResolver::parseEvent(
            R"({"id":7,"ev":"found","url":"u","host":"h.example","name":"a.rar","size":42,"hashType":"sha1","hash":"ab"})");
        QVERIFY(e);
        QCOMPARE(e->first, Ed3kResolver::TaskId{7});
        QCOMPARE(e->second.kind, K::Found);
        QCOMPARE(e->second.size, std::uint64_t{42});
        QCOMPARE(e->second.hashType, QStringLiteral("sha1"));
        e = Ed3kJdResolver::parseEvent(R"({"id":3,"ev":"needsUser","kind":"login"})");
        QCOMPARE(e->second.kind, K::NeedsUser);
        QCOMPARE(e->second.text, QStringLiteral("login"));
        e = Ed3kJdResolver::parseEvent(R"({"id":3,"ev":"wait","seconds":30,"reason":"quota"})");
        QCOMPARE(e->second.kind, K::Wait);
        QCOMPARE(e->second.seconds, std::uint64_t{30});
        QCOMPARE(e->second.text, QStringLiteral("quota"));
        QVERIFY(!e->second.isFinal());
        e = Ed3kJdResolver::parseEvent(R"({"id":4,"ev":"puzzle","puzzle":9,"image":"aGk=","timeout":300})");
        QCOMPARE(e->second.image, QByteArray("hi"));
        QCOMPARE(e->second.puzzle, std::uint64_t{9});
        e = Ed3kJdResolver::parseEvent(R"({"id":5,"ev":"status","text":"puzzle"})");
        QCOMPARE(e->second.kind, K::Status);
        QCOMPARE(e->second.text, QStringLiteral("puzzle"));
        QVERIFY(!e->second.isFinal());
        // captcha solver accounts and a test solve
        e = Ed3kJdResolver::parseEvent(
            R"({"id":6,"ev":"solvers","accounts":[{"host":"2captcha.com","valid":true,"balance":1.25,"currency":"USD"},)"
            R"({"host":"x.example","valid":false,"code":"noPlugin"}]})");
        QCOMPARE(e->second.kind, K::Solvers);
        QVERIFY(e->second.isFinal());
        QCOMPARE(e->second.solvers.size(), 2);
        QCOMPARE(e->second.solvers[0].host, QStringLiteral("2captcha.com"));
        QVERIFY(e->second.solvers[0].valid);
        QCOMPARE(e->second.solvers[0].balance, 1.25);
        QCOMPARE(e->second.solvers[0].currency, QStringLiteral("USD"));
        QVERIFY(!e->second.solvers[1].valid);
        QCOMPARE(e->second.solvers[1].code, QStringLiteral("noPlugin"));
        e = Ed3kJdResolver::parseEvent(R"({"id":8,"ev":"solved","text":"KoYy2j","solver":"9kw.eu","expected":"KoYy2j"})");
        QCOMPARE(e->second.kind, K::Solved);
        QCOMPARE(e->second.text, QStringLiteral("KoYy2j"));
        QCOMPARE(e->second.host, QStringLiteral("9kw.eu"));
        QCOMPARE(e->second.name, QStringLiteral("KoYy2j"));
        QVERIFY(!Ed3kJdResolver::parseEvent(R"({"id":1,"ev":"ok"})"));
        QVERIFY(!Ed3kJdResolver::parseEvent("not json"));
    }

    void jdResolver_unavailableWithoutJava()
    {
        Ed3kJdResolver r;
        QVERIFY(!r.available());
        QSignalSpy spy(&r, &Ed3kResolver::taskEvent);
        r.match(QStringLiteral("https://x.example/"));
        QTRY_COMPARE(spy.size(), 1);
        QCOMPARE(spy.at(0).at(1).value<ResolverEvent>().kind, ResolverEvent::Kind::Failed);
    }

    /// #88: the 48 MB of plugins only while hoster downloads are on; off removes them
    void jdResolver_installsOnlyWhenEnabled()
    {
        QTemporaryDir tmp;
        const QString zip = tmp.filePath(QStringLiteral("resolver.zip"));
        QVERIFY(writeResolverZip(zip));
        const QString cfgDir = tmp.filePath(QStringLiteral("cfg"));
        const QString data = cfgDir + QStringLiteral("/ed3k/resolver");
        Ed3kJdResolver r;
        JdResolverConfig cfg;
        cfg.dataDir = data;
        cfg.zip = zip;
        cfg.java = QStringLiteral("/usr/bin/java");
        cfg.enabled = false;
        r.setConfig(cfg);
        QVERIFY(!QFileInfo::exists(cfgDir));
        QVERIFY(!r.available());

        cfg.enabled = true;
        r.setConfig(cfg);
        QVERIFY(QFileInfo(data + QStringLiteral("/plugins")).isDir());
        QVERIFY(r.available());

        cfg.enabled = false;
        r.setConfig(cfg);
        QVERIFY(!QFileInfo::exists(data + QStringLiteral("/plugins")));
        QVERIFY(!r.available());
    }

    /// #88: an archive of the old loose-classes layout is not taken
    void jdResolver_rejectsLooseClassesLayout()
    {
        QTemporaryDir tmp;
        const QString zip = tmp.filePath(QStringLiteral("resolver.zip"));
        QVERIFY(writeZip(zip, {{QStringLiteral("resolver/classes/org/emuleqt/resolver/ResolverMain.class"), "cafe"},
                               {QStringLiteral("resolver/revision.txt"), "1"}}));
        const QString data = tmp.filePath(QStringLiteral("resolver"));
        QString error;
        QVERIFY(!Ed3kJdResolver::install(zip, data, &error));
        QCOMPARE(error, QStringLiteral("not a resolver archive"));
        QVERIFY(!QFileInfo::exists(data + QStringLiteral("/plugins")));
        QVERIFY(!QFileInfo::exists(data + QStringLiteral("/plugins.tmp")));
    }

    /// Core.jar first, then the root (JD finds jd/plugins beside the jar), then the libs
    void jdResolver_classpathCoreJarPluginsLibs()
    {
        QTemporaryDir tmp;
        const QString zip = tmp.filePath(QStringLiteral("resolver.zip"));
        QVERIFY(writeResolverZip(zip));
        Ed3kJdResolver r;
        JdResolverConfig cfg;
        cfg.dataDir = tmp.filePath(QStringLiteral("resolver"));
        cfg.zip = zip;
        cfg.java = QStringLiteral("/usr/bin/java");
        cfg.enabled = true;
        r.setConfig(cfg);
        QVERIFY2(r.available(), qPrintable(r.unavailableReason()));
        const QStringList args = r.arguments();
        const qsizetype at = args.indexOf(QStringLiteral("-cp"));
        QVERIFY(at >= 0 && at + 1 < args.size());
        const QString root = QDir(cfg.dataDir).filePath(QStringLiteral("plugins/resolver"));
        QCOMPARE(args.at(at + 1).split(QDir::listSeparator()),
                 (QStringList{root + QStringLiteral("/Core.jar"), root, root + QStringLiteral("/libs/*")}));
        QVERIFY(args.contains(QStringLiteral("-Demuleqt.jdRevision=53779")));

        // a lost Core.jar: missing, not started on a broken classpath
        QVERIFY(QFile::remove(root + QStringLiteral("/Core.jar")));
        QVERIFY(!r.available());
    }

    /// Protocol 2: requests over a loopback socket, stdin only carries the token. A fake "java"
    /// writes token and port to files; the test plays the sidecar.
    void jdResolver_socketNeedsToken()
    {
#ifdef Q_OS_WIN
        QSKIP("fake java is a shell script");
#else
        QTemporaryDir tmp;
        const QString zip = tmp.filePath(QStringLiteral("resolver.zip"));
        QVERIFY(writeResolverZip(zip));
        const QString java = tmp.filePath(QStringLiteral("java"));
        {
            QFile f(java);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("#!/bin/sh\n"
                    "read t\n"
                    "for a in \"$@\"; do case \"$a\" in -Demuleqt.port=*) printf '%s' \"${a#-Demuleqt.port=}\" > port;; esac; done\n"
                    "printf '%s' \"$t\" > token.tmp && mv token.tmp token\n"
                    "exec sleep 30\n");
            f.close();
            QVERIFY(f.setPermissions(f.permissions() | QFileDevice::ExeOwner));
        }
        Ed3kJdResolver r;
        JdResolverConfig cfg;
        cfg.dataDir = tmp.filePath(QStringLiteral("resolver"));
        cfg.zip = zip;
        cfg.java = java;
        r.setConfig(cfg);
        QVERIFY2(r.available(), qPrintable(r.unavailableReason()));
        QSignalSpy events(&r, &Ed3kResolver::taskEvent);
        const auto id = r.match(QStringLiteral("https://x.example/f"));
        const QString tokenFile = QDir(cfg.dataDir).filePath(QStringLiteral("token"));
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(tokenFile), 10000);
        const auto slurp = [](const QString& path) {
            QFile f(path);
            return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
        };
        const QByteArray token = slurp(tokenFile);
        const quint16 port = slurp(QDir(cfg.dataDir).filePath(QStringLiteral("port"))).toUShort();
        QCOMPARE(token.size(), 64);
        QVERIFY(port != 0);
        QVERIFY(!r.arguments().join(u' ').contains(QString::fromLatin1(token)));   // not on the command line

        const auto auth = [](const QByteArray& t) {
            return QJsonDocument(QJsonObject{{QStringLiteral("ev"), QStringLiteral("auth")}, {QStringLiteral("token"), QString::fromLatin1(t)}})
                       .toJson(QJsonDocument::Compact)
                   + '\n';
        };
        // a wrong token is dropped, the listener stays
        QTcpSocket bad;
        bad.connectToHost(QHostAddress::LocalHost, port);
        QVERIFY(bad.waitForConnected(5000));
        bad.write(auth("00"));
        QTRY_COMPARE_WITH_TIMEOUT(bad.state(), QAbstractSocket::UnconnectedState, 5000);

        QTcpSocket good;
        good.connectToHost(QHostAddress::LocalHost, port);
        QVERIFY(good.waitForConnected(5000));
        good.write(auth(token));
        QStringList ops;
        const auto readOps = [&] {
            while (good.canReadLine())
                ops << QJsonDocument::fromJson(good.readLine()).object().value(QStringLiteral("op")).toString();
            return ops.contains(QStringLiteral("match"));
        };
        QTRY_VERIFY2_WITH_TIMEOUT(readOps(), qPrintable(ops.join(u',')), 5000);
        QCOMPARE(ops.first(), QStringLiteral("hello"));   // queued before auth, sent in order

        // the sidecar closing its end ends the open task
        good.disconnectFromHost();
        QTRY_VERIFY_WITH_TIMEOUT(!events.isEmpty(), 10000);
        QCOMPARE(events.first().at(0).value<Ed3kResolver::TaskId>(), id);
        QCOMPARE(events.first().at(1).value<ResolverEvent>().kind, ResolverEvent::Kind::Failed);
        QTRY_VERIFY_WITH_TIMEOUT(!r.running(), 10000);
#endif
    }

    /// #88: instances with one sharedDir unpack the plugins once; JD's state stays per instance
    void jdResolver_sharedDirInstallsOnce()
    {
        QTemporaryDir tmp;
        const QString zip = tmp.filePath(QStringLiteral("resolver.zip"));
        QVERIFY(writeResolverZip(zip));
        const QString shared = tmp.filePath(QStringLiteral("shared"));
        const auto config = [&](const QString& name) {
            JdResolverConfig cfg;
            cfg.dataDir = tmp.filePath(name + QStringLiteral("/ed3k/resolver"));
            cfg.sharedDir = shared;
            cfg.zip = zip;
            cfg.java = QStringLiteral("/usr/bin/java");
            return cfg;
        };
        Ed3kJdResolver a;
        Ed3kJdResolver b;
        JdResolverConfig ca = config(QStringLiteral("a"));
        a.setConfig(ca);
        b.setConfig(config(QStringLiteral("b")));
        QVERIFY2(a.available(), qPrintable(a.unavailableReason()));
        QVERIFY2(b.available(), qPrintable(b.unavailableReason()));
        QCOMPARE(QDir(shared).entryList(QDir::Dirs | QDir::NoDotAndDotDot).size(), 1);
        QCOMPARE(a.pluginRoot(), b.pluginRoot());
        QVERIFY(a.pluginRoot().startsWith(shared + u'/'));
        QVERIFY(QFileInfo::exists(QFileInfo(a.pluginRoot()).path() + QStringLiteral("/.lastused")));
        QVERIFY(!QFileInfo::exists(ca.dataDir + QStringLiteral("/plugins")));

        const QString root = a.pluginRoot();
        const QStringList args = a.arguments();
        QCOMPARE(args.at(args.indexOf(QStringLiteral("-cp")) + 1).split(QDir::listSeparator()),
                 (QStringList{root + QStringLiteral("/Core.jar"), root, root + QStringLiteral("/libs/*")}));
        QVERIFY(args.contains(QStringLiteral("-DROOT=") + QDir(ca.dataDir).filePath(QStringLiteral("jd"))));
        QVERIFY(b.arguments().contains(QStringLiteral("-DROOT=") + QDir(config(QStringLiteral("b")).dataDir).filePath(QStringLiteral("jd"))));

        // one instance off: the shared copy stays for the other
        ca.enabled = false;
        a.setConfig(ca);
        QVERIFY(!a.available());
        QVERIFY(b.available());
        QVERIFY(QFileInfo(root).isDir());

        // no zip (a test build): takes what is in the shared folder
        Ed3kJdResolver c;
        JdResolverConfig cc = config(QStringLiteral("c"));
        cc.zip.clear();
        c.setConfig(cc);
        QCOMPARE(c.pluginRoot(), root);
    }

    /// an installed version is taken while another instance holds the install lock
    void jdResolver_sharedDirInstalledIgnoresLock()
    {
        QTemporaryDir tmp;
        const QString zip = tmp.filePath(QStringLiteral("resolver.zip"));
        QVERIFY(writeResolverZip(zip));
        const QString shared = tmp.filePath(QStringLiteral("shared"));
        QString error;
        const QString root = Ed3kJdResolver::installShared(zip, shared, &error);
        QVERIFY2(!root.isEmpty(), qPrintable(error));
        QLockFile other(shared + QStringLiteral("/.install.lock"));
        QVERIFY(other.tryLock(0));
        QElapsedTimer t;
        t.start();
        QCOMPARE(Ed3kJdResolver::installShared(zip, shared, &error), root);
        QVERIFY(t.elapsed() < 2000);
    }

    /// each zip gets its own folder (mixed app versions don't unpack over each other);
    /// a version nobody started for kSharedKeepDays and left-over tmp dirs go
    void jdResolver_sharedDirVersionsAndPrune()
    {
        QTemporaryDir tmp;
        const QString shared = tmp.filePath(QStringLiteral("shared"));
        const QString zip1 = tmp.filePath(QStringLiteral("r1.zip"));
        const QString zip2 = tmp.filePath(QStringLiteral("r2.zip"));
        const QString zip3 = tmp.filePath(QStringLiteral("r3.zip"));
        QVERIFY(writeResolverZip(zip1));
        const auto other = [](const QString& path, const QByteArray& rev) {
            return writeZip(path, {{QStringLiteral("resolver/Core.jar"), "PK"},
                                   {QStringLiteral("resolver/jd/plugins/hoster/ArchiveOrg.class"), "cafe"},
                                   {QStringLiteral("resolver/revision.txt"), rev}});
        };
        QVERIFY(other(zip2, "53780"));
        QVERIFY(other(zip3, "53781"));
        QString error;
        const QString root1 = Ed3kJdResolver::installShared(zip1, shared, &error);
        const QString root2 = Ed3kJdResolver::installShared(zip2, shared, &error);
        QVERIFY(!root1.isEmpty() && !root2.isEmpty() && root1 != root2);
        QVERIFY(QFileInfo(root1).isDir());   // still in use

        // version 1 unused for long, a crashed install's tmp dir
        QFile used(QFileInfo(root1).path() + QStringLiteral("/.lastused"));
        QVERIFY(used.open(QIODevice::ReadWrite));
        QVERIFY(used.setFileTime(QDateTime::currentDateTime().addDays(-(Ed3kJdResolver::kSharedKeepDays + 1)),
                                 QFileDevice::FileModificationTime));
        used.close();
        const QString stale = shared + QStringLiteral("/0123456789ab.tmp");
        QVERIFY(QDir().mkpath(stale));
        const QString foreign = shared + QStringLiteral("/notes");
        QVERIFY(QDir().mkpath(foreign));

        const QString root3 = Ed3kJdResolver::installShared(zip3, shared, &error);
        QVERIFY2(!root3.isEmpty(), qPrintable(error));
        QVERIFY(!QFileInfo::exists(root1));
        QVERIFY(QFileInfo(root2).isDir());
        QVERIFY(!QFileInfo::exists(stale));
        QVERIFY(QFileInfo(foreign).isDir());   // not ours: kept

        QVERIFY(Ed3kJdResolver::installShared(tmp.filePath(QStringLiteral("missing.zip")), shared, &error).isEmpty());
    }

    /// switching to a shared folder drops the per-instance copy of before
    void jdResolver_sharedDirReplacesInstanceCopy()
    {
        QTemporaryDir tmp;
        const QString zip = tmp.filePath(QStringLiteral("resolver.zip"));
        QVERIFY(writeResolverZip(zip));
        Ed3kJdResolver r;
        JdResolverConfig cfg;
        cfg.dataDir = tmp.filePath(QStringLiteral("resolver"));
        cfg.zip = zip;
        cfg.java = QStringLiteral("/usr/bin/java");
        r.setConfig(cfg);
        QVERIFY(QFileInfo(cfg.dataDir + QStringLiteral("/plugins")).isDir());
        cfg.sharedDir = tmp.filePath(QStringLiteral("shared"));
        r.setConfig(cfg);
        QVERIFY(r.available());
        QVERIFY(!QFileInfo::exists(cfg.dataDir + QStringLiteral("/plugins")));
        QVERIFY(r.pluginRoot().startsWith(cfg.sharedDir));
    }

    /// #155: mixed app versions share the folder: the best version of our api wins, by revision
    void jdResolver_sharedVersionPicksOurApi()
    {
        QTemporaryDir tmp;
        const QString shared = tmp.filePath(QStringLiteral("shared"));
        const QString ours = tmp.filePath(QStringLiteral("ours.zip"));
        const QString older = tmp.filePath(QStringLiteral("older.zip"));
        const QString foreign = tmp.filePath(QStringLiteral("foreign.zip"));
        const QByteArray api = QByteArray::number(Ed3kJdResolver::kProtocol);
        QVERIFY(writeResolverZip(ours, "53800", api));
        QVERIFY(writeResolverZip(older, "53700", api));
        QVERIFY(writeResolverZip(foreign, "53900", QByteArray::number(Ed3kJdResolver::kProtocol + 1)));
        QString error;
        const QString rootOurs = Ed3kJdResolver::installShared(ours, shared, &error);
        QVERIFY(!Ed3kJdResolver::installShared(older, shared, &error).isEmpty());   // used last
        QVERIFY(!Ed3kJdResolver::installShared(foreign, shared, &error).isEmpty());
        QCOMPARE(Ed3kJdResolver::sharedVersion(shared), rootOurs);
        QVERIFY(Ed3kJdResolver::sharedVersion(tmp.filePath(QStringLiteral("none"))).isEmpty());
    }

#ifndef Q_OS_WIN
    /// #155: a read-only shared folder (filled by an admin): a zip that cannot go in leaves
    /// the resolver on the version there, never without plugins
    void jdResolver_sharedDirReadOnlyKeepsInstalled()
    {
        QTemporaryDir tmp;
        const QString shared = tmp.filePath(QStringLiteral("shared"));
        const QString zip1 = tmp.filePath(QStringLiteral("r1.zip"));
        const QString zip2 = tmp.filePath(QStringLiteral("r2.zip"));
        QVERIFY(writeResolverZip(zip1, "53800"));
        QVERIFY(writeResolverZip(zip2, "53900"));
        QString error;
        const QString root1 = Ed3kJdResolver::installShared(zip1, shared, &error);
        QVERIFY(!root1.isEmpty());
        QVERIFY(Ed3kJdResolver::dirWritable(shared));
        QVERIFY(QFile::setPermissions(shared, QFileDevice::ReadOwner | QFileDevice::ExeOwner));
        const auto restore = qScopeGuard([&] {
            QFile::setPermissions(shared, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        });
        if (Ed3kJdResolver::dirWritable(shared))
            QSKIP("running as root: permissions do not hold");
        Ed3kJdResolver r;
        JdResolverConfig cfg;
        cfg.dataDir = tmp.filePath(QStringLiteral("a/ed3k/resolver"));
        cfg.sharedDir = shared;
        cfg.zip = zip2;
        cfg.java = QStringLiteral("/usr/bin/java");
        r.setConfig(cfg);
        QVERIFY2(r.available(), qPrintable(r.unavailableReason()));
        QCOMPARE(r.pluginRoot(), root1);
        cfg.zip.clear();
        r.setConfig(cfg);
        QCOMPARE(r.pluginRoot(), root1);
    }
#endif

    /// #155: instances on one sharedPluginDir fetch the zip once: one holds .fetch.lock, the
    /// others wait and take its zip; a tick picks up what another instance fetched
    void resolverUpdater_sharedDirFetchesOnce()
    {
        QTemporaryDir tmp;
        const QString served = tmp.filePath(QStringLiteral("served.zip"));
        QVERIFY(writeResolverZip(served, "53800", QByteArray::number(Ed3kJdResolver::kProtocol)));
        QFile f(served);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QByteArray zipBytes = f.readAll();
        f.close();
        FakeHttpServer http;
        QVERIFY(http.isListening());
        int zipRequests = 0;
        http.handler = [&](const QByteArray& path, const QByteArray&) -> FakeHttpServer::Response {
            if (path == "/resolver.zip") {
                ++zipRequests;
                return {200, zipBytes};
            }
            const QJsonObject res{{QStringLiteral("url"), http.url(QStringLiteral("/resolver.zip")).toString()},
                                  {QStringLiteral("sha256"), QString::fromLatin1(fileSha256(served))},
                                  {QStringLiteral("size"), zipBytes.size()},
                                  {QStringLiteral("jdRevision"), 53800},
                                  {QStringLiteral("api"), Ed3kJdResolver::kProtocol}};
            return {200, QJsonDocument(QJsonObject{{QStringLiteral("latest"), QStringLiteral("9.9.9")},
                                                   {QStringLiteral("hosterResolver"), res}})
                             .toJson()};
        };
        const QString dir = tmp.filePath(QStringLiteral("shared/update"));
        const auto make = [&] {
            auto u = std::make_unique<Ed3kResolverUpdater>(dir);
            u->setManifestUrl(http.url(QStringLiteral("/emuleqt-version.json")));
            u->setLockRetryMs(50);
            u->setBundledZip({});
            return u;
        };
        auto a = make();
        auto b = make();
        auto c = make();
        // another instance fetches: b waits, asks nothing
        QVERIFY(QDir().mkpath(dir));
        auto other = std::make_unique<QLockFile>(dir + QStringLiteral("/.fetch.lock"));
        QVERIFY(other->tryLock(0));
        QSignalSpy readyB(b.get(), &Ed3kResolverUpdater::zipReady);
        QVERIFY(b->fetchFirst());
        QVERIFY(b->busy());
        QTest::qWait(200);
        QCOMPARE(http.requestCount(), 0);
        // it ends; a fetches before b looks again (no event loop in between)
        other.reset();
        QSignalSpy readyA(a.get(), &Ed3kResolverUpdater::zipReady);
        QVERIFY(a->fetchFirst());
        QTRY_COMPARE_WITH_TIMEOUT(readyA.size(), 1, 10000);
        // b takes a's zip instead of a second download
        QTRY_COMPARE_WITH_TIMEOUT(readyB.size(), 1, 10000);
        QTRY_VERIFY_WITH_TIMEOUT(!b->busy(), 5000);
        QCOMPARE(b->zipToUse(), a->zipToUse());
        QCOMPARE(zipRequests, 1);
        // c was up all along: its tick reads update.json, no request of its own
        QSignalSpy readyC(c.get(), &Ed3kResolverUpdater::zipReady);
        const int requests = http.requestCount();
        c->setSchedule(true, 24);
        QTRY_COMPARE_WITH_TIMEOUT(readyC.size(), 1, 5000);
        QCOMPARE(c->fetchedRevision(), 53800);
        QTest::qWait(200);
        QCOMPARE(http.requestCount(), requests);   // a's check counts for c
    }

    /// #155: switching to a shared folder copies a fetched zip along; the old folder stays
    /// (the caller drops an instance folder, never the shared one)
    void resolverUpdater_setDirCopiesZip()
    {
        QTemporaryDir tmp;
        const QString own = tmp.filePath(QStringLiteral("own"));
        const QString shared = tmp.filePath(QStringLiteral("shared/update"));
        QVERIFY(QDir().mkpath(own));
        const QString zip = own + QStringLiteral("/resolver-r53800.zip");
        QVERIFY(writeResolverZip(zip, "53800", QByteArray::number(Ed3kJdResolver::kProtocol)));
        {
            QFile st(own + QStringLiteral("/update.json"));
            QVERIFY(st.open(QIODevice::WriteOnly));
            st.write(QJsonDocument(QJsonObject{{QStringLiteral("file"), QStringLiteral("resolver-r53800.zip")},
                                               {QStringLiteral("revision"), 53800},
                                               {QStringLiteral("api"), Ed3kJdResolver::kProtocol}})
                         .toJson());
        }
        Ed3kResolverUpdater u(own);
        QCOMPARE(u.zipToUse(), zip);
        u.setDir(shared);
        QCOMPARE(u.dir(), shared);
        QCOMPARE(u.zipToUse(), shared + QStringLiteral("/resolver-r53800.zip"));
        QVERIFY(QFileInfo::exists(zip));
        // a second instance sees it there
        Ed3kResolverUpdater v(shared);
        QCOMPARE(v.fetchedRevision(), 53800);
        // and back: the shared zip is copied, not taken
        u.setDir(own + QStringLiteral("2"));
        QVERIFY(QFileInfo::exists(shared + QStringLiteral("/resolver-r53800.zip")));
        QCOMPARE(u.fetchedRevision(), 53800);
    }

    /// #88: the built resolver.zip packs JD's core; only plugins are loose files and the
    /// jars a headless resolver never loads stay out. Skips without a built zip.
    /// revision.txt, api.txt and the layout read from a zip; an old zip has no api
    void resolverZip_info()
    {
        QTemporaryDir tmp;
        const QString zip = tmp.filePath(QStringLiteral("resolver.zip"));
        QVERIFY(writeResolverZip(zip, "53800", "2"));
        auto info = Ed3kJdResolver::readZipInfo(zip);
        QCOMPARE(info.revision, 53800);
        QCOMPARE(info.api, 2);
        QVERIFY(info.layout);
        QVERIFY(writeResolverZip(zip));
        info = Ed3kJdResolver::readZipInfo(zip);
        QCOMPARE(info.revision, 53779);
        QCOMPARE(info.api, 0);
        QVERIFY(writeZip(zip, {{QStringLiteral("resolver/revision.txt"), "1"}}));
        QVERIFY(!Ed3kJdResolver::readZipInfo(zip).layout);
        QCOMPARE(Ed3kJdResolver::readZipInfo(tmp.filePath(QStringLiteral("none.zip"))).revision, 0);
    }

    /// only our api, only newer, only https (http on loopback for tests), with a hash
    void resolverUpdater_wanted()
    {
        HosterResolverRelease r;
        r.file = {QStringLiteral("https://emule-qt.org/pub/resolver.zip"), QString(64, u'a'), 1000};
        r.jdRevision = 53800;
        r.api = Ed3kJdResolver::kProtocol;
        QVERIFY(Ed3kResolverUpdater::wanted(r, 53779));
        QVERIFY(!Ed3kResolverUpdater::wanted(r, 53800));
        auto other = r;
        other.api = Ed3kJdResolver::kProtocol + 1;
        QString why;
        QVERIFY(!Ed3kResolverUpdater::wanted(other, 0, &why));
        QVERIFY2(why.contains(QStringLiteral("api")), qPrintable(why));
        other.api = 0;   // an old manifest: never
        QVERIFY(!Ed3kResolverUpdater::wanted(other, 0));
        other = r;
        other.file.url = QStringLiteral("http://emule-qt.org/pub/resolver.zip");
        QVERIFY(!Ed3kResolverUpdater::wanted(other, 0));
        other.file.url = QStringLiteral("http://127.0.0.1:1/resolver.zip");
        QVERIFY(Ed3kResolverUpdater::wanted(other, 0));
        other = r;
        other.file.sha256.clear();
        QVERIFY(!Ed3kResolverUpdater::wanted(other, 0));
    }

    /// manifest → zip fetched and checked → zipToUse() picks it over an older bundled one;
    /// a later bundled zip that caught up removes it
    void resolverUpdater_fetchesNewerZip()
    {
        QTemporaryDir tmp;
        const QString bundled = tmp.filePath(QStringLiteral("bundled.zip"));
        QVERIFY(writeResolverZip(bundled, "53779", "2"));
        const QString served = tmp.filePath(QStringLiteral("served.zip"));
        QVERIFY(writeResolverZip(served, "53800", QByteArray::number(Ed3kJdResolver::kProtocol)));
        QFile f(served);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QByteArray zipBytes = f.readAll();
        f.close();

        FakeHttpServer http;
        QVERIFY(http.isListening());
        int api = Ed3kJdResolver::kProtocol;
        http.handler = [&](const QByteArray& path, const QByteArray&) -> FakeHttpServer::Response {
            if (path == "/resolver.zip")
                return {200, zipBytes};
            const QJsonObject res{{QStringLiteral("url"), http.url(QStringLiteral("/resolver.zip")).toString()},
                                  {QStringLiteral("sha256"), QString::fromLatin1(fileSha256(served))},
                                  {QStringLiteral("size"), zipBytes.size()},
                                  {QStringLiteral("jdRevision"), 53800},
                                  {QStringLiteral("api"), api}};
            return {200, QJsonDocument(QJsonObject{{QStringLiteral("latest"), QStringLiteral("9.9.9")},
                                                   {QStringLiteral("hosterResolver"), res}})
                             .toJson()};
        };
        const QString dir = tmp.filePath(QStringLiteral("update"));
        {
            Ed3kResolverUpdater u(dir);
            u.setManifestUrl(http.url(QStringLiteral("/emuleqt-version.json")));
            u.setBundledZip(bundled);
            QCOMPARE(u.zipToUse(), bundled);
            // another api: checked, nothing fetched
            api = Ed3kJdResolver::kProtocol + 1;
            QSignalSpy checked(&u, &Ed3kResolverUpdater::checked);
            u.setSchedule(true, 24);
            QTRY_COMPARE_WITH_TIMEOUT(checked.size(), 1, 10000);
            QVERIFY(checked.first().at(0).toBool());
            QCOMPARE(u.zipToUse(), bundled);
            QVERIFY(u.lastChecked().isValid());

            api = Ed3kJdResolver::kProtocol;
            QSignalSpy ready(&u, &Ed3kResolverUpdater::zipReady);
            u.checkNow();
            QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 1, 10000);
            QCOMPARE(u.fetchedRevision(), 53800);
            QCOMPARE(u.zipToUse(), QDir(dir).filePath(QStringLiteral("resolver-r53800.zip")));
            QCOMPARE(fileSha256(u.zipToUse()), fileSha256(served));

            // the interval holds the next scheduled check back
            const int requests = http.requestCount();
            u.checkSchedule();
            QTest::qWait(200);
            QCOMPARE(http.requestCount(), requests);
        }
        {
            // a restart remembers it
            Ed3kResolverUpdater u(dir);
            u.setBundledZip(bundled);
            QCOMPARE(u.fetchedRevision(), 53800);
            QCOMPARE(u.zipToUse(), QDir(dir).filePath(QStringLiteral("resolver-r53800.zip")));
            // an application update ships a newer zip: the fetched one goes
            const QString newer = tmp.filePath(QStringLiteral("newer.zip"));
            QVERIFY(writeResolverZip(newer, "53900", "2"));
            u.setBundledZip(newer);
            QCOMPARE(u.zipToUse(), newer);
            QVERIFY(!QFileInfo::exists(QDir(dir).filePath(QStringLiteral("resolver-r53800.zip"))));
        }
    }

    /// no zip shipped: nothing at start, fetched once a hoster link wants plugins
    void resolverUpdater_noZipWaitsForLink()
    {
        QTemporaryDir tmp;
        const QString served = tmp.filePath(QStringLiteral("served.zip"));
        QVERIFY(writeResolverZip(served, "53800", QByteArray::number(Ed3kJdResolver::kProtocol)));
        QFile f(served);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QByteArray zipBytes = f.readAll();
        f.close();
        FakeHttpServer http;
        QVERIFY(http.isListening());
        http.handler = [&](const QByteArray& path, const QByteArray&) -> FakeHttpServer::Response {
            if (path == "/resolver.zip")
                return {200, zipBytes};
            const QJsonObject res{{QStringLiteral("url"), http.url(QStringLiteral("/resolver.zip")).toString()},
                                  {QStringLiteral("sha256"), QString::fromLatin1(fileSha256(served))},
                                  {QStringLiteral("size"), zipBytes.size()},
                                  {QStringLiteral("jdRevision"), 53800},
                                  {QStringLiteral("api"), Ed3kJdResolver::kProtocol}};
            return {200, QJsonDocument(QJsonObject{{QStringLiteral("latest"), QStringLiteral("9.9.9")},
                                                   {QStringLiteral("hosterResolver"), res}})
                             .toJson()};
        };
        Ed3kResolverUpdater u(tmp.filePath(QStringLiteral("update")));
        u.setManifestUrl(http.url(QStringLiteral("/emuleqt-version.json")));
        u.setBundledZip({});
        u.setSchedule(true, 24);
        QTest::qWait(200);
        QCOMPARE(http.requestCount(), 0);   // not at start

        QSignalSpy ready(&u, &Ed3kResolverUpdater::zipReady);
        QVERIFY(u.fetchFirst());
        QVERIFY(u.fetchFirst());   // running: still fetching
        QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 1, 10000);
        QCOMPARE(u.fetchedRevision(), 53800);
        QVERIFY(!u.fetchFirst());   // there now

        // the resolver asks once, and says why it waits
        JdResolverConfig cfg;
        cfg.enabled = true;
        cfg.java = tmp.filePath(QStringLiteral("no-java"));
        cfg.dataDir = tmp.filePath(QStringLiteral("resolver"));
        Ed3kJdResolver r;
        r.setConfig(cfg);
        QSignalSpy needed(&r, &Ed3kJdResolver::pluginsNeeded);
        r.wantPlugins();
        QCOMPARE(needed.size(), 1);
        r.setFetching(true);
        r.wantPlugins();
        QCOMPARE(needed.size(), 1);
        QCOMPARE(r.unavailableReason(), QStringLiteral("Downloading resolver plugins"));
        r.setFetching(false);
        cfg.zip = u.zipToUse();
        r.setConfig(cfg);
        QTRY_VERIFY_WITH_TIMEOUT(!r.installing(), 10000);
        QVERIFY(!r.pluginRoot().isEmpty());
        r.wantPlugins();
        QCOMPARE(needed.size(), 1);   // plugins there
    }

    /// a zip whose content is not what the manifest says is dropped
    void resolverUpdater_rejectsWrongZip()
    {
        QTemporaryDir tmp;
        const QString served = tmp.filePath(QStringLiteral("served.zip"));
        QVERIFY(writeResolverZip(served, "53800", "99"));   // api inside differs
        QFile f(served);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QByteArray zipBytes = f.readAll();
        FakeHttpServer http;
        http.handler = [&](const QByteArray& path, const QByteArray&) -> FakeHttpServer::Response {
            if (path == "/resolver.zip")
                return {200, zipBytes};
            const QJsonObject res{{QStringLiteral("url"), http.url(QStringLiteral("/resolver.zip")).toString()},
                                  {QStringLiteral("sha256"), QString::fromLatin1(fileSha256(served))},
                                  {QStringLiteral("size"), zipBytes.size()},
                                  {QStringLiteral("jdRevision"), 53800},
                                  {QStringLiteral("api"), Ed3kJdResolver::kProtocol}};
            return {200, QJsonDocument(QJsonObject{{QStringLiteral("latest"), QStringLiteral("9.9.9")},
                                                   {QStringLiteral("hosterResolver"), res}})
                             .toJson()};
        };
        const QString dir = tmp.filePath(QStringLiteral("update"));
        Ed3kResolverUpdater u(dir);
        u.setManifestUrl(http.url(QStringLiteral("/m.json")));
        QSignalSpy checked(&u, &Ed3kResolverUpdater::checked);
        QVERIFY(u.fetchFirst());   // no zip: only on demand
        QTRY_COMPARE_WITH_TIMEOUT(checked.size(), 1, 10000);
        QVERIFY(!checked.first().at(0).toBool());
        QVERIFY(u.zipToUse().isEmpty());
        QVERIFY(!u.fetchFirst());   // retried after kFirstRetrySecs only
        QVERIFY(QDir(dir).entryList({QStringLiteral("*.zip")}, QDir::Files).isEmpty());
    }

    /// a new zip waits while the sidecar runs; once it ends, the new plugins are unpacked
    void jdResolver_newZipWaitsForIdle()
    {
#ifdef Q_OS_WIN
        QSKIP("fake java is a shell script");
#else
        QTemporaryDir tmp;
        const QString oldZip = tmp.filePath(QStringLiteral("old.zip"));
        const QString newZip = tmp.filePath(QStringLiteral("new.zip"));
        QVERIFY(writeResolverZip(oldZip, "53779", "2"));
        QVERIFY(writeResolverZip(newZip, "53800", "2"));
        const QString java = tmp.filePath(QStringLiteral("java"));
        {
            QFile f(java);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("#!/bin/sh\necho $$ > pid\nexec sleep 30\n");
            f.close();
            QVERIFY(f.setPermissions(f.permissions() | QFileDevice::ExeOwner));
        }
        Ed3kJdResolver r;
        JdResolverConfig cfg;
        cfg.dataDir = tmp.filePath(QStringLiteral("resolver"));
        cfg.zip = oldZip;
        cfg.java = java;
        r.setConfig(cfg);
        QCOMPARE(r.engineVersion(), QStringLiteral("r53779"));
        QSignalSpy events(&r, &Ed3kResolver::taskEvent);
        const auto first = r.match(QStringLiteral("https://x.example/f"));
        QVERIFY(r.running());

        cfg.zip = newZip;
        r.setConfig(cfg);
        QVERIFY(r.running());   // not cut
        QCOMPARE(r.pendingZip(), newZip);
        QCOMPARE(r.pendingVersion(), QStringLiteral("r53800"));
        QCOMPARE(r.engineVersion(), QStringLiteral("r53779"));
        QCOMPARE(r.config().zip, oldZip);

        // the sidecar ends (killed here; the idle stop takes the same path)
        const QString pidFile = QDir(cfg.dataDir).filePath(QStringLiteral("pid"));
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo(pidFile).size() > 0, 10000);
        QFile pf(pidFile);
        QVERIFY(pf.open(QIODevice::ReadOnly));
        QVERIFY(::kill(pid_t(pf.readAll().trimmed().toInt()), SIGTERM) == 0);
        QTRY_VERIFY_WITH_TIMEOUT(!r.running(), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(!events.isEmpty(), 10000);   // the open task failed, not lost
        QCOMPARE(events.first().at(0).value<Ed3kResolver::TaskId>(), first);
        QTRY_VERIFY_WITH_TIMEOUT(!r.installing(), 10000);
        QVERIFY(r.pendingZip().isEmpty());
        QCOMPARE(r.config().zip, newZip);
        QCOMPARE(r.engineVersion(), QStringLiteral("r53800"));
        QVERIFY2(r.unavailableReason().isEmpty(), qPrintable(r.unavailableReason()));
#endif
    }

    void bundledZip_layout()
    {
        const QString zip = Ed3kJdResolver::findBundledZip();
        if (zip.isEmpty())
            QSKIP("no resolver.zip: run scripts/build-hoster-resolver.sh");
        ArchiveReader reader;
        QVERIFY2(reader.open(zip), qPrintable(reader.lastError()));
        const QStringList names = reader.entryNames();
        QVERIFY(names.contains(QStringLiteral("resolver/Core.jar")));
        QVERIFY(names.contains(QStringLiteral("resolver/revision.txt")));
        QVERIFY(names.contains(QStringLiteral("resolver/jd/plugins/hoster/ArchiveOrg.class")));
        QVERIFY(names.contains(QStringLiteral("resolver/libs/seamless-util.jar")));   // ArchiveOrg uses it
        QVERIFY(names.contains(QStringLiteral("resolver/libs/bcprov-jdk15on.jar")));
        qsizetype files = 0;
        for (const QString& n : names) {
            QVERIFY2(!n.startsWith(QStringLiteral("resolver/classes/")), qPrintable(n));
            const QString base = n.section(u'/', -1);
            for (const auto* dropped : {"cling-", "seamless-http", "seamless-xml", "jsvg", "jna_platform.jar"})
                QVERIFY2(!(n.startsWith(QStringLiteral("resolver/libs/")) && base.startsWith(QLatin1StringView(dropped))), qPrintable(n));
            if (!n.endsWith(u'/'))
                ++files;
            if (n.endsWith(QStringLiteral(".class")))
                QVERIFY2(n.startsWith(QStringLiteral("resolver/jd/plugins/hoster/"))
                             || n.startsWith(QStringLiteral("resolver/jd/plugins/decrypter/")),
                         qPrintable(n));
        }
        QVERIFY2(files < 4000, qPrintable(QString::number(files)));
    }

    /// off: a request fails at once and creates no JD folder
    void jdResolver_offCreatesNoJdDir()
    {
        QTemporaryDir tmp;
        const QString data = tmp.filePath(QStringLiteral("cfg/ed3k/resolver"));
        Ed3kJdResolver r;
        JdResolverConfig cfg;
        cfg.dataDir = data;
        cfg.java = QStringLiteral("/usr/bin/java");
        cfg.enabled = false;
        r.setConfig(cfg);
        QSignalSpy spy(&r, &Ed3kResolver::taskEvent);
        r.match(QStringLiteral("https://x.example/"));
        QTRY_COMPARE(spy.size(), 1);
        QCOMPARE(spy.at(0).at(1).value<ResolverEvent>().kind, ResolverEvent::Kind::Failed);
        QVERIFY(!QFileInfo::exists(tmp.filePath(QStringLiteral("cfg"))));
    }

    /// one folder for ed3k: the runtime under config/ed3k/java
    void javaRuntime_livesUnderEd3kDir()
    {
        QTemporaryDir tmp;
        const QString ed3kDir = tmp.filePath(QStringLiteral("Config/ed3k"));
        Ed3kJavaRuntime java(ed3kDir);
        QCOMPARE(java.runtimeDir(), ed3kDir + QStringLiteral("/java/jre"));
    }

    /// a runtime an older version fetched to <eMuleQt>/runtime/jre is moved in, not fetched again
    void javaRuntime_oldRuntimeDirIsMoved()
    {
        QTemporaryDir tmp;
        const QString legacy = tmp.filePath(QStringLiteral("runtime/jre"));
        QVERIFY(QDir().mkpath(legacy + QStringLiteral("/bin")));
        {
            QFile f(legacy + QStringLiteral("/release"));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("JAVA_VERSION=\"21\"");
        }
        const QString ed3kDir = tmp.filePath(QStringLiteral("Config/ed3k"));
        Ed3kJavaRuntime java(ed3kDir, legacy);
        java.locate(QString());
        QVERIFY(QFileInfo::exists(ed3kDir + QStringLiteral("/java/jre/release")));
        QVERIFY(!QFileInfo::exists(legacy));
        QVERIFY(!QFileInfo::exists(tmp.filePath(QStringLiteral("runtime"))));
    }

    /// nothing is fetched until hoster downloads are switched on
    void javaRuntime_downloadNeedsHosters()
    {
        QTemporaryDir tmp;
        Ed3kJavaRuntime java(tmp.path());
        QVERIFY(!java.allowed());
        QVERIFY(!java.download());
        QCOMPARE(java.state(), Ed3kJavaRuntime::State::Unknown);
        QVERIFY(!QFileInfo::exists(java.runtimeDir() + QStringLiteral(".tmp")));
    }

    /// #155: with sharedPluginDir the runtime goes to <shared>/java/jre; probed before the
    /// instance's own; a read-only shared folder is probed but fetched into the instance's
    void javaRuntime_sharedDir()
    {
#ifdef Q_OS_WIN
        const QString kJavaExe = QStringLiteral("java.exe");
#else
        const QString kJavaExe = QStringLiteral("java");
#endif
        QTemporaryDir tmp;
        const QString ed3kDir = tmp.filePath(QStringLiteral("Config/ed3k"));
        const QString shared = tmp.filePath(QStringLiteral("shared"));
        const auto fakeJava = [&kJavaExe](const QString& home) {
            QVERIFY(QDir().mkpath(home + QStringLiteral("/bin")));
            QFile j(home + QStringLiteral("/bin/") + kJavaExe);
            QVERIFY(j.open(QIODevice::WriteOnly));
        };
        fakeJava(shared + QStringLiteral("/java/jre"));
        fakeJava(ed3kDir + QStringLiteral("/java/jre"));
        Ed3kJavaRuntime java(ed3kDir);
        java.setSharedDir(shared, true);
        QCOMPARE(java.runtimeDir(), shared + QStringLiteral("/java/jre"));
        const QStringList c = java.candidates({});
        const qsizetype s = c.indexOf(QDir::cleanPath(shared + QStringLiteral("/java/jre/bin/") + kJavaExe));
        const qsizetype o = c.indexOf(QDir::cleanPath(ed3kDir + QStringLiteral("/java/jre/bin/") + kJavaExe));
        QVERIFY(s >= 0 && o > s);
        java.setSharedDir(shared, false);
        QCOMPARE(java.runtimeDir(), ed3kDir + QStringLiteral("/java/jre"));
    }

    /// #155: an instance's own runtime moves into an empty shared folder, not fetched again
    void javaRuntime_sharedDirAdoptsOwn()
    {
        QTemporaryDir tmp;
        const QString ed3kDir = tmp.filePath(QStringLiteral("Config/ed3k"));
        const QString shared = tmp.filePath(QStringLiteral("shared"));
        QVERIFY(QDir().mkpath(ed3kDir + QStringLiteral("/java/jre/bin")));
        {
            QFile f(ed3kDir + QStringLiteral("/java/jre/release"));
            QVERIFY(f.open(QIODevice::WriteOnly));
        }
        Ed3kJavaRuntime java(ed3kDir);
        java.setSharedDir(shared, true);
        java.locate(QString());
        QVERIFY(QFileInfo::exists(shared + QStringLiteral("/java/jre/release")));
        QVERIFY(!QFileInfo::exists(ed3kDir + QStringLiteral("/java/jre")));
        QTRY_VERIFY_WITH_TIMEOUT(java.state() != Ed3kJavaRuntime::State::Probing, 60000);
    }

#ifndef Q_OS_WIN
    /// #155: another instance fetches into the shared folder: this one waits (no request of
    /// its own), then probes what that one installed and drops its own copy
    void javaRuntime_sharedDirWaitsForOtherFetch()
    {
        QTemporaryDir tmp;
        const QString ed3kDir = tmp.filePath(QStringLiteral("Config/ed3k"));
        const QString shared = tmp.filePath(QStringLiteral("shared"));
        QVERIFY(QDir().mkpath(shared + QStringLiteral("/java")));
        auto other = std::make_unique<QLockFile>(shared + QStringLiteral("/java/.install.lock"));
        QVERIFY(other->tryLock(0));
        // an own broken copy, kept until the shared one runs
        QVERIFY(QDir().mkpath(ed3kDir + QStringLiteral("/java/jre")));
        Ed3kJavaRuntime java(ed3kDir);
        java.setLockRetryMs(50);
        java.setSharedDir(shared, true);
        java.setAllowed(true);
        QVERIFY(java.download());
        QCOMPARE(java.state(), Ed3kJavaRuntime::State::Downloading);
        QTest::qWait(200);
        QCOMPARE(java.state(), Ed3kJavaRuntime::State::Downloading);
        QVERIFY(!QFileInfo::exists(shared + QStringLiteral("/java/jre.tmp")));
        // the other instance installs a runtime (a script answering like java) and ends
        const QString home = shared + QStringLiteral("/java/jre");
        QVERIFY(QDir().mkpath(home + QStringLiteral("/bin")));
        QFile j(home + QStringLiteral("/bin/java"));
        QVERIFY(j.open(QIODevice::WriteOnly));
        j.write(QStringLiteral("#!/bin/sh\necho 'java.specification.version = 21'\necho 'java.vendor = Test'\n"
                               "echo 'os.arch = %1'\n").arg(Ed3kJavaRuntime::hostArch()).toUtf8());
        j.close();
        QVERIFY(j.setPermissions(j.permissions() | QFileDevice::ExeOwner));
        other.reset();
        QTRY_COMPARE_WITH_TIMEOUT(java.state(), Ed3kJavaRuntime::State::Ready, 15000);
        QCOMPARE(java.runtime()->java, home + QStringLiteral("/bin/java"));
        QVERIFY(!QFileInfo::exists(ed3kDir + QStringLiteral("/java/jre")));
    }
#endif

    void javaRuntime_parsesProperties()
    {
        const QByteArray out = "Property settings:\n    java.home = /opt/jdk\n    java.specification.version = 21\n"
                               "    java.vendor = Eclipse Adoptium\n    os.arch = aarch64\nopenjdk version \"21.0.5\"\n";
        const auto info = Ed3kJavaRuntime::parseProperties(out, QStringLiteral("/opt/jdk/bin/java"));
        QVERIFY(info);
        QCOMPARE(info->feature, 21);
        QCOMPARE(info->arch, QStringLiteral("arm64"));
        QCOMPARE(info->vendor, QStringLiteral("Eclipse Adoptium"));
        const auto old = Ed3kJavaRuntime::parseProperties("    java.specification.version = 1.8\n    os.arch = amd64\n", {});
        QCOMPARE(old->feature, 8);
        QCOMPARE(old->arch, QStringLiteral("x86_64"));
        QVERIFY(!Ed3kJavaRuntime::parseProperties("garbage", {}));
    }

    void javaRuntime_parsesAdoptium()
    {
        // no multi-line raw string here: moc loses the next slot after one
        const QByteArray json = "[{\"binary\":{\"package\":{\"link\":\"https://github.com/adoptium/x.tar.gz\","
                                "\"checksum\":\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\","
                                "\"name\":\"x.tar.gz\",\"size\":47000000}}}]";
        const auto p = Ed3kJavaRuntime::parseAdoptium(json);
        QVERIFY(p);
        QCOMPARE(p->name, QStringLiteral("x.tar.gz"));
        QCOMPARE(p->size, qint64{47000000});
        // a name with a path, a plain http link or a bad checksum is refused
        QVERIFY(!Ed3kJavaRuntime::parseAdoptium(QByteArray(json).replace("x.tar.gz\",\"size", "../x\",\"size")));
        QVERIFY(!Ed3kJavaRuntime::parseAdoptium(QByteArray(json).replace("https://github", "http://github")));
        QVERIFY(!Ed3kJavaRuntime::parseAdoptium(QByteArray(json).replace("0123456789abcdef0123", "xyz")));
        QVERIFY(Ed3kJavaRuntime::adoptiumUrl().startsWith(QStringLiteral("https://api.adoptium.net/v3/assets/latest/21/")));
    }

    void javaRuntime_candidatesStartWithPref()
    {
        QTemporaryDir tmp;
        const QString fake = tmp.filePath(QStringLiteral("java"));
        QFile f(fake);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.close();
        Ed3kJavaRuntime rt(tmp.path());
        const QStringList c = rt.candidates(fake);
        QVERIFY(!c.isEmpty());
        QCOMPARE(c.first(), QDir::cleanPath(fake));
        QCOMPARE(c.count(QDir::cleanPath(fake)), 1);
    }

    /// Per-user installs are looked for: %LOCALAPPDATA%\Programs\<vendor> on Windows,
    /// ~/.sdkman elsewhere.
    void javaRuntime_candidatesHavePerUserInstalls()
    {
        QTemporaryDir tmp;
#ifdef Q_OS_WIN
        const char* var = "LOCALAPPDATA";
        const QString home = tmp.filePath(QStringLiteral("Programs/Eclipse Adoptium/jdk-21.0.5.11-hotspot"));
        const QString exe = QStringLiteral("bin/java.exe");
#else
        const char* var = "HOME";
        const QString home = tmp.filePath(QStringLiteral(".sdkman/candidates/java/21.0.5-tem"));
        const QString exe = QStringLiteral("bin/java");
#endif
        QVERIFY(QDir().mkpath(QDir(home).filePath(QStringLiteral("bin"))));
        QFile f(QDir(home).filePath(exe));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.close();
        const QByteArray saved = qgetenv(var);
        qputenv(var, tmp.path().toLocal8Bit());
        Ed3kJavaRuntime rt(tmp.filePath(QStringLiteral("ed3k")));
        const QStringList c = rt.candidates({});
        qputenv(var, saved);
        QVERIFY2(c.contains(QDir::cleanPath(f.fileName())), qPrintable(c.join(u'\n')));
    }

    /// Windows: a JDK found by its installer's registry key alone (no PATH, JAVA_HOME or
    /// Program Files). Skipped without one installed.
    void javaRuntime_candidatesReadRegistry()
    {
#ifndef Q_OS_WIN
        QSKIP("Windows only");
#else
        QStringList homes;
        for (const char* key : {"Microsoft\\JDK", "Eclipse Adoptium\\JDK", "Eclipse Adoptium\\JRE"}) {
            QSettings reg(QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\") + QString::fromLatin1(key), QSettings::Registry64Format);
            for (const QString& v : reg.childGroups())
                homes << reg.value(v + QStringLiteral("/hotspot/MSI/Path")).toString();
        }
        homes.removeAll(QString());
        if (homes.isEmpty())
            QSKIP("no Temurin or Microsoft JDK installed");
        const QString java = QDir::cleanPath(QDir(homes.first()).filePath(QStringLiteral("bin/java.exe")));
        const char* vars[] = {"JAVA_HOME", "PATH", "ProgramFiles", "ProgramW6432", "LOCALAPPDATA"};
        QList<QByteArray> saved;
        for (const char* v : vars) {
            saved << qgetenv(v);
            qunsetenv(v);
        }
        QTemporaryDir tmp;
        Ed3kJavaRuntime rt(tmp.path());
        const QStringList c = rt.candidates({});
        for (int i = 0; i < int(std::size(vars)); ++i)
            qputenv(vars[i], saved[i]);
        QVERIFY2(c.contains(java), qPrintable(c.join(u'\n')));
#endif
    }

    /// The real thing: a Java found on this machine, resolver.zip installed, JDownloader's
    /// archive.org plugin, the file through the join into incoming. Network: only with
    /// EMULE_HOSTER_LIVE=1 and a built resolver.zip (scripts/build-hoster-resolver.sh).
    void live_jdResolverDownloads()
    {
        if (qEnvironmentVariable("EMULE_HOSTER_LIVE") != QLatin1StringView("1"))
            QSKIP("live: set EMULE_HOSTER_LIVE=1");
        const QString zip = Ed3kJdResolver::findBundledZip();
        if (zip.isEmpty())
            QSKIP("no resolver.zip: run scripts/build-hoster-resolver.sh");
        QTemporaryDir tmp;
        Ed3kJavaRuntime java(tmp.path());
        java.locate({});
        QTRY_VERIFY_WITH_TIMEOUT(java.state() != Ed3kJavaRuntime::State::Probing, 60000);
        if (!java.runtime())
            QSKIP("no Java 17+ on this machine");

        Ed3kJdResolver resolver;
        JdResolverConfig cfg;
        cfg.java = java.runtime()->java;
        cfg.dataDir = tmp.filePath(QStringLiteral("resolver"));
        cfg.zip = zip;
        resolver.setConfig(cfg);
        QVERIFY2(resolver.available(), qPrintable(resolver.unavailableReason()));
        // a puzzle image of an earlier run: cleared when the sidecar starts
        const QString oldPuzzle = tmp.filePath(QStringLiteral("resolver/jd/captchas/k2s.cc/old.jpg"));
        QVERIFY(QDir().mkpath(QFileInfo(oldPuzzle).path()));
        QFile f(oldPuzzle);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("jpg");
        f.close();

        Ed3kSession session;
        QVERIFY(session.start(loopback(tmp.filePath(QStringLiteral("cfg")))));
        Ed3kHosterJoin join(session, tmp.filePath(QStringLiteral("cfg")));
        join.setResolver(&resolver);
        Ed3kHosterPrefs prefs;
        prefs.enabled = true;
        join.setConfig(prefs, tmp.filePath(QStringLiteral("temp")), tmp.filePath(QStringLiteral("incoming")));
        join.start();
        const auto ids = join.addLinks(QStringLiteral("https://archive.org/download/testmp3testfile/mpthreetest.mp3"));
        QCOMPARE(ids.size(), 1);
        QTRY_VERIFY_WITH_TIMEOUT(join.item(ids[0])->state == Ed3kHosterJoin::State::Complete
                                     || join.item(ids[0])->state == Ed3kHosterJoin::State::Failed,
                                 180000);
        const auto item = *join.item(ids[0]);
        QVERIFY2(item.state == Ed3kHosterJoin::State::Complete, qPrintable(item.reasonArg));
        QCOMPARE(item.name, QStringLiteral("mpthreetest.mp3"));
        QCOMPARE(QFileInfo(item.file).size(), qint64(198658));
        QVERIFY(!item.rootHex.isEmpty());
        QVERIFY(!QFileInfo::exists(oldPuzzle));
    }

    /// Every link container in data/dlc through the real sidecar (JD's DLC plugin asks
    /// service.jdownloader.org for each key): links come back, with the package names.
    /// EMULE_HOSTER_LIVE=1; the service refuses for a while after many files ("limit": skipped).
    void live_jdResolverOpensContainers()
    {
        if (qEnvironmentVariable("EMULE_HOSTER_LIVE") != QLatin1StringView("1"))
            QSKIP("live: set EMULE_HOSTER_LIVE=1");
        const QString zip = Ed3kJdResolver::findBundledZip();
        if (zip.isEmpty())
            QSKIP("no resolver.zip: run scripts/build-hoster-resolver.sh");
        const QDir dlcDir(QStringLiteral(EMULE_STRINGIFY(EMULE_PROJECT_DATA_DIR) "/dlc"));
        const QFileInfoList files = dlcDir.entryInfoList({QStringLiteral("*.dlc"), QStringLiteral("*.rsdf"), QStringLiteral("*.ccf")},
                                                        QDir::Files, QDir::Name);
        if (files.isEmpty())
            QSKIP("no containers in data/dlc");
        QTemporaryDir tmp;
        Ed3kJavaRuntime java(tmp.path());
        java.locate({});
        QTRY_VERIFY_WITH_TIMEOUT(java.state() != Ed3kJavaRuntime::State::Probing, 60000);
        if (!java.runtime())
            QSKIP("no Java 17+ on this machine");
        Ed3kJdResolver resolver;
        JdResolverConfig cfg;
        cfg.java = java.runtime()->java;
        cfg.dataDir = tmp.filePath(QStringLiteral("resolver"));
        cfg.zip = zip;
        resolver.setConfig(cfg);
        QVERIFY2(resolver.available(), qPrintable(resolver.unavailableReason()));

        Ed3kSession session;
        QVERIFY(session.start(loopback(tmp.filePath(QStringLiteral("cfg")))));
        Ed3kHosterJoin join(session, tmp.filePath(QStringLiteral("cfg")));
        join.setResolver(&resolver);
        Ed3kHosterPrefs prefs;
        prefs.enabled = true;
        join.setConfig(prefs, tmp.filePath(QStringLiteral("temp")), tmp.filePath(QStringLiteral("incoming")));
        // no join.start(): a container needs only the resolver
        for (const QFileInfo& fi : files) {
            QFile f(fi.filePath());
            QVERIFY(f.open(QIODevice::ReadOnly));
            std::optional<Opened> got;
            const auto c = connect(&join, &Ed3kHosterJoin::containerOpened, this, opened(got));
            QVERIFY(join.openContainer(f.readAll(), fi.fileName()) != 0);
            QTRY_VERIFY_WITH_TIMEOUT(got, 180000);
            disconnect(c);
            if (got->code == QLatin1StringView("limit"))
                QSKIP(qPrintable(QStringLiteral("DLC service limit at %1").arg(fi.fileName())));
            QVERIFY2(!got->links.isEmpty(), qPrintable(fi.fileName() + u' ' + got->code + u' ' + got->text));
            for (const ResolverLink& l : std::as_const(got->links)) {
                QVERIFY2(l.url.startsWith(QStringLiteral("http")), qPrintable(l.url));
                qInfo("%s: [%s] %s %s pw=%s", qPrintable(fi.fileName()), qPrintable(l.package), qPrintable(l.name),
                      qPrintable(l.url), qPrintable(l.passwords.join(u',')));
            }
            // the test files made for this (data/dlc/archive-*.dlc): known contents
            if (fi.fileName() == QLatin1StringView("archive-test-mp3.dlc")) {
                QCOMPARE(got->links.size(), 1);
                QCOMPARE(got->links[0].url, QStringLiteral("https://archive.org/download/testmp3testfile/mpthreetest.mp3"));
                QCOMPARE(got->links[0].name, QStringLiteral("mpthreetest.mp3"));
                QCOMPARE(got->links[0].package, QStringLiteral("Archive Test MP3"));
            } else if (fi.fileName() == QLatin1StringView("archive-two-packages.dlc")) {
                QCOMPARE(got->links.size(), 2);
                QCOMPARE(got->links[1].package, QStringLiteral("Archive Sample Video"));
                QStringList pws = got->links[1].passwords;   // JD keeps them in a hash set
                pws.sort();
                QCOMPARE(pws, (QStringList{QStringLiteral("emuleqt"), QStringLiteral("second pw")}));
            }
        }
    }

    /// Click'n'Load through the real sidecar: JD's DummyCNL runs jk in Rhino for the key and
    /// decrypts an archive.org link (AES-128-CBC, key = IV = "1234567890123456"). EMULE_HOSTER_LIVE=1.
    void live_jdResolverClickNLoad()
    {
        if (qEnvironmentVariable("EMULE_HOSTER_LIVE") != QLatin1StringView("1"))
            QSKIP("live: set EMULE_HOSTER_LIVE=1");
        const QString zip = Ed3kJdResolver::findBundledZip();
        if (zip.isEmpty())
            QSKIP("no resolver.zip: run scripts/build-hoster-resolver.sh");
        QTemporaryDir tmp;
        Ed3kJavaRuntime java(tmp.path());
        java.locate({});
        QTRY_VERIFY_WITH_TIMEOUT(java.state() != Ed3kJavaRuntime::State::Probing, 60000);
        if (!java.runtime())
            QSKIP("no Java 17+ on this machine");
        Ed3kJdResolver resolver;
        JdResolverConfig cfg;
        cfg.java = java.runtime()->java;
        cfg.dataDir = tmp.filePath(QStringLiteral("resolver"));
        cfg.zip = zip;
        resolver.setConfig(cfg);
        QVERIFY2(resolver.available(), qPrintable(resolver.unavailableReason()));
        Ed3kSession session;
        QVERIFY(session.start(loopback(tmp.filePath(QStringLiteral("cfg")))));
        Ed3kHosterJoin join(session, tmp.filePath(QStringLiteral("cfg")));
        join.setResolver(&resolver);
        Ed3kHosterPrefs prefs;
        prefs.enabled = true;
        join.setConfig(prefs, tmp.filePath(QStringLiteral("temp")), tmp.filePath(QStringLiteral("incoming")));
        const QString crypted = QStringLiteral("LqIguTAYsa6MSCA2694wFP+1AkY5NCqrJT+rwTSim5sbuiEKlUIpoKaS79CYxwl7na46P0CyJW0ygblIUgUBoQ==");
        const QString want = QStringLiteral("https://archive.org/download/testmp3testfile/mpthreetest.mp3");
        // jk (a JavaScript function), then k (the key as hex)
        for (const bool viaJk : {true, false}) {
            Ed3kHosterJoin::ClickNLoad post;
            post.crypted = crypted;
            if (viaJk)
                post.jk = QStringLiteral("function f(){ return '31323334353637383930313233343536';}");
            else
                post.k = QStringLiteral("31323334353637383930313233343536");
            post.package = QStringLiteral("CNL Test");
            std::optional<Opened> got;
            const auto c = connect(&join, &Ed3kHosterJoin::clickNLoadOpened, this, opened(got));
            QVERIFY(join.openClickNLoad(post) != 0);
            QTRY_VERIFY_WITH_TIMEOUT(got, 180000);
            disconnect(c);
            QVERIFY2(got->code.isEmpty(), qPrintable(got->code + u' ' + got->text));
            QCOMPARE(got->links.size(), 1);
            QCOMPARE(got->links[0].url, want);
            QCOMPARE(got->links[0].package, QStringLiteral("CNL Test"));
        }
    }

    /// #88: two sidecars at once from one shared copy; JD writes nothing into it
    void live_jdResolverSharedDir()
    {
        if (qEnvironmentVariable("EMULE_HOSTER_LIVE") != QLatin1StringView("1"))
            QSKIP("live: set EMULE_HOSTER_LIVE=1");
        const QString zip = Ed3kJdResolver::findBundledZip();
        if (zip.isEmpty())
            QSKIP("no resolver.zip: run scripts/build-hoster-resolver.sh");
        QTemporaryDir tmp;
        Ed3kJavaRuntime java(tmp.path());
        java.locate({});
        QTRY_VERIFY_WITH_TIMEOUT(java.state() != Ed3kJavaRuntime::State::Probing, 60000);
        if (!java.runtime())
            QSKIP("no Java 17+ on this machine");
        const QString shared = tmp.filePath(QStringLiteral("shared"));
        Ed3kJdResolver a;
        Ed3kJdResolver b;
        for (auto [r, name] : {std::pair{&a, "a"}, std::pair{&b, "b"}}) {
            JdResolverConfig cfg;
            cfg.java = java.runtime()->java;
            cfg.dataDir = tmp.filePath(QLatin1StringView(name) + QStringLiteral("/resolver"));
            cfg.sharedDir = shared;
            cfg.zip = zip;
            r->setConfig(cfg);
            QVERIFY2(r->available(), qPrintable(r->unavailableReason()));
        }
        QCOMPARE(a.pluginRoot(), b.pluginRoot());
        const QDateTime installed = QDateTime::currentDateTime();

        const QString url = QStringLiteral("https://archive.org/download/testmp3testfile/mpthreetest.mp3");
        std::optional<ResolverEvent> ra;
        std::optional<ResolverEvent> rb;
        connect(&a, &Ed3kResolver::taskEvent, this, [&ra](Ed3kResolver::TaskId, const ResolverEvent& e) {
            if (e.isFinal())
                ra = e;
        });
        connect(&b, &Ed3kResolver::taskEvent, this, [&rb](Ed3kResolver::TaskId, const ResolverEvent& e) {
            if (e.isFinal())
                rb = e;
        });
        a.check(url, std::nullopt);
        b.check(url, std::nullopt);
        QTRY_VERIFY_WITH_TIMEOUT(ra && rb, 180000);
        QVERIFY2(ra->kind == ResolverEvent::Kind::Found, qPrintable(ra->code + u' ' + ra->text));
        QVERIFY2(rb->kind == ResolverEvent::Kind::Found, qPrintable(rb->code + u' ' + rb->text));
        QCOMPARE(ra->size, quint64(198658));
        QCOMPARE(rb->size, quint64(198658));

        // JD's state went to each instance's jd/, not the shared plugins
        QVERIFY(QFileInfo(tmp.filePath(QStringLiteral("a/resolver/jd/cfg"))).isDir());
        QVERIFY(QFileInfo(tmp.filePath(QStringLiteral("b/resolver/jd/cfg"))).isDir());
        QDirIterator it(a.pluginRoot(), QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            it.next();
            if (it.fileInfo().isFile())
                QVERIFY2(it.fileInfo().lastModified() < installed, qPrintable(it.filePath()));
        }
    }

    /// Resume through the real sidecar: the front of the file in data.part, JD goes on from
    /// there (archive.org takes ranges) and tells from = have; the whole file is the same.
    void live_jdResolverResumes()
    {
        if (qEnvironmentVariable("EMULE_HOSTER_LIVE") != QLatin1StringView("1"))
            QSKIP("live: set EMULE_HOSTER_LIVE=1");
        const QString zip = Ed3kJdResolver::findBundledZip();
        if (zip.isEmpty())
            QSKIP("no resolver.zip: run scripts/build-hoster-resolver.sh");
        QTemporaryDir tmp;
        Ed3kJavaRuntime java(tmp.path());
        java.locate({});
        QTRY_VERIFY_WITH_TIMEOUT(java.state() != Ed3kJavaRuntime::State::Probing, 60000);
        if (!java.runtime())
            QSKIP("no Java 17+ on this machine");
        Ed3kJdResolver resolver;
        JdResolverConfig cfg;
        cfg.java = java.runtime()->java;
        cfg.dataDir = tmp.filePath(QStringLiteral("resolver"));
        cfg.zip = zip;
        resolver.setConfig(cfg);
        QVERIFY2(resolver.available(), qPrintable(resolver.unavailableReason()));

        const QString url = QStringLiteral("https://archive.org/download/testmp3testfile/mpthreetest.mp3");
        // the whole file once, for its bytes
        std::map<Ed3kResolver::TaskId, std::vector<ResolverEvent>> seen;
        connect(&resolver, &Ed3kResolver::taskEvent, this,
                [&seen](Ed3kResolver::TaskId t, const ResolverEvent& e) { seen[t].push_back(e); });
        const auto done = [&seen](Ed3kResolver::TaskId t) -> const ResolverEvent* {
            for (const ResolverEvent& e : seen[t])
                if (e.isFinal())
                    return &e;
            return nullptr;
        };
        const QString dir1 = tmp.filePath(QStringLiteral("t1"));
        QDir().mkpath(dir1);
        const auto t1 = resolver.open(url, std::nullopt, dir1, 0);
        QTRY_VERIFY_WITH_TIMEOUT(done(t1), 180000);
        QCOMPARE(done(t1)->kind, ResolverEvent::Kind::Done);
        QFile whole(done(t1)->path);
        QVERIFY(whole.open(QIODevice::ReadOnly));
        const QByteArray data = whole.readAll();
        QVERIFY(data.size() > 100000);

        // again with the first half on disk
        const qsizetype have = data.size() / 2;
        const QString dir2 = tmp.filePath(QStringLiteral("t2"));
        QDir().mkpath(dir2);
        {
            QFile part(QDir(dir2).filePath(QStringLiteral("data.part")));
            QVERIFY(part.open(QIODevice::WriteOnly));
            part.write(data.left(have));
        }
        const auto t2 = resolver.open(url, std::nullopt, dir2, std::uint64_t(have));
        QTRY_VERIFY_WITH_TIMEOUT(done(t2), 180000);
        QCOMPARE(done(t2)->kind, ResolverEvent::Kind::Done);
        std::optional<std::uint64_t> from;
        for (const ResolverEvent& e : seen[t2])
            if (e.from && !from)
                from = e.from;
        QVERIFY(from);
        QCOMPARE(*from, std::uint64_t(have));
        QFile again(done(t2)->path);
        QVERIFY(again.open(QIODevice::ReadOnly));
        QCOMPARE(again.readAll(), data);
    }

    /// Temurin fetched from adoptium.net, its SHA-256 checked, unpacked with tar and found.
    /// About 45 MB: only with EMULE_HOSTER_LIVE_JAVA=1.
    void live_javaDownload()
    {
        if (qEnvironmentVariable("EMULE_HOSTER_LIVE_JAVA") != QLatin1StringView("1"))
            QSKIP("live: set EMULE_HOSTER_LIVE_JAVA=1");
        QTemporaryDir tmp;
        Ed3kJavaRuntime java(tmp.path());
        java.setAllowed(true);
        QVERIFY(java.download());
        QTRY_VERIFY_WITH_TIMEOUT(java.state() == Ed3kJavaRuntime::State::Ready || java.state() == Ed3kJavaRuntime::State::Failed
                                     || java.state() == Ed3kJavaRuntime::State::Missing,
                                 600000);
        QVERIFY2(java.state() == Ed3kJavaRuntime::State::Ready, qPrintable(java.error()));
        QCOMPARE(java.runtime()->feature, Ed3kJavaRuntime::kDownloadFeature);
        QVERIFY(java.runtime()->java.startsWith(java.runtimeDir()));
    }
#endif
};

QTEST_GUILESS_MAIN(tst_Ed3kHoster)
#include "tst_Ed3kHoster.moc"
