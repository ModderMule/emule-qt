/// @file tst_ClientUDPSocket.cpp
/// @brief Tests for ClientUDPSocket — client-to-client UDP.

#include "TestHelpers.h"
#include "app/AppContext.h"
#include "net/ClientUDPSocket.h"
#include "net/Packet.h"
#include "stats/Statistics.h"
#include "utils/ByteOrder.h"
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
    void receivesReservedProt_dispatchesInsteadOfDropping_data();
    void receivesReservedProt_dispatchesInsteadOfDropping();
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

// ---------------------------------------------------------------------------
// Test: a datagram with a reserved protocol header (OP_UDPRESERVEDPROT1 0xA3 /
// OP_UDPRESERVEDPROT2 0xB2) is dispatched to the reserved-prot stub rather than
// being silently dropped.
//
// Both bytes are obfuscation-transparent (isProtocolHeader), so they arrive in
// the clear and used to fall through every case in onReadyRead() with no log and
// no stat. The stub accounts the payload via Statistics::addDownDataOverheadOther,
// which the old drop path never touched — so the overhead-packet counter is a
// clean discriminator that the new dispatch branch ran.
// ---------------------------------------------------------------------------

void tst_ClientUDPSocket::receivesReservedProt_dispatchesInsteadOfDropping_data()
{
    QTest::addColumn<int>("protoByte");
    QTest::newRow("prot1 (0xA3)") << static_cast<int>(OP_UDPRESERVEDPROT1);
    QTest::newRow("prot2 (0xB2)") << static_cast<int>(OP_UDPRESERVEDPROT2);
}

void tst_ClientUDPSocket::receivesReservedProt_dispatchesInsteadOfDropping()
{
    QFETCH(int, protoByte);

    Statistics stats;
    theApp.statistics = &stats;

    ClientUDPSocket sock;
    QVERIFY(sock.create());
    const uint16 port = sock.connectedPort();
    QVERIFY(port != 0);

    QUdpSocket sender;
    QVERIFY(sender.bind(QHostAddress::LocalHost, 0));

    // [proto][opcode=0x99][6 payload bytes] → the stub sees size = len - 2 = 6.
    QByteArray dgram;
    dgram.append(static_cast<char>(protoByte));
    dgram.append(static_cast<char>(0x99));
    dgram.append("payld!", 6);
    const uint64 expectedSize = 6;

    QCOMPARE(stats.downDataOverheadOtherPackets(), static_cast<uint64>(0));
    QCOMPARE(sender.writeDatagram(dgram, QHostAddress::LocalHost, port),
             static_cast<qint64>(dgram.size()));

    // Drive the receive; the stub increments the overhead counters (drop would not).
    QTRY_COMPARE(stats.downDataOverheadOtherPackets(), static_cast<uint64>(1));
    const uint64 gotBytes = stats.downDataOverheadOther();

    theApp.statistics = nullptr; // detach before the object leaves scope
    QCOMPARE(gotBytes, expectedSize);
}

QTEST_MAIN(tst_ClientUDPSocket)
#include "tst_ClientUDPSocket.moc"
