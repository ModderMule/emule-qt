/// @file tst_ServerSocket.cpp
/// @brief Tests for ServerSocket — server TCP protocol handling.

#include "TestHelpers.h"
#include "net/ServerSocket.h"
#include "net/Packet.h"
#include "server/Server.h"
#include "utils/ByteOrder.h"
#include "utils/Opcodes.h"
#include "utils/OtherFunctions.h"

#include <QSignalSpy>
#include <QTcpServer>
#include <QTest>

#include <cstring>

using namespace eMule;

class tst_ServerSocket : public QObject {
    Q_OBJECT

private slots:
    void constructionDefaults();
    void connectionStateSignal();
    void processServerMessage();
    void processIdChange();
    void processIdChangeExtended();
    void processIdChangeExtendedRejectsLowIDReport();
    void processServerStatus();
    void processReject();
    void connectTo_literalInDynIPSkipsDns();
};

/// Helper: write raw ED2K packet bytes to a socket.
static void writeRawPacket(QTcpSocket* sock, uint8 prot, uint8 opcode, const char* payload, uint32 payloadSize)
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

// ---------------------------------------------------------------------------
// Test: construction defaults
// ---------------------------------------------------------------------------

void tst_ServerSocket::constructionDefaults()
{
    ServerSocket sock;
    QCOMPARE(sock.connectionState(), ServerConnState::NotConnected);
    QVERIFY(!sock.isManualSingleConnect());
    QVERIFY(sock.currentServer() == nullptr);
}

// ---------------------------------------------------------------------------
// Test: connection state change signal
// ---------------------------------------------------------------------------

void tst_ServerSocket::connectionStateSignal()
{
    ServerSocket sock;
    QSignalSpy spy(&sock, &ServerSocket::connectionStateChanged);
    QVERIFY(spy.isValid());

    // Connect to loopback to trigger state change
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));

    Server srv(htonl(0x7F000001), server.serverPort());
    sock.connectTo(srv);

    // Wait for connection
    QVERIFY(server.waitForNewConnection(5000));
    QTRY_VERIFY_WITH_TIMEOUT(!spy.isEmpty(), 5000);

    // First signal should be Connecting
    QCOMPARE(spy.first().at(0).value<ServerConnState>(), ServerConnState::Connecting);
}

// ---------------------------------------------------------------------------
// Test: OP_SERVERMESSAGE processing
// ---------------------------------------------------------------------------

void tst_ServerSocket::processServerMessage()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));

    ServerSocket clientSocket;
    QSignalSpy msgSpy(&clientSocket, &ServerSocket::serverMessage);
    QVERIFY(msgSpy.isValid());

    Server srv(htonl(0x7F000001), server.serverPort());
    clientSocket.connectTo(srv);

    QVERIFY(server.waitForNewConnection(5000));
    auto* serverSide = server.nextPendingConnection();
    QVERIFY(serverSide != nullptr);
    QVERIFY(clientSocket.waitForConnected(5000));

    // Build OP_SERVERMESSAGE: uint16 len + message
    const char msg[] = "Welcome to test server";
    uint16 msgLen = static_cast<uint16>(std::strlen(msg));
    char payload[256];
    std::memcpy(payload, &msgLen, 2);
    std::memcpy(payload + 2, msg, msgLen);

    writeRawPacket(serverSide, OP_EDONKEYPROT, OP_SERVERMESSAGE, payload, 2 + msgLen);

    QTRY_COMPARE_WITH_TIMEOUT(msgSpy.count(), 1, 3000);
    QCOMPARE(msgSpy.first().at(0).toString(), QStringLiteral("Welcome to test server"));

    serverSide->close();
    clientSocket.close();
}

// ---------------------------------------------------------------------------
// Test: OP_IDCHANGE processing
// ---------------------------------------------------------------------------

void tst_ServerSocket::processIdChange()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));

    ServerSocket clientSocket;
    QSignalSpy loginSpy(&clientSocket, &ServerSocket::loginReceived);
    QVERIFY(loginSpy.isValid());

    Server srv(htonl(0x7F000001), server.serverPort());
    clientSocket.connectTo(srv);

    QVERIFY(server.waitForNewConnection(5000));
    auto* serverSide = server.nextPendingConnection();
    QVERIFY(serverSide != nullptr);
    QVERIFY(clientSocket.waitForConnected(5000));

    // Build OP_IDCHANGE: uint32 clientID, uint32 tcpFlags
    char payload[8];
    uint32 clientID = 12345678;
    uint32 tcpFlags = SRVCAP_ZLIB | SRVCAP_NEWTAGS | SRVCAP_UNICODE;
    std::memcpy(payload, &clientID, 4);
    std::memcpy(payload + 4, &tcpFlags, 4);

    writeRawPacket(serverSide, OP_EDONKEYPROT, OP_IDCHANGE, payload, 8);

    QTRY_COMPARE_WITH_TIMEOUT(loginSpy.count(), 1, 3000);
    QCOMPARE(loginSpy.first().at(0).toUInt(), clientID);
    QCOMPARE(loginSpy.first().at(1).toUInt(), tcpFlags);
    // Short form carries no server-reported IP; the field must stay 0 rather
    // than picking up whatever follows the packet in the buffer.
    QCOMPARE(loginSpy.first().at(2).toUInt(), uint32{0});

    QCOMPARE(clientSocket.connectionState(), ServerConnState::Connected);

    serverSide->close();
    clientSocket.close();
}

// ---------------------------------------------------------------------------
// Test: extended OP_IDCHANGE — public IP extracted, no phantom obfuscation port (#8)
//
// MFC: CServerSocket::ProcessPacket() — ServerSocket.cpp:306-315. The extended form
// is the only way to learn our public IP on a LowID connection: the server-reported
// IP sits at offset 12 and is read once the packet is >= 16 bytes. There is NO
// obfuscation-TCP-port field in IDCHANGE — that port is derived from the TCP flags,
// not read from offset 16. The port previously required size >= 20 and mis-read
// offset 16 as an obfuscation port.
// Layout: clientID(4) [serverflags(4)] [auxPort(4)] [serverReportedIP(4)]
// ---------------------------------------------------------------------------

void tst_ServerSocket::processIdChangeExtended()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));

    ServerSocket clientSocket;
    QSignalSpy loginSpy(&clientSocket, &ServerSocket::loginReceived);
    QVERIFY(loginSpy.isValid());

    Server srv(htonl(0x7F000001), server.serverPort());
    clientSocket.connectTo(srv);

    QVERIFY(server.waitForNewConnection(5000));
    auto* serverSide = server.nextPendingConnection();
    QVERIFY(serverSide != nullptr);
    QVERIFY(clientSocket.waitForConnected(5000));

    // A 16-byte extended answer is enough to carry the public IP (the port used to
    // require 20). Bytes past offset 12 are NOT an obfuscation port.
    char payload[16];
    uint32 clientID = 0x00000042;          // LowID — the case the IP field exists for
    uint32 tcpFlags = SRVCAP_ZLIB;
    uint32 auxPort  = 4661;
    uint32 reportedIP = 0x0100007F;        // 127.0.0.1 in ED2K order, a HighID value
    std::memcpy(payload, &clientID, 4);
    std::memcpy(payload + 4, &tcpFlags, 4);
    std::memcpy(payload + 8, &auxPort, 4);
    std::memcpy(payload + 12, &reportedIP, 4);

    writeRawPacket(serverSide, OP_EDONKEYPROT, OP_IDCHANGE, payload, 16);

    QTRY_COMPARE_WITH_TIMEOUT(loginSpy.count(), 1, 3000);
    QCOMPARE(loginSpy.first().at(0).toUInt(), clientID);
    QCOMPARE(loginSpy.first().at(2).toUInt(), reportedIP);

    // No obfuscation port is invented from stray bytes on a plain connection.
    QVERIFY(clientSocket.currentServer() != nullptr);
    QCOMPARE(clientSocket.currentServer()->obfuscationPortTCP(), uint16{0});

    serverSide->close();
    clientSocket.close();
}

void tst_ServerSocket::processIdChangeExtendedRejectsLowIDReport()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));

    ServerSocket clientSocket;
    QSignalSpy loginSpy(&clientSocket, &ServerSocket::loginReceived);

    Server srv(htonl(0x7F000001), server.serverPort());
    clientSocket.connectTo(srv);

    QVERIFY(server.waitForNewConnection(5000));
    auto* serverSide = server.nextPendingConnection();
    QVERIFY(serverSide != nullptr);
    QVERIFY(clientSocket.waitForConnected(5000));

    char payload[20] = {};
    uint32 clientID = 0x00000042;
    uint32 reportedIP = 0x00000063;  // a LowID here is nonsense — MFC asserts and zeroes it
    std::memcpy(payload, &clientID, 4);
    std::memcpy(payload + 12, &reportedIP, 4);

    writeRawPacket(serverSide, OP_EDONKEYPROT, OP_IDCHANGE, payload, 20);

    QTRY_COMPARE_WITH_TIMEOUT(loginSpy.count(), 1, 3000);
    // Dropped, not forwarded: the field is supposed to report a routable address,
    // and a LowID would be stored as our public IP and stamped onto UDP keys.
    QCOMPARE(loginSpy.first().at(2).toUInt(), uint32{0});

    serverSide->close();
    clientSocket.close();
}

// ---------------------------------------------------------------------------
// Test: OP_SERVERSTATUS processing
// ---------------------------------------------------------------------------

void tst_ServerSocket::processServerStatus()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));

    ServerSocket clientSocket;
    QSignalSpy statusSpy(&clientSocket, &ServerSocket::serverStatusReceived);
    QVERIFY(statusSpy.isValid());

    Server srv(htonl(0x7F000001), server.serverPort());
    clientSocket.connectTo(srv);

    QVERIFY(server.waitForNewConnection(5000));
    auto* serverSide = server.nextPendingConnection();
    QVERIFY(serverSide != nullptr);
    QVERIFY(clientSocket.waitForConnected(5000));

    // Build OP_SERVERSTATUS: uint32 users, uint32 files
    char payload[8];
    uint32 users = 1000;
    uint32 files = 50000;
    std::memcpy(payload, &users, 4);
    std::memcpy(payload + 4, &files, 4);

    writeRawPacket(serverSide, OP_EDONKEYPROT, OP_SERVERSTATUS, payload, 8);

    QTRY_COMPARE_WITH_TIMEOUT(statusSpy.count(), 1, 3000);
    QCOMPARE(statusSpy.first().at(0).toUInt(), users);
    QCOMPARE(statusSpy.first().at(1).toUInt(), files);

    serverSide->close();
    clientSocket.close();
}

// ---------------------------------------------------------------------------
// Test: OP_REJECT processing
// ---------------------------------------------------------------------------

void tst_ServerSocket::processReject()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));

    ServerSocket clientSocket;
    QSignalSpy rejectSpy(&clientSocket, &ServerSocket::rejectReceived);
    QVERIFY(rejectSpy.isValid());

    Server srv(htonl(0x7F000001), server.serverPort());
    clientSocket.connectTo(srv);

    QVERIFY(server.waitForNewConnection(5000));
    auto* serverSide = server.nextPendingConnection();
    QVERIFY(serverSide != nullptr);
    QVERIFY(clientSocket.waitForConnected(5000));

    writeRawPacket(serverSide, OP_EDONKEYPROT, OP_REJECT, nullptr, 0);

    QTRY_COMPARE_WITH_TIMEOUT(rejectSpy.count(), 1, 3000);

    serverSide->close();
    clientSocket.close();
}

// ---------------------------------------------------------------------------
// Test: a literal parked in the dynIP slot must not be resolved
// ---------------------------------------------------------------------------

void tst_ServerSocket::connectTo_literalInDynIPSkipsDns()
{
    // A legacy staticservers.dat line or an [emDynIP:] echo can leave a numeric address
    // in dynIP. Resolving it — QDnsLookup(A, "127.0.0.1") — NXDOMAINs and the server is
    // marked dead, so connectTo() must recognise the literal and dial it directly.
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));

    ServerSocket sock;
    Server srv(uint32{0}, server.serverPort());
    srv.setDynIP(QStringLiteral("127.0.0.1"));

    sock.connectTo(srv);

    QVERIFY(server.waitForNewConnection(5000));
    QVERIFY(sock.connectionState() != ServerConnState::ServerDead);
    QVERIFY(sock.currentServer() != nullptr);
    QCOMPARE(sock.currentServer()->ipAddress(),
             Address::fromString(QStringLiteral("127.0.0.1")));

    sock.close();
}

QTEST_MAIN(tst_ServerSocket)
#include "tst_ServerSocket.moc"
