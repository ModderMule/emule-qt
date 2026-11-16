/// @file tst_HosterCatalog.cpp
/// @brief What the Add account dialogs offer (Ed3kHosterCatalog): the resolver's account
/// plugins and the sites' favicons, kept on disk; through a fake resolver.

#include "utils/BackgroundFileWriter.h"
#include "Ed3kHosterCatalog.h"
#include "Ed3kResolver.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

using namespace eMule::ed3k;
using eMule::BackgroundFileWriter;

namespace {

const QByteArray kPng = QByteArray("\x89PNG\r\n\x1a\n", 8) + "fake";

/// accountHosts: hosts (failed "unavailable" while !ready); favicon: icons[host], else "none".
class FakeResolver : public Ed3kResolver {
public:
    QList<AccountHost> hosts;
    QHash<QString, QByteArray> iconBytes;
    QStringList iconAsks;
    int hostAsks = 0;
    int pluginWants = 0;
    bool ready = true;

    bool available() const override { return ready; }
    QString unavailableReason() const override { return {}; }
    QString description() const override { return QStringLiteral("fake"); }
    void wantPlugins() override { ++pluginWants; }
    void becomeReady()
    {
        ready = true;
        emit stateChanged();
    }

    TaskId accountHosts() override
    {
        ++hostAsks;
        ResolverEvent e;
        if (!ready) {
            e.kind = ResolverEvent::Kind::Failed;
            e.code = QStringLiteral("unavailable");
        } else {
            e.kind = ResolverEvent::Kind::AccountHosts;
            e.accountHosts = hosts;
        }
        return later(e);
    }
    TaskId favicon(const QString& host) override
    {
        iconAsks << host;
        ResolverEvent e;
        if (const auto it = iconBytes.constFind(host); it != iconBytes.cend()) {
            e.kind = ResolverEvent::Kind::Icon;
            e.host = host;
            e.image = *it;
        } else {
            e.kind = ResolverEvent::Kind::Failed;
            e.code = QStringLiteral("none");
        }
        return later(e);
    }
    TaskId match(const QString&) override { return failed(); }
    TaskId check(const QString&, const std::optional<ResolverAccount>&) override { return failed(); }
    TaskId expand(const QString&) override { return failed(); }
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

} // namespace

class TestHosterCatalog : public QObject {
    Q_OBJECT

private slots:
    void validHost_data();
    void validHost();
    void accountHosts_askedOncePerRunAndSaved();
    void accountHosts_unavailableFetchesPluginsThenAsks();
    void icons_fetchedOnceCachedOnDisk();
    void icons_noneRemembered();
    void icons_atMostFourAtOnce();
};

void TestHosterCatalog::validHost_data()
{
    QTest::addColumn<QString>("host");
    QTest::addColumn<bool>("ok");
    QTest::newRow("plain") << QStringLiteral("rapidgator.net") << true;
    QTest::newRow("dash") << QStringLiteral("real-debrid.com") << true;
    QTest::newRow("no dot") << QStringLiteral("localhost") << false;
    QTest::newRow("upper") << QStringLiteral("Rapidgator.net") << false;
    QTest::newRow("path") << QStringLiteral("../x.png") << false;
    QTest::newRow("slash") << QStringLiteral("a.b/c") << false;
    QTest::newRow("dots") << QStringLiteral("a..b") << false;
    QTest::newRow("lead dot") << QStringLiteral(".a.b") << false;
}

void TestHosterCatalog::validHost()
{
    QFETCH(QString, host);
    QFETCH(bool, ok);
    QCOMPARE(Ed3kHosterCatalog::validHost(host), ok);
}

void TestHosterCatalog::accountHosts_askedOncePerRunAndSaved()
{
    QTemporaryDir dir;
    FakeResolver r;
    r.hosts = {{QStringLiteral("rapidgator.net"), {}, false, false},
               {QStringLiteral("real-debrid.com"), QStringLiteral("Real-Debrid"), true, false}};
    {
        Ed3kHosterCatalog c(dir.path());
        c.setResolver(&r);
        QSignalSpy changed(&c, &Ed3kHosterCatalog::accountHostsChanged);
        bool asking = false;
        QVERIFY(c.accountHosts(&asking).isEmpty());
        QVERIFY(asking);
        QVERIFY(changed.wait());
        QCOMPARE(c.accountHosts(&asking), r.hosts);
        QVERIFY(!asking);
        QCOMPARE(r.hostAsks, 1);   // not again this run
        BackgroundFileWriter::instance().flush();
    }
    // next run: the saved list at once, asked again
    Ed3kHosterCatalog c(dir.path());
    c.setResolver(&r);
    QCOMPARE(c.accountHosts(), r.hosts);
    QCOMPARE(r.hostAsks, 2);
}

void TestHosterCatalog::accountHosts_unavailableFetchesPluginsThenAsks()
{
    QTemporaryDir dir;
    FakeResolver r;
    r.ready = false;
    r.hosts = {{QStringLiteral("rapidgator.net"), {}, false, false}};
    Ed3kHosterCatalog c(dir.path());
    c.setResolver(&r);
    QSignalSpy changed(&c, &Ed3kHosterCatalog::accountHostsChanged);
    (void)c.accountHosts();
    QVERIFY(changed.wait());
    QCOMPARE(r.pluginWants, 1);
    bool asking = false;
    (void)c.accountHosts(&asking);
    QVERIFY(asking);   // waits for the plugins
    QCOMPARE(r.hostAsks, 1);
    r.becomeReady();
    QVERIFY(changed.wait());
    QCOMPARE(c.accountHosts(), r.hosts);
}

void TestHosterCatalog::icons_fetchedOnceCachedOnDisk()
{
    QTemporaryDir dir;
    FakeResolver r;
    r.iconBytes.insert(QStringLiteral("rapidgator.net"), kPng);
    {
        Ed3kHosterCatalog c(dir.path());
        c.setResolver(&r);
        QSignalSpy ready(&c, &Ed3kHosterCatalog::iconReady);
        QVERIFY(c.icons({QStringLiteral("Rapidgator.net"), QStringLiteral("../evil")}).isEmpty());
        QVERIFY(ready.wait());
        QCOMPARE(ready.at(0).at(0).toString(), QStringLiteral("rapidgator.net"));
        QCOMPARE(ready.at(0).at(1).toByteArray(), kPng);
        QCOMPARE(r.iconAsks, QStringList{QStringLiteral("rapidgator.net")});   // the bad one never
        BackgroundFileWriter::instance().flush();
    }
    Ed3kHosterCatalog c(dir.path());
    c.setResolver(&r);
    const auto icons = c.icons({QStringLiteral("rapidgator.net")});
    QCOMPARE(icons.value(QStringLiteral("rapidgator.net")), kPng);
    QTest::qWait(20);
    QCOMPARE(r.iconAsks.size(), 1);   // fresh on disk: not fetched again
}

void TestHosterCatalog::icons_noneRemembered()
{
    QTemporaryDir dir;
    FakeResolver r;
    {
        Ed3kHosterCatalog c(dir.path());
        c.setResolver(&r);
        QSignalSpy ready(&c, &Ed3kHosterCatalog::iconReady);
        (void)c.icons({QStringLiteral("noicon.example")});
        QVERIFY(ready.wait());
        QVERIFY(ready.at(0).at(1).toByteArray().isEmpty());
        BackgroundFileWriter::instance().flush();
    }
    Ed3kHosterCatalog c(dir.path());
    c.setResolver(&r);
    const auto icons = c.icons({QStringLiteral("noicon.example")});
    QVERIFY(icons.contains(QStringLiteral("noicon.example")));   // known: none
    QVERIFY(icons.value(QStringLiteral("noicon.example")).isEmpty());
    QTest::qWait(20);
    QCOMPARE(r.iconAsks.size(), 1);
}

void TestHosterCatalog::icons_atMostFourAtOnce()
{
    QTemporaryDir dir;
    FakeResolver r;
    Ed3kHosterCatalog c(dir.path());
    c.setResolver(&r);
    QStringList hosts;
    for (int i = 0; i < 10; ++i)
        hosts << QStringLiteral("site%1.example").arg(i);
    QSignalSpy ready(&c, &Ed3kHosterCatalog::iconReady);
    (void)c.icons(hosts);
    QCOMPARE(r.iconAsks.size(), Ed3kHosterCatalog::kMaxFetches);
    QTRY_COMPARE(ready.size(), 10);
    QCOMPARE(r.iconAsks.size(), 10);
}

QTEST_GUILESS_MAIN(TestHosterCatalog)
#include "tst_HosterCatalog.moc"
