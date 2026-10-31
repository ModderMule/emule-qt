/// @file tst_Ed3kHoster.cpp
/// @brief Hoster links (hoster.md) through the hoster join (src/ed3k/Ed3kHosterJoin) on a real
/// ed3k engine, with a fake resolver that writes its part file the way the JDownloader
/// sidecar does: a link without a root downloaded whole (§4.1) and moved to incoming, the
/// user's switch (§3), no plugin, a removed file. And the pure parts of the JD resolver and
/// the Java runtime lookup.

#include "TestHelpers.h"

#include "Ed3kHosterJoin.h"
#include "Ed3kResolver.h"
#include "Ed3kSession.h"
#ifdef EMULE_HAVE_HOSTER_JD
#include "Ed3kJavaRuntime.h"
#include "Ed3kJdResolver.h"
#include "archive/ArchiveReader.h"
#endif

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
#include <QSet>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QUrl>

#include <functional>

#ifdef EMULE_HAVE_HOSTER_JD
#include <archive.h>
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

/// Takes links of fake.example; writes <dir>/data.part in steps, then renames it to data.
class FakeResolver : public Ed3kResolver {
public:
    QByteArray data = content(700 * 1024 + 123);
    bool gone = false;
    QSet<QString> goneUrls;      ///< check() says Gone for these
    QSet<QString> quotaUrls;     ///< open() says a quota wait of an hour for these
    QStringList opened;          ///< urls open() was asked for
    int opens = 0;
    int checks = 0;
    int accountChecks = 0;
    int checkDelayMs = 0;        ///< check() answers this much later
    QSet<QString> hosts{QStringLiteral("fake.example")};   ///< hosters match() takes
    QSet<QString> premiumHosts;  ///< accountCheck() says premium for these
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
        e.name = QStringLiteral("movie.bin");
        e.size = std::uint64_t(data.size());
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
    TaskId expand(const QString&) override
    {
        const TaskId t = next++;
        ResolverEvent e;
        e.kind = ResolverEvent::Kind::Links;
        e.links = {{QStringLiteral("https://fake.example/f/a"), QStringLiteral("a"), QStringLiteral("fake.example"), 1}};
        later(t, e);
        return t;
    }
    TaskId open(const QString& url, const std::optional<ResolverAccount>&, const QString& dir, std::uint64_t have) override
    {
        ++opens;
        opened << url;
        const TaskId t = next++;
        if (quotaUrls.contains(url)) {
            ResolverEvent w;
            w.kind = ResolverEvent::Kind::Wait;
            w.text = QStringLiteral("quota");
            w.seconds = 3600;
            later(t, w);
            return t;
        }
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
        later(t, e);
        return t;
    }
    void answerPuzzle(std::uint64_t, const QString&) override {}
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
            d.name = QStringLiteral("movie.bin");
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
                        {QStringLiteral("have"), QJsonArray{QJsonArray{0, blocks}}}};
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
bool writeResolverZip(const QString& path)
{
    return writeZip(path, {{QStringLiteral("resolver/Core.jar"), "PK"},
                           {QStringLiteral("resolver/jd/plugins/hoster/ArchiveOrg.class"), "cafe"},
                           {QStringLiteral("resolver/libs/js.jar"), "PK"},
                           {QStringLiteral("resolver/revision.txt"), "53779"}});
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

    /// #88: the built resolver.zip packs JD's core; only plugins are loose files and the
    /// jars a headless resolver never loads stay out. Skips without a built zip.
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
                if (e.kind != ResolverEvent::Kind::Progress && e.kind != ResolverEvent::Kind::Wait)
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
