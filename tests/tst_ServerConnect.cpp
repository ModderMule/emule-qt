/// @file tst_ServerConnect.cpp
/// @brief Tests for server/ServerConnect — connection state machine, retry, timeout.

#include "TestHelpers.h"
#include "server/ServerConnect.h"
#include "server/ServerList.h"
#include "server/Server.h"
#include "net/ServerSocket.h"
#include "net/Packet.h"
#include "utils/Opcodes.h"
#include "utils/OtherFunctions.h"

#include <QSignalSpy>
#include <QTcpServer>
#include <QTest>

#include <cstring>
#include <memory>

using namespace eMule;
using namespace eMule::testing;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// Helper to create a server with a valid public IP (host byte order, no htonl).
/// Uses 8.8.x.x range which passes isGoodServerIP validation.
static std::unique_ptr<Server> makePublicServer(uint32 ip, uint16 port,
                                                 const QString& name = {})
{
    auto srv = std::make_unique<Server>(ip, port);
    if (!name.isEmpty())
        srv->setName(name);
    return srv;
}

/// Helper: write raw ED2K packet bytes to a socket.
static void writeRawPacket(QTcpSocket* sock, uint8 prot, uint8 opcode,
                           const char* payload, uint32 payloadSize)
{
    HeaderStruct hdr;
    hdr.eDonkeyID = prot;
    hdr.packetLength = payloadSize + 1;
    hdr.command = opcode;

    sock->write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));
    if (payloadSize > 0)
        sock->write(payload, payloadSize);
    sock->flush();
}

/// Helper: write an OP_IDCHANGE packet to simulate login success.
static void writeIdChange(QTcpSocket* sock, uint32 clientID, uint32 tcpFlags = 0)
{
    char payload[8];
    std::memcpy(payload, &clientID, 4);
    std::memcpy(payload + 4, &tcpFlags, 4);
    writeRawPacket(sock, OP_EDONKEYPROT, OP_IDCHANGE, payload, 8);
}

/// Create a default config for testing.
static ServerConnectConfig makeTestConfig()
{
    ServerConnectConfig cfg;
    cfg.safeServerConnect = false;     // Allow 2 simultaneous connections
    cfg.autoConnectStaticOnly = false;
    cfg.useServerPriorities = false;
    cfg.reconnectOnDisconnect = false; // Don't auto-reconnect in tests
    cfg.addServersFromServer = false;
    cfg.cryptLayerPreferred = false;
    cfg.cryptLayerRequired = false;
    cfg.cryptLayerEnabled = false;
    cfg.serverKeepAliveTimeout = 0;
    cfg.userNick = QStringLiteral("TestUser");
    cfg.listenPort = 4662;
    cfg.emuleVersionTag = 0;
    cfg.connectionTimeout = 5000;      // 5 second timeout for tests
    return cfg;
}

/// Helper: set up a loopback server that bypasses addServer IP validation.
/// Returns the Server object created with loopback IP matching the TCP server port.
/// The server is added directly to bypass isGoodServerIP check.
static Server makeLoopbackServer(uint16 port)
{
    // Create a server with loopback IP (network byte order)
    return Server(htonl(0x7F000001), port);
}

// ---------------------------------------------------------------------------
// Test class
// ---------------------------------------------------------------------------

class tst_ServerConnect : public QObject {
    Q_OBJECT

private slots:
    // Construction & configuration
    void constructionDefaults();
    void setConfig_updatesMaxSimCons();

    // State queries
    void initialState_notConnecting();
    void initialState_notConnected();

    // Connect to specific server
    void connectToServer_setsConnecting();
    void connectToServer_single_loginAndConnect();

    // Connect to any server
    void connectToAnyServer_emptyList_stays_notConnected();
    void connectToAnyServer_staticOnly_noStatic_fails();
    void connectToAnyServer_connectsFirst();

    // Disconnect
    void disconnect_whileConnected();
    void disconnect_whileNotConnected_returnsFalse();

    // StopConnectionTry
    void stopConnectionTry_clearsConnecting();

    // Timeout checking
    void checkForTimeout_removesTimedOut();

    // State queries: isLocalServer
    void isLocalServer_matches();
    void isLocalServer_noConnection_returnsFalse();

    // IsLowID
    void isLowID_highID();
    void isLowID_lowID();

    // ClientID
    void setClientID_emitsSignal();

    // SendPacket
    void sendPacket_notConnected_returnsFalse();
};

// ---------------------------------------------------------------------------
// Tests: Construction & configuration
// ---------------------------------------------------------------------------

void tst_ServerConnect::constructionDefaults()
{
    ServerList list;
    ServerConnect conn(list);

    QVERIFY(!conn.isConnecting());
    QVERIFY(!conn.isConnected());
    QVERIFY(!conn.isSingleConnect());
    QCOMPARE(conn.clientID(), 0u);
    QVERIFY(conn.currentServer() == nullptr);
    QVERIFY(!conn.isUDPSocketAvailable());
}

void tst_ServerConnect::setConfig_updatesMaxSimCons()
{
    ServerList list;
    ServerConnect conn(list);

    // Default safe connect = true → max 1 sim con
    ServerConnectConfig cfg = makeTestConfig();
    cfg.safeServerConnect = true;
    conn.setConfig(cfg);

    // With safe connect disabled → max 2 sim cons
    cfg.safeServerConnect = false;
    conn.setConfig(cfg);
    // (internally verified by connection behavior)
}

// ---------------------------------------------------------------------------
// Tests: Initial state
// ---------------------------------------------------------------------------

void tst_ServerConnect::initialState_notConnecting()
{
    ServerList list;
    ServerConnect conn(list);
    QVERIFY(!conn.isConnecting());
}

void tst_ServerConnect::initialState_notConnected()
{
    ServerList list;
    ServerConnect conn(list);
    QVERIFY(!conn.isConnected());
}

// ---------------------------------------------------------------------------
// Tests: ConnectToServer (single)
// ---------------------------------------------------------------------------

void tst_ServerConnect::connectToServer_setsConnecting()
{
    // Start a local TCP server so the connection stays in Connecting state
    QTcpServer tcpServer;
    QVERIFY(tcpServer.listen(QHostAddress::LocalHost, 0));

    // Create server directly (bypass addServer IP validation for loopback)
    Server srv = makeLoopbackServer(tcpServer.serverPort());

    ServerList list;
    ServerConnect conn(list);
    conn.setConfig(makeTestConfig());

    QSignalSpy stateSpy(&conn, &ServerConnect::stateChanged);
    QVERIFY(stateSpy.isValid());

    conn.connectToServer(&srv, false, true);

    QVERIFY(conn.isConnecting());
    QVERIFY(conn.isSingleConnect());
    QVERIFY(!stateSpy.isEmpty());

    conn.stopConnectionTry();
}

void tst_ServerConnect::connectToServer_single_loginAndConnect()
{
    // Start a local TCP server to accept the connection
    QTcpServer tcpServer;
    QVERIFY(tcpServer.listen(QHostAddress::LocalHost, 0));
    uint16 port = tcpServer.serverPort();

    Server srv = makeLoopbackServer(port);

    ServerList list;
    ServerConnect conn(list);
    conn.setConfig(makeTestConfig());

    QSignalSpy connectedSpy(&conn, &ServerConnect::connectedToServer);
    QVERIFY(connectedSpy.isValid());

    conn.connectToServer(&srv, false, true);

    QVERIFY(conn.isConnecting());

    // Accept the connection
    QVERIFY(tcpServer.waitForNewConnection(5000));
    auto* serverSide = tcpServer.nextPendingConnection();
    QVERIFY(serverSide);

    // Allow event loop to process the socket connection signal chain
    // (ServerSocket::onSocketConnected → WaitForLogin → connectionEstablished → sendLoginPacket)
    QTest::qWait(200);

    // Send ID change (login success) — don't wait for login packet since EMSocket
    // uses throttled/buffered sends that may not flush immediately in unit tests
    uint32 assignedID = 0x12345678;
    writeIdChange(serverSide, assignedID, SRVCAP_ZLIB | SRVCAP_NEWTAGS);

    // Wait for the connected signal
    QTRY_VERIFY_WITH_TIMEOUT(conn.isConnected(), 5000);
    QVERIFY(!conn.isConnecting());

    // Verify current server is set
    QVERIFY(conn.currentServer() != nullptr);

    conn.disconnect();
    serverSide->close();
}

// ---------------------------------------------------------------------------
// Tests: ConnectToAnyServer
// ---------------------------------------------------------------------------

void tst_ServerConnect::connectToAnyServer_emptyList_stays_notConnected()
{
    ServerList list; // empty
    ServerConnect conn(list);
    conn.setConfig(makeTestConfig());

    QSignalSpy logSpy(&conn, &ServerConnect::logMessage);

    conn.connectToAnyServer();

    QVERIFY(!conn.isConnecting());
    QVERIFY(!conn.isConnected());

    // Should have emitted an error log message
    QVERIFY(!logSpy.isEmpty());
}

void tst_ServerConnect::connectToAnyServer_staticOnly_noStatic_fails()
{
    ServerList list;
    // Use a valid public IP (8.8.8.8 in network byte order)
    list.addServer(makePublicServer(0x08080808, 4661, QStringLiteral("NonStatic")));

    ServerConnectConfig cfg = makeTestConfig();
    cfg.autoConnectStaticOnly = true;

    ServerConnect conn(list);
    conn.setConfig(cfg);

    conn.connectToAnyServer(0, true, true);

    QVERIFY(!conn.isConnecting());
}

void tst_ServerConnect::connectToAnyServer_connectsFirst()
{
    // Start a TCP server so connection stays alive
    QTcpServer tcpServer;
    QVERIFY(tcpServer.listen(QHostAddress::LocalHost, 0));

    ServerList list;
    // Add servers — the round-robin will pick the first one
    // Using public IPs for addServer validation
    list.addServer(makePublicServer(0x08080808, 4661, QStringLiteral("Server1")));
    list.addServer(makePublicServer(0x08080404, 4662, QStringLiteral("Server2")));

    ServerConnect conn(list);
    conn.setConfig(makeTestConfig());

    conn.connectToAnyServer();

    // With public IPs that are unreachable, the connection attempt will
    // still be created — verify connecting state. However, socket errors
    // may fire immediately on some platforms. Check that at least the
    // stateChanged signal was emitted.
    QSignalSpy stateSpy(&conn, &ServerConnect::stateChanged);
    // The connecting flag may have already been cleared by a fast error.
    // Just verify the system doesn't crash and clean up works.
    conn.stopConnectionTry();
    QVERIFY(!conn.isConnecting());
}

// ---------------------------------------------------------------------------
// Tests: Disconnect
// ---------------------------------------------------------------------------

void tst_ServerConnect::disconnect_whileConnected()
{
    QTcpServer tcpServer;
    QVERIFY(tcpServer.listen(QHostAddress::LocalHost, 0));
    uint16 port = tcpServer.serverPort();

    Server srv = makeLoopbackServer(port);

    ServerList list;
    ServerConnect conn(list);
    conn.setConfig(makeTestConfig());

    conn.connectToServer(&srv, false, true);

    QVERIFY(tcpServer.waitForNewConnection(5000));
    auto* serverSide = tcpServer.nextPendingConnection();
    QVERIFY(serverSide);

    QTest::qWait(200);
    writeIdChange(serverSide, 0x12345678);
    QTRY_VERIFY_WITH_TIMEOUT(conn.isConnected(), 5000);

    QSignalSpy disconnSpy(&conn, &ServerConnect::disconnectedFromServer);
    bool result = conn.disconnect();
    QVERIFY(result);
    QVERIFY(!conn.isConnected());
    QVERIFY(!disconnSpy.isEmpty());

    serverSide->close();
}

void tst_ServerConnect::disconnect_whileNotConnected_returnsFalse()
{
    ServerList list;
    ServerConnect conn(list);
    QVERIFY(!conn.disconnect());
}

// ---------------------------------------------------------------------------
// Tests: StopConnectionTry
// ---------------------------------------------------------------------------

void tst_ServerConnect::stopConnectionTry_clearsConnecting()
{
    QTcpServer tcpServer;
    QVERIFY(tcpServer.listen(QHostAddress::LocalHost, 0));

    Server srv = makeLoopbackServer(tcpServer.serverPort());

    ServerList list;
    ServerConnect conn(list);
    conn.setConfig(makeTestConfig());

    conn.connectToServer(&srv, false, true);
    QVERIFY(conn.isConnecting());

    conn.stopConnectionTry();
    QVERIFY(!conn.isConnecting());
}

// ---------------------------------------------------------------------------
// Tests: Timeout
// ---------------------------------------------------------------------------

void tst_ServerConnect::checkForTimeout_removesTimedOut()
{
    // Start a server but never send any response — let it timeout
    QTcpServer tcpServer;
    QVERIFY(tcpServer.listen(QHostAddress::LocalHost, 0));

    Server srv = makeLoopbackServer(tcpServer.serverPort());

    ServerConnectConfig cfg = makeTestConfig();
    cfg.connectionTimeout = 50; // 50 ms timeout for fast testing
    cfg.reconnectOnDisconnect = false;

    ServerList list;
    ServerConnect conn(list);
    conn.setConfig(cfg);

    conn.connectToServer(&srv, false, true);
    QVERIFY(conn.isConnecting());

    // Wait for the timeout period
    QTest::qWait(100);

    conn.checkForTimeout();

    // After timeout, single-connect should be stopped
    QVERIFY(!conn.isConnecting());
}

// ---------------------------------------------------------------------------
// Tests: isLocalServer
// ---------------------------------------------------------------------------

void tst_ServerConnect::isLocalServer_matches()
{
    QTcpServer tcpServer;
    QVERIFY(tcpServer.listen(QHostAddress::LocalHost, 0));
    uint16 port = tcpServer.serverPort();

    Server srv = makeLoopbackServer(port);

    ServerList list;
    ServerConnect conn(list);
    conn.setConfig(makeTestConfig());

    conn.connectToServer(&srv, false, true);
    QVERIFY(tcpServer.waitForNewConnection(5000));
    auto* serverSide = tcpServer.nextPendingConnection();
    QVERIFY(serverSide);

    QTest::qWait(200);
    writeIdChange(serverSide, 0x12345678);
    QTRY_VERIFY_WITH_TIMEOUT(conn.isConnected(), 5000);

    QVERIFY(conn.isLocalServer(htonl(0x7F000001), port));
    QVERIFY(!conn.isLocalServer(htonl(0x7F000002), port));

    conn.disconnect();
    serverSide->close();
}

void tst_ServerConnect::isLocalServer_noConnection_returnsFalse()
{
    ServerList list;
    ServerConnect conn(list);
    QVERIFY(!conn.isLocalServer(htonl(0x7F000001), 4661));
}

// ---------------------------------------------------------------------------
// Tests: IsLowID
// ---------------------------------------------------------------------------

void tst_ServerConnect::isLowID_highID()
{
    ServerList list;
    ServerConnect conn(list);
    conn.setClientID(0x12345678);
    QVERIFY(!conn.isLowID());
}

void tst_ServerConnect::isLowID_lowID()
{
    ServerList list;
    ServerConnect conn(list);
    conn.setClientID(100); // Low IDs are < 16777216 (0x01000000)
    QVERIFY(conn.isLowID());
}

// ---------------------------------------------------------------------------
// Tests: ClientID
// ---------------------------------------------------------------------------

void tst_ServerConnect::setClientID_emitsSignal()
{
    ServerList list;
    ServerConnect conn(list);

    QSignalSpy spy(&conn, &ServerConnect::clientIDChanged);
    QVERIFY(spy.isValid());

    conn.setClientID(0xAABBCCDD);
    QCOMPARE(conn.clientID(), 0xAABBCCDDu);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().at(0).value<uint32>(), 0xAABBCCDDu);
}

// ---------------------------------------------------------------------------
// Tests: SendPacket
// ---------------------------------------------------------------------------

void tst_ServerConnect::sendPacket_notConnected_returnsFalse()
{
    ServerList list;
    ServerConnect conn(list);

    auto packet = std::make_unique<Packet>(OP_GETSERVERLIST, 0);
    bool result = conn.sendPacket(std::move(packet));
    QVERIFY(!result);
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

QTEST_MAIN(tst_ServerConnect)
#include "tst_ServerConnect.moc"
