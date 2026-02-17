/// @file tst_EMSocket.cpp
/// @brief Tests for EMSocket — packet framing over loopback TCP.

#include "TestHelpers.h"
#include "net/EMSocket.h"
#include "net/Packet.h"
#include "utils/Opcodes.h"

#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>

#include <cstring>
#include <memory>
#include <vector>

using namespace eMule;

// ---------------------------------------------------------------------------
// Test subclass that records received packets
// ---------------------------------------------------------------------------

class TestEMSocket : public EMSocket {
    Q_OBJECT

public:
    using EMSocket::EMSocket;

    struct ReceivedPacket {
        uint8 opcode;
        uint8 prot;
        std::vector<char> data;
    };

    std::vector<ReceivedPacket> receivedPackets;
    int lastErrorCode = 0;

protected:
    bool packetReceived(Packet* packet) override
    {
        ReceivedPacket rp;
        rp.opcode = packet->opcode;
        rp.prot = packet->prot;
        if (packet->pBuffer && packet->size > 0)
            rp.data.assign(packet->pBuffer, packet->pBuffer + packet->size);
        receivedPackets.push_back(std::move(rp));
        return true;
    }

    void onError(int errorCode) override
    {
        lastErrorCode = errorCode;
    }
};

// ---------------------------------------------------------------------------
// Test class
// ---------------------------------------------------------------------------

class tst_EMSocket : public QObject {
    Q_OBJECT

private slots:
    void singlePacketFraming();
    void multiplePacketsInOneRead();
    void partialPacketReassembly();
    void wrongHeaderRejection();
    void oversizedPacketRejection();
    void downloadRateLimiting();
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
// Single packet framing
// ---------------------------------------------------------------------------

void tst_EMSocket::singlePacketFraming()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));

    TestEMSocket clientSocket;
    clientSocket.connectToHost(QHostAddress::LocalHost, server.serverPort());

    QVERIFY(server.waitForNewConnection(5000));
    auto* serverSide = server.nextPendingConnection();
    QVERIFY(serverSide != nullptr);
    QVERIFY(clientSocket.waitForConnected(5000));

    // Send a simple packet from server → client
    const char payload[] = "Hello";
    writeRawPacket(serverSide, OP_EDONKEYPROT, 0x01, payload, 5);

    // Wait for the client to receive and process
    QTRY_COMPARE_WITH_TIMEOUT(clientSocket.receivedPackets.size(), static_cast<std::size_t>(1), 3000);

    QCOMPARE(clientSocket.receivedPackets[0].opcode, static_cast<uint8>(0x01));
    QCOMPARE(clientSocket.receivedPackets[0].prot, static_cast<uint8>(OP_EDONKEYPROT));
    QCOMPARE(clientSocket.receivedPackets[0].data.size(), static_cast<std::size_t>(5));
    QVERIFY(std::memcmp(clientSocket.receivedPackets[0].data.data(), "Hello", 5) == 0);

    serverSide->close();
    clientSocket.close();
}

// ---------------------------------------------------------------------------
// Multiple packets in one read
// ---------------------------------------------------------------------------

void tst_EMSocket::multiplePacketsInOneRead()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));

    TestEMSocket clientSocket;
    clientSocket.connectToHost(QHostAddress::LocalHost, server.serverPort());

    QVERIFY(server.waitForNewConnection(5000));
    auto* serverSide = server.nextPendingConnection();
    QVERIFY(serverSide != nullptr);
    QVERIFY(clientSocket.waitForConnected(5000));

    // Send 3 packets back-to-back
    writeRawPacket(serverSide, OP_EDONKEYPROT, 0x01, "AAA", 3);
    writeRawPacket(serverSide, OP_EMULEPROT, 0x02, "BB", 2);
    writeRawPacket(serverSide, OP_EDONKEYPROT, 0x03, "C", 1);

    QTRY_COMPARE_WITH_TIMEOUT(clientSocket.receivedPackets.size(), static_cast<std::size_t>(3), 3000);

    QCOMPARE(clientSocket.receivedPackets[0].opcode, static_cast<uint8>(0x01));
    QCOMPARE(clientSocket.receivedPackets[1].opcode, static_cast<uint8>(0x02));
    QCOMPARE(clientSocket.receivedPackets[2].opcode, static_cast<uint8>(0x03));

    serverSide->close();
    clientSocket.close();
}

// ---------------------------------------------------------------------------
// Partial packet reassembly
// ---------------------------------------------------------------------------

void tst_EMSocket::partialPacketReassembly()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));

    TestEMSocket clientSocket;
    clientSocket.connectToHost(QHostAddress::LocalHost, server.serverPort());

    QVERIFY(server.waitForNewConnection(5000));
    auto* serverSide = server.nextPendingConnection();
    QVERIFY(serverSide != nullptr);
    QVERIFY(clientSocket.waitForConnected(5000));

    // Build a packet with 100 bytes payload
    std::vector<char> payload(100, 'X');
    HeaderStruct hdr;
    hdr.eDonkeyID = OP_EDONKEYPROT;
    hdr.packetLength = 100 + 1;
    hdr.command = 0x46;

    // Send header + first 30 bytes
    serverSide->write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));
    serverSide->write(payload.data(), 30);
    serverSide->flush();
    QTest::qWait(100);

    // Packet should not be complete yet
    QCOMPARE(clientSocket.receivedPackets.size(), static_cast<std::size_t>(0));

    // Send remaining 70 bytes
    serverSide->write(payload.data() + 30, 70);
    serverSide->flush();

    QTRY_COMPARE_WITH_TIMEOUT(clientSocket.receivedPackets.size(), static_cast<std::size_t>(1), 3000);
    QCOMPARE(clientSocket.receivedPackets[0].data.size(), static_cast<std::size_t>(100));
    QCOMPARE(clientSocket.receivedPackets[0].opcode, static_cast<uint8>(0x46));

    serverSide->close();
    clientSocket.close();
}

// ---------------------------------------------------------------------------
// Wrong header rejection
// ---------------------------------------------------------------------------

void tst_EMSocket::wrongHeaderRejection()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));

    TestEMSocket clientSocket;
    clientSocket.connectToHost(QHostAddress::LocalHost, server.serverPort());

    QVERIFY(server.waitForNewConnection(5000));
    auto* serverSide = server.nextPendingConnection();
    QVERIFY(serverSide != nullptr);
    QVERIFY(clientSocket.waitForConnected(5000));

    // Send a packet with invalid protocol byte
    writeRawPacket(serverSide, 0xFF, 0x01, "Bad", 3);

    QTRY_VERIFY_WITH_TIMEOUT(clientSocket.lastErrorCode != 0, 3000);
    QCOMPARE(clientSocket.lastErrorCode, kErrWrongHeader);

    serverSide->close();
    clientSocket.close();
}

// ---------------------------------------------------------------------------
// Oversized packet rejection
// ---------------------------------------------------------------------------

void tst_EMSocket::oversizedPacketRejection()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));

    TestEMSocket clientSocket;
    clientSocket.connectToHost(QHostAddress::LocalHost, server.serverPort());

    QVERIFY(server.waitForNewConnection(5000));
    auto* serverSide = server.nextPendingConnection();
    QVERIFY(serverSide != nullptr);
    QVERIFY(clientSocket.waitForConnected(5000));

    // Send a header claiming a huge payload
    HeaderStruct hdr;
    hdr.eDonkeyID = OP_EDONKEYPROT;
    hdr.packetLength = 3'000'001; // > 2MB
    hdr.command = 0x01;

    serverSide->write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));
    serverSide->flush();

    QTRY_VERIFY_WITH_TIMEOUT(clientSocket.lastErrorCode != 0, 3000);
    QCOMPARE(clientSocket.lastErrorCode, kErrTooBig);

    serverSide->close();
    clientSocket.close();
}

// ---------------------------------------------------------------------------
// Download rate limiting
// ---------------------------------------------------------------------------

void tst_EMSocket::downloadRateLimiting()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));

    TestEMSocket clientSocket;
    clientSocket.connectToHost(QHostAddress::LocalHost, server.serverPort());

    QVERIFY(server.waitForNewConnection(5000));
    auto* serverSide = server.nextPendingConnection();
    QVERIFY(serverSide != nullptr);
    QVERIFY(clientSocket.waitForConnected(5000));

    // Set download limit to 0 — should block receiving
    clientSocket.setDownloadLimit(0);

    writeRawPacket(serverSide, OP_EDONKEYPROT, 0x01, "Data", 4);
    QTest::qWait(200);

    // Should not have received the packet yet
    QCOMPARE(clientSocket.receivedPackets.size(), static_cast<std::size_t>(0));

    // Now increase the limit
    clientSocket.setDownloadLimit(1000);

    QTRY_COMPARE_WITH_TIMEOUT(clientSocket.receivedPackets.size(), static_cast<std::size_t>(1), 3000);

    serverSide->close();
    clientSocket.close();
}

QTEST_MAIN(tst_EMSocket)
#include "tst_EMSocket.moc"
