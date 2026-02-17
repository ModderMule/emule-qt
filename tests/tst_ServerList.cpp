/// @file tst_ServerList.cpp
/// @brief Tests for server/ServerList — add/remove, persistence, lookups, signals.

#include "TestHelpers.h"
#include "server/ServerList.h"
#include "server/Server.h"
#include "utils/SafeFile.h"
#include "utils/OtherFunctions.h"
#include "utils/Opcodes.h"

#include <QSignalSpy>
#include <QTest>
#include <QFile>
#include <QTextStream>

#include <memory>

using namespace eMule;
using namespace eMule::testing;

// Helper to create a server with a valid public IP
static std::unique_ptr<Server> makeServer(uint32 ip, uint16 port,
                                           const QString& name = {})
{
    auto srv = std::make_unique<Server>(ip, port);
    if (!name.isEmpty())
        srv->setName(name);
    return srv;
}

// Helper to create a dynIP server
static std::unique_ptr<Server> makeDynServer(const QString& dynIP, uint16 port,
                                              const QString& name = {})
{
    auto srv = std::make_unique<Server>(0, port);
    srv->setDynIP(dynIP);
    if (!name.isEmpty())
        srv->setName(name);
    return srv;
}

class tst_ServerList : public QObject {
    Q_OBJECT

private slots:
    // Add/Remove
    void add_success();
    void add_duplicate_rejected();
    void add_badIP_rejected();
    void add_dynIP_server();
    void add_nullptr_rejected();
    void remove_success();
    void remove_nonexistent();
    void removeAll();

    // Lookups
    void findByIPTcp_found();
    void findByIPTcp_notFound();
    void findByIPUdp_standardPort();
    void findByIPUdp_obfuscationPort();
    void findByAddress_found();
    void findByAddress_notFound();
    void findByAddress_dynIP();

    // Round-robin
    void nextServer_wraps();
    void nextSearchServer_wraps();
    void nextStatServer_wraps();

    // Persistence: server.met
    void serverMet_saveLoad_roundTrip();
    void serverMet_corruptHeader();
    void serverMet_merge();

    // Static servers
    void staticServers_roundTrip();

    // Text import
    void textImport_ipPort();
    void textImport_ed2kLink();
    void textImport_mixed();
    void textImport_comments();

    // Stats
    void stats_aggregation();

    // Sorting
    void sort_byPreference();

    // Crypto keys
    void checkExpiredUDPKeys();

    // Signals
    void signal_serverAdded();
    void signal_serverAboutToBeRemoved();
    void signal_listReloaded();
    void signal_listSaved();

    // Index adjustment after removal
    void removal_doesNotCorruptRoundRobin();
};

// ---------------------------------------------------------------------------
// Add/Remove
// ---------------------------------------------------------------------------

void tst_ServerList::add_success()
{
    ServerList list;
    auto* srv = list.addServer(makeServer(0x08080808, 4661, QStringLiteral("S1")));
    QVERIFY(srv != nullptr);
    QCOMPARE(list.serverCount(), size_t{1});
    QCOMPARE(srv->name(), QStringLiteral("S1"));
}

void tst_ServerList::add_duplicate_rejected()
{
    ServerList list;
    list.addServer(makeServer(0x08080808, 4661));
    auto* dup = list.addServer(makeServer(0x08080808, 4661));
    QVERIFY(dup == nullptr);
    QCOMPARE(list.serverCount(), size_t{1});
}

void tst_ServerList::add_badIP_rejected()
{
    ServerList list;
    // 0.0.0.0 is not a good IP
    auto* srv = list.addServer(makeServer(0, 4661));
    QVERIFY(srv == nullptr);
    QCOMPARE(list.serverCount(), size_t{0});
}

void tst_ServerList::add_dynIP_server()
{
    ServerList list;
    auto* srv = list.addServer(makeDynServer(QStringLiteral("server.example.com"), 4661));
    QVERIFY(srv != nullptr);
    QCOMPARE(list.serverCount(), size_t{1});
    QCOMPARE(srv->dynIP(), QStringLiteral("server.example.com"));
}

void tst_ServerList::add_nullptr_rejected()
{
    ServerList list;
    auto* srv = list.addServer(nullptr);
    QVERIFY(srv == nullptr);
}

void tst_ServerList::remove_success()
{
    ServerList list;
    auto* srv = list.addServer(makeServer(0x08080808, 4661));
    QVERIFY(list.removeServer(srv));
    QCOMPARE(list.serverCount(), size_t{0});
}

void tst_ServerList::remove_nonexistent()
{
    ServerList list;
    Server fake(0x01020304, 4661);
    QVERIFY(!list.removeServer(&fake));
}

void tst_ServerList::removeAll()
{
    ServerList list;
    list.addServer(makeServer(0x08080808, 4661));
    list.addServer(makeServer(0x08080404, 4662));
    list.addServer(makeDynServer(QStringLiteral("dyn.test"), 4663));
    QCOMPARE(list.serverCount(), size_t{3});

    list.removeAllServers();
    QCOMPARE(list.serverCount(), size_t{0});
}

// ---------------------------------------------------------------------------
// Lookups
// ---------------------------------------------------------------------------

void tst_ServerList::findByIPTcp_found()
{
    ServerList list;
    list.addServer(makeServer(0x08080808, 4661));
    list.addServer(makeServer(0x08080404, 4662));

    auto* found = list.findByIPTcp(0x08080404, 4662);
    QVERIFY(found != nullptr);
    QCOMPARE(found->ip(), uint32{0x08080404});
    QCOMPARE(found->port(), uint16{4662});
}

void tst_ServerList::findByIPTcp_notFound()
{
    ServerList list;
    list.addServer(makeServer(0x08080808, 4661));
    QVERIFY(list.findByIPTcp(0x08080808, 9999) == nullptr);
}

void tst_ServerList::findByIPUdp_standardPort()
{
    ServerList list;
    list.addServer(makeServer(0x08080808, 4661));
    // Standard UDP port = TCP port + 4
    auto* found = list.findByIPUdp(0x08080808, 4665);
    QVERIFY(found != nullptr);
}

void tst_ServerList::findByIPUdp_obfuscationPort()
{
    ServerList list;
    auto srv = makeServer(0x08080808, 4661);
    srv->setObfuscationPortUDP(4670);
    list.addServer(std::move(srv));

    auto* found = list.findByIPUdp(0x08080808, 4670, true);
    QVERIFY(found != nullptr);
}

void tst_ServerList::findByAddress_found()
{
    ServerList list;
    list.addServer(makeServer(0x08080808, 4661));
    auto* found = list.findByAddress(ipstr(0x08080808), 4661);
    QVERIFY(found != nullptr);
}

void tst_ServerList::findByAddress_notFound()
{
    ServerList list;
    list.addServer(makeServer(0x08080808, 4661));
    QVERIFY(list.findByAddress(QStringLiteral("9.9.9.9"), 4661) == nullptr);
}

void tst_ServerList::findByAddress_dynIP()
{
    ServerList list;
    list.addServer(makeDynServer(QStringLiteral("test.server.com"), 4661));
    auto* found = list.findByAddress(QStringLiteral("test.server.com"), 4661);
    QVERIFY(found != nullptr);
    QCOMPARE(found->dynIP(), QStringLiteral("test.server.com"));
}

// ---------------------------------------------------------------------------
// Round-robin
// ---------------------------------------------------------------------------

void tst_ServerList::nextServer_wraps()
{
    ServerList list;
    list.addServer(makeServer(0x08080808, 4661, QStringLiteral("A")));
    list.addServer(makeServer(0x08080404, 4662, QStringLiteral("B")));

    auto* s1 = list.nextServer();
    auto* s2 = list.nextServer();
    auto* s3 = list.nextServer();  // should wrap

    QVERIFY(s1 != nullptr);
    QVERIFY(s2 != nullptr);
    QVERIFY(s3 != nullptr);
    QCOMPARE(s1->name(), QStringLiteral("A"));
    QCOMPARE(s2->name(), QStringLiteral("B"));
    QCOMPARE(s3->name(), QStringLiteral("A"));  // wrapped
}

void tst_ServerList::nextSearchServer_wraps()
{
    ServerList list;
    list.addServer(makeServer(0x08080808, 4661, QStringLiteral("X")));
    list.addServer(makeServer(0x08080404, 4662, QStringLiteral("Y")));

    auto* s1 = list.nextSearchServer();
    auto* s2 = list.nextSearchServer();
    auto* s3 = list.nextSearchServer();

    QCOMPARE(s1->name(), QStringLiteral("X"));
    QCOMPARE(s2->name(), QStringLiteral("Y"));
    QCOMPARE(s3->name(), QStringLiteral("X"));
}

void tst_ServerList::nextStatServer_wraps()
{
    ServerList list;
    list.addServer(makeServer(0x08080808, 4661, QStringLiteral("M")));

    auto* s1 = list.nextStatServer();
    auto* s2 = list.nextStatServer();
    QCOMPARE(s1, s2);  // Single server, wraps to same
}

// ---------------------------------------------------------------------------
// Persistence: server.met
// ---------------------------------------------------------------------------

void tst_ServerList::serverMet_saveLoad_roundTrip()
{
    TempDir tmp;
    const QString metPath = tmp.filePath(QStringLiteral("server.met"));

    // Create and populate list
    ServerList original;
    {
        auto srv1 = makeServer(0x08080808, 4661, QStringLiteral("Server1"));
        srv1->setDescription(QStringLiteral("First"));
        srv1->setUsers(1000);
        srv1->setFiles(50000);
        srv1->setPreference(ServerPriority::High);
        srv1->setVersion(QStringLiteral("17.15"));
        original.addServer(std::move(srv1));
    }
    {
        auto srv2 = makeDynServer(QStringLiteral("dyn.example.com"), 4662, QStringLiteral("Server2"));
        srv2->setDescription(QStringLiteral("Second"));
        srv2->setUsers(2000);
        original.addServer(std::move(srv2));
    }

    // Save
    QVERIFY(original.saveServerMet(metPath));
    QVERIFY(QFile::exists(metPath));

    // Load into new list
    ServerList loaded;
    QVERIFY(loaded.loadServerMet(metPath));
    QCOMPARE(loaded.serverCount(), size_t{2});

    // Verify first server
    auto* s1 = loaded.serverAt(0);
    QVERIFY(s1 != nullptr);
    QCOMPARE(s1->name(), QStringLiteral("Server1"));
    QCOMPARE(s1->description(), QStringLiteral("First"));
    QCOMPARE(s1->users(), uint32{1000});
    QCOMPARE(s1->files(), uint32{50000});
    QCOMPARE(s1->preference(), ServerPriority::High);
    QCOMPARE(s1->version(), QStringLiteral("17.15"));

    // Verify second server (dynIP → ip written as 0)
    auto* s2 = loaded.serverAt(1);
    QVERIFY(s2 != nullptr);
    QCOMPARE(s2->name(), QStringLiteral("Server2"));
    QCOMPARE(s2->dynIP(), QStringLiteral("dyn.example.com"));
    QCOMPARE(s2->ip(), uint32{0});
    QCOMPARE(s2->users(), uint32{2000});
}

void tst_ServerList::serverMet_corruptHeader()
{
    TempDir tmp;
    const QString metPath = tmp.filePath(QStringLiteral("corrupt.met"));

    // Write garbage header
    QFile f(metPath);
    QVERIFY(f.open(QIODevice::WriteOnly));
    const char bad = '\xFF';
    f.write(&bad, 1);
    f.close();

    ServerList list;
    QVERIFY(!list.loadServerMet(metPath));
    QCOMPARE(list.serverCount(), size_t{0});
}

void tst_ServerList::serverMet_merge()
{
    TempDir tmp;
    const QString met1 = tmp.filePath(QStringLiteral("servers1.met"));
    const QString met2 = tmp.filePath(QStringLiteral("servers2.met"));

    // Create two .met files
    {
        ServerList list1;
        list1.addServer(makeServer(0x08080808, 4661, QStringLiteral("S1")));
        QVERIFY(list1.saveServerMet(met1));
    }
    {
        ServerList list2;
        list2.addServer(makeServer(0x08080404, 4662, QStringLiteral("S2")));
        QVERIFY(list2.saveServerMet(met2));
    }

    // Load first, merge second
    ServerList merged;
    QVERIFY(merged.loadServerMet(met1));
    QCOMPARE(merged.serverCount(), size_t{1});

    QVERIFY(merged.addServerMetToList(met2, true));
    QCOMPARE(merged.serverCount(), size_t{2});
}

// ---------------------------------------------------------------------------
// Static servers
// ---------------------------------------------------------------------------

void tst_ServerList::staticServers_roundTrip()
{
    TempDir tmp;
    const QString staticPath = tmp.filePath(QStringLiteral("staticservers.dat"));

    ServerList original;
    {
        auto srv = makeDynServer(QStringLiteral("static.example.com"), 4661, QStringLiteral("StaticServer"));
        srv->setStaticMember(true);
        srv->setPreference(ServerPriority::High);
        original.addServer(std::move(srv));
    }
    {
        // Non-static server should not be saved
        original.addServer(makeServer(0x08080808, 4662, QStringLiteral("NonStatic")));
    }

    QVERIFY(original.saveStaticServers(staticPath));

    // Load into new list that already has some servers
    ServerList loaded;
    loaded.addServer(makeServer(0x04040404, 5000, QStringLiteral("Existing")));
    QVERIFY(loaded.loadStaticServers(staticPath));

    // The static server should have been added
    auto* found = loaded.findByAddress(QStringLiteral("static.example.com"), 4661);
    QVERIFY(found != nullptr);
    QVERIFY(found->isStaticMember());
    QCOMPARE(found->preference(), ServerPriority::High);
}

// ---------------------------------------------------------------------------
// Text import
// ---------------------------------------------------------------------------

void tst_ServerList::textImport_ipPort()
{
    TempDir tmp;
    const QString filePath = tmp.filePath(QStringLiteral("servers.txt"));

    QFile f(filePath);
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
    QTextStream s(&f);
    s << "8.8.8.8:4661\n";
    s << "8.8.4.4:4662\n";
    f.close();

    ServerList list;
    QCOMPARE(list.addServersFromTextFile(filePath), 2);
    QCOMPARE(list.serverCount(), size_t{2});
}

void tst_ServerList::textImport_ed2kLink()
{
    TempDir tmp;
    const QString filePath = tmp.filePath(QStringLiteral("servers.txt"));

    QFile f(filePath);
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
    QTextStream s(&f);
    s << "ed2k://|server|example.com|4661|/\n";
    f.close();

    ServerList list;
    QCOMPARE(list.addServersFromTextFile(filePath), 1);
    auto* srv = list.serverAt(0);
    QVERIFY(srv != nullptr);
    QCOMPARE(srv->dynIP(), QStringLiteral("example.com"));
    QCOMPARE(srv->port(), uint16{4661});
}

void tst_ServerList::textImport_mixed()
{
    TempDir tmp;
    const QString filePath = tmp.filePath(QStringLiteral("servers.txt"));

    QFile f(filePath);
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
    QTextStream s(&f);
    s << "8.8.8.8:4661\n";
    s << "ed2k://|server|srv.example.com|4662|/\n";
    s << "# comment line\n";
    s << "  \n";  // blank line
    f.close();

    ServerList list;
    QCOMPARE(list.addServersFromTextFile(filePath), 2);
}

void tst_ServerList::textImport_comments()
{
    TempDir tmp;
    const QString filePath = tmp.filePath(QStringLiteral("servers.txt"));

    QFile f(filePath);
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
    QTextStream s(&f);
    s << "# This is a comment\n";
    s << "// Also a comment\n";
    s << "\n";
    f.close();

    ServerList list;
    QCOMPARE(list.addServersFromTextFile(filePath), 0);
}

// ---------------------------------------------------------------------------
// Stats
// ---------------------------------------------------------------------------

void tst_ServerList::stats_aggregation()
{
    ServerList list;
    {
        auto srv = makeServer(0x08080808, 4661);
        srv->setUsers(1000);
        srv->setFiles(50000);
        srv->setLowIDUsers(100);
        list.addServer(std::move(srv));
    }
    {
        auto srv = makeServer(0x08080404, 4662);
        srv->setUsers(2000);
        srv->setFiles(80000);
        srv->setLowIDUsers(200);
        srv->setFailedCount(1);  // failed server
        list.addServer(std::move(srv));
    }

    auto s = list.stats();
    QCOMPARE(s.total, uint32{2});
    QCOMPARE(s.failed, uint32{1});
    QCOMPARE(s.users, uint32{1000});    // only non-failed
    QCOMPARE(s.files, uint32{50000});   // only non-failed
    QCOMPARE(s.lowIDUsers, uint32{100});
}

// ---------------------------------------------------------------------------
// Sorting
// ---------------------------------------------------------------------------

void tst_ServerList::sort_byPreference()
{
    ServerList list;
    {
        auto srv = makeServer(0x08080808, 4661, QStringLiteral("Normal"));
        srv->setPreference(ServerPriority::Normal);
        list.addServer(std::move(srv));
    }
    {
        auto srv = makeServer(0x08080404, 4662, QStringLiteral("Low"));
        srv->setPreference(ServerPriority::Low);
        list.addServer(std::move(srv));
    }
    {
        auto srv = makeServer(0x08080101, 4663, QStringLiteral("High"));
        srv->setPreference(ServerPriority::High);
        list.addServer(std::move(srv));
    }

    list.sortByPreference();

    QCOMPARE(list.serverAt(0)->name(), QStringLiteral("High"));
    QCOMPARE(list.serverAt(1)->name(), QStringLiteral("Normal"));
    QCOMPARE(list.serverAt(2)->name(), QStringLiteral("Low"));
}

// ---------------------------------------------------------------------------
// Crypto keys
// ---------------------------------------------------------------------------

void tst_ServerList::checkExpiredUDPKeys()
{
    ServerList list;
    {
        auto srv = makeServer(0x08080808, 4661);
        srv->setUDPFlags(SrvUdpFlag::UdpObfuscation);
        srv->setServerKeyUDP(0xDEADBEEF);
        srv->setServerKeyUDPIP(0xAAAAAAAA);  // old IP
        srv->setLastPingedTime(1700000000);
        list.addServer(std::move(srv));
    }

    // Check with new client IP — should expire the key
    list.checkForExpiredUDPKeys(0xBBBBBBBB);

    auto* srv = list.serverAt(0);
    QCOMPARE(srv->lastPingedTime(), uint32{0});  // reset for immediate re-ping
}

// ---------------------------------------------------------------------------
// Signals
// ---------------------------------------------------------------------------

void tst_ServerList::signal_serverAdded()
{
    ServerList list;
    QSignalSpy spy(&list, &ServerList::serverAdded);
    list.addServer(makeServer(0x08080808, 4661));

    QCOMPARE(spy.count(), 1);
}

void tst_ServerList::signal_serverAboutToBeRemoved()
{
    ServerList list;
    auto* srv = list.addServer(makeServer(0x08080808, 4661));
    QSignalSpy spy(&list, &ServerList::serverAboutToBeRemoved);

    list.removeServer(srv);
    QCOMPARE(spy.count(), 1);
}

void tst_ServerList::signal_listReloaded()
{
    TempDir tmp;
    const QString metPath = tmp.filePath(QStringLiteral("server.met"));

    // Create a valid .met file
    ServerList writer;
    writer.addServer(makeServer(0x08080808, 4661));
    QVERIFY(writer.saveServerMet(metPath));

    ServerList reader;
    QSignalSpy spy(&reader, &ServerList::listReloaded);
    QVERIFY(reader.loadServerMet(metPath));
    QCOMPARE(spy.count(), 1);
}

void tst_ServerList::signal_listSaved()
{
    TempDir tmp;
    const QString metPath = tmp.filePath(QStringLiteral("server.met"));

    ServerList list;
    list.addServer(makeServer(0x08080808, 4661));

    QSignalSpy spy(&list, &ServerList::listSaved);
    QVERIFY(list.saveServerMet(metPath));
    QCOMPARE(spy.count(), 1);
}

// ---------------------------------------------------------------------------
// Index adjustment after removal
// ---------------------------------------------------------------------------

void tst_ServerList::removal_doesNotCorruptRoundRobin()
{
    ServerList list;
    list.addServer(makeServer(0x08080808, 4661, QStringLiteral("A")));
    list.addServer(makeServer(0x08080404, 4662, QStringLiteral("B")));
    list.addServer(makeServer(0x08080101, 4663, QStringLiteral("C")));

    // Advance round-robin past first server
    auto* s1 = list.nextServer();
    QCOMPARE(s1->name(), QStringLiteral("A"));

    // Remove B (middle)
    auto* b = list.findByIPTcp(0x08080404, 4662);
    QVERIFY(b != nullptr);
    list.removeServer(b);

    // Next should still work without crash
    auto* s2 = list.nextServer();
    QVERIFY(s2 != nullptr);
    QCOMPARE(list.serverCount(), size_t{2});
}

QTEST_MAIN(tst_ServerList)
#include "tst_ServerList.moc"
