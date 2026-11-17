/// @file tst_HosterCatalog.cpp
/// @brief What the Add account dialogs offer (Ed3kHosterCatalog): the resolver's account
/// plugins and the sites' favicons, kept on disk; through a fake resolver.

#include "utils/BackgroundFileWriter.h"
#include "Ed3kHosterCatalog.h"
#include "Ed3kResolver.h"

#include <QDir>
#include <QFile>
#include <QHostAddress>
#include <QJsonObject>
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
    void icons_atMostMaxFetchesAtOnce();
    void icons_newestAskFirst();
    void behindCdn_data();
    void behindCdn();
    void countries_cdnRootTriesSubdomains();
    void countries_noDnsBehindProxy();
    void countries_downloadServerFirst();
    void accounts_keptAcrossRuns();
};

namespace {

/// DNS from a table, answered on the next turn; names asked in @p asked
void fakeDns(Ed3kHosterCatalog& c, const QHash<QString, QList<QHostAddress>>& table, QStringList* asked)
{
    c.setDnsLookup([table, asked](const QString& name, QObject* ctx, std::function<void(QList<QHostAddress>)> done) {
        *asked << name;
        QTimer::singleShot(0, ctx, [table, name, done] { done(table.value(name)); });
    });
}

void fakeGeo(Ed3kHosterCatalog& c)
{
    // TEST-NET-1 = de, TEST-NET-2 = nl
    c.setGeo([] { return true; }, [](const QHostAddress& a) {
        if (a.isInSubnet(QHostAddress(QStringLiteral("192.0.2.0")), 24))
            return QStringLiteral("de");
        if (a.isInSubnet(QHostAddress(QStringLiteral("198.51.100.0")), 24))
            return QStringLiteral("nl");
        return QString();
    });
}

} // namespace

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

void TestHosterCatalog::icons_atMostMaxFetchesAtOnce()
{
    QTemporaryDir dir;
    FakeResolver r;
    Ed3kHosterCatalog c(dir.path());
    c.setResolver(&r);
    QStringList hosts;
    for (int i = 0; i < 15; ++i)
        hosts << QStringLiteral("site%1.example").arg(i);
    QSignalSpy ready(&c, &Ed3kHosterCatalog::iconReady);
    (void)c.icons(hosts);
    QCOMPARE(r.iconAsks.size(), Ed3kHosterCatalog::kMaxFetches);
    QCOMPARE(r.iconAsks.first(), hosts.first());   // in view order
    QTRY_COMPARE(ready.size(), 15);
    QCOMPARE(r.iconAsks.size(), 15);
}

void TestHosterCatalog::icons_newestAskFirst()
{
    QTemporaryDir dir;
    FakeResolver r;
    Ed3kHosterCatalog c(dir.path());
    c.setResolver(&r);
    QStringList old;
    for (int i = 0; i < Ed3kHosterCatalog::kMaxFetches + 2; ++i)
        old << QStringLiteral("old%1.example").arg(i);
    (void)c.icons(old);
    // scrolled: the rows now in view before the ones left behind
    (void)c.icons({QStringLiteral("new0.example"), QStringLiteral("new1.example"), old.last()});
    QSignalSpy ready(&c, &Ed3kHosterCatalog::iconReady);
    QTRY_COMPARE(ready.size(), old.size() + 2);
    QCOMPARE(r.iconAsks.mid(Ed3kHosterCatalog::kMaxFetches),
             (QStringList{QStringLiteral("new0.example"), QStringLiteral("new1.example"), old.last(), old.at(old.size() - 2)}));
}

void TestHosterCatalog::behindCdn_data()
{
    QTest::addColumn<QString>("address");
    QTest::addColumn<bool>("cdn");
    QTest::newRow("cloudflare v4") << QStringLiteral("104.21.3.4") << true;
    QTest::newRow("cloudflare v6") << QStringLiteral("2606:4700::6810:1") << true;
    QTest::newRow("mapped v4") << QStringLiteral("::ffff:172.67.1.1") << true;
    // by AS: the others that front sites
    QTest::newRow("fastly") << QStringLiteral("151.101.1.140") << true;
    QTest::newRow("fastly v6") << QStringLiteral("2a04:4e42::1") << true;
    QTest::newRow("akamai") << QStringLiteral("23.192.0.1") << true;
    QTest::newRow("akamai v6") << QStringLiteral("2600:1400::1") << true;
    QTest::newRow("ddos-guard") << QStringLiteral("45.132.16.1") << true;
    QTest::newRow("cloudfront") << QStringLiteral("13.32.0.1") << true;
    QTest::newRow("test-net") << QStringLiteral("192.0.2.1") << false;
    QTest::newRow("doc v6") << QStringLiteral("2001:db8::1") << false;
}

void TestHosterCatalog::behindCdn()
{
    QFETCH(QString, address);
    QFETCH(bool, cdn);
    QCOMPARE(Ed3kHosterCatalog::behindCdn(QHostAddress(address)), cdn);
}

void TestHosterCatalog::countries_cdnRootTriesSubdomains()
{
    QTemporaryDir dir;
    FakeResolver r;
    r.hosts = {{QStringLiteral("cdn.example"), {}, false, false, {QStringLiteral("dl.cdn.example"), QStringLiteral("api.cdn.example")}},
               {QStringLiteral("plain.example"), {}, false, false, {QStringLiteral("dl.plain.example")}},
               {QStringLiteral("hidden.example"), {}, false, false, {}}};
    const QHash<QString, QList<QHostAddress>> dns{
        {QStringLiteral("cdn.example"), {QHostAddress(QStringLiteral("104.16.1.1"))}},
        {QStringLiteral("api.cdn.example"), {QHostAddress(QStringLiteral("198.51.100.7"))}},
        {QStringLiteral("dl.cdn.example"), {QHostAddress(QStringLiteral("2606:4700::1"))}},
        {QStringLiteral("plain.example"), {QHostAddress(QStringLiteral("192.0.2.9"))}},
        {QStringLiteral("hidden.example"), {QHostAddress(QStringLiteral("172.64.0.1"))}}};
    QStringList asked;
    {
        Ed3kHosterCatalog c(dir.path());
        c.setResolver(&r);
        fakeGeo(c);
        fakeDns(c, dns, &asked);
        QSignalSpy hostsCame(&c, &Ed3kHosterCatalog::accountHostsChanged);
        (void)c.accountHosts();
        QVERIFY(hostsCame.wait());
        QSignalSpy ready(&c, &Ed3kHosterCatalog::countryReady);
        QVERIFY(c.countries({QStringLiteral("cdn.example"), QStringLiteral("plain.example"), QStringLiteral("hidden.example")}).isEmpty());
        QTRY_COMPARE(ready.size(), 3);
        QHash<QString, QString> got;
        for (const auto& args : ready)
            got.insert(args.at(0).toString(), args.at(1).toString());
        QCOMPARE(got.value(QStringLiteral("cdn.example")), QStringLiteral("NL"));   // a subdomain outside the CDN
        QCOMPARE(got.value(QStringLiteral("plain.example")), QStringLiteral("DE"));
        QVERIFY(got.contains(QStringLiteral("hidden.example")));
        QVERIFY(got.value(QStringLiteral("hidden.example")).isEmpty());   // only the CDN: no flag
        QVERIFY(!asked.contains(QStringLiteral("dl.plain.example")));      // root was enough
        QTest::qWait(2100);   // the batched save
        BackgroundFileWriter::instance().flush();
    }
    // next run: from disk, no DNS
    asked.clear();
    Ed3kHosterCatalog c(dir.path());
    c.setResolver(&r);
    fakeGeo(c);
    fakeDns(c, dns, &asked);
    const auto known = c.countries({QStringLiteral("cdn.example"), QStringLiteral("hidden.example")});
    QCOMPARE(known.value(QStringLiteral("cdn.example")), QStringLiteral("NL"));
    QVERIFY(known.contains(QStringLiteral("hidden.example")));
    QTest::qWait(20);
    QVERIFY(asked.isEmpty());
}

void TestHosterCatalog::countries_noDnsBehindProxy()
{
    QTemporaryDir dir;
    FakeResolver r;
    Ed3kHosterCatalog c(dir.path());
    c.setResolver(&r);
    fakeGeo(c);
    QStringList asked;
    fakeDns(c, {}, &asked);
    c.setDnsAllowed(false);
    QVERIFY(c.countries({QStringLiteral("plain.example")}).isEmpty());
    QTest::qWait(20);
    QVERIFY(asked.isEmpty());
}

void TestHosterCatalog::countries_downloadServerFirst()
{
    QTemporaryDir dir;
    FakeResolver r;
    r.hosts = {{QStringLiteral("cdn.example"), {}, false, false, {QStringLiteral("alias.example")}}};
    const QHash<QString, QList<QHostAddress>> dns{
        {QStringLiteral("cdn.example"), {QHostAddress(QStringLiteral("104.16.1.1"))}},
        {QStringLiteral("alias.example"), {QHostAddress(QStringLiteral("104.16.1.2"))}},
        {QStringLiteral("www.alias.example"), {QHostAddress(QStringLiteral("104.16.1.3"))}},
        {QStringLiteral("old.cdn.example"), {QHostAddress(QStringLiteral("192.0.2.3"))}},
        {QStringLiteral("pr7.cdn.example"), {QHostAddress(QStringLiteral("198.51.100.7"))}}};
    QStringList asked;
    {
        Ed3kHosterCatalog c(dir.path());
        c.setResolver(&r);
        fakeGeo(c);
        fakeDns(c, dns, &asked);
        QSignalSpy hostsCame(&c, &Ed3kHosterCatalog::accountHostsChanged);
        (void)c.accountHosts();
        QVERIFY(hostsCame.wait());
        QSignalSpy ready(&c, &Ed3kHosterCatalog::countryReady);
        (void)c.countries({QStringLiteral("cdn.example")});
        QTRY_COMPARE(ready.size(), 1);
        QVERIFY(ready.at(0).at(1).toString().isEmpty());   // only the CDN
        // a download of a link on the plugin's alias, with "www.": counted for the plugin's site
        ready.clear();
        c.noteServer(QStringLiteral("www.alias.example"), QStringLiteral("old.cdn.example"));
        QTRY_VERIFY(ready.size() >= 2);   // the link's name and the site: looked up again
        QCOMPARE(ready.last().at(1).toString(), QStringLiteral("DE"));
        QTest::qWait(10);
        c.noteServer(QStringLiteral("cdn.example"), QStringLiteral("PR7.cdn.example"));
        QCOMPARE(c.servers(QStringLiteral("alias.example")),
                 (QStringList{QStringLiteral("pr7.cdn.example"), QStringLiteral("old.cdn.example")}));
        ready.clear();
        QTRY_COMPARE(ready.size(), 1);
        QCOMPARE(ready.at(0).at(1).toString(), QStringLiteral("NL"));   // the newest server
        c.noteServer(QStringLiteral("cdn.example"), QStringLiteral("not a host"));
        QTest::qWait(2500);   // batched save
        BackgroundFileWriter::instance().flush();
    }
    // kept with last seen times, the newest first next run
    Ed3kHosterCatalog c(dir.path());
    c.setResolver(&r);
    QCOMPARE(c.servers(QStringLiteral("cdn.example")),
             (QStringList{QStringLiteral("pr7.cdn.example"), QStringLiteral("old.cdn.example")}));
}

void TestHosterCatalog::accounts_keptAcrossRuns()
{
    QTemporaryDir dir;
    const QJsonObject premium{{QStringLiteral("valid"), true},
                              {QStringLiteral("premium"), true},
                              {QStringLiteral("expires"), 2000000000.0},
                              {QStringLiteral("trafficLeft"), 5e9},
                              {QStringLiteral("trafficMax"), 1e10}};
    {
        Ed3kHosterCatalog c(dir.path());
        c.setDnsAllowed(false);
        QVERIFY(c.account(QStringLiteral("files.example"), QStringLiteral("me")).isEmpty());
        c.noteAccount(QStringLiteral("www.files.example"), QStringLiteral("me"), premium);
        c.noteAccount(QStringLiteral("solver.example"), {}, {{QStringLiteral("valid"), true}, {QStringLiteral("balance"), 1.5}});
        c.noteAccount(QStringLiteral("gone.example"), QStringLiteral("old"), {{QStringLiteral("valid"), false}});
        c.noteServer(QStringLiteral("files.example"), QStringLiteral("dl1.files.example"));   // the same file
        c.keepAccounts({{QStringLiteral("files.example"), QStringLiteral("me")}, {QStringLiteral("solver.example"), {}}});
    }   // written at once on quit
    BackgroundFileWriter::instance().flush();
    QVERIFY(QFile::exists(QDir(dir.path()).filePath(QStringLiteral("ed3k/hoster/sites.json"))));
    Ed3kHosterCatalog c(dir.path());
    QJsonObject got = c.account(QStringLiteral("files.example"), QStringLiteral("me"));
    QVERIFY(got.take(QStringLiteral("checked")).toDouble() > 0);
    QCOMPARE(got, premium);
    QCOMPARE(c.account(QStringLiteral("solver.example"), {}).value(QStringLiteral("balance")).toDouble(), 1.5);
    QVERIFY(c.account(QStringLiteral("gone.example"), QStringLiteral("old")).isEmpty());   // a removed login
    QCOMPARE(c.servers(QStringLiteral("files.example")), QStringList{QStringLiteral("dl1.files.example")});
}

QTEST_GUILESS_MAIN(TestHosterCatalog)
#include "tst_HosterCatalog.moc"
