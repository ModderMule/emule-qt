/// @file tst_ListenSocket.cpp
/// @brief Tests for ListenSocket + ClientReqSocket.

#include "TestHelpers.h"
#include "app/AppContext.h"
#include "client/ClientList.h"
#include "client/UpDownClient.h"
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

#include <cstring>
#include <vector>

using namespace eMule;

namespace {

// ---------------------------------------------------------------------------
// A ClientReqSocket that can be handed a packet without a wire
//
// packetReceived() is protected, so a subclass is the way in. deliver() frames the payload
// exactly the way EMSocket::onReadyRead does — the Packet(const char* header) ctor plus a
// standalone `new char[size + 1]` pBuffer — because that is the ownership shape
// unPackPacket() then frees (EMSocket.cpp:265-268 vs Packet.cpp:179-185). Building the
// Packet any other way would exercise a buffer arrangement production never sees.
// ---------------------------------------------------------------------------

class PacketProbeSocket : public ClientReqSocket {
    Q_OBJECT

public:
    /// Which dispatch a packet came out of, plus what it carried.
    struct Received {
        QByteArray payload;
        uint8 opcode = 0;
        uint8 protocol = 0;      ///< only packetForClient reports one; else 0
        QByteArray via;          ///< "ext" | "client" | "hello" | "fileRequest"
    };

    std::vector<Received> received;

    PacketProbeSocket()
    {
        const auto record = [this](const QByteArray& via) {
            return [this, via](const uint8* data, uint32 size, uint8 opcode) {
                received.push_back({ QByteArray(reinterpret_cast<const char*>(data),
                                                static_cast<int>(size)),
                                     opcode, 0, via });
            };
        };
        connect(this, &ClientReqSocket::extPacketReceived, this, record("ext"));
        connect(this, &ClientReqSocket::helloReceived, this, record("hello"));
        connect(this, &ClientReqSocket::fileRequestReceived, this, record("fileRequest"));
        connect(this, &ClientReqSocket::packetForClient, this,
                [this](const uint8* data, uint32 size, uint8 opcode, uint8 protocol) {
                    received.push_back({ QByteArray(reinterpret_cast<const char*>(data),
                                                    static_cast<int>(size)),
                                         opcode, protocol, "client" });
                });
    }

    /// Run one framed packet through the real dispatch. Returns packetReceived()'s verdict,
    /// which is what EMSocket uses to decide whether to keep draining the read buffer.
    bool deliver(uint8 protocol, uint8 opcode, const QByteArray& payload)
    {
        char header[kPacketHeaderSize];
        auto* h = reinterpret_cast<HeaderStruct*>(header);
        h->eDonkeyID = protocol;
        h->packetLength = static_cast<uint32>(payload.size()) + 1;
        h->command = opcode;

        Packet packet(header);
        packet.pBuffer = new char[packet.size + 1];
        std::memcpy(packet.pBuffer, payload.constData(), packet.size);
        return packetReceived(&packet);
    }

    [[nodiscard]] bool isTornDown() const { return m_deleteThis; }
};

/// Payload with a realistic compression ratio. Deliberately not a run of zeros: those
/// compress ~1000:1 and land the inflate ladder somewhere no real packet reaches (see
/// packedPacket_underCapButOver50kSucceeds for why the ratio decides the outcome).
QByteArray compressibleBody(int bytes)
{
    QByteArray out;
    out.reserve(bytes + 8);
    quint32 x = 0x12345678u;
    while (out.size() < bytes) {
        x = x * 1664525u + 1013904223u;      // deterministic — no seeded RNG in tests
        out.append(static_cast<char>(x >> 24));
        out.append(static_cast<char>(x >> 16));
        out.append("\x00\x01\x02\x03", 4);   // structure, so zlib has something to find
    }
    out.truncate(bytes);
    return out;
}

/// The wire bytes a peer sends after zlib-packing `body`, produced by the very same
/// Packet::packPacket() our own emitter uses for source answers over 354 bytes
/// (KnownFile.cpp:915-917). Returns an empty array when the body did not compress.
QByteArray packBody(const QByteArray& body, uint8 opcode)
{
    Packet packet(opcode, static_cast<uint32>(body.size()), OP_EMULEPROT);
    std::memcpy(packet.pBuffer, body.constData(), static_cast<std::size_t>(body.size()));
    packet.packPacket();
    if (packet.prot != OP_PACKEDPROT)
        return {};
    return QByteArray(packet.pBuffer, static_cast<int>(packet.size));
}

} // namespace

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

    // Timeout extensions — MFC srchybrid/ListenSocket.cpp:139-147
    void clientReqSocketTimeout_extendedForBuddy();
    void clientReqSocketTimeout_extendedForUploadingClient();
    void clientReqSocketTimeout_extendedForChattingClient();
    void clientReqSocketTimeout_notExtendedWhileDownloadingFromPeer();
    void tooManySockets();
    void statisticsUpdate();

    // Accept-path guards — MFC CListenSocket::OnAccept / AcceptConnectionCond
    void incomingConnection_rejectsFilteredIP();
    void incomingConnection_rejectsBannedClient();
    void incomingConnection_acceptsCleanIP();

    // OP_PACKEDPROT decompression — MFC CClientReqSocket::PacketReceived
    // (srchybrid/ListenSocket.cpp:1804-1809)
    void packedPacket_isDecompressedBeforeDispatch();
    void packedPacket_reportsWireSizeToStatistics();
    void uncompressedEmulePacket_dispatchIsUnchanged();
    void edonkeyPacket_dispatchIsUnchanged();
    void corruptPackedPayload_isDroppedWithoutDisconnect();
    void packedPacket_underCapButOver50kSucceeds();
    void packedPacket_overCapIsDroppedGracefully();
    void packedPacket_alwaysRoutesToExtDispatch();

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
// Timeout extensions — MFC CClientReqSocket::CheckTimeOut, ListenSocket.cpp:139-147
//
// An else-if chain over the associated client: buddy, then upload slow-start, then chat.
// Each test drops the base timeout to zero so a few milliseconds of idling is already over
// it, leaving the extension as the only thing that can keep the socket alive.
// ---------------------------------------------------------------------------

void tst_ListenSocket::clientReqSocketTimeout_extendedForBuddy()
{
    UpDownClient client;
    ClientReqSocket reqSocket(&client);
    reqSocket.setTimeOut(0);
    reqSocket.resetTimeOutTimer();
    QTest::qWait(20);

    QVERIFY2(reqSocket.checkTimeOut(), "a client in no special state gets no extension");

    client.setKadState(KadState::ConnectedBuddy);
    QVERIFY(!reqSocket.checkTimeOut());
}

void tst_ListenSocket::clientReqSocketTimeout_extendedForUploadingClient()
{
    UpDownClient client;
    ClientReqSocket reqSocket(&client);
    reqSocket.setTimeOut(0);
    reqSocket.resetTimeOutTimer();
    QTest::qWait(20);

    QVERIFY(reqSocket.checkTimeOut());

    // MFC's guard is IsDownloading() — the upload state — because it is our send side that
    // may sit silent while TCP flow control finds the peer's rate.
    client.setUploadState(UploadState::Uploading);
    QVERIFY(!reqSocket.checkTimeOut());
}

void tst_ListenSocket::clientReqSocketTimeout_extendedForChattingClient()
{
    UpDownClient client;
    ClientReqSocket reqSocket(&client);
    reqSocket.setTimeOut(0);
    reqSocket.resetTimeOutTimer();
    QTest::qWait(20);

    QVERIFY(reqSocket.checkTimeOut());

    client.setChatState(ChatState::Chatting);
    QVERIFY(!reqSocket.checkTimeOut());
}

void tst_ListenSocket::clientReqSocketTimeout_notExtendedWhileDownloadingFromPeer()
{
    UpDownClient client;
    ClientReqSocket reqSocket(&client);
    reqSocket.setTimeOut(0);
    reqSocket.resetTimeOutTimer();
    QTest::qWait(20);

    // This port used to extend the timeout here, having read MFC's IsDownloading() as the
    // download direction. It is not, and MFC covers that direction elsewhere: entering
    // DS_DOWNLOADING raises the socket's base timeout to CONNECTION_TIMEOUT * 4
    // (srchybrid/DownloadClient.cpp:653, ported in UpDownClient::setDownloadState). A branch
    // here as well stacked a second extension on top of that one.
    client.setDownloadState(DownloadState::Downloading);
    QVERIFY(client.isDownloadingFromPeer());
    QVERIFY(reqSocket.checkTimeOut());
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

// ---------------------------------------------------------------------------
// OP_PACKEDPROT
//
// Any answer our own emitter compresses (source answers over 354 bytes,
// KnownFile.cpp:915-917) comes back at us from every peer running the same code. Before
// the unPackPacket() call in packetReceived those arrived as zlib bytes parsed as protocol,
// misparsed, and were dropped — so peer source exchange did not work at all.
// ---------------------------------------------------------------------------

void tst_ListenSocket::packedPacket_isDecompressedBeforeDispatch()
{
    const QByteArray body = compressibleBody(2000);
    const QByteArray wire = packBody(body, OP_ANSWERSOURCES2);
    QVERIFY(!wire.isEmpty());
    QVERIFY(wire.size() < body.size());

    PacketProbeSocket socket;
    QVERIFY(socket.deliver(OP_PACKEDPROT, OP_ANSWERSOURCES2, wire));

    QCOMPARE(socket.received.size(), std::size_t(1));
    QCOMPARE(socket.received[0].via, QByteArray("ext"));
    QCOMPARE(socket.received[0].opcode, uint8(OP_ANSWERSOURCES2));
    // The whole point: the parsers see the inflated bytes, not what came off the wire.
    QCOMPARE(socket.received[0].payload, body);
}

void tst_ListenSocket::packedPacket_reportsWireSizeToStatistics()
{
    const QByteArray body = compressibleBody(2000);
    const QByteArray wire = packBody(body, OP_ANSWERSOURCES2);
    QVERIFY(!wire.isEmpty());

    const uint64 before = m_statistics->downDataOverheadSourceExchange();

    PacketProbeSocket socket;
    QVERIFY(socket.deliver(OP_PACKEDPROT, OP_ANSWERSOURCES2, wire));

    // Overhead means bytes off the wire. Charging the inflated size would overstate our
    // download overhead by exactly what zlib saved — MFC meters uRawSize for the same
    // reason (srchybrid/ListenSocket.cpp:1790, :1820).
    const uint64 charged = m_statistics->downDataOverheadSourceExchange() - before;
    QCOMPARE(charged, uint64(wire.size()));
    QVERIFY(charged < uint64(body.size()));
}

void tst_ListenSocket::uncompressedEmulePacket_dispatchIsUnchanged()
{
    const QByteArray body = compressibleBody(64);

    PacketProbeSocket socket;
    QVERIFY(socket.deliver(OP_EMULEPROT, OP_ANSWERSOURCES2, body));

    QCOMPARE(socket.received.size(), std::size_t(1));
    QCOMPARE(socket.received[0].via, QByteArray("ext"));
    QCOMPARE(socket.received[0].payload, body);
}

void tst_ListenSocket::edonkeyPacket_dispatchIsUnchanged()
{
    const QByteArray body = compressibleBody(48);

    PacketProbeSocket socket;
    QVERIFY(socket.deliver(OP_EDONKEYPROT, OP_MESSAGE, body));

    QCOMPARE(socket.received.size(), std::size_t(1));
    QCOMPARE(socket.received[0].via, QByteArray("client"));
    QCOMPARE(socket.received[0].protocol, uint8(OP_EDONKEYPROT));
    QCOMPARE(socket.received[0].payload, body);
}

void tst_ListenSocket::corruptPackedPayload_isDroppedWithoutDisconnect()
{
    // Not a zlib stream at all.
    QByteArray garbage(120, '\x7f');
    garbage[0] = '\x00';

    PacketProbeSocket socket;
    // MFC breaks out of the switch and returns true: the packet is dropped, the connection
    // lives. Returning false would make EMSocket::onReadyRead abandon the rest of the read
    // buffer along with any partial header in it, desyncing the stream over one bad packet.
    QVERIFY(socket.deliver(OP_PACKEDPROT, OP_ANSWERSOURCES2, garbage));

    QVERIFY(socket.received.empty());
    QVERIFY(!socket.isTornDown());
}

void tst_ListenSocket::packedPacket_underCapButOver50kSucceeds()
{
    const QByteArray body = compressibleBody(200000);
    const QByteArray wire = packBody(body, OP_ANSWERSOURCES2);
    QVERIFY(!wire.isEmpty());

    // What actually decides this is not the cap alone. unPackPacket starts its output
    // buffer at size * 10 + 300 (clamped to the cap) and doubles on Z_BUF_ERROR only while
    // the *next* rung is still below the cap — so the ladder can stop short of the cap
    // itself. Assert the first rung already clears the body, which is the case for any
    // realistic ratio, so this test measures the cap and not zlib's mood.
    QVERIFY(uint32(wire.size()) * 10 + 300 >= uint32(body.size()));
    QVERIFY(wire.size() < 250000);      // ... and the cap is what lets that rung be allocated

    PacketProbeSocket socket;
    QVERIFY(socket.deliver(OP_PACKEDPROT, OP_ANSWERSOURCES2, wire));

    QCOMPARE(socket.received.size(), std::size_t(1));
    QCOMPARE(socket.received[0].payload, body);
}

void tst_ListenSocket::packedPacket_overCapIsDroppedGracefully()
{
    const QByteArray body = compressibleBody(400000);
    const QByteArray wire = packBody(body, OP_ANSWERSOURCES2);
    QVERIFY(!wire.isEmpty());
    QVERIFY(body.size() > 250000);

    PacketProbeSocket socket;
    QVERIFY(socket.deliver(OP_PACKEDPROT, OP_ANSWERSOURCES2, wire));

    QVERIFY(socket.received.empty());
    QVERIFY(!socket.isTornDown());
}

void tst_ListenSocket::packedPacket_alwaysRoutesToExtDispatch()
{
    // unPackPacket rewrites prot to OP_EMULEPROT unconditionally (Packet.cpp:186), so a
    // packed packet lands in the extended dispatch even when its opcode also exists on the
    // eDonkey side. OP_CHANGE_CLIENT_IP is handled in both switches, which is what makes
    // the two routes distinguishable here. MFC behaves identically — its OP_PACKEDPROT
    // case falls through to OP_EMULEPROT, never to ProcessPacket.
    const QByteArray body = compressibleBody(600);
    const QByteArray wire = packBody(body, OP_CHANGE_CLIENT_IP);
    QVERIFY(!wire.isEmpty());

    PacketProbeSocket socket;
    QVERIFY(socket.deliver(OP_PACKEDPROT, OP_CHANGE_CLIENT_IP, wire));

    QCOMPARE(socket.received.size(), std::size_t(1));
    QCOMPARE(socket.received[0].via, QByteArray("ext"));

    // Control: the same opcode unpacked goes the other way.
    PacketProbeSocket plain;
    QVERIFY(plain.deliver(OP_EDONKEYPROT, OP_CHANGE_CLIENT_IP, body));
    QCOMPARE(plain.received.size(), std::size_t(1));
    QCOMPARE(plain.received[0].via, QByteArray("client"));
}

QTEST_MAIN(tst_ListenSocket)
#include "tst_ListenSocket.moc"
