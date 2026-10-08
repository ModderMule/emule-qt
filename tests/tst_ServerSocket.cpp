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
    void socketError_classification_data();
    void socketError_classification();
    void refusedConnect_reportedAsANetworkErrorStillCounts();
    void failure_namesPhaseAndReason_data();
    void failure_namesPhaseAndReason();
    void socketError_reportsOnceThroughBothEntryPoints_data();
    void socketError_reportsOnceThroughBothEntryPoints();
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

// ---------------------------------------------------------------------------
// Test: only a failure of the server itself reads as "dead"
// ---------------------------------------------------------------------------

void tst_ServerSocket::socketError_classification_data()
{
    QTest::addColumn<ServerConnState>("current");
    QTest::addColumn<QAbstractSocket::SocketError>("error");
    QTest::addColumn<ServerConnState>("expected");

    using S = ServerConnState;
    using E = QAbstractSocket;
    QTest::newRow("refused") << S::Connecting << E::ConnectionRefusedError << S::ServerDead;
    QTest::newRow("timed out") << S::Connecting << E::SocketTimeoutError << S::ServerDead;
    QTest::newRow("closed while connecting") << S::Connecting << E::RemoteHostClosedError << S::ServerDead;
    QTest::newRow("closed at login") << S::WaitForLogin << E::RemoteHostClosedError << S::ServerFull;
    // The outage cases: none of them may count against the server.
    QTest::newRow("network down") << S::Connecting << E::NetworkError << S::FatalError;
    QTest::newRow("network down at login") << S::WaitForLogin << E::NetworkError << S::FatalError;
    QTest::newRow("bind refused") << S::Connecting << E::SocketAccessError << S::FatalError;
    QTest::newRow("address gone") << S::Connecting << E::SocketAddressNotAvailableError << S::FatalError;
    QTest::newRow("proxy down") << S::Connecting << E::ProxyConnectionRefusedError << S::FatalError;
    QTest::newRow("resolver") << S::Connecting << E::HostNotFoundError << S::Error;
    QTest::newRow("established") << S::Connected << E::NetworkError << S::Disconnected;
}

void tst_ServerSocket::socketError_classification()
{
    QFETCH(ServerConnState, current);
    QFETCH(QAbstractSocket::SocketError, error);
    QFETCH(ServerConnState, expected);
    QCOMPARE(ServerSocket::stateForSocketError(current, error), expected);
}

// Seen live on Darwin: a refused dial came back as NetworkError, text "Connection
// refused" — and was filed as our own network trouble.
void tst_ServerSocket::refusedConnect_reportedAsANetworkErrorStillCounts()
{
    using E = QAbstractSocket;
    const auto refined = [](E::SocketError error, const char* text, bool tcp) {
        return ServerSocket::refinedSocketError(error, QString::fromLatin1(text), tcp);
    };
    QCOMPARE(refined(E::NetworkError, "Connection refused", false), E::ConnectionRefusedError);
    QCOMPARE(ServerSocket::stateForSocketError(
                 ServerConnState::Connecting,
                 refined(E::NetworkError, "Connection refused", false)),
             ServerConnState::ServerDead);

    // Real trouble on our side stays what it is, and so does anything after the
    // TCP connect or under another code.
    QCOMPARE(refined(E::NetworkError, "No route to host", false), E::NetworkError);
    QCOMPARE(refined(E::NetworkError, "Network is unreachable", false), E::NetworkError);
    QCOMPARE(refined(E::NetworkError, "Connection refused", true), E::NetworkError);
    QCOMPARE(refined(E::SocketTimeoutError, "Connection refused", false), E::SocketTimeoutError);
}

void tst_ServerSocket::failure_namesPhaseAndReason_data()
{
    QTest::addColumn<ServerConnState>("current");
    QTest::addColumn<bool>("tcpConnected");
    QTest::addColumn<int>("error");          // kNoError = no socket error
    QTest::addColumn<QString>("phase");
    QTest::addColumn<QString>("reason");

    using S = ServerConnState;
    using E = QAbstractSocket;
    constexpr int kNoError = 1000;   // -1 is UnknownSocketError
    const auto row = [](const char* name, S state, bool tcp, int error,
                        const char* phase, const char* reason) {
        QTest::newRow(name) << state << tcp << error
                            << QString::fromLatin1(phase) << QString::fromLatin1(reason);
    };
    row("refused", S::Connecting, false, E::ConnectionRefusedError, "connect", "connection-refused");
    row("timed out", S::Connecting, false, E::SocketTimeoutError, "connect", "timeout-unreachable");
    row("closed in obfuscation", S::Connecting, true, E::RemoteHostClosedError,
        "handshake", "protocol-rejection");
    row("closed at login", S::WaitForLogin, true, E::RemoteHostClosedError,
        "handshake", "protocol-rejection");
    row("garbage at login", S::WaitForLogin, true, kNoError, "handshake", "protocol-rejection");
    row("bind refused", S::Connecting, false, E::SocketAccessError,
        "socket-setup", "local-bind-interface");
    row("network down", S::Connecting, false, E::NetworkError,
        "socket-setup", "local-bind-interface");
    row("network down at login", S::WaitForLogin, true, E::NetworkError,
        "handshake", "local-bind-interface");
    row("resolver", S::Connecting, false, E::HostNotFoundError, "resolve", "dns-resolution");
    row("unknown", S::Connecting, false, E::UnknownSocketError, "connect", "transport-other");
    row("lost session", S::Connected, true, E::RemoteHostClosedError,
        "established", "established-disconnect");
}

void tst_ServerSocket::failure_namesPhaseAndReason()
{
    QFETCH(ServerConnState, current);
    QFETCH(bool, tcpConnected);
    QFETCH(int, error);
    QFETCH(QString, phase);
    QFETCH(QString, reason);

    const std::optional<QAbstractSocket::SocketError> socketError =
        error == 1000 ? std::nullopt
                  : std::optional(static_cast<QAbstractSocket::SocketError>(error));
    const ServerFailure failure = ServerSocket::failureFor(current, tcpConnected, socketError);
    QCOMPARE(QString(failure.phaseName()), phase);
    QCOMPARE(QString(failure.reasonName()), reason);
}

void tst_ServerSocket::socketError_reportsOnceThroughBothEntryPoints_data()
{
    QTest::addColumn<QAbstractSocket::SocketError>("error");
    QTest::addColumn<ServerConnState>("expected");
    QTest::newRow("network down") << QAbstractSocket::NetworkError << ServerConnState::FatalError;
    QTest::newRow("refused") << QAbstractSocket::ConnectionRefusedError << ServerConnState::ServerDead;
}

void tst_ServerSocket::socketError_reportsOnceThroughBothEntryPoints()
{
    QFETCH(QAbstractSocket::SocketError, error);
    QFETCH(ServerConnState, expected);

    // A listener that never accepts keeps the socket in Connecting.
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));
    server.pauseAccepting();

    ServerSocket sock;
    Server srv(uint32{0}, server.serverPort());
    srv.setDynIP(QStringLiteral("127.0.0.1"));
    sock.connectTo(srv);
    QCOMPARE(sock.connectionState(), ServerConnState::Connecting);

    QSignalSpy failed(&sock, &ServerSocket::connectionFailed);
    emit sock.errorOccurred(error);   // reaches EMSocket's slot, then ServerSocket's

    QCOMPARE(failed.count(), 1);
    QCOMPARE(failed.first().first().value<ServerConnState>(), expected);
    QCOMPARE(sock.connectionState(), expected);
    sock.abort();
}

QTEST_MAIN(tst_ServerSocket)
#include "tst_ServerSocket.moc"

