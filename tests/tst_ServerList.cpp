/// @file tst_ServerList.cpp
/// @brief Tests for server/ServerList — add/remove, persistence, lookups, signals.

#include "TestHelpers.h"
#include "app/AppContext.h"
#include "prefs/Preferences.h"
#include "server/ServerList.h"
#include "server/Server.h"
#include "server/ServerConnect.h"
#include "kademlia/Kademlia.h"
#include "kademlia/KadPrefs.h"
#include "ipfilter/IPFilter.h"
#include "protocol/Tag.h"
#include "utils/SafeFile.h"
#include "utils/OtherFunctions.h"
#include "utils/Opcodes.h"

#include <QDateTime>
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

    // OP_GLOBSERVSTATRES parsing
    void statusResponse_full40Bytes();
    void statusResponse_short34Bytes();
    void statusResponse_oversizedVendorExtension();
    void statusResponse_challengeMismatchIgnored();
    void statusResponse_unknownSenderIgnored();
    void statusResponse_defaultObfuscationPorts();

    // OP_GLOBSERVSTATRES trailing observed-IPv4 reflection (+40)
    void observedIPv4_adoptedAfterThresholdDistinctServers();
    void observedIPv4_needsExactly44Bytes();
    void observedIPv4_rejectsImplausibleValues();
    void observedIPv4_shadowedByEd2kSession();
    void observedIPv4_stickyWhenNoNewWinner();
    void observedIPv4_expiresServerUDPKeys();

    // OP_SERVERLIST parsing
    void serverListPacket_addsServers();
    void serverListPacket_rejectsTruncated();

    // OP_SERVER_DESC_RES parsing
    void descResponse_taggedRefresh();
    void descResponse_wrongChallengeIgnored();
    void descResponse_oldFormat();

    // server.met write header
    void serverMet_writeHeaderIsE0();

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

    // IPv6 servers
    void add_ipv6Server_accepted();
    void add_ipv6_notDuplicateOfDynIP();
    void add_ipv6_duplicateRejectedOnce();
    void serverMet_ipv6RoundTrip();
    void serverMet_ipv6MixedList();
    void staticServers_ipv6RoundTrip();
    void staticServers_numericIPv4NotDynIP();
    void textImport_ipv6Bracketed();
    void textImport_ed2kLinkIPv6();
    void findByIPUdp_ipv6();

    // Stats
    void stats_aggregation();

    // Sorting
    void sort_byPreference();

    // Crypto keys
    void checkExpiredUDPKeys();
    void checkExpiredUDPKeys_staggersRecentlyPinged();
    void udpKey_hiddenWhenPublicIPDiffers();
    void udpKey_hiddenWhenPublicIPUnknown();
    void udpKey_visibleWhenPublicIPMatches();
    void setServerKeyUDP_stampsCurrentPublicIP();
    void setPublicIP_expiresKeysOnRealChange();
    void setPublicIP_noExpiryWhenUnchanged();
    void setPublicIP_noExpiryOnClear();

    // Signals
    void signal_serverAdded();
    void signal_serverAboutToBeRemoved();
    void signal_listReloaded();
    void signal_listSaved();

    // Index adjustment after removal
    void removal_doesNotCorruptRoundRobin();

    // Divergence-audit follow-ups (#11, #13, #18, #24, #29, #30, #33, #34)
    void addServer_ipFilterRejects();                  // #11
    void addTagFromFile_stPortDoesNotOverridePort();   // #13
    void removeDuplicatesByAddress_collapses();        // #18
    void applyUserOrder_reordersList();                // #24
    void isGoodServerIP_honorsFilterLANIPs();          // #29
    void addDuplicate_resetsFailedCount();             // #30
    void moveServerDown_toBottom();                    // #33
    void getSuccServer_nonWrapping();                  // #34
    void getServerByIP_ipOnly();                       // #34

    // Obfuscated stat crypt-ping (port+12) — serverStats() two-branch state machine
    void serverStats_sendsObfuscatedCryptPingFirst();
    void serverStats_cryptPingSentWithoutAKnownPublicIP();
    void serverStats_fallsBackToPlaintextWhenPending();
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
    QCOMPARE(found->ipAddress().toNetworkUint32(), uint32{0x08080404});
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
    QCOMPARE(s2->ipAddress().toNetworkUint32(), uint32{0});
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
// IPv6 servers
//
// 2a01:4f8::1 is genuine global unicast; 2001:db8::/32 is documentation space and is
// rejected by Address::isPublicIP(), so it would never reach the list.
// ---------------------------------------------------------------------------

static std::unique_ptr<Server> makeIPv6Server(const char* literal, uint16 port,
                                              const QString& name = {})
{
    auto srv = std::make_unique<Server>(Address::fromString(QString::fromLatin1(literal)), port);
    if (!name.isEmpty())
        srv->setName(name);
    return srv;
}

void tst_ServerList::add_ipv6Server_accepted()
{
    ServerList list;
    auto* srv = list.addServer(makeIPv6Server("2a01:4f8::1", 4661, QStringLiteral("v6")));
    QVERIFY(srv != nullptr);
    QCOMPARE(list.serverCount(), size_t{1});
    QVERIFY(srv->ipAddress().isIPv6());

    const Address v6 = Address::fromString(QStringLiteral("2a01:4f8::1"));
    QCOMPARE(list.findByIPTcp(v6, 4661), srv);
    QCOMPARE(list.findByAddress(QStringLiteral("2a01:4f8::1"), 4661), srv);
}

void tst_ServerList::add_ipv6_notDuplicateOfDynIP()
{
    // Regression: both projected through toNetworkUint32() == 0, so the IPv6 server was
    // rejected as the dynIP server's duplicate — and reset that server's failed count.
    ServerList list;
    auto dyn = makeDynServer(QStringLiteral("dyn.example.com"), 4661, QStringLiteral("dyn"));
    dyn->setFailedCount(3);
    Server* dynEntry = list.addServer(std::move(dyn));
    QVERIFY(dynEntry != nullptr);

    QVERIFY(list.addServer(makeIPv6Server("2a01:4f8::1", 4661, QStringLiteral("v6"))) != nullptr);
    QCOMPARE(list.serverCount(), size_t{2});
    QCOMPARE(dynEntry->failedCount(), uint32{3});   // untouched
}

void tst_ServerList::add_ipv6_duplicateRejectedOnce()
{
    ServerList list;
    Server* first = list.addServer(makeIPv6Server("2a01:4f8::1", 4661));
    QVERIFY(first != nullptr);
    first->setFailedCount(4);

    QVERIFY(list.addServer(makeIPv6Server("2a01:4f8::1", 4661)) == nullptr);
    QCOMPARE(list.serverCount(), size_t{1});
    QCOMPARE(first->failedCount(), uint32{0});   // re-announced ⇒ revived
}

void tst_ServerList::serverMet_ipv6RoundTrip()
{
    TempDir tmp;
    const QString metPath = tmp.filePath(QStringLiteral("server.met"));

    ServerList original;
    {
        auto srv = makeIPv6Server("2a01:4f8::1", 4661, QStringLiteral("v6 server"));
        srv->setDescription(QStringLiteral("IPv6 only"));
        original.addServer(std::move(srv));
    }
    QVERIFY(original.saveServerMet(metPath));

    // Without the ST_IPV6 tag the entry would come back with a null address and be
    // rejected by isGoodServerIP — i.e. vanish.
    ServerList loaded;
    QVERIFY(loaded.loadServerMet(metPath));
    QCOMPARE(loaded.serverCount(), size_t{1});

    auto* s = loaded.serverAt(0);
    QVERIFY(s != nullptr);
    QVERIFY(s->ipAddress().isIPv6());
    QCOMPARE(s->address(), QStringLiteral("2a01:4f8::1"));
    QCOMPARE(s->port(), uint16{4661});
    QCOMPARE(s->name(), QStringLiteral("v6 server"));
    QVERIFY(!s->hasDynIP());
}

void tst_ServerList::serverMet_ipv6MixedList()
{
    TempDir tmp;
    const QString metPath = tmp.filePath(QStringLiteral("server.met"));

    ServerList original;
    original.addServer(makeServer(0x08080808, 4661, QStringLiteral("v4")));
    original.addServer(makeIPv6Server("2a01:4f8::1", 4662, QStringLiteral("v6")));
    original.addServer(makeDynServer(QStringLiteral("dyn.example.com"), 4663,
                                     QStringLiteral("dyn")));
    QVERIFY(original.saveServerMet(metPath));

    ServerList loaded;
    QVERIFY(loaded.loadServerMet(metPath));
    QCOMPARE(loaded.serverCount(), size_t{3});
    QVERIFY(loaded.serverAt(0)->ipAddress().isIPv4());
    QVERIFY(loaded.serverAt(1)->ipAddress().isIPv6());
    QCOMPARE(loaded.serverAt(2)->dynIP(), QStringLiteral("dyn.example.com"));
}

void tst_ServerList::staticServers_ipv6RoundTrip()
{
    TempDir tmp;
    const QString staticPath = tmp.filePath(QStringLiteral("staticservers.dat"));

    ServerList original;
    {
        auto srv = makeIPv6Server("2a01:4f8::1", 4661, QStringLiteral("v6 static"));
        srv->setStaticMember(true);
        srv->setPreference(ServerPriority::High);
        original.addServer(std::move(srv));
    }
    QVERIFY(original.saveStaticServers(staticPath));

    // The line must carry the bracketed endpoint form so it parses back.
    QFile f(staticPath);
    QVERIFY(f.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString content = QString::fromUtf8(f.readAll());
    f.close();
    QVERIFY(content.contains(QStringLiteral("[2a01:4f8::1]:4661")));

    ServerList loaded;
    QVERIFY(loaded.loadStaticServers(staticPath));
    auto* found = loaded.findByAddress(QStringLiteral("2a01:4f8::1"), 4661);
    QVERIFY(found != nullptr);
    QVERIFY(found->isStaticMember());
    QVERIFY(found->ipAddress().isIPv6());
    QVERIFY(!found->hasDynIP());          // a literal must not be resolved at connect time
    QCOMPARE(found->preference(), ServerPriority::High);
}

void tst_ServerList::staticServers_numericIPv4NotDynIP()
{
    TempDir tmp;
    const QString staticPath = tmp.filePath(QStringLiteral("staticservers.dat"));

    QFile f(staticPath);
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
    QTextStream s(&f);
    s << "8.8.8.8:4661,1,NumericStatic\n";
    f.close();

    // Regression: loadStaticServers() used to call setDynIP() unconditionally, so every
    // connect issued QDnsLookup(A, "8.8.8.8"), NXDOMAINed and marked the server dead.
    ServerList list;
    QVERIFY(list.loadStaticServers(staticPath));
    auto* srv = list.findByAddress(QStringLiteral("8.8.8.8"), 4661);
    QVERIFY(srv != nullptr);
    QVERIFY(!srv->hasDynIP());
    QVERIFY(srv->ipAddress().isIPv4());
}

void tst_ServerList::textImport_ipv6Bracketed()
{
    TempDir tmp;
    const QString filePath = tmp.filePath(QStringLiteral("servers.txt"));

    QFile f(filePath);
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
    QTextStream s(&f);
    s << "[2a01:4f8::1]:4661\n";
    f.close();

    ServerList list;
    QCOMPARE(list.addServersFromTextFile(filePath), 1);
    auto* srv = list.serverAt(0);
    QVERIFY(srv != nullptr);
    QVERIFY(srv->ipAddress().isIPv6());
    QCOMPARE(srv->port(), uint16{4661});
    QVERIFY(!srv->hasDynIP());
}

void tst_ServerList::textImport_ed2kLinkIPv6()
{
    TempDir tmp;
    const QString filePath = tmp.filePath(QStringLiteral("servers.txt"));

    QFile f(filePath);
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
    QTextStream s(&f);
    s << "ed2k://|server|[2a01:4f8::1]|4661|/\n";
    f.close();

    ServerList list;
    QCOMPARE(list.addServersFromTextFile(filePath), 1);
    auto* srv = list.serverAt(0);
    QVERIFY(srv != nullptr);
    QVERIFY(srv->ipAddress().isIPv6());
    QVERIFY(!srv->hasDynIP());
    QCOMPARE(srv->address(), QStringLiteral("2a01:4f8::1"));
}

void tst_ServerList::findByIPUdp_ipv6()
{
    ServerList list;
    auto* srv = list.addServer(makeIPv6Server("2a01:4f8::1", 4661));
    QVERIFY(srv != nullptr);

    const Address v6 = Address::fromString(QStringLiteral("2a01:4f8::1"));
    QCOMPARE(list.findByIPUdp(v6, 4665), srv);                 // TCP + 4
    QCOMPARE(list.findByIPUdp(v6, 4673), srv);                 // TCP + 12 (obfuscated)
    QVERIFY(list.findByIPUdp(v6, 9999) == nullptr);
    QVERIFY(list.findByIPUdp(Address{}, 4665) == nullptr);     // a null address never matches
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

// An IP change must not make the whole list come due at once. A server pinged
// within UDPSERVSTATMINREASKTIME is backdated so it falls due exactly when that
// minimum elapses. MFC: CServerList::CheckForExpiredUDPKeys — ServerList.cpp:1101.
void tst_ServerList::checkExpiredUDPKeys_staggersRecentlyPinged()
{
    const auto now = static_cast<uint32>(QDateTime::currentSecsSinceEpoch());

    ServerList list;
    {
        auto srv = makeServer(0x08080808, 4661);
        srv->setUDPFlags(SrvUdpFlag::UdpObfuscation);
        srv->setServerKeyUDP(0xDEADBEEF);
        srv->setServerKeyUDPIP(0xAAAAAAAA);      // old IP
        srv->setRealLastPingedTime(now - 60);    // pinged a minute ago
        srv->setLastPingedTime(1700000000);
        list.addServer(std::move(srv));
    }

    list.checkForExpiredUDPKeys(0xBBBBBBBB);

    auto* srv = list.serverAt(0);
    // Backdated, not zeroed: due in (MINREASK - 60) seconds from now.
    QVERIFY(srv->lastPingedTime() != 0);
    const uint32 dueAt = srv->lastPingedTime() + static_cast<uint32>(UDPSERVSTATREASKTIME);
    QVERIFY(dueAt > now);
    QCOMPARE(dueAt - now, static_cast<uint32>(UDPSERVSTATMINREASKTIME) - 60);
}

// ---------------------------------------------------------------------------
// UDP key visibility — the guard against obfuscating with a dead key.
//
// A key is bound to the public IP it was issued for. serverKeyUDP() must hide
// it once that IP no longer matches, so UDPSocket::sendPacket falls back to a
// plaintext ping on port+4 instead of encrypting to port+14 with a key the
// server has already dropped (which yields zero replies).
// MFC: CServer::GetServerKeyUDP() — Server.cpp:322.
// ---------------------------------------------------------------------------

namespace {
/// Restores theApp.publicIP() on scope exit so case ordering stays independent.
/// Reads the raw ED2K slot: that is what setPublicIP() writes, so restoring through
/// either of the tiered getters would store Kad's address — or a server-corroborated
/// one — as though a session had reported it.
class PublicIPGuard {
public:
    explicit PublicIPGuard(uint32 ip) : m_saved(theApp.ed2kSessionIP()) { theApp.setPublicIP(ip); }
    ~PublicIPGuard() { theApp.setPublicIP(m_saved); }
    PublicIPGuard(const PublicIPGuard&) = delete;
    PublicIPGuard& operator=(const PublicIPGuard&) = delete;
private:
    uint32 m_saved;
};
} // namespace

void tst_ServerList::udpKey_hiddenWhenPublicIPDiffers()
{
    PublicIPGuard guard(0xBBBBBBBB);  // our IP changed since the key was issued

    auto srv = makeServer(0x08080808, 4661);
    srv->setUDPFlags(SrvUdpFlag::UdpObfuscation);
    srv->setServerKeyUDP(0xDEADBEEF);
    srv->setServerKeyUDPIP(0xAAAAAAAA);

    QCOMPARE(srv->serverKeyUDP(), uint32{0});             // hidden → plaintext ping
    QCOMPARE(srv->serverKeyUDPRaw(), uint32{0xDEADBEEF}); // still stored, for expiry/persistence
}

void tst_ServerList::udpKey_hiddenWhenPublicIPUnknown()
{
    PublicIPGuard guard(0);  // Kad-only: no peer has told us our public IP yet

    auto srv = makeServer(0x08080808, 4661);
    srv->setUDPFlags(SrvUdpFlag::UdpObfuscation);
    srv->setServerKeyUDP(0xDEADBEEF);
    srv->setServerKeyUDPIP(0);  // stamped before we knew our IP

    // Must not match just because both sides are zero.
    QCOMPARE(srv->serverKeyUDP(), uint32{0});
    QCOMPARE(srv->serverKeyUDPRaw(), uint32{0xDEADBEEF});
}

void tst_ServerList::udpKey_visibleWhenPublicIPMatches()
{
    PublicIPGuard guard(0xAAAAAAAA);

    auto srv = makeServer(0x08080808, 4661);
    srv->setUDPFlags(SrvUdpFlag::UdpObfuscation);
    srv->setServerKeyUDP(0xDEADBEEF);
    srv->setServerKeyUDPIP(0xAAAAAAAA);

    // The positive case: gating must not degrade into "obfuscation never happens".
    QCOMPARE(srv->serverKeyUDP(), uint32{0xDEADBEEF});
}

void tst_ServerList::setServerKeyUDP_stampsCurrentPublicIP()
{
    PublicIPGuard guard(0xC0A80101);

    auto srv = makeServer(0x08080808, 4661);
    srv->setUDPFlags(SrvUdpFlag::UdpObfuscation);
    srv->setServerKeyUDP(0x12345678);  // stamps the IP itself, as MFC does

    QCOMPARE(srv->serverKeyUDPIP(), uint32{0xC0A80101});
    QCOMPARE(srv->serverKeyUDP(), uint32{0x12345678});
}

// ---------------------------------------------------------------------------
// theApp.setPublicIP() carries MFC's key-expiry trigger (Emule.cpp:1563), so
// every writer — HighID, server-reported IP, peer answer — gets it for free
// rather than each having to remember. These cases pin the trigger condition.
// ---------------------------------------------------------------------------

namespace {
/// Installs a ServerList on theApp for the duration of a case and restores
/// whatever was there, since theApp is process-global.
class ScopedServerList {
public:
    explicit ScopedServerList(ServerList* list) : m_saved(theApp.serverList) { theApp.serverList = list; }
    ~ScopedServerList() { theApp.serverList = m_saved; }
    ScopedServerList(const ScopedServerList&) = delete;
    ScopedServerList& operator=(const ScopedServerList&) = delete;
private:
    ServerList* m_saved;
};

/// A server holding an obfuscation key issued for @p keyIP, pinged just now.
Server* addKeyedServer(ServerList& list, uint32 keyIP)
{
    auto srv = makeServer(0x08080808, 4661);
    srv->setUDPFlags(SrvUdpFlag::UdpObfuscation);
    srv->setServerKeyUDP(0xDEADBEEF);
    srv->setServerKeyUDPIP(keyIP);
    auto* added = list.addServer(std::move(srv));
    added->setLastPingedTime(static_cast<uint32>(QDateTime::currentSecsSinceEpoch()));
    return added;
}
} // namespace

void tst_ServerList::setPublicIP_expiresKeysOnRealChange()
{
    PublicIPGuard guard(0xAAAAAAAA);
    ServerList list;
    ScopedServerList installed(&list);

    auto* srv = addKeyedServer(list, 0xAAAAAAAA);
    QCOMPARE(srv->serverKeyUDP(), uint32{0xDEADBEEF});  // valid before the change

    theApp.setPublicIP(0xBBBBBBBB);

    // Key is now hidden, and the server was re-queued for a stat ping to get a
    // fresh one. Without the re-queue it would sit unusable for UDPSERVSTATREASKTIME.
    QCOMPARE(srv->serverKeyUDP(), uint32{0});
    QVERIFY(srv->realLastPingedTime() != static_cast<uint32>(QDateTime::currentSecsSinceEpoch()));
}

void tst_ServerList::setPublicIP_noExpiryWhenUnchanged()
{
    PublicIPGuard guard(0xAAAAAAAA);
    ServerList list;
    ScopedServerList installed(&list);

    auto* srv = addKeyedServer(list, 0xAAAAAAAA);
    const uint32 pingedBefore = srv->realLastPingedTime();

    theApp.setPublicIP(0xAAAAAAAA);  // same IP — nothing was invalidated

    QCOMPARE(srv->serverKeyUDP(), uint32{0xDEADBEEF});
    QCOMPARE(srv->realLastPingedTime(), pingedBefore);
}

void tst_ServerList::setPublicIP_noExpiryOnClear()
{
    PublicIPGuard guard(0xAAAAAAAA);
    ServerList list;
    ScopedServerList installed(&list);

    auto* srv = addKeyedServer(list, 0xAAAAAAAA);
    const uint32 pingedBefore = srv->realLastPingedTime();

    // Server disconnect clears the IP. MFC skips the expiry sweep for 0 (the
    // dwIP != 0 guard): we have not learned a *new* address, so re-pinging the
    // whole list would be pointless churn on every disconnect.
    //
    // The sweep does still run if a *lower* tier now supplies a different address —
    // see observedIPv4_expiresServerUDPKeys. Here there is no corroborated address, so
    // the effective one really is gone and this stays the no-op MFC intends.
    theApp.setPublicIP(0);

    QCOMPARE(srv->realLastPingedTime(), pingedBefore);
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

// ---------------------------------------------------------------------------
// OP_GLOBSERVSTATRES (0x97) parsing — ServerList::processStatusResponse
// ---------------------------------------------------------------------------

// Build a GLOBSERVSTATRES body (everything after prot+opcode).
static QByteArray makeStatusBody(uint32 challenge, uint32 users, uint32 files,
                                 uint32 maxUsers = 0, uint32 softFiles = 0,
                                 uint32 hardFiles = 0, uint32 udpFlags = 0,
                                 uint32 lowIDUsers = 0, uint16 udpObfPort = 0,
                                 uint16 tcpObfPort = 0, uint32 udpKey = 0,
                                 int truncateTo = -1)
{
    QByteArray b(40, '\0');
    auto* p = reinterpret_cast<uint8*>(b.data());
    pokeUInt32(p,      challenge);
    pokeUInt32(p + 4,  users);
    pokeUInt32(p + 8,  files);
    pokeUInt32(p + 12, maxUsers);
    pokeUInt32(p + 16, softFiles);
    pokeUInt32(p + 20, hardFiles);
    pokeUInt32(p + 24, udpFlags);
    pokeUInt32(p + 28, lowIDUsers);
    pokeUInt16(p + 32, udpObfPort);
    pokeUInt16(p + 34, tcpObfPort);
    pokeUInt32(p + 36, udpKey);
    if (truncateTo >= 0)
        b.truncate(truncateTo);
    return b;
}

static Endpoint udpSenderFor(uint32 ip, uint16 tcpPort)
{
    return Endpoint::fromNetworkOrder(ip, static_cast<uint16>(tcpPort + 4));
}

void tst_ServerList::statusResponse_full40Bytes()
{
    ServerList list;
    auto srv = makeServer(0x08080808, 4661);
    srv->setChallenge(0x55AA1234);
    Server* s = list.addServer(std::move(srv));
    QVERIFY(s != nullptr);

    const QByteArray body = makeStatusBody(0x55AA1234, 1000, 2000, 3000, 4000, 5000,
                                           SrvUdpFlag::UdpObfuscation, 42, 4670, 4661,
                                           0xDEADBEEF);

    list.processStatusResponse(reinterpret_cast<const uint8*>(body.constData()),
                               static_cast<uint32>(body.size()),
                               udpSenderFor(0x08080808, 4661));

    QCOMPARE(s->users(), 1000u);
    QCOMPARE(s->files(), 2000u);
    QCOMPARE(s->maxUsers(), 3000u);
    QCOMPARE(s->softFiles(), 4000u);
    QCOMPARE(s->hardFiles(), 5000u);
    QCOMPARE(s->lowIDUsers(), 42u);
    // Raw accessor: this asserts the key was *parsed*, not that it is currently
    // usable — serverKeyUDP() also requires our public IP to match the stamp.
    QCOMPARE(s->serverKeyUDPRaw(), 0xDEADBEEFu);
    QCOMPARE(s->obfuscationPortUDP(), quint16{4670});
    QCOMPARE(s->obfuscationPortTCP(), quint16{4661});
    // Challenge is cleared so a replayed packet cannot be re-applied
    QCOMPARE(s->challenge(), 0u);
}

void tst_ServerList::statusResponse_short34Bytes()
{
    // Real-world Lugdunum servers reply with 34 bytes: no obfuscation ports,
    // no UDP key. Everything up to lowIDUsers must still be applied.
    ServerList list;
    auto srv = makeServer(0x08080808, 4661);
    srv->setChallenge(0x55AA1234);
    Server* s = list.addServer(std::move(srv));

    const QByteArray body = makeStatusBody(0x55AA1234, 111, 222, 333, 444, 555,
                                           0, 66, 0, 0, 0, /*truncateTo*/ 34);
    QCOMPARE(body.size(), qsizetype{34});

    list.processStatusResponse(reinterpret_cast<const uint8*>(body.constData()),
                               static_cast<uint32>(body.size()),
                               udpSenderFor(0x08080808, 4661));

    QCOMPARE(s->users(), 111u);
    QCOMPARE(s->files(), 222u);
    QCOMPARE(s->maxUsers(), 333u);
    QCOMPARE(s->lowIDUsers(), 66u);
    // Must NOT invent a UDP key from absent bytes
    QCOMPARE(s->serverKeyUDP(), 0u);
}

void tst_ServerList::statusResponse_oversizedVendorExtension()
{
    // ed2kNET appends a tagged X25519 public key past byte 40. We must parse the
    // standard fields and ignore the tail rather than reject the packet.
    ServerList list;
    auto srv = makeServer(0x08080808, 4661);
    srv->setChallenge(0x55AA1234);
    Server* s = list.addServer(std::move(srv));

    QByteArray body = makeStatusBody(0x55AA1234, 7, 8, 0, 0, 0, 0, 0, 0, 0, 0xCAFEBABE);
    body.append(QByteArray(64, '\xAB'));   // vendor tail
    QCOMPARE(body.size(), qsizetype{104});

    list.processStatusResponse(reinterpret_cast<const uint8*>(body.constData()),
                               static_cast<uint32>(body.size()),
                               udpSenderFor(0x08080808, 4661));

    QCOMPARE(s->users(), 7u);
    QCOMPARE(s->files(), 8u);
    QCOMPARE(s->serverKeyUDPRaw(), 0xCAFEBABEu);  // parsed; see full40Bytes note
}

void tst_ServerList::statusResponse_challengeMismatchIgnored()
{
    ServerList list;
    auto srv = makeServer(0x08080808, 4661);
    srv->setChallenge(0x55AA1234);
    Server* s = list.addServer(std::move(srv));

    const QByteArray body = makeStatusBody(0x55AA9999, 1000, 2000);
    list.processStatusResponse(reinterpret_cast<const uint8*>(body.constData()),
                               static_cast<uint32>(body.size()),
                               udpSenderFor(0x08080808, 4661));

    QCOMPARE(s->users(), 0u);
    QCOMPARE(s->files(), 0u);
    QCOMPARE(s->challenge(), 0x55AA1234u);   // still pending
}

void tst_ServerList::statusResponse_unknownSenderIgnored()
{
    ServerList list;
    auto srv = makeServer(0x08080808, 4661);
    srv->setChallenge(0x55AA1234);
    Server* s = list.addServer(std::move(srv));

    const QByteArray body = makeStatusBody(0x55AA1234, 1000, 2000);
    // Same challenge, wrong source IP — must not be applied to our server
    list.processStatusResponse(reinterpret_cast<const uint8*>(body.constData()),
                               static_cast<uint32>(body.size()),
                               udpSenderFor(0x09090909, 4661));

    QCOMPARE(s->users(), 0u);
    QCOMPARE(s->challenge(), 0x55AA1234u);
}

void tst_ServerList::statusResponse_defaultObfuscationPorts()
{
    // Short packet carried no port data, but the flags claim obfuscation:
    // fall back to port and port+12 (matches original eMule).
    ServerList list;
    auto srv = makeServer(0x08080808, 4661);
    srv->setChallenge(0x55AA1234);
    Server* s = list.addServer(std::move(srv));

    const QByteArray body = makeStatusBody(
        0x55AA1234, 1, 2, 0, 0, 0,
        SrvUdpFlag::UdpObfuscation | SrvUdpFlag::TcpObfuscation,
        0, 0, 0, 0, /*truncateTo*/ 32);

    list.processStatusResponse(reinterpret_cast<const uint8*>(body.constData()),
                               static_cast<uint32>(body.size()),
                               udpSenderFor(0x08080808, 4661));

    QCOMPARE(s->obfuscationPortTCP(), quint16{4661});
    QCOMPARE(s->obfuscationPortUDP(), quint16{4673});
}

// ---------------------------------------------------------------------------
// The 4 trailing bytes at payload offset +40: the IPv4 the server observed us on.
// eNode-go appends it on both the plain and the obfuscated channel; the original
// eserver only on the obfuscated one. eMule discards it. We adopt it, but only as
// the lowest tier of theApp.publicIP() and only once several distinct servers
// independently agree — a single unauthenticated datagram is not evidence.
// ---------------------------------------------------------------------------

namespace {

/// A 44-byte status body: the standard 40 plus the reflected address.
QByteArray withObservedIP(QByteArray body, uint32 ipNet)
{
    const auto at = body.size();
    body.resize(at + 4);
    pokeUInt32(reinterpret_cast<uint8*>(body.data()) + at, ipNet);
    return body;
}

/// Feeds one 44-byte reply from a distinct server, wiring up the challenge first so
/// the reflection is reached at all.
void feedObservedIP(ServerList& list, uint32 serverIP, uint32 observedNet, int extraTail = 0)
{
    constexpr uint16 kPort = 4661;
    auto srv = makeServer(serverIP, kPort);
    srv->setChallenge(0x55AA1234);
    Server* s = list.addServer(std::move(srv));
    QVERIFY(s != nullptr);

    QByteArray body = withObservedIP(makeStatusBody(0x55AA1234, 1, 2), observedNet);
    if (extraTail > 0)
        body.append(QByteArray(extraTail, '\xAB'));

    list.processStatusResponse(reinterpret_cast<const uint8*>(body.constData()),
                               static_cast<uint32>(body.size()),
                               udpSenderFor(serverIP, kPort));
}

/// Clears the corroborated tier and pins the threshold/window for the duration of a
/// case. Required: the tier is sticky by design, so without this an adopted address
/// would leak into every later case in the same binary.
class ServerCorroborationGuard {
public:
    ServerCorroborationGuard(uint32 threshold, uint32 windowSecs)
        : m_savedThreshold(thePrefs.ipv4PublicServerConfirmThreshold())
        , m_savedWindow(thePrefs.ipv4PublicServerConfirmWindowSecs())
    {
        theApp.clearServerCorroboratedIP();
        thePrefs.setIpv4PublicServerConfirmThreshold(threshold);
        thePrefs.setIpv4PublicServerConfirmWindowSecs(windowSecs);
    }
    ~ServerCorroborationGuard()
    {
        theApp.clearServerCorroboratedIP();
        thePrefs.setIpv4PublicServerConfirmThreshold(m_savedThreshold);
        thePrefs.setIpv4PublicServerConfirmWindowSecs(m_savedWindow);
    }
    ServerCorroborationGuard(const ServerCorroborationGuard&) = delete;
    ServerCorroborationGuard& operator=(const ServerCorroborationGuard&) = delete;
private:
    uint32 m_savedThreshold;
    uint32 m_savedWindow;
};

// Wire form is the ed2k ID convention — first octet in the LSB. The documentation and
// TEST-NET ranges would be rejected as non-public, so the fixtures use routable ones.
constexpr uint32 kObservedA  = 0x04030281;   // 129.2.3.4
constexpr uint32 kObservedB  = 0x08070685;   // 133.6.7.8

} // namespace

void tst_ServerList::observedIPv4_adoptedAfterThresholdDistinctServers()
{
    ServerCorroborationGuard corr(2, 900);
    PublicIPGuard guard(0);            // no ED2K session; Kad is not running here
    ServerList list;

    feedObservedIP(list, 0x08080808, kObservedA);
    QCOMPARE(theApp.serverCorroboratedIP(), uint32{0});   // one server is not evidence
    QCOMPARE(theApp.publicIP(), uint32{0});

    feedObservedIP(list, 0x09090909, kObservedA);
    QCOMPARE(theApp.serverCorroboratedIP(), kObservedA);
    QCOMPARE(theApp.publicIP(), kObservedA);              // lowest tier now supplies it
}

void tst_ServerList::observedIPv4_needsExactly44Bytes()
{
    // ed2kNET extends the same packet past +40 with a tag block. Its leading bytes can
    // decode as a plausible address, so a longer tail must never be read as one.
    ServerCorroborationGuard corr(2, 900);
    PublicIPGuard guard(0);
    ServerList list;

    feedObservedIP(list, 0x08080808, kObservedA, /*extraTail*/ 60);
    feedObservedIP(list, 0x09090909, kObservedA, /*extraTail*/ 60);
    feedObservedIP(list, 0x04040404, kObservedA, /*extraTail*/ 60);

    QCOMPARE(theApp.serverCorroboratedIP(), uint32{0});
    QCOMPARE(theApp.publicIP(), uint32{0});
}

void tst_ServerList::observedIPv4_rejectsImplausibleValues()
{
    ServerCorroborationGuard corr(2, 900);
    PublicIPGuard guard(0);

    // Each value is offered by more servers than the threshold, so anything that got
    // through the ladder would be adopted.
    const uint32 cases[] = {
        0,             // "no routable IPv4 for you"
        0x00FFFFFF,    // LowID range
        0x0101A8C0,    // 192.168.1.1 — private
    };

    for (uint32 raw : cases) {
        ServerList list;
        feedObservedIP(list, 0x08080808, raw);
        feedObservedIP(list, 0x09090909, raw);
        feedObservedIP(list, 0x04040404, raw);
        QCOMPARE(theApp.serverCorroboratedIP(), uint32{0});
    }

    // A server naming its own address as ours is describing itself, not us.
    {
        ServerList selfList;
        feedObservedIP(selfList, 0x08080808, 0x08080808);
        feedObservedIP(selfList, 0x09090909, 0x09090909);
        feedObservedIP(selfList, 0x04040404, 0x04040404);
        QCOMPARE(theApp.serverCorroboratedIP(), uint32{0});
    }

    // The packet itself is still parsed — a bad reflection must not cost us the rest.
    ServerList list;
    auto srv = makeServer(0x08080808, 4661);
    srv->setChallenge(0x55AA1234);
    Server* s = list.addServer(std::move(srv));
    const QByteArray body = withObservedIP(
        makeStatusBody(0x55AA1234, 77, 88, 0, 0, 0, 0, 0, 0, 0, 0xCAFEBABE), 0x0101A8C0);
    list.processStatusResponse(reinterpret_cast<const uint8*>(body.constData()),
                               static_cast<uint32>(body.size()),
                               udpSenderFor(0x08080808, 4661));
    QCOMPARE(s->users(), 77u);
    QCOMPARE(s->serverKeyUDPRaw(), 0xCAFEBABEu);
}

void tst_ServerList::observedIPv4_shadowedByEd2kSession()
{
    ServerCorroborationGuard corr(2, 900);
    ServerList list;

    {
        PublicIPGuard guard(0);
        feedObservedIP(list, 0x08080808, kObservedA);
        feedObservedIP(list, 0x09090909, kObservedA);
        QCOMPARE(theApp.publicIP(), kObservedA);

        // A logged-in server outranks any amount of UDP agreement.
        theApp.setPublicIP(kObservedB);
        QCOMPARE(theApp.publicIP(), kObservedB);
        QCOMPARE(theApp.serverCorroboratedIP(), kObservedA);   // still held underneath

        // ...and losing the session falls back to it rather than to "unknown".
        theApp.setPublicIP(0);
        QCOMPARE(theApp.publicIP(), kObservedA);
    }
}

void tst_ServerList::observedIPv4_stickyWhenNoNewWinner()
{
    ServerCorroborationGuard corr(2, 900);
    PublicIPGuard guard(0);
    ServerList list;

    feedObservedIP(list, 0x08080808, kObservedA);
    feedObservedIP(list, 0x09090909, kObservedA);
    QCOMPARE(theApp.publicIP(), kObservedA);

    // A single server now claims something else. Stat pings re-ask a given server at
    // most every 4.5 h, so the tier must hold what N servers agreed on rather than
    // flapping to "unknown" — every flip would invalidate all server UDP keys.
    thePrefs.setIpv4PublicServerConfirmThreshold(5);
    feedObservedIP(list, 0x04040404, kObservedB);
    QCOMPARE(theApp.serverCorroboratedIP(), kObservedA);
    QCOMPARE(theApp.publicIP(), kObservedA);
}

void tst_ServerList::observedIPv4_expiresServerUDPKeys()
{
    ServerCorroborationGuard corr(2, 900);
    PublicIPGuard guard(0);
    ServerList list;
    ScopedServerList installed(&list);

    // A key stamped before we knew our address is dead once we learn a real one.
    auto* keyed = addKeyedServer(list, 0);
    feedObservedIP(list, 0x09090909, kObservedA);
    feedObservedIP(list, 0x04040404, kObservedA);

    QCOMPARE(theApp.publicIP(), kObservedA);
    QCOMPARE(keyed->serverKeyUDP(), uint32{0});   // hidden until re-issued
    // ...and re-queued for a stat ping, or it would sit unusable for UDPSERVSTATREASKTIME.
    QCOMPARE(keyed->lastPingedTime(), uint32{0});
}

// ---------------------------------------------------------------------------
// OP_SERVERLIST (0x32) parsing — ServerList::addServersFromPacket
// ---------------------------------------------------------------------------

// Build an OP_SERVERLIST body: uint8 count, then count * (ip[4] network order, port[2]).
static QByteArray makeServerListBody(const std::vector<std::pair<uint32, uint16>>& entries,
                                     int overrideCount = -1)
{
    QByteArray b;
    b.append(static_cast<char>(overrideCount >= 0 ? overrideCount
                                                   : static_cast<int>(entries.size())));
    for (const auto& [ipNet, port] : entries) {
        char tmp[6];
        pokeUInt32(tmp, ipNet);
        pokeUInt16(tmp + 4, port);
        b.append(tmp, 6);
    }
    return b;
}

void tst_ServerList::serverListPacket_addsServers()
{
    ServerList list;
    QCOMPARE(list.serverCount(), size_t{0});

    const uint32 ip1 = Address::fromString(QStringLiteral("81.82.83.84")).toNetworkUint32();
    const uint32 ip2 = Address::fromString(QStringLiteral("91.92.93.94")).toNetworkUint32();

    const QByteArray body = makeServerListBody({{ip1, 4661}, {ip2, 5000}});
    list.addServersFromPacket(reinterpret_cast<const uint8*>(body.constData()),
                              static_cast<uint32>(body.size()));

    QCOMPARE(list.serverCount(), size_t{2});
    QVERIFY(list.findByIPTcp(ip1, 4661) != nullptr);
    QVERIFY(list.findByIPTcp(ip2, 5000) != nullptr);
}

void tst_ServerList::serverListPacket_rejectsTruncated()
{
    ServerList list;

    // Header claims 3 servers, but only one entry is present (13 bytes; a valid
    // 3-server packet needs 1 + 3*6 = 19). The bounds check must reject it whole.
    const uint32 ip1 = Address::fromString(QStringLiteral("81.82.83.84")).toNetworkUint32();
    const QByteArray body = makeServerListBody({{ip1, 4661}}, /*overrideCount*/ 3);
    QCOMPARE(body.size(), qsizetype{7});

    list.addServersFromPacket(reinterpret_cast<const uint8*>(body.constData()),
                              static_cast<uint32>(body.size()));

    QCOMPARE(list.serverCount(), size_t{0});
}

// ---------------------------------------------------------------------------
// OP_SERVER_DESC_RES (0xA3) parsing — ServerList::processDescResponse
// ---------------------------------------------------------------------------

// Build a new-format (tagged) desc-res body: uint32 challenge, uint32 tagCount, tags.
static QByteArray makeTaggedDescBody(uint32 challenge, const QString& name,
                                     const QString& desc, const QString& version)
{
    SafeMemFile f;
    f.writeUInt32(challenge);
    f.writeUInt32(3);
    Tag(ST_SERVERNAME, name).writeNewEd2kTag(f, UTF8Mode::OptBOM);
    Tag(ST_DESCRIPTION, desc).writeNewEd2kTag(f, UTF8Mode::OptBOM);
    Tag(ST_VERSION, version).writeNewEd2kTag(f, UTF8Mode::OptBOM);
    return f.buffer();
}

void tst_ServerList::descResponse_taggedRefresh()
{
    ServerList list;
    auto srv = makeServer(0x08080808, 4661, QStringLiteral("Old Name"));
    srv->setDescription(QStringLiteral("Old Desc"));
    Server* s = list.addServer(std::move(srv));
    QVERIFY(s != nullptr);

    // Low 16 bits must equal INV_SERV_DESC_LEN so the new format is recognized.
    const uint32 challenge = (0x1234u << 16) | INV_SERV_DESC_LEN;
    s->setDescReqChallenge(challenge);

    const QByteArray body = makeTaggedDescBody(challenge, QStringLiteral("New Name"),
                                               QStringLiteral("New Desc"),
                                               QStringLiteral("17.15"));
    list.processDescResponse(reinterpret_cast<const uint8*>(body.constData()),
                             static_cast<uint32>(body.size()),
                             udpSenderFor(0x08080808, 4661));

    // A refresh must OVERWRITE the previously-set name/description (unlike load,
    // which keeps existing values) and clear the outstanding challenge.
    QCOMPARE(s->name(), QStringLiteral("New Name"));
    QCOMPARE(s->description(), QStringLiteral("New Desc"));
    QCOMPARE(s->version(), QStringLiteral("17.15"));
    QCOMPARE(s->descReqChallenge(), 0u);
}

void tst_ServerList::descResponse_wrongChallengeIgnored()
{
    ServerList list;
    auto srv = makeServer(0x08080808, 4661, QStringLiteral("Keep Name"));
    Server* s = list.addServer(std::move(srv));

    const uint32 expected = (0x1234u << 16) | INV_SERV_DESC_LEN;
    s->setDescReqChallenge(expected);

    // Same format marker, different challenge — an unsolicited reply we ignore.
    const uint32 wrong = (0x9999u << 16) | INV_SERV_DESC_LEN;
    const QByteArray body = makeTaggedDescBody(wrong, QStringLiteral("Evil Name"),
                                               QStringLiteral("Evil Desc"),
                                               QStringLiteral("0.1"));
    list.processDescResponse(reinterpret_cast<const uint8*>(body.constData()),
                             static_cast<uint32>(body.size()),
                             udpSenderFor(0x08080808, 4661));

    QCOMPARE(s->name(), QStringLiteral("Keep Name"));  // unchanged
    QCOMPARE(s->descReqChallenge(), expected);         // not cleared
}

void tst_ServerList::descResponse_oldFormat()
{
    ServerList list;
    auto srv = makeServer(0x08080808, 4661, QStringLiteral("Old Name"));
    Server* s = list.addServer(std::move(srv));

    // Legacy format: two length-prefixed strings, no challenge. The first two
    // bytes are the name length (11), which is not INV_SERV_DESC_LEN.
    SafeMemFile f;
    f.writeString(QStringLiteral("Legacy Name"), UTF8Mode::None);
    f.writeString(QStringLiteral("Legacy Desc"), UTF8Mode::None);
    const QByteArray body = f.buffer();

    list.processDescResponse(reinterpret_cast<const uint8*>(body.constData()),
                             static_cast<uint32>(body.size()),
                             udpSenderFor(0x08080808, 4661));

    QCOMPARE(s->name(), QStringLiteral("Legacy Name"));
    QCOMPARE(s->description(), QStringLiteral("Legacy Desc"));
}

// ---------------------------------------------------------------------------
// server.met write header (#6) — must be 0xE0 for stock-eMule interop
// ---------------------------------------------------------------------------

void tst_ServerList::serverMet_writeHeaderIsE0()
{
    TempDir tmp;
    const QString metPath = tmp.filePath(QStringLiteral("server.met"));

    ServerList list;
    list.addServer(makeServer(0x08080808, 4661, QStringLiteral("S1")));
    QVERIFY(list.saveServerMet(metPath));

    QFile f(metPath);
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QByteArray head = f.read(1);
    QCOMPARE(head.size(), qsizetype{1});
    QCOMPARE(static_cast<uint8>(head[0]), uint8{0xE0});
}

// ---------------------------------------------------------------------------
// Divergence-audit follow-ups
// ---------------------------------------------------------------------------

// #11 — a blocklisted server IP must be rejected by addServer when
// filterServerByIP is enabled (previously the IP filter was never consulted).
void tst_ServerList::addServer_ipFilterRejects()
{
    const uint32 ipNet = Address::fromString(QStringLiteral("5.6.7.8")).toNetworkUint32();
    const uint32 ipHost = ntohl(ipNet);

    IPFilter filter;
    filter.addIPRange(ipHost, ipHost, 0 /*most restrictive level*/, "audit test block");

    IPFilter* savedFilter = theApp.ipFilter;
    const bool savedPref = thePrefs.filterServerByIP();
    theApp.ipFilter = &filter;
    thePrefs.setFilterServerByIP(true);

    {
        ServerList list;
        auto* blocked = list.addServer(makeServer(ipNet, 4661));
        QVERIFY(blocked == nullptr);
        QCOMPARE(list.serverCount(), size_t{0});

        // A different, unfiltered IP still adds fine.
        const uint32 okNet = Address::fromString(QStringLiteral("9.9.9.9")).toNetworkUint32();
        QVERIFY(list.addServer(makeServer(okNet, 4661)) != nullptr);
    }

    theApp.ipFilter = savedFilter;
    thePrefs.setFilterServerByIP(savedPref);
}

// #13 — an ST_PORT tag in a server.met entry must NOT override the authoritative
// port from the entry header (defends against a crafted list redirecting the port).
void tst_ServerList::addTagFromFile_stPortDoesNotOverridePort()
{
    const uint16 headerPort = 4661;
    const uint16 bogusTagPort = 6667;

    SafeMemFile f;
    f.writeUInt32(Address::fromString(QStringLiteral("8.8.8.8")).toNetworkUint32());
    f.writeUInt16(headerPort);
    f.writeUInt32(1);  // one tag
    Tag(ST_PORT, static_cast<uint32>(bogusTagPort)).writeNewEd2kTag(f, UTF8Mode::None);

    const QByteArray body = f.buffer();
    SafeMemFile rf(body);
    Server s(rf, true);

    QCOMPARE(s.port(), headerPort);        // header wins
    QVERIFY(s.port() != bogusTagPort);     // tag ignored
}

// #18 — after a dynIP server resolves, duplicate entries sharing its address+port
// are collapsed, keeping the excepted one.
void tst_ServerList::removeDuplicatesByAddress_collapses()
{
    ServerList list;
    // Three dynIP entries with the same address/port, plus one unrelated server.
    auto* keep = list.addServer(makeDynServer(QStringLiteral("dyn.example.com"), 4661, QStringLiteral("Keep")));
    // isDuplicate blocks identical adds, so build the duplicates via distinct names
    // by adding directly (dedup is by address+port, so these would be rejected) —
    // instead insert unique-port dyn entries then rename to force the address clash.
    QVERIFY(keep != nullptr);

    // Add a distinct dynIP server that we will alias to the same address to simulate
    // duplicates arriving from different sources.
    auto extra = makeDynServer(QStringLiteral("other.example.com"), 4661, QStringLiteral("Dup"));
    Server* dup = list.addServer(std::move(extra));
    QVERIFY(dup != nullptr);
    dup->setDynIP(QStringLiteral("dyn.example.com"));   // now shares keep's address+port

    const uint32 unrelatedNet = Address::fromString(QStringLiteral("9.9.9.9")).toNetworkUint32();
    QVERIFY(list.addServer(makeServer(unrelatedNet, 4661, QStringLiteral("Other"))) != nullptr);

    QCOMPARE(list.serverCount(), size_t{3});
    list.removeDuplicatesByAddress(keep);

    QCOMPARE(list.serverCount(), size_t{2});             // the dup is gone
    QVERIFY(list.findByAddress(QStringLiteral("dyn.example.com"), 4661) == keep);
    QVERIFY(list.findByIPTcp(unrelatedNet, 4661) != nullptr);
}

// #24 — applyUserOrder reorders m_servers to a supplied ip:port sequence, with any
// unlisted server appended stably, and round-trips through server.met.
void tst_ServerList::applyUserOrder_reordersList()
{
    const uint32 a = Address::fromString(QStringLiteral("1.1.1.1")).toNetworkUint32();
    const uint32 b = Address::fromString(QStringLiteral("2.2.2.2")).toNetworkUint32();
    const uint32 c = Address::fromString(QStringLiteral("3.3.3.3")).toNetworkUint32();

    ServerList list;
    list.addServer(makeServer(a, 4661, QStringLiteral("A")));
    list.addServer(makeServer(b, 4661, QStringLiteral("B")));
    list.addServer(makeServer(c, 4661, QStringLiteral("C")));

    // Ask for C, A first; B is unlisted and must end up last.
    list.applyUserOrder({{Address::fromNetworkOrder(c), 4661},
                         {Address::fromNetworkOrder(a), 4661}});

    QCOMPARE(list.serverCount(), size_t{3});
    QCOMPARE(list.serverAt(0)->name(), QStringLiteral("C"));
    QCOMPARE(list.serverAt(1)->name(), QStringLiteral("A"));
    QCOMPARE(list.serverAt(2)->name(), QStringLiteral("B"));

    // Order persists through a save/reload (saveServerMet writes m_servers in order).
    TempDir tmp;
    const QString metPath = tmp.filePath(QStringLiteral("server.met"));
    QVERIFY(list.saveServerMet(metPath));

    ServerList reloaded;
    QVERIFY(reloaded.addServerMetToList(metPath, /*merge*/false));
    QCOMPARE(reloaded.serverAt(0)->name(), QStringLiteral("C"));
    QCOMPARE(reloaded.serverAt(1)->name(), QStringLiteral("A"));
    QCOMPARE(reloaded.serverAt(2)->name(), QStringLiteral("B"));
}

// #29 — isGoodServerIP (via addServer) accepts a private/LAN IP only when the
// filterLANIPs pref is off.
void tst_ServerList::isGoodServerIP_honorsFilterLANIPs()
{
    const uint32 lanNet = Address::fromString(QStringLiteral("192.168.1.50")).toNetworkUint32();
    const bool savedPref = thePrefs.filterLANIPs();

    thePrefs.setFilterLANIPs(true);
    {
        ServerList list;
        QVERIFY(list.addServer(makeServer(lanNet, 4661)) == nullptr);  // rejected
    }

    thePrefs.setFilterLANIPs(false);
    {
        ServerList list;
        QVERIFY(list.addServer(makeServer(lanNet, 4661)) != nullptr);  // accepted
    }

    thePrefs.setFilterLANIPs(savedPref);
}

// #30 — re-announcing a duplicate server revives it by resetting its failed count.
void tst_ServerList::addDuplicate_resetsFailedCount()
{
    ServerList list;
    Server* s = list.addServer(makeServer(0x08080808, 4661));
    QVERIFY(s != nullptr);
    s->incFailedCount();
    s->incFailedCount();
    QCOMPARE(s->failedCount(), uint32{2});

    // A duplicate add is rejected (returns nullptr) but must reset the live entry.
    QVERIFY(list.addServer(makeServer(0x08080808, 4661)) == nullptr);
    QCOMPARE(list.serverCount(), size_t{1});
    QCOMPARE(s->failedCount(), uint32{0});
}

// #33 — moveServerDown relocates a server to the bottom of the list.
void tst_ServerList::moveServerDown_toBottom()
{
    const uint32 a = Address::fromString(QStringLiteral("1.1.1.1")).toNetworkUint32();
    const uint32 b = Address::fromString(QStringLiteral("2.2.2.2")).toNetworkUint32();
    const uint32 c = Address::fromString(QStringLiteral("3.3.3.3")).toNetworkUint32();

    ServerList list;
    Server* sa = list.addServer(makeServer(a, 4661, QStringLiteral("A")));
    list.addServer(makeServer(b, 4661, QStringLiteral("B")));
    list.addServer(makeServer(c, 4661, QStringLiteral("C")));

    list.moveServerDown(sa);
    QCOMPARE(list.serverAt(0)->name(), QStringLiteral("B"));
    QCOMPARE(list.serverAt(1)->name(), QStringLiteral("C"));
    QCOMPARE(list.serverAt(2)->name(), QStringLiteral("A"));

    // Moving the already-last server is a no-op.
    list.moveServerDown(sa);
    QCOMPARE(list.serverAt(2)->name(), QStringLiteral("A"));
}

// #34 — getSuccServer does NOT wrap (returns nullptr past the tail); getServerByIP
// matches on IP regardless of port.
void tst_ServerList::getSuccServer_nonWrapping()
{
    const uint32 a = Address::fromString(QStringLiteral("1.1.1.1")).toNetworkUint32();
    const uint32 b = Address::fromString(QStringLiteral("2.2.2.2")).toNetworkUint32();

    ServerList list;
    Server* sa = list.addServer(makeServer(a, 4661, QStringLiteral("A")));
    Server* sb = list.addServer(makeServer(b, 4661, QStringLiteral("B")));

    QCOMPARE(list.getSuccServer(nullptr), sa);   // null → first
    QCOMPARE(list.getSuccServer(sa), sb);        // A → B
    QVERIFY(list.getSuccServer(sb) == nullptr);  // past the tail → nullptr (no wrap)
}

void tst_ServerList::getServerByIP_ipOnly()
{
    const uint32 a = Address::fromString(QStringLiteral("1.1.1.1")).toNetworkUint32();

    ServerList list;
    Server* sa = list.addServer(makeServer(a, 4661, QStringLiteral("A")));

    QCOMPARE(list.getServerByIP(a), sa);
    QVERIFY(list.getServerByIP(Address::fromString(QStringLiteral("2.2.2.2")).toNetworkUint32())
            == nullptr);
}

// ---------------------------------------------------------------------------
// Obfuscated stat crypt-ping — serverStats() two-branch state machine
//
// serverStats() probes a server on port+12 with a raw random challenge sent in
// the clear (the server encrypts its reply with that challenge as the RC4 base
// key). Only if that goes unanswered does it fall back, 20s later, to a plaintext
// OP_GLOBSERVSTATREQ on port+4. MFC: CServerList::ServerStats() — ServerList.cpp:273-316.
// ---------------------------------------------------------------------------

namespace {
/// Brings theApp to a "connected" state so serverStats() runs its full body:
/// installs a ServerConnect (whose send calls no-op without a UDP socket) and a
/// running Kad instance made connected via setLastContact(), and sets publicIP +
/// the crypt-layer pref. All globals are restored on scope exit. Satisfies
/// theApp.isConnected() through the Kad arm the same way
/// tst_Kademlia::appContextIsConnected_satisfiedByKadAlone does.
class CryptPingFixture {
public:
    CryptPingFixture(ServerList& list, uint32 publicIP, bool cryptEnabled)
        : m_savedSC(theApp.serverConnect)
        , m_savedSL(theApp.serverList)
        , m_savedIP(theApp.publicIP(true))
        , m_savedCrypt(thePrefs.cryptLayerSupported())
        , m_sc(list)
    {
        theApp.serverList = &list;
        theApp.serverConnect = &m_sc;
        theApp.setPublicIP(publicIP);
        thePrefs.setCryptLayerSupported(cryptEnabled);
        m_kad.start();
        m_kad.getPrefs()->setLastContact();   // Kad "connected" → theApp.isConnected()
    }
    ~CryptPingFixture()
    {
        m_kad.stop();                          // deletes Kad components, stops searches
        thePrefs.setCryptLayerSupported(m_savedCrypt);
        theApp.setPublicIP(m_savedIP);
        theApp.serverConnect = m_savedSC;
        theApp.serverList = m_savedSL;
    }
    CryptPingFixture(const CryptPingFixture&) = delete;
    CryptPingFixture& operator=(const CryptPingFixture&) = delete;
private:
    ServerConnect* m_savedSC;
    ServerList*    m_savedSL;
    uint32         m_savedIP;
    bool           m_savedCrypt;
    ServerConnect  m_sc;
    kad::Kademlia  m_kad;
};
} // namespace

void tst_ServerList::serverStats_sendsObfuscatedCryptPingFirst()
{
    ServerList list;
    CryptPingFixture fx(list, /*publicIP*/ 0x04030201u, /*crypt*/ true);
    QVERIFY(theApp.isConnected());

    auto* srv = list.addServer(makeServer(0x08080808, 5555, QStringLiteral("cryptsrv")));
    QVERIFY(srv != nullptr);
    QVERIFY(!srv->cryptPingReplyPending());
    QCOMPARE(srv->challenge(), 0u);

    const auto before = static_cast<uint32>(QDateTime::currentSecsSinceEpoch());
    list.serverStats();

    // Obfuscated branch taken: pending is armed with a non-zero challenge, and the
    // server is backdated to come due again in ~20s so the plaintext fallback fires.
    QVERIFY(srv->cryptPingReplyPending());
    QVERIFY(srv->challenge() != 0);
    const uint32 dueAt = srv->lastPingedTime() + static_cast<uint32>(UDPSERVSTATREASKTIME);
    QVERIFY(dueAt >= before + 19 && dueAt <= before + 22);
    // The free obfuscated probe must NOT bump the failure counter.
    QCOMPARE(srv->failedCount(), 0u);
}

void tst_ServerList::serverStats_cryptPingSentWithoutAKnownPublicIP()
{
    // MFC gates the obfuscated probe on already knowing our public IP. We do not: the
    // challenge is random and the reply is keyed on it, so the probe needs no address of
    // ours — and this is the one state where the reflection it carries back matters, since
    // the original eserver only sends the extended reply on the obfuscated channel.
    ServerList list;
    ServerCorroborationGuard corr(2, 900);
    CryptPingFixture fx(list, /*publicIP*/ 0, /*crypt*/ true);
    QVERIFY(theApp.isConnected());
    // The fixture needs a *connected* Kad to satisfy isConnected(), but Kad seeds an
    // address of its own — clear it, or this would not be the "nobody knows our address"
    // state that the old gate refused to probe in.
    if (auto* kadInst = kad::Kademlia::instance())
        kadInst->getPrefs()->setIPAddress(0);
    QCOMPARE(theApp.publicIP(), uint32{0});

    auto* srv = list.addServer(makeServer(0x08080808, 5555, QStringLiteral("cryptsrv")));
    QVERIFY(srv != nullptr);

    list.serverStats();

    // Obfuscated branch, not the plaintext one. Falling through to OP_GLOBSERVSTATREQ
    // would clear pending and bump the failure counter — and would never see the
    // extended reply. (Not asserted on the challenge's high half: the obfuscated one is
    // fully random and can legitimately collide with the 0x55AA marker.)
    QVERIFY(srv->cryptPingReplyPending());
    QVERIFY(srv->challenge() != 0);
    QCOMPARE(srv->failedCount(), 0u);
}

void tst_ServerList::serverStats_fallsBackToPlaintextWhenPending()
{
    ServerList list;
    CryptPingFixture fx(list, /*publicIP*/ 0x04030201u, /*crypt*/ true);
    QVERIFY(theApp.isConnected());

    auto* srv = list.addServer(makeServer(0x08080808, 5555, QStringLiteral("cryptsrv")));
    QVERIFY(srv != nullptr);
    // Simulate an obfuscated probe that was already sent and never answered: pending
    // is still set and the server is due now.
    srv->setCryptPingReplyPending(true);
    srv->setLastPingedTime(0);

    list.serverStats();

    // Plaintext fallback: pending cleared, the 0x55AA<rand16> challenge convention,
    // and the failure counter bumped (a real, countable stat request).
    QVERIFY(!srv->cryptPingReplyPending());
    QCOMPARE(srv->challenge() & 0xFFFF0000u, 0x55AA0000u);
    QCOMPARE(srv->failedCount(), 1u);
}

QTEST_MAIN(tst_ServerList)
#include "tst_ServerList.moc"
