/// @file tst_ServerSocket.cpp
/// @brief Tests for ServerSocket — server TCP protocol handling.

#include "TestHelpers.h"
#include "net/ServerSocket.h"
#include "net/Packet.h"
#include "server/Server.h"
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
    void processServerStatus();
    void processReject();
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

    QCOMPARE(clientSocket.connectionState(), ServerConnState::Connected);

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

QTEST_MAIN(tst_ServerSocket)
#include "tst_ServerSocket.moc"
