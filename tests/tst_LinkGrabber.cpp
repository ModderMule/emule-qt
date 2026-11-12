/// @file tst_LinkGrabber.cpp
/// @brief The Link Grabber (Ed3kLinkGrabber, hoster.md "Link Grabber"): crawls through a fake
/// resolver, checks and downloads through a fake backend; grouping, edits, confirm, saving.

#include "utils/BackgroundFileWriter.h"
#include "Ed3kLinkGrabber.h"
#include "Ed3kResolver.h"

#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QUrl>

using namespace eMule::ed3k;

namespace {

/// expand: the links of folders (folders[url]); a url not there is gone. match: hosts.
class FakeResolver : public Ed3kResolver {
public:
    QHash<QString, QList<ResolverLink>> folders;
    QSet<QString> hosts{QStringLiteral("files.example")};
    QStringList expanded;   ///< urls expand() was asked for
    QStringList deepExpanded;
    bool deep = true;

    bool available() const override { return true; }
    QString unavailableReason() const override { return {}; }
    QString description() const override { return QStringLiteral("fake"); }
    bool canDeepCrawl() const override { return deep; }

    TaskId match(const QString& url) override
    {
        ResolverEvent e;
        e.kind = ResolverEvent::Kind::Matched;
        if (hosts.contains(QUrl(url).host()))
            e.host = QUrl(url).host();
        return later(e);
    }
    TaskId expand(const QString& url) override
    {
        expanded << url;
        ResolverEvent e;
        if (const auto it = folders.constFind(url); it != folders.cend()) {
            e.kind = ResolverEvent::Kind::Links;
            e.links = *it;
        } else {
            e.kind = ResolverEvent::Kind::Gone;
        }
        return later(e);
    }
    TaskId expandDeep(const QString& url) override
    {
        deepExpanded << url;
        return expand(url);
    }
    TaskId check(const QString&, const std::optional<ResolverAccount>&) override { return failed(); }
    TaskId openContainer(const QString&) override { return failed(); }
    TaskId open(const QString&, const std::optional<ResolverAccount>&, const QString&, std::uint64_t) override
    {
        return failed();
    }
    TaskId accountCheck(const QString&, const ResolverAccount&) override { return failed(); }
    TaskId sites(const QString&) override { return failed(); }
    TaskId setSolverAccounts(const QList<SolverAccount>&) override { return failed(); }
    TaskId solveTest(const QString&, const QString&, const QByteArray&) override { return failed(); }
    void answerPuzzle(std::uint64_t, const QString&) override {}
    void cancel(TaskId) override {}
    void setAskPuzzles(bool) override {}
    void setProxy(const QString&) override {}

private:
    TaskId next = 1;
    TaskId later(const ResolverEvent& e)
    {
        const TaskId t = next++;
        QTimer::singleShot(0, this, [this, t, e] { emit taskEvent(t, e); });
        return t;
    }
    TaskId failed()
    {
        ResolverEvent e;
        e.kind = ResolverEvent::Kind::Failed;
        e.code = QStringLiteral("failed");
        return later(e);
    }
};

ResolverLink link(const QString& name, std::uint64_t size = 1000, const QString& host = QStringLiteral("files.example"))
{
    return ResolverLink{QStringLiteral("https://%1/%2").arg(host, name), name, host, size};
}

/// The join's part: checks answered at once (offline: offline[url]), downloads recorded.
struct Backend {
    QSet<QString> offline;
    QSet<QString> items;   ///< hoster downloads there are
    QList<std::tuple<QList<QStringList>, QString, QStringList>> added;
    bool answer = true;
    Ed3kLinkGrabber* grabber = nullptr;

    Ed3kLinkGrabber::Backend make()
    {
        Ed3kLinkGrabber::Backend b;
        b.check = [this](const QStringList& urls) {
            if (answer)
                for (const QString& u : urls)
                    grabber->onLinkChecked(u, offline.contains(u) ? 2 : 1, QUrl(u).host(),
                                           offline.contains(u) ? QString() : QUrl(u).fileName(), 0);
            return true;
        };
        b.add = [this](const QList<QStringList>& files, const QString& package, const QStringList& passwords) {
            added.append({files, package, passwords});
            QList<quint32> ids;
            for (qsizetype i = 0; i < files.size(); ++i)
                ids << quint32(100 + i);
            return ids;
        };
        b.isItem = [this](const QString& url) { return items.contains(url); };
        return b;
    }
};

const Ed3kLinkGrabber::Link* byName(const Ed3kLinkGrabber& g, const QString& name)
{
    for (const auto& k : g.links())
        if (k.name == name)
            return g.link(k.id);
    return nullptr;
}

QString packageOf(const Ed3kLinkGrabber& g, const QString& name)
{
    const auto* k = byName(g, name);
    const auto* p = k ? g.package(k->packageId) : nullptr;
    return p ? p->name : QString();
}

} // namespace

class tst_LinkGrabber : public QObject {
    Q_OBJECT

private slots:
    void packageNames()
    {
        QCOMPARE(Ed3kLinkGrabber::packageNameFor(QStringLiteral("Movie.part1.rar")), QStringLiteral("Movie"));
        QCOMPARE(Ed3kLinkGrabber::packageNameFor(QStringLiteral("Movie.part12.rar")), QStringLiteral("Movie"));
        QCOMPARE(Ed3kLinkGrabber::packageNameFor(QStringLiteral("Set.r01")), QStringLiteral("Set"));
        QCOMPARE(Ed3kLinkGrabber::packageNameFor(QStringLiteral("Set.rar")), QStringLiteral("Set"));
        QCOMPARE(Ed3kLinkGrabber::packageNameFor(QStringLiteral("Big.7z.001")), QStringLiteral("Big"));
        QCOMPARE(Ed3kLinkGrabber::packageNameFor(QStringLiteral("Big.zip.002")), QStringLiteral("Big"));
        QCOMPARE(Ed3kLinkGrabber::packageNameFor(QStringLiteral("Clip.mkv")), QStringLiteral("Clip"));
        QCOMPARE(Ed3kLinkGrabber::packageNameFor(QStringLiteral("a/b\\c.txt")), QStringLiteral("a_b_c"));
        QCOMPARE(Ed3kLinkGrabber::packageNameFor(QString()), QString());
    }

    void addGroupsAndChecks()
    {
        QTemporaryDir tmp;
        Ed3kLinkGrabber g(tmp.path());
        Backend b;
        b.grabber = &g;
        b.offline.insert(QStringLiteral("https://files.example/Movie.part2.rar"));
        g.setBackend(b.make());
        const int n = g.add({link(QStringLiteral("Movie.part1.rar")), link(QStringLiteral("Movie.part2.rar")),
                             link(QStringLiteral("Clip.mkv"))},
                            QStringLiteral("test"));
        QCOMPARE(n, 3);
        QCOMPARE(g.packages().size(), 2);
        QCOMPARE(packageOf(g, QStringLiteral("Movie.part1.rar")), QStringLiteral("Movie"));
        QCOMPARE(packageOf(g, QStringLiteral("Movie.part2.rar")), QStringLiteral("Movie"));
        QCOMPARE(packageOf(g, QStringLiteral("Clip.mkv")), QStringLiteral("Clip"));
        // checked: online / offline
        QCOMPARE(byName(g, QStringLiteral("Movie.part1.rar"))->status, Ed3kLinkGrabber::Status::Online);
        QCOMPARE(byName(g, QStringLiteral("Movie.part2.rar"))->status, Ed3kLinkGrabber::Status::Offline);
        // the same link again: skipped
        QCOMPARE(g.add({link(QStringLiteral("Clip.mkv"))}, QStringLiteral("again")), 0);
        QCOMPARE(g.links().size(), 3);
    }

    void givenPackageAndPasswordsWin()
    {
        QTemporaryDir tmp;
        Ed3kLinkGrabber g(tmp.path());
        Backend b;
        b.grabber = &g;
        g.setBackend(b.make());
        ResolverLink own = link(QStringLiteral("a.rar"));
        own.package = QStringLiteral("From DLC");
        own.passwords = {QStringLiteral("pw1")};
        g.add({own, link(QStringLiteral("b.rar"))}, QStringLiteral("src"), QStringLiteral("Given"), {QStringLiteral("pw2")});
        QCOMPARE(packageOf(g, QStringLiteral("a.rar")), QStringLiteral("From DLC"));
        QCOMPARE(packageOf(g, QStringLiteral("b.rar")), QStringLiteral("Given"));
        const auto* p = g.package(byName(g, QStringLiteral("a.rar"))->packageId);
        QCOMPARE(p->passwords, (QStringList{QStringLiteral("pw1"), QStringLiteral("pw2")}));
    }

    void itemDupeFlagged()
    {
        QTemporaryDir tmp;
        Ed3kLinkGrabber g(tmp.path());
        Backend b;
        b.grabber = &g;
        b.items.insert(link(QStringLiteral("x.bin")).url);
        g.setBackend(b.make());
        g.add({link(QStringLiteral("x.bin")), link(QStringLiteral("y.bin"))}, {});
        QVERIFY(byName(g, QStringLiteral("x.bin"))->dupe);
        QVERIFY(!byName(g, QStringLiteral("y.bin"))->dupe);
        QVERIFY(g.cleanup(QStringLiteral("dupes")));
        QCOMPARE(g.links().size(), 1);
        QVERIFY(!g.cleanup(QStringLiteral("nonsense")));
    }

    void unnamedLinkFollowsItsCheckedName()
    {
        QTemporaryDir tmp;
        Ed3kLinkGrabber g(tmp.path());
        Backend b;
        b.grabber = &g;
        b.answer = false;
        g.setBackend(b.make());
        ResolverLink l{QStringLiteral("https://files.example/abc123"), {}, QStringLiteral("files.example"), 0};
        g.add({l}, {});
        QCOMPARE(g.links().first().status, Ed3kLinkGrabber::Status::Checking);
        QCOMPARE(g.packages().first().name, QStringLiteral("abc123"));
        g.onLinkChecked(l.url, 1, QStringLiteral("files.example"), QStringLiteral("Real.Name.part1.rar"), 5000);
        QCOMPARE(g.links().first().name, QStringLiteral("Real.Name.part1.rar"));
        QCOMPARE(g.links().first().size, std::uint64_t(5000));
        QCOMPARE(g.packages().size(), 1);
        QCOMPARE(g.packages().first().name, QStringLiteral("Real.Name"));
    }

    void edits()
    {
        QTemporaryDir tmp;
        Ed3kLinkGrabber g(tmp.path());
        Backend b;
        b.grabber = &g;
        g.setBackend(b.make());
        g.add({link(QStringLiteral("s.part1.rar")), link(QStringLiteral("s.part2.rar"), 1000, QStringLiteral("other.example")),
               link(QStringLiteral("t.mkv"))},
              {});
        const quint32 s = byName(g, QStringLiteral("s.part1.rar"))->packageId;
        QVERIFY(g.editPackages({s}, QStringLiteral("Renamed"), QStringList{QStringLiteral("secret")}));
        QCOMPARE(g.package(s)->name, QStringLiteral("Renamed"));
        QCOMPARE(g.package(s)->passwords, QStringList{QStringLiteral("secret")});
        // name only: passwords kept
        QVERIFY(g.editPackages({s}, QStringLiteral("Again"), std::nullopt));
        QCOMPARE(g.package(s)->passwords, QStringList{QStringLiteral("secret")});

        // split: the second hoster gets its own package
        g.splitByHost({s});
        QCOMPARE(g.packages().size(), 3);
        QCOMPARE(packageOf(g, QStringLiteral("s.part2.rar")), QStringLiteral("Again (other.example)"));

        // merge everything into a new package; the empty ones go
        QList<quint32> all;
        for (const auto& k : g.links())
            all << k.id;
        const quint32 merged = g.moveLinks(all, 0, QStringLiteral("All"));
        QVERIFY(merged != 0);
        QCOMPARE(g.packages().size(), 1);
        QCOMPARE(g.package(merged)->passwords, QStringList{QStringLiteral("secret")});

        // disable one, remove one
        const quint32 t = byName(g, QStringLiteral("t.mkv"))->id;
        g.setLinksEnabled({t}, false);
        QVERIFY(!g.link(t)->enabled);
        QVERIFY(g.cleanup(QStringLiteral("disabled")));
        QVERIFY(!g.link(t));
        g.remove({byName(g, QStringLiteral("s.part1.rar"))->id}, {});
        QCOMPARE(g.links().size(), 1);
        g.remove({}, {merged});
        QVERIFY(g.links().isEmpty());
        QVERIFY(g.packages().isEmpty());
    }

    void confirmPerPackage()
    {
        QTemporaryDir tmp;
        Ed3kLinkGrabber g(tmp.path());
        Backend b;
        b.grabber = &g;
        b.offline.insert(link(QStringLiteral("m.part3.rar")).url);
        g.setBackend(b.make());
        g.add({link(QStringLiteral("m.part1.rar")), link(QStringLiteral("m.part2.rar")), link(QStringLiteral("m.part3.rar")),
               link(QStringLiteral("o.mkv"))},
              {}, {}, {QStringLiteral("pw")});
        g.setLinksEnabled({byName(g, QStringLiteral("m.part2.rar"))->id}, false);
        const quint32 m = byName(g, QStringLiteral("m.part1.rar"))->packageId;
        const auto ids = g.confirm({m}, {}, false);
        // one package: only the enabled online link
        QCOMPARE(b.added.size(), 1);
        QCOMPARE(std::get<0>(b.added[0]), (QList<QStringList>{{link(QStringLiteral("m.part1.rar")).url}}));
        QCOMPARE(std::get<1>(b.added[0]), QStringLiteral("m"));
        QCOMPARE(std::get<2>(b.added[0]), QStringList{QStringLiteral("pw")});
        QCOMPARE(ids.size(), 1);
        // started links leave; the disabled and offline ones stay, o.mkv untouched
        QVERIFY(!byName(g, QStringLiteral("m.part1.rar")));
        QVERIFY(byName(g, QStringLiteral("m.part2.rar")));
        QVERIFY(byName(g, QStringLiteral("m.part3.rar")));
        QVERIFY(byName(g, QStringLiteral("o.mkv")));
        // offline included on request
        g.confirm({m}, {}, true);
        QVERIFY(!byName(g, QStringLiteral("m.part3.rar")));
        // a single link
        g.confirm({}, {byName(g, QStringLiteral("o.mkv"))->id}, false);
        QCOMPARE(std::get<1>(b.added.last()), QStringLiteral("o"));
    }

    void grabCrawlsIntoList()
    {
        QTemporaryDir tmp;
        Ed3kLinkGrabber g(tmp.path());
        FakeResolver r;
        r.folders.insert(QStringLiteral("https://files.example/folder/1"),
                         {link(QStringLiteral("f.part1.rar")), link(QStringLiteral("f.part2.rar"))});
        Backend b;
        b.grabber = &g;
        g.setResolver(&r);
        g.setBackend(b.make());
        g.setEnabled(true, false);
        const quint64 token = g.grab(QStringLiteral("look: https://files.example/folder/1 and https://nowhere.example/x"),
                                     QStringLiteral("test"), false, QStringLiteral("Pack"), {QStringLiteral("pw")},
                                     Ed3kLinkGrabber::Mode::List);
        QVERIFY(token != 0);
        QVERIFY(g.crawling() > 0);
        QTRY_COMPARE(g.links().size(), 2);
        QTRY_COMPARE(g.crawling(), 0);
        QCOMPARE(packageOf(g, QStringLiteral("f.part1.rar")), QStringLiteral("Pack"));
        QCOMPARE(g.packages().first().passwords, QStringList{QStringLiteral("pw")});
        QCOMPARE(r.expanded.size(), 2);
        QVERIFY(r.deepExpanded.isEmpty());
        // nothing new in the text: no token
        QCOMPARE(g.grab(QStringLiteral("no links here"), {}, false, {}, {}, Ed3kLinkGrabber::Mode::List), quint64(0));
    }

    void grabDeep()
    {
        QTemporaryDir tmp;
        Ed3kLinkGrabber g(tmp.path());
        FakeResolver r;
        r.folders.insert(QStringLiteral("https://blog.example/post"), {link(QStringLiteral("d.bin"))});
        Backend b;
        b.grabber = &g;
        g.setResolver(&r);
        g.setBackend(b.make());
        QVERIFY(g.canDeepCrawl());
        QVERIFY(g.grab(QStringLiteral("https://blog.example/post"), {}, true, {}, {}, Ed3kLinkGrabber::Mode::List) != 0);
        QTRY_COMPARE(g.links().size(), 1);
        QCOMPARE(r.deepExpanded, QStringList{QStringLiteral("https://blog.example/post")});
    }

    void clipboardAskDropsUnmatched()
    {
        QTemporaryDir tmp;
        Ed3kLinkGrabber g(tmp.path());
        FakeResolver r;
        r.folders.insert(QStringLiteral("https://files.example/a"), {link(QStringLiteral("a.bin"))});
        Backend b;
        b.grabber = &g;
        g.setResolver(&r);
        g.setBackend(b.make());
        QSignalSpy spy(&g, &Ed3kLinkGrabber::grabbed);
        const quint64 token = g.grab(QStringLiteral("https://files.example/a\nhttps://news.example/article"),
                                     QStringLiteral("clipboard"), false, {}, {}, Ed3kLinkGrabber::Mode::Ask, true);
        QVERIFY(token != 0);
        QTRY_COMPARE(spy.size(), 1);
        QCOMPARE(spy[0][0].toULongLong(), token);
        const auto links = spy[0][2].value<QList<ResolverLink>>();
        QCOMPARE(links.size(), 1);
        QCOMPARE(links[0].name, QStringLiteral("a.bin"));
        // the unmatched one never reached expand; nothing went into the list
        QCOMPARE(r.expanded, QStringList{QStringLiteral("https://files.example/a")});
        QVERIFY(g.links().isEmpty());

        // nothing a plugin takes: an empty answer with a reason
        spy.clear();
        QVERIFY(g.grab(QStringLiteral("https://news.example/b"), {}, false, {}, {}, Ed3kLinkGrabber::Mode::Ask, true) != 0);
        QTRY_COMPARE(spy.size(), 1);
        QVERIFY(spy[0][2].value<QList<ResolverLink>>().isEmpty());
        QCOMPARE(spy[0][3].toString(), QStringLiteral("noPlugin"));
    }

    void changesCoalesced()
    {
        QTemporaryDir tmp;
        Ed3kLinkGrabber g(tmp.path());
        Backend b;
        b.grabber = &g;
        g.setBackend(b.make());
        QSignalSpy spy(&g, &Ed3kLinkGrabber::changed);
        g.add({link(QStringLiteral("a.bin")), link(QStringLiteral("b.bin"))}, {});
        QTRY_COMPARE(spy.size(), 1);
        const auto d = spy[0][0].value<Ed3kLinkGrabber::Delta>();
        QCOMPARE(d.links.size(), 2);
        QCOMPARE(d.packages.size(), 2);
        spy.clear();
        g.remove({byName(g, QStringLiteral("a.bin"))->id}, {});
        QTRY_COMPARE(spy.size(), 1);
        const auto r = spy[0][0].value<Ed3kLinkGrabber::Delta>();
        QCOMPARE(r.removedLinks.size(), 1);
        QCOMPARE(r.removedPackages.size(), 1);
    }

    void savedAndLoaded()
    {
        QTemporaryDir tmp;
        {
            Ed3kLinkGrabber g(tmp.path());
            // nothing: no file under config/ed3k
            g.save();
            QVERIFY(!QFileInfo::exists(tmp.filePath(QStringLiteral("ed3k/hoster/linkgrabber.json"))));
            Backend b;
            b.grabber = &g;
            b.offline.insert(link(QStringLiteral("gone.bin")).url);
            g.setBackend(b.make());
            g.add({link(QStringLiteral("keep.part1.rar")), link(QStringLiteral("gone.bin"))}, QStringLiteral("src"), {},
                  {QStringLiteral("pw")});
            g.setLinksEnabled({byName(g, QStringLiteral("gone.bin"))->id}, false);
        }   // the destructor writes what waits
        eMule::BackgroundFileWriter::instance().flush();   // saves are queued (report 43)
        QVERIFY(QFileInfo::exists(tmp.filePath(QStringLiteral("ed3k/hoster/linkgrabber.json"))));
        Ed3kLinkGrabber g(tmp.path());
        g.load();
        QCOMPARE(g.links().size(), 2);
        QCOMPARE(g.packages().size(), 2);
        const auto* keep = byName(g, QStringLiteral("keep.part1.rar"));
        QVERIFY(keep);
        QCOMPARE(keep->status, Ed3kLinkGrabber::Status::Online);
        QCOMPARE(g.package(keep->packageId)->name, QStringLiteral("keep"));
        QCOMPARE(g.package(keep->packageId)->passwords, QStringList{QStringLiteral("pw")});
        QCOMPARE(g.package(keep->packageId)->source, QStringLiteral("src"));
        const auto* gone = byName(g, QStringLiteral("gone.bin"));
        QVERIFY(gone && !gone->enabled && gone->status == Ed3kLinkGrabber::Status::Offline);
        // new ids go on after the loaded ones
        Backend b;
        b.grabber = &g;
        g.setBackend(b.make());
        g.add({link(QStringLiteral("new.bin"))}, {});
        QVERIFY(byName(g, QStringLiteral("new.bin"))->id > keep->id);
    }
};

QTEST_GUILESS_MAIN(tst_LinkGrabber)
#include "tst_LinkGrabber.moc"
