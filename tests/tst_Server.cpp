/// @file tst_Server.cpp
/// @brief Tests for server/Server — construction, tag round-trips, capability flags.

#include "TestHelpers.h"
#include "server/Server.h"
#include "protocol/Tag.h"
#include "utils/SafeFile.h"
#include "utils/OtherFunctions.h"
#include "utils/Opcodes.h"

#include <QTest>

using namespace eMule;

class tst_Server : public QObject {
    Q_OBJECT

private slots:
    // Construction
    void construct_ipPort();
    void construct_fromStream();
    void construct_copy();

    // Tag deserialization
    void addTag_serverName();
    void addTag_description();
    void addTag_ping();
    void addTag_fail();
    void addTag_preference();
    void addTag_dynIP();
    void addTag_maxUsers();
    void addTag_softHardFiles();
    void addTag_lastPing();
    void addTag_version_string();
    void addTag_version_int();
    void addTag_udpFlags();
    void addTag_auxPortsList();
    void addTag_lowIDUsers();
    void addTag_udpKey();
    void addTag_obfuscationPorts();
    void addTag_port();

    // Serialization round-trip
    void writeTags_roundTrip();
    void writeTags_skipsDefaults();

    // Full stream round-trip
    void fullStreamRoundTrip();

    // Capability flags
    void caps_supportsZlib();
    void caps_supportsLargeFilesTCP();
    void caps_supportsLargeFilesUDP();
    void caps_supportsObfuscationTCP();
    void caps_supportsObfuscationUDP();
    void caps_supportsUnicode();
    void caps_supportsRelatedSearch();

    // UDP key validity
    void udpKey_valid();
    void udpKey_mismatchIP();
    void udpKey_zeroKey();

    // Edge cases
    void edge_zeroIP();
    void edge_emptyDynIP();
    void edge_address();
    void edge_lastDescPingedCount();
};

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

void tst_Server::construct_ipPort()
{
    Server srv(0x0100007F, 4661);  // 127.0.0.1 in network byte order
    QCOMPARE(srv.ip(), uint32{0x0100007F});
    QCOMPARE(srv.port(), uint16{4661});
    QCOMPARE(srv.name(), QString());
    QCOMPARE(srv.failedCount(), uint32{0});
    QCOMPARE(srv.preference(), ServerPriority::Normal);
    QVERIFY(!srv.isStaticMember());
}

void tst_Server::construct_fromStream()
{
    // Build a mini server.met entry in memory
    SafeMemFile f;
    f.writeUInt32(0xC0A80164);  // IP: 192.168.1.100
    f.writeUInt16(4662);        // port
    f.writeUInt32(2);           // 2 tags

    // Tag 1: server name
    Tag(ST_SERVERNAME, QStringLiteral("TestServer")).writeNewEd2kTag(f, UTF8Mode::OptBOM);
    // Tag 2: users
    Tag(QByteArray("users"), uint32{5000}).writeTagToFile(f);

    f.seek(0, 0);
    Server srv(f, true);

    QCOMPARE(srv.ip(), uint32{0xC0A80164});
    QCOMPARE(srv.port(), uint16{4662});
    QCOMPARE(srv.name(), QStringLiteral("TestServer"));
    QCOMPARE(srv.users(), uint32{5000});
}

void tst_Server::construct_copy()
{
    Server original(0x01020304, 4661);
    original.setName(QStringLiteral("Original"));
    original.setDescription(QStringLiteral("Desc"));
    original.setPreference(ServerPriority::High);
    original.setUsers(1000);
    original.setFiles(50000);
    original.setTCPFlags(SrvTcpFlag::LargeFiles | SrvTcpFlag::Unicode);
    original.setObfuscationPortTCP(4663);
    original.setStaticMember(true);

    Server copy(original);

    QCOMPARE(copy.ip(), original.ip());
    QCOMPARE(copy.port(), original.port());
    QCOMPARE(copy.name(), original.name());
    QCOMPARE(copy.description(), original.description());
    QCOMPARE(copy.preference(), original.preference());
    QCOMPARE(copy.users(), original.users());
    QCOMPARE(copy.files(), original.files());
    QCOMPARE(copy.tcpFlags(), original.tcpFlags());
    QCOMPARE(copy.obfuscationPortTCP(), original.obfuscationPortTCP());
    QCOMPARE(copy.isStaticMember(), original.isStaticMember());
}

// ---------------------------------------------------------------------------
// Tag deserialization tests
// ---------------------------------------------------------------------------

void tst_Server::addTag_serverName()
{
    Server srv(0, 4661);
    Tag tag(ST_SERVERNAME, QStringLiteral("MyServer"));
    srv.addTagFromFile(tag);
    QCOMPARE(srv.name(), QStringLiteral("MyServer"));

    // Second name should not overwrite
    Tag tag2(ST_SERVERNAME, QStringLiteral("Other"));
    srv.addTagFromFile(tag2);
    QCOMPARE(srv.name(), QStringLiteral("MyServer"));
}

void tst_Server::addTag_description()
{
    Server srv(0, 4661);
    Tag tag(ST_DESCRIPTION, QStringLiteral("A great server"));
    srv.addTagFromFile(tag);
    QCOMPARE(srv.description(), QStringLiteral("A great server"));
}

void tst_Server::addTag_ping()
{
    Server srv(0, 4661);
    Tag tag(ST_PING, uint32{42});
    srv.addTagFromFile(tag);
    QCOMPARE(srv.ping(), uint32{42});
}

void tst_Server::addTag_fail()
{
    Server srv(0, 4661);
    Tag tag(ST_FAIL, uint32{3});
    srv.addTagFromFile(tag);
    QCOMPARE(srv.failedCount(), uint32{3});
}

void tst_Server::addTag_preference()
{
    Server srv(0, 4661);
    Tag tag(ST_PREFERENCE, uint32{1});  // High
    srv.addTagFromFile(tag);
    QCOMPARE(srv.preference(), ServerPriority::High);
}

void tst_Server::addTag_dynIP()
{
    Server srv(0x01020304, 4661);
    Tag tag(ST_DYNIP, QStringLiteral("server.example.com"));
    srv.addTagFromFile(tag);
    QCOMPARE(srv.dynIP(), QStringLiteral("server.example.com"));
    QCOMPARE(srv.ip(), uint32{0});  // IP reset when dynIP is set
}

void tst_Server::addTag_maxUsers()
{
    Server srv(0, 4661);
    Tag tag(ST_MAXUSERS, uint32{10000});
    srv.addTagFromFile(tag);
    QCOMPARE(srv.maxUsers(), uint32{10000});
}

void tst_Server::addTag_softHardFiles()
{
    Server srv(0, 4661);
    srv.addTagFromFile(Tag(ST_SOFTFILES, uint32{200}));
    srv.addTagFromFile(Tag(ST_HARDFILES, uint32{300}));
    QCOMPARE(srv.softFiles(), uint32{200});
    QCOMPARE(srv.hardFiles(), uint32{300});
}

void tst_Server::addTag_lastPing()
{
    Server srv(0, 4661);
    Tag tag(ST_LASTPING, uint32{1700000000});
    srv.addTagFromFile(tag);
    QCOMPARE(srv.lastPingedTime(), uint32{1700000000});
}

void tst_Server::addTag_version_string()
{
    Server srv(0, 4661);
    Tag tag(ST_VERSION, QStringLiteral("17.15"));
    srv.addTagFromFile(tag);
    QCOMPARE(srv.version(), QStringLiteral("17.15"));
}

void tst_Server::addTag_version_int()
{
    Server srv(0, 4661);
    // Version tag as integer: major=17, minor=15 → 0x0011000F
    Tag tag(ST_VERSION, uint32{(17 << 16) | 15});
    srv.addTagFromFile(tag);
    QCOMPARE(srv.version(), QStringLiteral("17.15"));
}

void tst_Server::addTag_udpFlags()
{
    Server srv(0, 4661);
    Tag tag(ST_UDPFLAGS, uint32{0x0201});
    srv.addTagFromFile(tag);
    QCOMPARE(srv.udpFlags(), uint32{0x0201});
}

void tst_Server::addTag_auxPortsList()
{
    Server srv(0, 4661);
    Tag tag(ST_AUXPORTSLIST, QStringLiteral("4663,4664,4665"));
    srv.addTagFromFile(tag);
    QCOMPARE(srv.auxPortsList(), QStringLiteral("4663,4664,4665"));
}

void tst_Server::addTag_lowIDUsers()
{
    Server srv(0, 4661);
    Tag tag(ST_LOWIDUSERS, uint32{50});
    srv.addTagFromFile(tag);
    QCOMPARE(srv.lowIDUsers(), uint32{50});
}

void tst_Server::addTag_udpKey()
{
    Server srv(0, 4661);
    srv.addTagFromFile(Tag(ST_UDPKEY, uint32{0xDEADBEEF}));
    srv.addTagFromFile(Tag(ST_UDPKEYIP, uint32{0xC0A80001}));
    QCOMPARE(srv.serverKeyUDP(), uint32{0xDEADBEEF});
    QCOMPARE(srv.serverKeyUDPIP(), uint32{0xC0A80001});
}

void tst_Server::addTag_obfuscationPorts()
{
    Server srv(0, 4661);
    srv.addTagFromFile(Tag(ST_TCPPORTOBFUSCATION, uint32{4663}));
    srv.addTagFromFile(Tag(ST_UDPPORTOBFUSCATION, uint32{4664}));
    QCOMPARE(srv.obfuscationPortTCP(), uint16{4663});
    QCOMPARE(srv.obfuscationPortUDP(), uint16{4664});
}

void tst_Server::addTag_port()
{
    Server srv(0, 4661);
    srv.addTagFromFile(Tag(ST_PORT, uint32{4777}));
    QCOMPARE(srv.port(), uint16{4777});
}

// ---------------------------------------------------------------------------
// Serialization round-trip
// ---------------------------------------------------------------------------

void tst_Server::writeTags_roundTrip()
{
    Server original(0x01020304, 4661);
    original.setName(QStringLiteral("TestServer"));
    original.setDescription(QStringLiteral("A test"));
    original.setDynIP(QStringLiteral("dyn.example.com"));
    original.setPreference(ServerPriority::High);
    original.setUsers(5000);
    original.setFiles(100000);
    original.setPing(25);
    original.setLastPingedTime(1700000000);
    original.setMaxUsers(20000);
    original.setSoftFiles(200);
    original.setHardFiles(300);
    original.setVersion(QStringLiteral("17.15"));
    original.setUDPFlags(0x0201);
    original.setLowIDUsers(50);
    original.setServerKeyUDP(0xDEADBEEF);
    original.setServerKeyUDPIP(0xC0A80001);
    original.setObfuscationPortTCP(4663);
    original.setObfuscationPortUDP(4664);
    original.setAuxPortsList(QStringLiteral("4665,4666"));
    original.setFailedCount(2);

    // Write tags
    SafeMemFile f;
    const uint32 tagCount = original.writeTags(f);
    QVERIFY(tagCount > 0);

    // Read tags back into a new server
    f.seek(0, 0);
    Server restored(0x01020304, 4661);
    for (uint32 i = 0; i < tagCount; ++i) {
        Tag tag(f, true);
        restored.addTagFromFile(tag);
    }

    QCOMPARE(restored.name(), original.name());
    QCOMPARE(restored.description(), original.description());
    QCOMPARE(restored.dynIP(), original.dynIP());
    QCOMPARE(restored.preference(), original.preference());
    QCOMPARE(restored.users(), original.users());
    QCOMPARE(restored.files(), original.files());
    QCOMPARE(restored.ping(), original.ping());
    QCOMPARE(restored.lastPingedTime(), original.lastPingedTime());
    QCOMPARE(restored.maxUsers(), original.maxUsers());
    QCOMPARE(restored.softFiles(), original.softFiles());
    QCOMPARE(restored.hardFiles(), original.hardFiles());
    QCOMPARE(restored.version(), original.version());
    QCOMPARE(restored.udpFlags(), original.udpFlags());
    QCOMPARE(restored.lowIDUsers(), original.lowIDUsers());
    QCOMPARE(restored.serverKeyUDP(), original.serverKeyUDP());
    QCOMPARE(restored.serverKeyUDPIP(), original.serverKeyUDPIP());
    QCOMPARE(restored.obfuscationPortTCP(), original.obfuscationPortTCP());
    QCOMPARE(restored.obfuscationPortUDP(), original.obfuscationPortUDP());
    QCOMPARE(restored.auxPortsList(), original.auxPortsList());
    QCOMPARE(restored.failedCount(), original.failedCount());
}

void tst_Server::writeTags_skipsDefaults()
{
    // A default-constructed server should produce zero tags
    Server srv(0x01020304, 4661);
    SafeMemFile f;
    const uint32 tagCount = srv.writeTags(f);
    QCOMPARE(tagCount, uint32{0});
    QCOMPARE(f.length(), qint64{0});
}

// ---------------------------------------------------------------------------
// Full stream round-trip (server.met binary format)
// ---------------------------------------------------------------------------

void tst_Server::fullStreamRoundTrip()
{
    Server original(0xC0A80164, 4662);
    original.setName(QStringLiteral("FullTest"));
    original.setDescription(QStringLiteral("Full round-trip"));
    original.setPreference(ServerPriority::Low);
    original.setUsers(10000);
    original.setFiles(500000);
    original.setPing(15);
    original.setMaxUsers(50000);
    original.setVersion(QStringLiteral("17.15"));

    // Write as server.met format: ip, port, tagCount, tags
    SafeMemFile f;
    f.writeUInt32(original.ip());
    f.writeUInt16(original.port());

    SafeMemFile tagBuf;
    const uint32 tagCount = original.writeTags(tagBuf);
    f.writeUInt32(tagCount);

    // Copy tag data
    const QByteArray& tagData = tagBuf.buffer();
    f.write(tagData.constData(), tagData.size());

    // Read back
    f.seek(0, 0);
    Server restored(f, true);

    QCOMPARE(restored.ip(), original.ip());
    QCOMPARE(restored.port(), original.port());
    QCOMPARE(restored.name(), original.name());
    QCOMPARE(restored.description(), original.description());
    QCOMPARE(restored.preference(), original.preference());
    QCOMPARE(restored.users(), original.users());
    QCOMPARE(restored.files(), original.files());
    QCOMPARE(restored.ping(), original.ping());
    QCOMPARE(restored.maxUsers(), original.maxUsers());
    QCOMPARE(restored.version(), original.version());
}

// ---------------------------------------------------------------------------
// Capability flags
// ---------------------------------------------------------------------------

void tst_Server::caps_supportsZlib()
{
    Server srv(0, 4661);
    QVERIFY(!srv.supportsZlib());
    srv.setTCPFlags(SrvTcpFlag::Compression);
    QVERIFY(srv.supportsZlib());
}

void tst_Server::caps_supportsLargeFilesTCP()
{
    Server srv(0, 4661);
    QVERIFY(!srv.supportsLargeFilesTCP());
    srv.setTCPFlags(SrvTcpFlag::LargeFiles);
    QVERIFY(srv.supportsLargeFilesTCP());
}

void tst_Server::caps_supportsLargeFilesUDP()
{
    Server srv(0, 4661);
    QVERIFY(!srv.supportsLargeFilesUDP());
    srv.setUDPFlags(SrvUdpFlag::LargeFiles);
    QVERIFY(srv.supportsLargeFilesUDP());
}

void tst_Server::caps_supportsObfuscationTCP()
{
    Server srv(0, 4661);
    QVERIFY(!srv.supportsObfuscationTCP());

    // Need obfuscation port AND (UDP obfuscation OR TCP obfuscation flag)
    srv.setObfuscationPortTCP(4663);
    QVERIFY(!srv.supportsObfuscationTCP());  // still no flags

    srv.setUDPFlags(SrvUdpFlag::UdpObfuscation);
    QVERIFY(srv.supportsObfuscationTCP());

    // Alternative: TCP obfuscation flag
    srv.setUDPFlags(0);
    srv.setTCPFlags(SrvTcpFlag::TcpObfuscation);
    QVERIFY(srv.supportsObfuscationTCP());
}

void tst_Server::caps_supportsObfuscationUDP()
{
    Server srv(0, 4661);
    QVERIFY(!srv.supportsObfuscationUDP());
    srv.setUDPFlags(SrvUdpFlag::UdpObfuscation);
    QVERIFY(srv.supportsObfuscationUDP());
}

void tst_Server::caps_supportsUnicode()
{
    Server srv(0, 4661);
    QVERIFY(!srv.supportsUnicode());
    srv.setTCPFlags(SrvTcpFlag::Unicode);
    QVERIFY(srv.supportsUnicode());
}

void tst_Server::caps_supportsRelatedSearch()
{
    Server srv(0, 4661);
    QVERIFY(!srv.supportsRelatedSearch());
    srv.setTCPFlags(SrvTcpFlag::RelatedSearch);
    QVERIFY(srv.supportsRelatedSearch());
}

// ---------------------------------------------------------------------------
// UDP key validity
// ---------------------------------------------------------------------------

void tst_Server::udpKey_valid()
{
    Server srv(0, 4661);
    srv.setServerKeyUDP(0x12345678);
    srv.setServerKeyUDPIP(0xC0A80001);
    QVERIFY(srv.hasValidUDPKey(0xC0A80001));
}

void tst_Server::udpKey_mismatchIP()
{
    Server srv(0, 4661);
    srv.setServerKeyUDP(0x12345678);
    srv.setServerKeyUDPIP(0xC0A80001);
    QVERIFY(!srv.hasValidUDPKey(0xC0A80002));  // different IP
}

void tst_Server::udpKey_zeroKey()
{
    Server srv(0, 4661);
    srv.setServerKeyUDP(0);
    srv.setServerKeyUDPIP(0xC0A80001);
    QVERIFY(!srv.hasValidUDPKey(0xC0A80001));  // zero key = invalid
}

// ---------------------------------------------------------------------------
// Edge cases
// ---------------------------------------------------------------------------

void tst_Server::edge_zeroIP()
{
    Server srv(0, 4661);
    QCOMPARE(srv.ip(), uint32{0});
}

void tst_Server::edge_emptyDynIP()
{
    Server srv(0x01020304, 4661);
    QVERIFY(!srv.hasDynIP());
    QCOMPARE(srv.dynIP(), QString());
}

void tst_Server::edge_address()
{
    // Numeric IP
    Server srv1(0x0100007F, 4661);
    QVERIFY(!srv1.address().isEmpty());

    // DynIP
    Server srv2(0, 4661);
    srv2.setDynIP(QStringLiteral("example.com"));
    QCOMPARE(srv2.address(), QStringLiteral("example.com"));
}

void tst_Server::edge_lastDescPingedCount()
{
    Server srv(0, 4661);
    QCOMPARE(srv.lastDescPingedCount(), uint32{0});
    srv.setLastDescPingedCount(false);  // increment
    QCOMPARE(srv.lastDescPingedCount(), uint32{1});
    srv.setLastDescPingedCount(false);  // increment
    QCOMPARE(srv.lastDescPingedCount(), uint32{2});
    srv.setLastDescPingedCount(true);   // reset
    QCOMPARE(srv.lastDescPingedCount(), uint32{0});
}

QTEST_MAIN(tst_Server)
#include "tst_Server.moc"
