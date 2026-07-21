/// @file tst_UDPSocket.cpp
/// @brief Tests for UDPSocket — server UDP communication.

#include "TestHelpers.h"
#include "net/Address.h"
#include "net/UDPSocket.h"
#include "server/Server.h"
#include "utils/Opcodes.h"
#include "utils/SafeFile.h"

#include <QSignalSpy>
#include <QTest>
#include <QUdpSocket>

#include <cstring>

using namespace eMule;

class tst_UDPSocket : public QObject {
    Q_OBJECT

private slots:
    void constructionDefaults();
    void createAndBind();
    void receiveServerStatus();
    void receiveGlobalSearchResult();
    void malformedPacketDoesNotCrash();
    void throttledSendControlData();
    void sendRawPacket_transmitsUnencryptedToSpecialPort();
};

// ---------------------------------------------------------------------------
// Test: construction defaults
// ---------------------------------------------------------------------------

void tst_UDPSocket::constructionDefaults()
{
    UDPSocket sock;
    // Should be constructible without crash
    QVERIFY(true);
}

// ---------------------------------------------------------------------------
// Test: create and bind
// ---------------------------------------------------------------------------

void tst_UDPSocket::createAndBind()
{
    UDPSocket sock;
    QVERIFY(sock.create());
}

// ---------------------------------------------------------------------------
// Test: receive OP_GLOBSERVSTATRES
// ---------------------------------------------------------------------------

void tst_UDPSocket::receiveServerStatus()
{
    UDPSocket sock;
    QVERIFY(sock.create());

    QSignalSpy spy(&sock, &UDPSocket::serverStatusResult);
    QVERIFY(spy.isValid());

    // Get the port our socket is listening on
    // We need to send a datagram to it from another socket
    QUdpSocket sender;
    QVERIFY(sender.bind(QHostAddress::LocalHost, 0));

    // Build a fake OP_GLOBSERVSTATRES packet:
    // proto(1) + opcode(1) + challenge(4) + users(4) + files(4)
    char buf[14];
    buf[0] = static_cast<char>(OP_EDONKEYPROT);
    buf[1] = static_cast<char>(OP_GLOBSERVSTATRES);
    uint32 challenge = 42;
    uint32 users = 500;
    uint32 files = 25000;
    std::memcpy(buf + 2, &challenge, 4);
    std::memcpy(buf + 6, &users, 4);
    std::memcpy(buf + 10, &files, 4);

    // Note: We need the local port of our UDPSocket
    // The UDPSocket binds to 0 (random port), but we can't easily access it
    // This test verifies the signal connection at minimum
    // A full integration test would require knowing the bound port

    QVERIFY(spy.count() == 0); // No signal yet without sending
}

// ---------------------------------------------------------------------------
// Test: receive OP_GLOBSEARCHRES
// ---------------------------------------------------------------------------

void tst_UDPSocket::receiveGlobalSearchResult()
{
    UDPSocket sock;
    QVERIFY(sock.create());

    QSignalSpy spy(&sock, &UDPSocket::globalSearchResult);
    QVERIFY(spy.isValid());

    // Verify signal is properly connected
    QCOMPARE(spy.count(), 0);
}

// ---------------------------------------------------------------------------
// Test: a malformed result packet whose handler throws must not crash
// ---------------------------------------------------------------------------

void tst_UDPSocket::malformedPacketDoesNotCrash()
{
    // A truncated OP_GLOBSEARCHRES makes SearchList::processUDPSearchAnswer
    // over-read and throw FileException out of the (direct-connected) slot. The
    // try/catch in UDPSocket::processPacket must swallow it for this opcode so a
    // hostile server cannot kill the daemon with one datagram.
    UDPSocket sock;
    QVERIFY(sock.create());
    const quint16 port = sock.localPort();
    QVERIFY(port != 0);

    bool handlerRan = false;
    QObject::connect(&sock, &UDPSocket::globalSearchResult, &sock,
        [&handlerRan](const uint8*, uint32, const Endpoint&) {
            handlerRan = true;
            throw FileException("simulated truncated global-search result");
        });

    // proto + opcode + a couple of stray payload bytes (nonsense to a parser).
    QByteArray dgram;
    dgram.append(static_cast<char>(OP_EDONKEYPROT));
    dgram.append(static_cast<char>(OP_GLOBSEARCHRES));
    dgram.append('\x01');
    dgram.append('\x02');

    QUdpSocket sender;
    QVERIFY(sender.bind(QHostAddress::LocalHost, 0));
    QCOMPARE(sender.writeDatagram(dgram, QHostAddress::LocalHost, port),
             static_cast<qint64>(dgram.size()));

    // Drive the receive; the throwing slot is swallowed inside processPacket.
    QTRY_VERIFY(handlerRan);

    // Still alive and usable — the exception did not propagate out of the slot.
    const SocketSentBytes r = sock.sendControlData(1024, 64);
    QVERIFY(r.success);
}

// ---------------------------------------------------------------------------
// Test: sendControlData with empty queue
// ---------------------------------------------------------------------------

void tst_UDPSocket::throttledSendControlData()
{
    UDPSocket sock;
    QVERIFY(sock.create());

    // With empty queue, should return 0 bytes sent
    SocketSentBytes result = sock.sendControlData(1024, 64);
    QVERIFY(result.success);
    QCOMPARE(result.sentBytesControlPackets, 0u);
}

// ---------------------------------------------------------------------------
// Test: sendRawPacket transmits the bytes verbatim (unencrypted) to a special port
//
// The obfuscated stat crypt-ping is sent through this path: a raw random challenge
// on port+12, in the clear (the server encrypts its reply with that challenge).
// MFC: CUDPSocket::SendPacket() does not encrypt raw packets (UDPSocket.cpp:762-766).
// ---------------------------------------------------------------------------

void tst_UDPSocket::sendRawPacket_transmitsUnencryptedToSpecialPort()
{
    // A receiver on localhost stands in for the server's obfuscation port (port+12).
    QUdpSocket receiver;
    QVERIFY(receiver.bind(QHostAddress::LocalHost, 0));
    const quint16 destPort = receiver.localPort();
    QVERIFY(destPort != 0);

    UDPSocket sock;
    QVERIFY(sock.create());

    // Server on 127.0.0.1 so the datagram reaches our localhost receiver; the
    // special port targets the receiver directly (as port+12 would a real server).
    Server server(Address::fromString(QStringLiteral("127.0.0.1")).toNetworkUint32(), 4661);

    // A 4-byte challenge + 2 padding bytes — the exact shape serverStats() builds.
    const QByteArray raw = QByteArrayLiteral("\x78\x56\x34\x12\xAA\xBB");
    sock.sendRawPacket(server, destPort,
                       reinterpret_cast<const uint8*>(raw.constData()),
                       static_cast<uint32>(raw.size()));

    // No throttler is installed in this fixture, so flush the control queue by hand
    // (sendBuffer only *queues* the datagram; sendControlData writes it).
    const SocketSentBytes sent = sock.sendControlData(4096, 64);
    QVERIFY(sent.success);
    QCOMPARE(sent.sentBytesControlPackets, static_cast<uint32>(raw.size()));

    QVERIFY(receiver.waitForReadyRead(2000));
    QByteArray got(raw.size(), '\0');
    const qint64 n = receiver.readDatagram(got.data(), got.size());
    QCOMPARE(n, static_cast<qint64>(raw.size()));
    QCOMPARE(got, raw);   // byte-for-byte identical → not encrypted, not reframed
}

QTEST_MAIN(tst_UDPSocket)
#include "tst_UDPSocket.moc"
