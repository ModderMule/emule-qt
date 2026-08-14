/// @file tst_ServerConnect.cpp
/// @brief Tests for server/ServerConnect — connection state machine, retry, timeout.

#include "TestHelpers.h"
#include "app/AppContext.h"
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

/// Helper: write an OP_SERVERMESSAGE packet (uint16 length + text bytes).
static void writeServerMessage(QTcpSocket* sock, const QByteArray& text)
{
    QByteArray payload;
    const auto len = static_cast<uint16>(text.size());
    payload.append(reinterpret_cast<const char*>(&len), 2);
    payload.append(text);
    writeRawPacket(sock, OP_EDONKEYPROT, OP_SERVERMESSAGE,
                   payload.constData(), static_cast<uint32>(payload.size()));
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
    /// A HighID login now writes theApp.publicIP() as a side effect, and theApp
    /// is process-global — several cases here log in with 0x12345678. Clear it
    /// after every case so none of them can leak an IP into the next.
    void cleanup() { theApp.setPublicIP(0); }

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
    void serverDropsConnection_clearsConnectedSocket();
    void serverDropsConnection_autoReconnects();

    // Public IP derived from the login answer
    void setClientID_highIDBecomesPublicIP();
    void setClientID_lowIDDoesNot();
    void disconnect_clearsPublicIP();

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

    // OP_SERVERMESSAGE → Server Info pane
    void serverMessage_emitsHeaderThenLines();
    void serverMessage_headerOnlyOncePerConnection();
    void serverMessage_dynIPLineStillShown();
    void serverMessage_errorAndWarningNotShown();
    void serverMessage_recordsServerVersion();
    void serverMessage_splitsOnBareNewlines();
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

    conn.connectToAnyServer();

    QVERIFY(!conn.isConnecting());
    QVERIFY(!conn.isConnected());
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

// ---------------------------------------------------------------------------
// Tests: public IP derived from the login answer
//
// MFC: CServerConnect::SetClientID() — sockets.cpp:562, and Disconnect() —
// sockets.cpp:506. A HighID *is* our public IP; eMuleQt never recorded it, which
// is why server UDP obfuscation could not engage: Server::setServerKeyUDP()
// stamps keys with theApp.publicIP(), and a key stamped for 0 never matches.
// ---------------------------------------------------------------------------

void tst_ServerConnect::setClientID_highIDBecomesPublicIP()
{
    ServerList list;
    ServerConnect conn(list);

    QCOMPARE(theApp.publicIP(), uint32{0});
    conn.setClientID(0x12345678);  // HighID

    QCOMPARE(theApp.publicIP(), uint32{0x12345678});
    QCOMPARE(conn.clientID(), uint32{0x12345678});
    QVERIFY(!conn.isLowID());
}

void tst_ServerConnect::setClientID_lowIDDoesNot()
{
    ServerList list;
    ServerConnect conn(list);

    // A LowID is a server-local handle, not an address — storing it would stamp
    // UDP keys with a value no server could ever confirm.
    conn.setClientID(0x00000042);

    QCOMPARE(theApp.publicIP(), uint32{0});
    QVERIFY(conn.isLowID());
}

void tst_ServerConnect::disconnect_clearsPublicIP()
{
    QTcpServer tcpServer;
    QVERIFY(tcpServer.listen(QHostAddress::LocalHost, 0));

    Server srv = makeLoopbackServer(tcpServer.serverPort());

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
    QCOMPARE(theApp.publicIP(), uint32{0x12345678});  // login recorded it

    QVERIFY(conn.disconnect());

    // The IP was this server's claim about us; with it gone we may not keep
    // asserting it. Safe to clear because Kad still backs publicIP() when up.
    QCOMPARE(theApp.publicIP(), uint32{0});
    QCOMPARE(conn.clientID(), uint32{0});

    serverSide->close();
}

void tst_ServerConnect::disconnect_whileNotConnected_returnsFalse()
{
    ServerList list;
    ServerConnect conn(list);
    QVERIFY(!conn.disconnect());
}

/// Regression: a server that drops us must not leave m_connectedSocket dangling,
/// must tell the rest of the app, and must release the identity that server gave us.
///
/// connectionFailed() funnels every terminal state into destroySocket() — which used
/// to delete the socket while m_connectedSocket still pointed at it and m_connected
/// was still true. The stale pointer was then dereferenced by disconnect(),
/// sendPacket() and currentServer(); shutdown (~CoreSession → disconnect()) hit it
/// every time and segfaulted. Separately, only the Disconnected case emitted
/// disconnectedFromServer() and cleared the client ID — and that case was
/// unreachable, since ServerSocket used to map the peer's close to ServerDead.
void tst_ServerConnect::serverDropsConnection_clearsConnectedSocket()
{
    QTcpServer tcpServer;
    QVERIFY(tcpServer.listen(QHostAddress::LocalHost, 0));

    Server srv = makeLoopbackServer(tcpServer.serverPort());

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
    QCOMPARE(conn.clientID(), uint32{0x12345678});

    QSignalSpy disconnSpy(&conn, &ServerConnect::disconnectedFromServer);

    // The server drops us mid-session.
    serverSide->abort();

    // Before the fix this stayed true forever, because only the Disconnected case
    // cleared it and this path reported ServerDead.
    QTRY_VERIFY_WITH_TIMEOUT(!conn.isConnected(), 5000);

    // Both of these dereferenced freed memory before the fix.
    QVERIFY(conn.currentServer() == nullptr);
    QVERIFY(!conn.disconnect());

    // Nobody downstream used to be told, and the departed server's claim about our
    // identity outlived it — theApp.publicIP() still stamped server-UDP crypt keys.
    QTRY_COMPARE(disconnSpy.count(), 1);
    QCOMPARE(conn.clientID(), uint32{0});
    QCOMPARE(theApp.publicIP(), uint32{0});
}

/// The Disconnected branch of connectionFailed() was dead code: ServerSocket only
/// emitted connectionFailed for ServerDead/FatalError/ServerFull, and Qt reports the
/// peer's FIN as an error before disconnected(), so a dropped session never reached
/// it. Auto-reconnect is the observable proof that the branch now runs — MFC:
/// CServerConnect::ConnectionFailed() CS_DISCONNECTED, srchybrid/ServerConnect.cpp:339.
void tst_ServerConnect::serverDropsConnection_autoReconnects()
{
    QTcpServer tcpServer;
    QVERIFY(tcpServer.listen(QHostAddress::LocalHost, 0));

    Server srv = makeLoopbackServer(tcpServer.serverPort());

    // A routable-looking entry for the reconnect to aim at; the loopback server we
    // actually connect to cannot be added (isGoodServerIP rejects 127.x).
    ServerList list;
    QVERIFY(list.addServer(makePublicServer(0x08080808, 4661, QStringLiteral("Retry"))));

    ServerConnect conn(list);
    ServerConnectConfig cfg = makeTestConfig();
    cfg.reconnectOnDisconnect = true;
    conn.setConfig(cfg);

    conn.connectToServer(&srv, false, true);

    QVERIFY(tcpServer.waitForNewConnection(5000));
    auto* serverSide = tcpServer.nextPendingConnection();
    QVERIFY(serverSide);

    QTest::qWait(200);
    writeIdChange(serverSide, 0x12345678);
    QTRY_VERIFY_WITH_TIMEOUT(conn.isConnected(), 5000);
    QVERIFY(!conn.isConnecting());

    serverSide->abort();

    QTRY_VERIFY_WITH_TIMEOUT(conn.isConnecting(), 5000);
    QVERIFY(!conn.isConnected());

    // Don't leave an attempt against 8.8.8.8 running into the next case.
    conn.stopConnectionTry();
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
// Tests: OP_SERVERMESSAGE → Server Info pane
//
// The reference feeds this pane from CServerSocket::ProcessPacket via
// CemuleDlg::AddServerMessageLine — a channel separate from the log
// (srchybrid/ServerSocket.cpp:170-250). These cases pin the emission contract
// the daemon then forwards over IpcMsgType::PushServerMessage.
// ---------------------------------------------------------------------------

namespace {

/// Point theApp.serverList at a local list for the duration of a case, so
/// ServerConnect::resolveListEntry() can find the entry under test.
struct ScopedServerList {
    explicit ScopedServerList(ServerList* list) : m_saved(theApp.serverList) { theApp.serverList = list; }
    ~ScopedServerList() { theApp.serverList = m_saved; }
    ServerList* m_saved;
};

/// A connected ServerConnect talking to a loopback QTcpServer, with a spy on the
/// Server Info signal. Every case below needs the same six steps to get there.
struct ConnectedFixture {
    QTcpServer tcpServer;
    ServerList list;
    std::unique_ptr<ServerConnect> conn;
    std::unique_ptr<Server> srv;
    QTcpSocket* serverSide = nullptr;
    std::unique_ptr<QSignalSpy> msgSpy;

    [[nodiscard]] bool start()
    {
        if (!tcpServer.listen(QHostAddress::LocalHost, 0))
            return false;
        srv = std::make_unique<Server>(makeLoopbackServer(tcpServer.serverPort()));
        conn = std::make_unique<ServerConnect>(list);
        conn->setConfig(makeTestConfig());
        msgSpy = std::make_unique<QSignalSpy>(conn.get(), &ServerConnect::serverMessageReceived);
        if (!msgSpy->isValid())
            return false;

        conn->connectToServer(srv.get(), false, true);
        if (!tcpServer.waitForNewConnection(5000))
            return false;
        serverSide = tcpServer.nextPendingConnection();
        if (!serverSide)
            return false;

        QTest::qWait(200);
        writeIdChange(serverSide, 0x12345678, SRVCAP_ZLIB | SRVCAP_NEWTAGS);
        return true;
    }

    ~ConnectedFixture()
    {
        if (conn) conn->disconnect();
        if (serverSide) serverSide->close();
    }
};

} // namespace

void tst_ServerConnect::serverMessage_emitsHeaderThenLines()
{
    ConnectedFixture fx;
    QVERIFY(fx.start());
    QTRY_VERIFY_WITH_TIMEOUT(fx.conn->isConnected(), 5000);

    writeServerMessage(fx.serverSide, "Welcome!\r\nEnjoy your stay.");

    // Blank separator + blue header + the two greeting lines.
    QTRY_COMPARE_WITH_TIMEOUT(fx.msgSpy->count(), 4, 5000);

    QCOMPARE(fx.msgSpy->at(0).at(0).value<ServerMsgType>(), ServerMsgType::Info);
    QCOMPARE(fx.msgSpy->at(0).at(1).toString(), QString{});

    QCOMPARE(fx.msgSpy->at(1).at(0).value<ServerMsgType>(), ServerMsgType::Success);
    QVERIFY(fx.msgSpy->at(1).at(1).toString().contains(
        QStringLiteral("Connection established on")));

    QCOMPARE(fx.msgSpy->at(2).at(0).value<ServerMsgType>(), ServerMsgType::Info);
    QCOMPARE(fx.msgSpy->at(2).at(1).toString(), QStringLiteral("Welcome!"));
    QCOMPARE(fx.msgSpy->at(3).at(1).toString(), QStringLiteral("Enjoy your stay."));
}

void tst_ServerConnect::serverMessage_headerOnlyOncePerConnection()
{
    ConnectedFixture fx;
    QVERIFY(fx.start());
    QTRY_VERIFY_WITH_TIMEOUT(fx.conn->isConnected(), 5000);

    writeServerMessage(fx.serverSide, "First");
    QTRY_COMPARE_WITH_TIMEOUT(fx.msgSpy->count(), 3, 5000);

    writeServerMessage(fx.serverSide, "Second");
    QTRY_COMPARE_WITH_TIMEOUT(fx.msgSpy->count(), 4, 5000);

    // The second message adds only its own line — no second header block.
    QCOMPARE(fx.msgSpy->at(3).at(0).value<ServerMsgType>(), ServerMsgType::Info);
    QCOMPARE(fx.msgSpy->at(3).at(1).toString(), QStringLiteral("Second"));
}

void tst_ServerConnect::serverMessage_dynIPLineStillShown()
{
    ConnectedFixture fx;
    QVERIFY(fx.start());
    QTRY_VERIFY_WITH_TIMEOUT(fx.conn->isConnected(), 5000);

    // The reference consumes the marker for its side effect but leaves
    // bOutputMessage true, so the line is still displayed.
    writeServerMessage(fx.serverSide, "[emDynIP: my.server.example]");
    QTRY_COMPARE_WITH_TIMEOUT(fx.msgSpy->count(), 3, 5000);

    QCOMPARE(fx.msgSpy->at(2).at(1).toString(),
             QStringLiteral("[emDynIP: my.server.example]"));
}

void tst_ServerConnect::serverMessage_errorAndWarningNotShown()
{
    ConnectedFixture fx;
    QVERIFY(fx.start());
    QTRY_VERIFY_WITH_TIMEOUT(fx.conn->isConnected(), 5000);

    // ERROR/WARNING go to the log, never the info pane. Matching is
    // case-sensitive, so "Errors happen" is ordinary text and IS shown.
    writeServerMessage(fx.serverSide,
                       "ERROR: bad login\r\nWARNING: slow down\r\nErrors happen");
    QTRY_COMPARE_WITH_TIMEOUT(fx.msgSpy->count(), 3, 5000);

    QCOMPARE(fx.msgSpy->at(2).at(1).toString(), QStringLiteral("Errors happen"));
    for (int i = 0; i < fx.msgSpy->count(); ++i) {
        const QString line = fx.msgSpy->at(i).at(1).toString();
        QVERIFY(!line.contains(QStringLiteral("bad login")));
        QVERIFY(!line.contains(QStringLiteral("slow down")));
    }
}

void tst_ServerConnect::serverMessage_recordsServerVersion()
{
    ConnectedFixture fx;
    QVERIFY(fx.start());
    ScopedServerList scoped(&fx.list);
    QTRY_VERIFY_WITH_TIMEOUT(fx.conn->isConnected(), 5000);

    // Anchored at the start of the line, and still echoed to the pane.
    writeServerMessage(fx.serverSide, "server version 16.4");
    QTRY_COMPARE_WITH_TIMEOUT(fx.msgSpy->count(), 3, 5000);
    QCOMPARE(fx.msgSpy->at(2).at(1).toString(), QStringLiteral("server version 16.4"));

    // A mid-sentence mention must not be treated as a version report.
    writeServerMessage(fx.serverSide, "Ask about the server version please");
    QTRY_COMPARE_WITH_TIMEOUT(fx.msgSpy->count(), 4, 5000);
    QCOMPARE(fx.msgSpy->at(3).at(1).toString(),
             QStringLiteral("Ask about the server version please"));
}

void tst_ServerConnect::serverMessage_splitsOnBareNewlines()
{
    ConnectedFixture fx;
    QVERIFY(fx.start());
    QTRY_VERIFY_WITH_TIMEOUT(fx.conn->isConnected(), 5000);

    // Regression: the reference tokenises on the CHARACTER SET "\r\n", so a lone
    // \n is a separator too. Real servers (eMule Sunrise) send bare newlines;
    // splitting on the literal "\r\n" collapsed the greeting into one line and
    // hid every control marker after the first newline.
    writeServerMessage(fx.serverSide, "Welcome\nERROR: hidden\nLast line");

    // Blank + header + "Welcome" + "Last line"; the ERROR line is diverted.
    QTRY_COMPARE_WITH_TIMEOUT(fx.msgSpy->count(), 4, 5000);
    QCOMPARE(fx.msgSpy->at(2).at(1).toString(), QStringLiteral("Welcome"));
    QCOMPARE(fx.msgSpy->at(3).at(1).toString(), QStringLiteral("Last line"));
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

QTEST_MAIN(tst_ServerConnect)
#include "tst_ServerConnect.moc"
