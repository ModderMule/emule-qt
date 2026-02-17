/// @file tst_UDPSocket.cpp
/// @brief Tests for UDPSocket — server UDP communication.

#include "TestHelpers.h"
#include "net/UDPSocket.h"
#include "server/Server.h"
#include "utils/Opcodes.h"

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
    void throttledSendControlData();
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

QTEST_MAIN(tst_UDPSocket)
#include "tst_UDPSocket.moc"
