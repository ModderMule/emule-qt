#include "pch.h"
/// @file tst_CoreSessionHandlers.cpp
/// @brief Tests for the protocol handlers CoreSession installs on the shared sockets:
///        the OP_DIRECTCALLBACKREQ receiver and the public-IPv6 change hook.
///
/// Both are static members rather than lambdas precisely so they are reachable here — a
/// CoreSession cannot be stood up in a unit test (its constructor binds real sockets and
/// starts Kad, its destructor tears down twelve subsystems and joins three threads).
/// The direct-callback guard ladder needs a Kademlia that is running *and* firewalled,
/// which KadFixture provides without any network.

#include "TestFixtures.h"
#include "TestHelpers.h"

#include "app/AppContext.h"
#include "app/CoreSession.h"
#include "client/ClientList.h"
#include "client/UpDownClient.h"
#include "ipfilter/IPFilter.h"
#include "kademlia/Kademlia.h"
#include "net/Address.h"
#include "net/ClientReqSocket.h"
#include "net/ClientUDPSocket.h"
#include "prefs/Preferences.h"
#include "utils/Opcodes.h"
#include "utils/OtherFunctions.h"
#include "utils/SafeFile.h"

#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QUdpSocket>

#include <cstring>
#include <vector>

using namespace eMule;
using namespace eMule::testing;

// ===========================================================================
// Helpers
// ===========================================================================

namespace {

/// TEST-NET-2. Passes isGoodIP() without lab mode — the classic ed2k IPv4 rules only
/// reject 0.x, 224+ and (when filterLANIPs is on) LAN — while being an address no
/// packet will ever actually reach.
const Address kSender = Address::fromString(QStringLiteral("198.51.100.7"));
constexpr uint16 kSenderUdpPort = 9000;
constexpr uint16 kTcpPort = 4670;
constexpr uint8 kHashByte = 0x7A;

/// The 19-byte OP_DIRECTCALLBACKREQ body:
/// [uint16 LE tcpPort][16-byte userHash][uint8 connectOptions].
QByteArray makeDirectCallbackPayload(uint16 tcpPort, uint8 hashByte, uint8 connectOptions)
{
    SafeMemFile data;
    data.writeUInt16(tcpPort);
    uint8 hash[16];
    std::memset(hash, hashByte, sizeof(hash));
    data.writeHash16(hash);
    data.writeUInt8(connectOptions);
    return data.buffer();
}

void feedDirectCallback(const Endpoint& from, const QByteArray& payload)
{
    CoreSession::handleDirectCallbackRequest(
        from, reinterpret_cast<const uint8*>(payload.constData()),
        static_cast<uint32>(payload.size()));
}

} // namespace

// ===========================================================================
// Test class
// ===========================================================================

class tst_CoreSessionHandlers : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    // -- OP_DIRECTCALLBACKREQ receive: guard ladder --
    void directCallbackReq_noClientList_isIgnored();
    void directCallbackReq_noKadInstance_isIgnored();
    void directCallbackReq_kadNotStarted_isIgnored();
    void directCallbackReq_kadNotFirewalled_isIgnored();
    void directCallbackReq_shortPayload_isIgnored();
    void directCallbackReq_badSenderIP_isIgnored_data();
    void directCallbackReq_badSenderIP_isIgnored();
    void directCallbackReq_bannedSender_isIgnored();
    void directCallbackReq_filteredSender_isIgnored();

    // -- OP_DIRECTCALLBACKREQ receive: accepted --
    void directCallbackReq_unknownSender_createsClient();
    void directCallbackReq_knownSenderByUdpEndpoint_updatesInPlace();
    void directCallbackReq_knownSenderByAddressPort_updatesInPlace();
    void directCallbackReq_ipv6Sender_existingClient_setsUserIPv6();
    void directCallbackReq_ipv6Sender_newClient_setsUserIPv6();
    void directCallbackReq_deliveredThroughClientUDPSocket();

    // -- Public-IPv6 change hook --
    void markPeersForIPChange_marksOnlyConnectedIPv6Peers();
    void markPeersForIPChange_nullAddress_marksNothing();
    void markPeersForIPChange_probedUnreachable_marksNothing();
    void publicIPv6Change_firesInstalledHook();
    void flushAfterMark_reachesTheWire();

private:
    TempDir* m_tmpDir = nullptr;
    ClientList* m_clientList = nullptr;
};

// ===========================================================================
// Per-test setup — deliberately init()/cleanup() rather than initTestCase():
// the cases differ in Kad mode and in lab mode, and a fixture-wide Kad would
// decide the guard outcome for all of them.
// ===========================================================================

void tst_CoreSessionHandlers::init()
{
    m_tmpDir = new TempDir();
    thePrefs.load(m_tmpDir->filePath(QStringLiteral("prefs.yaml")));
    thePrefs.setConfigDir(m_tmpDir->path());
    thePrefs.setPort(4662);

    m_clientList = new ClientList();
    theApp.clientList = m_clientList;
}

void tst_CoreSessionHandlers::cleanup()
{
    // The handler news UpDownClients and hands them to ClientList, which does not own
    // them (~ClientList() is defaulted and deleteAll() only clears the container).
    std::vector<UpDownClient*> owned;
    m_clientList->forEachClient([&owned](UpDownClient* c) { owned.push_back(c); });
    m_clientList->deleteAll();
    for (auto* c : owned) {
        c->setSocket(nullptr);
        delete c;
    }

    theApp.clientList = nullptr;
    theApp.ipFilter = nullptr;
    theApp.statistics = nullptr;
    delete m_clientList;
    m_clientList = nullptr;

    QCoreApplication::processEvents();   // drain anything tryToConnect() started

    delete m_tmpDir;
    m_tmpDir = nullptr;
}

// ===========================================================================
// Guard ladder — every one of these must leave the client list untouched
// ===========================================================================

void tst_CoreSessionHandlers::directCallbackReq_noClientList_isIgnored()
{
    KadFixture fx;
    ClientList* saved = theApp.clientList;
    theApp.clientList = nullptr;

    feedDirectCallback(Endpoint(kSender, kSenderUdpPort),
                       makeDirectCallbackPayload(kTcpPort, kHashByte, 0x03));

    theApp.clientList = saved;           // restore so cleanup() still works
    QCOMPARE(m_clientList->clientCount(), 0);
}

void tst_CoreSessionHandlers::directCallbackReq_noKadInstance_isIgnored()
{
    // No KadFixture at all: Kademlia binds its singleton in the constructor, so with
    // none alive instance() is null and the handler stops at the first Kad check.
    QVERIFY(kad::Kademlia::instance() == nullptr);

    feedDirectCallback(Endpoint(kSender, kSenderUdpPort),
                       makeDirectCallbackPayload(kTcpPort, kHashByte, 0x03));

    QCOMPARE(m_clientList->clientCount(), 0);
}

void tst_CoreSessionHandlers::directCallbackReq_kadNotStarted_isIgnored()
{
    KadFixture fx{KadMode::Stopped};
    QVERIFY(kad::Kademlia::instance() != nullptr);
    QVERIFY(!kad::Kademlia::instance()->isRunning());

    feedDirectCallback(Endpoint(kSender, kSenderUdpPort),
                       makeDirectCallbackPayload(kTcpPort, kHashByte, 0x03));

    QCOMPARE(m_clientList->clientCount(), 0);
}

void tst_CoreSessionHandlers::directCallbackReq_kadNotFirewalled_isIgnored()
{
    // A direct callback is a request to dial *out* on the sender's behalf, and we only
    // honour it while we are the firewalled party. An open node ignores it.
    KadFixture fx{KadMode::Open};
    QVERIFY(kad::Kademlia::instance()->isRunning());
    QVERIFY(!kad::Kademlia::instance()->isFirewalled());

    feedDirectCallback(Endpoint(kSender, kSenderUdpPort),
                       makeDirectCallbackPayload(kTcpPort, kHashByte, 0x03));

    QCOMPARE(m_clientList->clientCount(), 0);
}

void tst_CoreSessionHandlers::directCallbackReq_shortPayload_isIgnored()
{
    KadFixture fx;

    QByteArray shortBody = makeDirectCallbackPayload(kTcpPort, kHashByte, 0x03);
    shortBody.chop(1);                                   // 18 bytes
    QCOMPARE(shortBody.size(), 18);
    feedDirectCallback(Endpoint(kSender, kSenderUdpPort), shortBody);
    QCOMPARE(m_clientList->clientCount(), 0);

    // Positive control: one more byte and the very same sender is accepted, so the
    // assertion above is about the length check and not about the rest of the ladder.
    feedDirectCallback(Endpoint(kSender, kSenderUdpPort),
                       makeDirectCallbackPayload(kTcpPort, kHashByte, 0x03));
    QCOMPARE(m_clientList->clientCount(), 1);
}

void tst_CoreSessionHandlers::directCallbackReq_badSenderIP_isIgnored_data()
{
    QTest::addColumn<QString>("sender");
    QTest::addColumn<bool>("labMode");
    QTest::addColumn<bool>("accepted");

    QTest::newRow("null address") << QStringLiteral("0.0.0.0") << false << false;
    QTest::newRow("multicast") << QStringLiteral("224.0.0.1") << false << false;
    // Loopback is rejected only because filterLANIPs is on...
    QTest::newRow("loopback, filtering LAN") << QStringLiteral("127.0.0.1") << false << false;
    // ...so the same address is fine on an interop rig, which is what lab mode means.
    QTest::newRow("loopback, lab mode") << QStringLiteral("127.0.0.1") << true << true;
}

void tst_CoreSessionHandlers::directCallbackReq_badSenderIP_isIgnored()
{
    QFETCH(QString, sender);
    QFETCH(bool, labMode);
    QFETCH(bool, accepted);

    KadFixture fx;
    LabModeGuard lab{labMode};

    feedDirectCallback(Endpoint(Address::fromString(sender), kSenderUdpPort),
                       makeDirectCallbackPayload(kTcpPort, kHashByte, 0x03));

    QCOMPARE(m_clientList->clientCount(), accepted ? 1 : 0);
}

void tst_CoreSessionHandlers::directCallbackReq_bannedSender_isIgnored()
{
    KadFixture fx;
    m_clientList->addBannedClient(kSender);
    QVERIFY(m_clientList->isBannedClient(kSender));

    feedDirectCallback(Endpoint(kSender, kSenderUdpPort),
                       makeDirectCallbackPayload(kTcpPort, kHashByte, 0x03));

    QCOMPARE(m_clientList->clientCount(), 0);
}

void tst_CoreSessionHandlers::directCallbackReq_filteredSender_isIgnored()
{
    // Reachable only by calling the handler directly: ClientUDPSocket::onReadyRead drops
    // filtered and banned senders before it ever emits directCallbackReceived, so the
    // in-handler check is a second line of defence for any other caller.
    KadFixture fx;
    IPFilter filter;
    const uint32 hostOrder = kSender.toUint32();
    filter.addIPRange(hostOrder, hostOrder, 0, "test-block");
    filter.sortAndMerge();
    theApp.ipFilter = &filter;
    QVERIFY(filter.isFiltered(kSender));

    feedDirectCallback(Endpoint(kSender, kSenderUdpPort),
                       makeDirectCallbackPayload(kTcpPort, kHashByte, 0x03));

    QCOMPARE(m_clientList->clientCount(), 0);
    theApp.ipFilter = nullptr;           // before `filter` leaves scope
}

// ===========================================================================
// Accepted requests
// ===========================================================================

void tst_CoreSessionHandlers::directCallbackReq_unknownSender_createsClient()
{
    KadFixture fx;

    feedDirectCallback(Endpoint(kSender, kSenderUdpPort),
                       makeDirectCallbackPayload(kTcpPort, kHashByte, 0x03));

    QCOMPARE(m_clientList->clientCount(), 1);
    auto* client = m_clientList->findByAddress(kSender, kTcpPort);
    QVERIFY(client != nullptr);
    QCOMPARE(client->userPort(), kTcpPort);

    uint8 expectedHash[16];
    std::memset(expectedHash, kHashByte, sizeof(expectedHash));
    QVERIFY(md4equ(client->userHash(), expectedHash));

    // Both address slots, not just one: the sender used to be passed in the ctor's
    // *serverIP* slot with userId = 0, which left m_connectAddress unset entirely and
    // made the dial-back target 0.0.0.0.
    QCOMPARE(client->userAddress(), kSender);
    QCOMPARE(client->connectAddress(), kSender);

    // 0x03 = supported | requested, and required stays clear.
    QVERIFY(client->supportsCryptLayer());
    QVERIFY(client->requestsCryptLayer());
    QVERIFY(!client->requiresCryptLayer());

    // No dial is attempted: the client is built with userId 0, so it reads as LowID with
    // an IPv4 connect address, direct-UDP-callback is off (see the next test) and there is
    // no serverConnect and no buddy — tryToConnect() falls through every path.
    QCOMPARE(client->connectingState(), ConnectingState::None);
}

void tst_CoreSessionHandlers::directCallbackReq_knownSenderByUdpEndpoint_updatesInPlace()
{
    KadFixture fx;

    auto* existing = new UpDownClient();
    existing->setUserAddress(kSender);
    existing->setUDPPort(kSenderUdpPort);          // what findByEndpoint_UDP matches
    existing->setUserPort(1111);                   // stale — the request must refresh it
    existing->setKadPort(4672);
    uint8 hash[16];
    std::memset(hash, kHashByte, sizeof(hash));
    existing->setUserHash(hash);
    existing->setConnectOptions(0x08, true, true); // peer currently offers direct callback
    QVERIFY(existing->supportsDirectUDPCallback());
    // Already dialling, so the handler's trailing tryToConnect() returns early and this
    // test stays about the parsing rather than about connection setup.
    existing->setConnectingState(ConnectingState::DirectTCP);
    m_clientList->addClient(existing);

    feedDirectCallback(Endpoint(kSender, kSenderUdpPort),
                       makeDirectCallbackPayload(kTcpPort, kHashByte, 0x0F));

    QCOMPARE(m_clientList->clientCount(), 1);      // matched, not duplicated
    QCOMPARE(existing->userPort(), kTcpPort);
    QCOMPARE(existing->connectAddress(), kSender);
    QVERIFY(existing->supportsCryptLayer());       // the options byte really was parsed
    QVERIFY(existing->requiresCryptLayer());

    // The one place bit 3 is observable: setConnectOptions(opts, true, false) refuses to
    // honour an inbound direct-callback offer, and a freshly created client has no Kad
    // port so supportsDirectUDPCallback() would be false there for another reason.
    QVERIFY2(!existing->supportsDirectUDPCallback(),
             "bit 3 must not be honoured on an inbound request");
}

void tst_CoreSessionHandlers::directCallbackReq_knownSenderByAddressPort_updatesInPlace()
{
    KadFixture fx;

    auto* existing = new UpDownClient();
    existing->setUserAddress(kSender);
    existing->setUserPort(kTcpPort);               // findByAddress hits on this...
    existing->setUDPPort(1);                       // ...while findByEndpoint_UDP misses
    existing->setConnectingState(ConnectingState::DirectTCP);
    m_clientList->addClient(existing);

    feedDirectCallback(Endpoint(kSender, kSenderUdpPort),
                       makeDirectCallbackPayload(kTcpPort, kHashByte, 0x01));

    QCOMPARE(m_clientList->clientCount(), 1);
    QCOMPARE(existing->connectAddress(), kSender);
    QVERIFY(existing->supportsCryptLayer());
}

void tst_CoreSessionHandlers::directCallbackReq_ipv6Sender_existingClient_setsUserIPv6()
{
    // A ULA sender is accepted only in lab mode, which makes the dependency explicit.
    LabModeGuard lab{true};
    KadFixture fx;
    const Address v6Sender = Address::fromString(QStringLiteral("fd00::1"));
    QVERIFY(isGoodIP(v6Sender));

    auto* existing = new UpDownClient();
    existing->setUserAddress(v6Sender);
    existing->setUDPPort(kSenderUdpPort);
    existing->setConnectingState(ConnectingState::DirectTCP);   // no dial from here
    m_clientList->addClient(existing);

    feedDirectCallback(Endpoint(v6Sender, kSenderUdpPort),
                       makeDirectCallbackPayload(kTcpPort, kHashByte, 0x03));

    QCOMPARE(m_clientList->clientCount(), 1);
    QCOMPARE(existing->userIPv6(), v6Sender);
    QVERIFY(existing->openIPv6());
    QCOMPARE(existing->connectAddress(), v6Sender);
}

void tst_CoreSessionHandlers::directCallbackReq_ipv6Sender_newClient_setsUserIPv6()
{
    LabModeGuard lab{true};
    KadFixture fx;

    // Unlike the IPv4 case this one really does dial: a new client with an IPv6 connect
    // address takes the direct-TCP path. Point it at our own listener so the connect
    // completes locally instead of going out to the network.
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHostIPv6, 0));
    const uint16 tcpPort = server.serverPort();
    const Address v6Sender = Address::fromString(QStringLiteral("::1"));

    feedDirectCallback(Endpoint(v6Sender, kSenderUdpPort),
                       makeDirectCallbackPayload(tcpPort, kHashByte, 0x03));

    QCOMPARE(m_clientList->clientCount(), 1);
    auto* client = m_clientList->findByAddress(v6Sender, tcpPort);
    QVERIFY(client != nullptr);
    QCOMPARE(client->userIPv6(), v6Sender);
    QVERIFY(client->openIPv6());
    QCOMPARE(client->connectingState(), ConnectingState::DirectTCP);

    QVERIFY(server.waitForNewConnection(2000));
    QTcpSocket* peer = server.nextPendingConnection();
    QVERIFY(peer != nullptr);
    peer->close();
    QCoreApplication::processEvents();
}

void tst_CoreSessionHandlers::directCallbackReq_deliveredThroughClientUDPSocket()
{
    // The wiring case: a real datagram through a real ClientUDPSocket, connected exactly
    // as CoreSession::initClientUDP() connects it. OP_EMULEPROT takes the plaintext branch
    // of onReadyRead, which slices off the 2-byte header before emitting.
    //
    // A QSignalSpy cannot stand in here — directCallbackReceived carries a const uint8*
    // into onReadyRead's local buffer, valid only for the duration of the emission.
    LabModeGuard lab{true};              // the datagram arrives from 127.0.0.1
    KadFixture fx;

    ClientUDPSocket udp;
    QVERIFY(udp.rebind(0));
    connect(&udp, &ClientUDPSocket::directCallbackReceived,
            this, &CoreSession::handleDirectCallbackRequest);

    QUdpSocket sender;
    QVERIFY(sender.bind(QHostAddress::LocalHost, 0));

    QByteArray datagram;
    datagram.append(static_cast<char>(OP_EMULEPROT));
    datagram.append(static_cast<char>(OP_DIRECTCALLBACKREQ));
    datagram.append(makeDirectCallbackPayload(kTcpPort, kHashByte, 0x03));
    QCOMPARE(sender.writeDatagram(datagram, QHostAddress::LocalHost, udp.connectedPort()),
             static_cast<qint64>(datagram.size()));

    QTRY_COMPARE(m_clientList->clientCount(), 1);
    auto* client = m_clientList->findByAddress(Address::fromString(QStringLiteral("127.0.0.1")),
                                               kTcpPort);
    QVERIFY(client != nullptr);
    QCOMPARE(client->userPort(), kTcpPort);
}

// ===========================================================================
// Public-IPv6 change hook — CoreSession::markPeersForIPChange
// ===========================================================================

void tst_CoreSessionHandlers::markPeersForIPChange_marksOnlyConnectedIPv6Peers()
{
    IPv6AdvertiseGuard guard;

    // (a) capable and connected — the only one that should be marked.
    QTcpServer serverA;
    auto* capableConnected = new UpDownClient();
    QTcpSocket* peerA = wireLoopbackSocket(serverA, *capableConnected);
    QVERIFY(peerA != nullptr);
    feedIPv6CapableHello(*capableConnected, 0xA1);

    // (b) capable but no socket at all.
    auto* capableNoSocket = new UpDownClient();
    feedIPv6CapableHello(*capableNoSocket, 0xB2);

    // (c) connected but never said it understands IPv6.
    QTcpServer serverC;
    auto* connectedNoIPv6 = new UpDownClient();
    QTcpSocket* peerC = wireLoopbackSocket(serverC, *connectedNoIPv6);
    QVERIFY(peerC != nullptr);

    // (d) capable, with a socket that was never dialled.
    auto* capableUndialled = new UpDownClient();
    feedIPv6CapableHello(*capableUndialled, 0xD4);
    auto* undialled = new ClientReqSocket();
    capableUndialled->setSocket(undialled);
    QVERIFY(!undialled->isConnected());

    for (auto* c : {capableConnected, capableNoSocket, connectedNoIPv6, capableUndialled})
        m_clientList->addClient(c, /*skipDupTest*/ true);

    CoreSession::markPeersForIPChange(IPv6AdvertiseGuard::testPublicIPv6());

    QVERIFY(capableConnected->sendIPPending());
    QVERIFY2(!capableNoSocket->sendIPPending(), "a peer with no socket cannot be told");
    QVERIFY2(!connectedNoIPv6->sendIPPending(), "a peer that cannot use IPv6 is skipped");
    QVERIFY2(!capableUndialled->sendIPPending(), "a half-open socket is not connected");

    peerA->close();
    peerC->close();
    QCoreApplication::processEvents();
}

void tst_CoreSessionHandlers::markPeersForIPChange_nullAddress_marksNothing()
{
    IPv6AdvertiseGuard guard;

    QTcpServer server;
    auto* client = new UpDownClient();
    QTcpSocket* peer = wireLoopbackSocket(server, *client);
    QVERIFY(peer != nullptr);
    feedIPv6CapableHello(*client);
    m_clientList->addClient(client);

    CoreSession::markPeersForIPChange(Address{});
    QVERIFY(!client->sendIPPending());

    peer->close();
    QCoreApplication::processEvents();
}

void tst_CoreSessionHandlers::markPeersForIPChange_probedUnreachable_marksNothing()
{
    IPv6AdvertiseGuard guard;
    guard.setProbedUnreachable();

    QTcpServer server;
    auto* client = new UpDownClient();
    QTcpSocket* peer = wireLoopbackSocket(server, *client);
    QVERIFY(peer != nullptr);
    feedIPv6CapableHello(*client);
    m_clientList->addClient(client);

    CoreSession::markPeersForIPChange(IPv6AdvertiseGuard::testPublicIPv6());
    QVERIFY2(!client->sendIPPending(),
             "an address a server probed and could not reach must not be queued for peers");

    // Positive control on the same client, once the verdict turns good.
    guard.setProbedReachable();
    CoreSession::markPeersForIPChange(IPv6AdvertiseGuard::testPublicIPv6());
    QVERIFY(client->sendIPPending());

    peer->close();
    QCoreApplication::processEvents();
}

void tst_CoreSessionHandlers::publicIPv6Change_firesInstalledHook()
{
    // The plumbing initLocalIPv6() sets up: a tier setter changes the effective address,
    // AppContext::noteEffectiveIPv6Change() calls the hook, the hook marks the peers.
    IPv6AdvertiseGuard guard{Address{}};
    theApp.onPublicIPv6Changed = &CoreSession::markPeersForIPChange;

    QTcpServer server;
    auto* client = new UpDownClient();
    QTcpSocket* peer = wireLoopbackSocket(server, *client);
    QVERIFY(peer != nullptr);
    feedIPv6CapableHello(*client);
    m_clientList->addClient(client);
    QVERIFY(!client->sendIPPending());

    const Address v6 = IPv6AdvertiseGuard::testPublicIPv6();
    theApp.setLocalIPv6Addresses({v6});
    theApp.setPublicIPv6Local(v6);

    QVERIFY(client->sendIPPending());

    peer->close();
    QCoreApplication::processEvents();
    // The guard's destructor clears theApp.onPublicIPv6Changed.
}

void tst_CoreSessionHandlers::flushAfterMark_reachesTheWire()
{
    // End to end for the send path: our address changes, the hook marks the peer, and the
    // next walk of that client puts OP_CHANGE_CLIENT_IP on the socket.
    IPv6AdvertiseGuard guard{Address{}};
    theApp.onPublicIPv6Changed = &CoreSession::markPeersForIPChange;

    QTcpServer server;
    auto* client = new UpDownClient();
    QTcpSocket* peer = wireLoopbackSocket(server, *client);
    QVERIFY(peer != nullptr);
    feedIPv6CapableHello(*client);
    m_clientList->addClient(client);

    const Address v6 = IPv6AdvertiseGuard::testPublicIPv6();
    theApp.setLocalIPv6Addresses({v6});
    theApp.setPublicIPv6Local(v6);
    QVERIFY(client->sendIPPending());

    client->flushPendingIPChange();

    QDeadlineTimer deadline(2000);
    while (peer->bytesAvailable() < 22 && !deadline.hasExpired()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        peer->waitForReadyRead(10);
    }
    const QByteArray raw = peer->readAll();
    QCOMPARE(raw.size(), 22);
    QCOMPARE(static_cast<uint8>(raw[0]), static_cast<uint8>(OP_EDONKEYPROT));
    QCOMPARE(static_cast<uint8>(raw[5]), static_cast<uint8>(OP_CHANGE_CLIENT_IP));
    QCOMPARE(raw.mid(6, 16),
             QByteArray(reinterpret_cast<const char*>(v6.ipv6Bytes().data()), 16));

    peer->close();
    QCoreApplication::processEvents();
}

QTEST_MAIN(tst_CoreSessionHandlers)
#include "tst_CoreSessionHandlers.moc"
