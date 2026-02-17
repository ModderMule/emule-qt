/// @file tst_ClientUDPSocket.cpp
/// @brief Tests for ClientUDPSocket — client-to-client UDP.

#include "TestHelpers.h"
#include "net/ClientUDPSocket.h"
#include "net/Packet.h"
#include "utils/Opcodes.h"

#include <QSignalSpy>
#include <QTest>
#include <QUdpSocket>

#include <cstring>

using namespace eMule;

class tst_ClientUDPSocket : public QObject {
    Q_OBJECT

private slots:
    void constructionDefaults();
    void createAndBind();
    void rebindToPort();
    void sendControlDataEmptyQueue();
    void sendPacketQueues();
    void signalConnections();
};

// ---------------------------------------------------------------------------
// Test: construction defaults
// ---------------------------------------------------------------------------

void tst_ClientUDPSocket::constructionDefaults()
{
    ClientUDPSocket sock;
    QCOMPARE(sock.connectedPort(), static_cast<uint16>(0));
}

// ---------------------------------------------------------------------------
// Test: create and bind
// ---------------------------------------------------------------------------

void tst_ClientUDPSocket::createAndBind()
{
    ClientUDPSocket sock;
    QVERIFY(sock.create());
    QVERIFY(sock.connectedPort() != 0);
}

// ---------------------------------------------------------------------------
// Test: rebind to specific port
// ---------------------------------------------------------------------------

void tst_ClientUDPSocket::rebindToPort()
{
    ClientUDPSocket sock;
    QVERIFY(sock.create());
    uint16 originalPort = sock.connectedPort();
    QVERIFY(originalPort != 0);

    // Rebind to a different port (let OS choose)
    QVERIFY(sock.rebind(0));
}

// ---------------------------------------------------------------------------
// Test: sendControlData with empty queue returns 0
// ---------------------------------------------------------------------------

void tst_ClientUDPSocket::sendControlDataEmptyQueue()
{
    ClientUDPSocket sock;
    QVERIFY(sock.create());

    SocketSentBytes result = sock.sendControlData(1024, 64);
    QVERIFY(result.success);
    QCOMPARE(result.sentBytesControlPackets, 0u);
}

// ---------------------------------------------------------------------------
// Test: sendPacket adds to queue
// ---------------------------------------------------------------------------

void tst_ClientUDPSocket::sendPacketQueues()
{
    ClientUDPSocket sock;
    QVERIFY(sock.create());

    auto pkt = std::make_unique<Packet>(OP_REASKFILEPING, 0, OP_EMULEPROT);
    // Send to localhost
    uint32 ip = htonl(0x7F000001);
    QVERIFY(sock.sendPacket(std::move(pkt), ip, 12345, false, nullptr, false, 0));
}

// ---------------------------------------------------------------------------
// Test: signal connections are valid
// ---------------------------------------------------------------------------

void tst_ClientUDPSocket::signalConnections()
{
    ClientUDPSocket sock;

    QSignalSpy reaskSpy(&sock, &ClientUDPSocket::reaskFilePingReceived);
    QSignalSpy kadSpy(&sock, &ClientUDPSocket::kadPacketReceived);
    QSignalSpy portTestSpy(&sock, &ClientUDPSocket::portTestReceived);

    QVERIFY(reaskSpy.isValid());
    QVERIFY(kadSpy.isValid());
    QVERIFY(portTestSpy.isValid());
}

QTEST_MAIN(tst_ClientUDPSocket)
#include "tst_ClientUDPSocket.moc"
