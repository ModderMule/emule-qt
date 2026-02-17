/// @file tst_ListenSocket.cpp
/// @brief Tests for ListenSocket + ClientReqSocket.

#include "TestHelpers.h"
#include "net/ClientReqSocket.h"
#include "net/ListenSocket.h"
#include "net/Packet.h"
#include "utils/Opcodes.h"

#include <QSignalSpy>
#include <QTcpSocket>
#include <QTest>

using namespace eMule;

class tst_ListenSocket : public QObject {
    Q_OBJECT

private slots:
    void constructionDefaults();
    void startAndStopListening();
    void acceptIncomingConnection();
    void clientReqSocketDefaults();
    void clientReqSocketTimeout();
    void tooManySockets();
    void statisticsUpdate();
};

// ---------------------------------------------------------------------------
// Test: construction defaults
// ---------------------------------------------------------------------------

void tst_ListenSocket::constructionDefaults()
{
    ListenSocket listener;
    QCOMPARE(listener.openSockets(), 0u);
    QCOMPARE(listener.connectedPort(), static_cast<uint16>(0));
    QCOMPARE(listener.peakConnections(), 0u);
}

// ---------------------------------------------------------------------------
// Test: start and stop listening
// ---------------------------------------------------------------------------

void tst_ListenSocket::startAndStopListening()
{
    ListenSocket listener;
    QVERIFY(listener.startListening(0)); // Bind to random port
    QVERIFY(listener.connectedPort() != 0 || listener.isListening());

    listener.stopListening();
    QVERIFY(!listener.isListening());
}

// ---------------------------------------------------------------------------
// Test: accept incoming connection
// ---------------------------------------------------------------------------

void tst_ListenSocket::acceptIncomingConnection()
{
    ListenSocket listener;
    QVERIFY(listener.startListening(0));

    QSignalSpy spy(&listener, &ListenSocket::newClientConnection);
    QVERIFY(spy.isValid());

    // Connect from external socket
    QTcpSocket client;
    client.connectToHost(QHostAddress::LocalHost, listener.serverPort());
    QVERIFY(client.waitForConnected(5000));

    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 3000);

    // Verify socket was added to pool
    QCOMPARE(listener.openSockets(), 1u);

    client.close();
    listener.killAllSockets();
    listener.stopListening();
}

// ---------------------------------------------------------------------------
// Test: ClientReqSocket defaults
// ---------------------------------------------------------------------------

void tst_ListenSocket::clientReqSocketDefaults()
{
    ClientReqSocket reqSocket;
    QVERIFY(reqSocket.getClient() == nullptr);
    QVERIFY(!reqSocket.isPortTestConnection());
    QVERIFY(!reqSocket.isConnected());
}

// ---------------------------------------------------------------------------
// Test: ClientReqSocket timeout check
// ---------------------------------------------------------------------------

void tst_ListenSocket::clientReqSocketTimeout()
{
    ClientReqSocket reqSocket;
    reqSocket.resetTimeOutTimer();

    // Immediately after reset, should not be timed out
    QVERIFY(!reqSocket.checkTimeOut());
}

// ---------------------------------------------------------------------------
// Test: tooManySockets rate limiting
// ---------------------------------------------------------------------------

void tst_ListenSocket::tooManySockets()
{
    ListenSocket listener;
    // With no sockets, should not be too many
    QVERIFY(!listener.tooManySockets());
}

// ---------------------------------------------------------------------------
// Test: statistics update
// ---------------------------------------------------------------------------

void tst_ListenSocket::statisticsUpdate()
{
    ListenSocket listener;
    listener.addConnection();
    QCOMPARE(listener.totalConnectionChecks(), 1u);

    listener.recalculateStats();
    QCOMPARE(listener.activeConnections(), 0u);
}

QTEST_MAIN(tst_ListenSocket)
#include "tst_ListenSocket.moc"
