/// @file tst_ListenSocket.cpp
/// @brief Tests for ListenSocket + ClientReqSocket.

#include "TestHelpers.h"
#include "app/AppContext.h"
#include "client/ClientList.h"
#include "ipfilter/IPFilter.h"
#include "net/Address.h"
#include "net/ClientReqSocket.h"
#include "net/ListenSocket.h"
#include "net/Packet.h"
#include "prefs/Preferences.h"
#include "stats/Statistics.h"
#include "utils/Opcodes.h"

#include <QSignalSpy>
#include <QTcpSocket>
#include <QTest>

using namespace eMule;

class tst_ListenSocket : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void constructionDefaults();
    void startAndStopListening();
    void acceptIncomingConnection();
    void clientReqSocketDefaults();
    void clientReqSocketTimeout();
    void tooManySockets();
    void statisticsUpdate();

    // Accept-path guards — MFC CListenSocket::OnAccept / AcceptConnectionCond
    void incomingConnection_rejectsFilteredIP();
    void incomingConnection_rejectsBannedClient();
    void incomingConnection_acceptsCleanIP();

private:
    /// Open a connection to `listener` and give it a chance to be accepted or rejected.
    /// Returns how many newClientConnection signals fired.
    static qsizetype probeConnection(ListenSocket& listener);

    std::unique_ptr<IPFilter> m_ipFilter;
    std::unique_ptr<ClientList> m_clientList;
    std::unique_ptr<Statistics> m_statistics;
};

// ---------------------------------------------------------------------------
// Fixture — the accept path now consults theApp.ipFilter / clientList / statistics
// ---------------------------------------------------------------------------

void tst_ListenSocket::init()
{
    m_ipFilter = std::make_unique<IPFilter>();
    m_clientList = std::make_unique<ClientList>();
    m_statistics = std::make_unique<Statistics>();

    theApp.ipFilter = m_ipFilter.get();
    theApp.clientList = m_clientList.get();
    theApp.statistics = m_statistics.get();
}

void tst_ListenSocket::cleanup()
{
    theApp.ipFilter = nullptr;
    theApp.clientList = nullptr;
    theApp.statistics = nullptr;

    m_statistics.reset();
    m_clientList.reset();
    m_ipFilter.reset();
}

qsizetype tst_ListenSocket::probeConnection(ListenSocket& listener)
{
    QSignalSpy spy(&listener, &ListenSocket::newClientConnection);

    QTcpSocket client;
    client.connectToHost(QHostAddress::LocalHost, listener.serverPort());
    if (!client.waitForConnected(5000))
        return -1;

    // A rejection produces no signal, so we cannot QTRY_ on a count going up. Pump the
    // event loop long enough for incomingConnection() to have run either way.
    QTest::qWait(300);
    client.close();
    return spy.count();
}

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

// ---------------------------------------------------------------------------
// Test: a filtered IP is rejected before it enters the socket pool
// ---------------------------------------------------------------------------

void tst_ListenSocket::incomingConnection_rejectsFilteredIP()
{
    // addIPRange takes host order; level 0 is below the default filter level of 100.
    m_ipFilter->addIPRange(0x7F000001, 0x7F000001, 0, "loopback under test");
    QCOMPARE(m_ipFilter->entryCount(), 1);

    const uint32 filteredBefore = m_statistics->filteredClients();

    ListenSocket listener;
    QVERIFY(listener.startListening(0));

    QCOMPARE(probeConnection(listener), qsizetype(0));          // no client was handed out
    QCOMPARE(listener.openSockets(), 0u);            // and none entered the pool
    QCOMPARE(m_statistics->filteredClients(), filteredBefore + 1);

    listener.stopListening();
}

// ---------------------------------------------------------------------------
// Test: a banned client is rejected, but does NOT count as a filtered client
// ---------------------------------------------------------------------------

void tst_ListenSocket::incomingConnection_rejectsBannedClient()
{
    m_clientList->addBannedClient(Address::fromHostOrder(0x7F000001));
    QVERIFY(m_clientList->isBannedClient(Address::fromHostOrder(0x7F000001)));

    const uint32 filteredBefore = m_statistics->filteredClients();

    ListenSocket listener;
    QVERIFY(listener.startListening(0));

    QCOMPARE(probeConnection(listener), qsizetype(0));
    QCOMPARE(listener.openSockets(), 0u);
    // MFC increments theStats.filteredclients only on the IP-filter branch, never on the
    // ban branch. Pin that so the statistic keeps meaning "blocked by the IP filter".
    QCOMPARE(m_statistics->filteredClients(), filteredBefore);

    listener.stopListening();
}

// ---------------------------------------------------------------------------
// Test: control — a clean IP is still accepted
// ---------------------------------------------------------------------------

void tst_ListenSocket::incomingConnection_acceptsCleanIP()
{
    const uint32 filteredBefore = m_statistics->filteredClients();

    ListenSocket listener;
    QVERIFY(listener.startListening(0));

    QCOMPARE(probeConnection(listener), qsizetype(1));
    QCOMPARE(listener.openSockets(), 1u);
    QCOMPARE(m_statistics->filteredClients(), filteredBefore);

    listener.killAllSockets();
    listener.stopListening();
}

QTEST_MAIN(tst_ListenSocket)
#include "tst_ListenSocket.moc"
