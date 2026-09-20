/// @file tst_EMSocket.cpp
/// @brief Tests for EMSocket — packet framing over loopback TCP.

#include "TestHelpers.h"
#include "net/EMSocket.h"
#include "net/Packet.h"
#include "utils/Opcodes.h"

#include <QEventLoop>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QTimer>

#include <array>
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

    /// Emulate a real socket tearing itself down on error (ClientReqSocket sets
    /// m_deleteThis and drops the connection). With this on, nothing deferred can
    /// run after onError, so only a synchronous drain can deliver buffered bytes.
    bool disconnectOnError = false;

    /// Re-enter the read path from inside the first packet's handler, the way
    /// ClientReqSocket::disconnect()'s flush() can when the write fails synchronously.
    /// disableDownloadLimit() is the reachable lever: it calls onReadyRead() directly
    /// whenever pendingOnReceive() is set.
    bool reenterOnFirstPacket = false;

protected:
    bool packetReceived(Packet* packet) override
    {
        if (reenterOnFirstPacket) {
            reenterOnFirstPacket = false;   // once, or it recurses
            disableDownloadLimit();
        }

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
        if (disconnectOnError)
            setConState(EMSState::Disconnected);
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
    void drainsBufferWhenPeerGoesQuiet();
    void drainsBufferWhenPeerCloses();
    void retryChainQuiescesWhenEncryptionNotReady();
    void sharedReadBuffer_twoSocketsInterleave();
    void nestedPassDoesNotCorruptOuterBuffer();
    void fullReceive_falseOnShortRead();
    void fullReceive_trueWhenBufferFilled();
    void fullReceive_trueUnderAThrottleThatCapsTheRead();
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
// Shared setup for the two buffer-draining cases
//
// Both need more than kMaxReadBuffer (2 MB) sitting in Qt's read buffer before
// the socket is allowed to touch it, so that one read/parse pass provably cannot
// finish the job. setDownloadLimit(0) is the lever: onReadyRead() returns without
// reading while the limit is zero, so Qt goes on buffering everything the peer
// sends and the socket consumes none of it.
// ---------------------------------------------------------------------------

namespace {

constexpr uint32 kBurstPayload = 65536;                    ///< per packet
constexpr std::size_t kBurstPackets = 80;                  ///< ~5.2 MB total, ~3 read passes

/// Fill the peer's buffer with kBurstPackets framed packets while @p sock reads none.
/// Returns false if the data never arrived, so a caller can fail rather than assert
/// on an empty socket.
[[nodiscard]] bool sendBurstWithoutReading(TestEMSocket& sock, QTcpSocket* peer)
{
    sock.setDownloadLimit(0);

    const std::vector<char> payload(kBurstPayload, '\xAB');
    for (std::size_t i = 0; i < kBurstPackets; ++i)
        writeRawPacket(peer, OP_EDONKEYPROT, 0x01, payload.data(), kBurstPayload);

    constexpr qint64 expected =
        static_cast<qint64>(kBurstPackets) * (kBurstPayload + sizeof(HeaderStruct));

    // The socket must be holding more than one pass can consume — otherwise these
    // cases would pass without the fix and prove nothing.
    return QTest::qWaitFor([&sock] { return sock.bytesAvailable() >= expected; }, 15000)
        && sock.bytesAvailable() > 2'000'000;
}

} // namespace

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

// ---------------------------------------------------------------------------
// A read pass that leaves bytes behind must come back for them
//
// onReadyRead() consumes at most kMaxReadBuffer (2 MB) per pass, and Qt re-emits
// readyRead only when *new* data arrives. A peer that has finished sending never
// sends again, so without a re-arm the remainder is stranded until the connection
// closes — a stall of exactly the peer's keep-alive timeout, which reads as a slow
// server rather than a stuck client.
// ---------------------------------------------------------------------------

void tst_EMSocket::drainsBufferWhenPeerGoesQuiet()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));

    TestEMSocket clientSocket;
    clientSocket.connectToHost(QHostAddress::LocalHost, server.serverPort());

    QVERIFY(server.waitForNewConnection(5000));
    auto* serverSide = server.nextPendingConnection();
    QVERIFY(serverSide != nullptr);
    QVERIFY(clientSocket.waitForConnected(5000));

    QVERIFY2(sendBurstWithoutReading(clientSocket, serverSide),
             "peer never buffered past kMaxReadBuffer — the case would prove nothing");
    QCOMPARE(clientSocket.receivedPackets.size(), std::size_t(0));

    // One full pass: 2 MB consumed, the rest left in Qt's buffer. The peer stays
    // connected and silent from here on, so nothing else can wake the socket.
    clientSocket.disableDownloadLimit();

    QTRY_COMPARE_WITH_TIMEOUT(clientSocket.receivedPackets.size(), kBurstPackets, 10000);
    QCOMPARE(clientSocket.bytesAvailable(), qint64(0));

    // Proves the close-path drain is not what rescued it.
    QCOMPARE(clientSocket.state(), QAbstractSocket::ConnectedState);
    QCOMPARE(clientSocket.receivedPackets.back().data.size(), std::size_t(kBurstPayload));

    serverSide->close();
    clientSocket.close();
}

// ---------------------------------------------------------------------------
// ...and a socket that dies on close must drain before it goes
//
// onSocketError() is the last chance to read what the peer sent: onError() can
// destroy the socket, so a deferred pass would never run. MFC drains here too, and
// ignores the download rate limit while doing it (CEMSocket::OnReceive's
// `nErrorCode != WSAESHUTDOWN` checks) — there is nothing left to pace once the
// peer is gone, and a throttle that will never be lifted would strand the data.
// ---------------------------------------------------------------------------

void tst_EMSocket::drainsBufferWhenPeerCloses()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));

    TestEMSocket clientSocket;
    clientSocket.disconnectOnError = true;
    clientSocket.connectToHost(QHostAddress::LocalHost, server.serverPort());

    QVERIFY(server.waitForNewConnection(5000));
    auto* serverSide = server.nextPendingConnection();
    QVERIFY(serverSide != nullptr);
    QVERIFY(clientSocket.waitForConnected(5000));

    QVERIFY2(sendBurstWithoutReading(clientSocket, serverSide),
             "peer never buffered past kMaxReadBuffer — the case would prove nothing");
    QCOMPARE(clientSocket.receivedPackets.size(), std::size_t(0));

    // The rate limit is still zero and is never lifted. Only the close drain can
    // deliver these packets, and it gets one shot before onError() disconnects us.
    serverSide->close();

    QTRY_COMPARE_WITH_TIMEOUT(clientSocket.receivedPackets.size(), kBurstPackets, 10000);
    QCOMPARE(clientSocket.lastErrorCode, int(QAbstractSocket::RemoteHostClosedError));

    clientSocket.close();
}

// ---------------------------------------------------------------------------
// Send-retry chain must not poll while the encryption layer is not ready
// ---------------------------------------------------------------------------

void tst_EMSocket::retryChainQuiescesWhenEncryptionNotReady()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));

    TestEMSocket clientSocket;

    // Outgoing obfuscation: the state parks in Negotiating until the peer answers
    // the handshake, and this server never will. send() therefore early-returns
    // without draining, which used to make scheduleRetryIfNeeded() re-arm its 10 ms
    // timer forever — 100 Hz per socket, indefinitely.
    const std::array<uint8, 16> peerHash{};
    clientSocket.setConnectionEncryption(true, peerHash.data(), false);
    clientSocket.connectToHost(QHostAddress::LocalHost, server.serverPort());

    QVERIFY(server.waitForNewConnection(5000));
    auto* serverSide = server.nextPendingConnection();
    QVERIFY(serverSide != nullptr);
    QVERIFY(clientSocket.waitForConnected(5000));
    QVERIFY(!clientSocket.isEncryptionLayerReady());

    QCOMPARE(clientSocket.sendRetryCount(), uint64(0));

    // Queue a control packet — this starts the retry chain.
    clientSocket.sendPacket(std::make_unique<Packet>(OP_EDONKEYPROT, 4), true);

    // A plain nested event loop, not QTest::qWait — qWait polls the event loop and
    // would perturb timer delivery.
    QEventLoop loop;
    QTimer::singleShot(500, &loop, &QEventLoop::quit);
    loop.exec();

    // The chain must have started (otherwise the test proves nothing) but must have
    // backed off: retries land at 10/30/70/150/310 ms, so 5 in a 500 ms window.
    // A flat 10 ms re-arm gives 18+ here and 100/s in the field.
    const uint64 retries = clientSocket.sendRetryCount();
    QVERIFY2(retries > 0, "retry chain never started — the test would prove nothing");
    QVERIFY2(retries < 10,
             qPrintable(QStringLiteral("retry chain fired %1 times in 500ms").arg(retries)));

    serverSide->close();
    clientSocket.close();
}

// ---------------------------------------------------------------------------
// One read buffer, many sockets
//
// The 2 MB buffer is shared (MFC's GlobalReadBuffer). What must stay per-socket is
// the partial-header tail, so two sockets each holding one across a pass may not
// bleed into each other.
// ---------------------------------------------------------------------------

void tst_EMSocket::sharedReadBuffer_twoSocketsInterleave()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));

    TestEMSocket a;
    TestEMSocket b;
    a.connectToHost(QHostAddress::LocalHost, server.serverPort());
    QVERIFY(server.waitForNewConnection(5000));
    auto* peerA = server.nextPendingConnection();
    b.connectToHost(QHostAddress::LocalHost, server.serverPort());
    QVERIFY(server.waitForNewConnection(5000));
    auto* peerB = server.nextPendingConnection();
    QVERIFY(peerA && peerB);
    QVERIFY(a.waitForConnected(5000));
    QVERIFY(b.waitForConnected(5000));

    // Header plus two payload bytes, so each socket parks a partial packet.
    const auto writeHead = [](QTcpSocket* peer, uint8 opcode, const char* first2) {
        HeaderStruct hdr;
        hdr.eDonkeyID = OP_EDONKEYPROT;
        hdr.packetLength = 6 + 1;   // 6 payload bytes
        hdr.command = opcode;
        peer->write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));
        peer->write(first2, 2);
        peer->flush();
    };

    writeHead(peerA, 0x11, "AA");
    writeHead(peerB, 0x22, "BB");
    QTest::qWait(150);           // both passes run and save their tail

    peerA->write("AAAA", 4);
    peerA->flush();
    peerB->write("BBBB", 4);
    peerB->flush();

    QTRY_COMPARE_WITH_TIMEOUT(a.receivedPackets.size(), static_cast<std::size_t>(1), 3000);
    QTRY_COMPARE_WITH_TIMEOUT(b.receivedPackets.size(), static_cast<std::size_t>(1), 3000);

    QCOMPARE(a.receivedPackets[0].opcode, static_cast<uint8>(0x11));
    QCOMPARE(b.receivedPackets[0].opcode, static_cast<uint8>(0x22));
    QCOMPARE(QByteArray(a.receivedPackets[0].data.data(), 6), QByteArray("AAAAAA", 6));
    QCOMPARE(QByteArray(b.receivedPackets[0].data.data(), 6), QByteArray("BBBBBB", 6));

    peerA->close();
    peerB->close();
    a.close();
    b.close();
}

// ---------------------------------------------------------------------------
// A nested pass must not eat the outer pass's bytes
//
// The outer pass is mid-parse with pointers into the shared buffer when the nested
// one reads over it. The lease hands the nested pass a buffer of its own; without it
// the outer's remaining packets come back as whatever the nested read overwrote them
// with.
// ---------------------------------------------------------------------------

void tst_EMSocket::nestedPassDoesNotCorruptOuterBuffer()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));

    TestEMSocket clientSocket;
    clientSocket.connectToHost(QHostAddress::LocalHost, server.serverPort());
    QVERIFY(server.waitForNewConnection(5000));
    auto* serverSide = server.nextPendingConnection();
    QVERIFY(serverSide != nullptr);
    QVERIFY(clientSocket.waitForConnected(5000));

    // 8 packets of 16 bytes each. Each payload is its own index, so a packet that came
    // out of clobbered memory is recognisable.
    constexpr int kPackets = 8;
    constexpr uint32 kPayload = 10;
    constexpr qint64 kPacketBytes = kPayload + sizeof(HeaderStruct);

    clientSocket.setDownloadLimit(0);
    for (int i = 0; i < kPackets; ++i) {
        const std::vector<char> payload(kPayload, static_cast<char>('0' + i));
        writeRawPacket(serverSide, OP_EDONKEYPROT, static_cast<uint8>(0x40 + i),
                       payload.data(), kPayload);
    }
    QVERIFY(QTest::qWaitFor([&] { return clientSocket.bytesAvailable() >= kPackets * kPacketBytes; },
                            5000));

    // A limit of exactly four packets: the first pass fills its whole request (so
    // pendingOnReceive is set) and leaves the other four buffered for the nested pass.
    clientSocket.reenterOnFirstPacket = true;
    clientSocket.setDownloadLimit(4 * kPacketBytes);

    QTRY_COMPARE_WITH_TIMEOUT(clientSocket.receivedPackets.size(),
                              static_cast<std::size_t>(kPackets), 3000);

    // Order is interleaved by the nesting; what matters is that every packet arrived
    // once and carries its own bytes.
    std::vector<bool> seen(kPackets, false);
    for (const auto& rp : clientSocket.receivedPackets) {
        const int idx = rp.opcode - 0x40;
        QVERIFY2(idx >= 0 && idx < kPackets, "packet with an opcode we never sent");
        QVERIFY2(!seen[idx], "the same packet arrived twice");
        seen[idx] = true;
        QCOMPARE(rp.data.size(), static_cast<std::size_t>(kPayload));
        QCOMPARE(QByteArray(rp.data.data(), kPayload),
                 QByteArray(kPayload, static_cast<char>('0' + idx)));
    }

    serverSide->close();
    clientSocket.close();
}

// ---------------------------------------------------------------------------
// m_fullReceive — MFC's "did the read fill the ask?" (EncryptedStreamSocket.cpp:230)
// ---------------------------------------------------------------------------

void tst_EMSocket::fullReceive_falseOnShortRead()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));

    TestEMSocket clientSocket;
    clientSocket.connectToHost(QHostAddress::LocalHost, server.serverPort());
    QVERIFY(server.waitForNewConnection(5000));
    auto* serverSide = server.nextPendingConnection();
    QVERIFY(serverSide != nullptr);
    QVERIFY(clientSocket.waitForConnected(5000));

    writeRawPacket(serverSide, OP_EDONKEYPROT, 0x01, "Hello", 5);
    QTRY_COMPARE_WITH_TIMEOUT(clientSocket.receivedPackets.size(), static_cast<std::size_t>(1), 3000);

    // 11 bytes against a 2 MB request: there is plainly nothing more to fetch, so
    // lifting the rate limit must not fire a re-entrant read.
    QVERIFY(!clientSocket.pendingOnReceive());

    serverSide->close();
    clientSocket.close();
}

void tst_EMSocket::fullReceive_trueWhenBufferFilled()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));

    TestEMSocket clientSocket;
    clientSocket.connectToHost(QHostAddress::LocalHost, server.serverPort());
    QVERIFY(server.waitForNewConnection(5000));
    auto* serverSide = server.nextPendingConnection();
    QVERIFY(serverSide != nullptr);
    QVERIFY(clientSocket.waitForConnected(5000));

    QVERIFY2(sendBurstWithoutReading(clientSocket, serverSide),
             "peer data never arrived — the case would prove nothing");

    // Synchronous: one pass that consumes the whole 2 MB request runs inside this call.
    clientSocket.disableDownloadLimit();
    QVERIFY(clientSocket.pendingOnReceive());

    serverSide->close();
    clientSocket.close();
}

void tst_EMSocket::fullReceive_trueUnderAThrottleThatCapsTheRead()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));

    TestEMSocket clientSocket;
    clientSocket.connectToHost(QHostAddress::LocalHost, server.serverPort());
    QVERIFY(server.waitForNewConnection(5000));
    auto* serverSide = server.nextPendingConnection();
    QVERIFY(serverSide != nullptr);
    QVERIFY(clientSocket.waitForConnected(5000));

    clientSocket.setDownloadLimit(0);
    const std::vector<char> payload(1000, '\xCD');
    writeRawPacket(serverSide, OP_EDONKEYPROT, 0x01, payload.data(), 1000);
    QVERIFY(QTest::qWaitFor([&] { return clientSocket.bytesAvailable() >= 1006; }, 5000));

    // The throttle, not the peer, is what ends this read — more is waiting, so the
    // next limit change must re-read immediately. This is the pacing MFC relies on.
    clientSocket.setDownloadLimit(64);
    QVERIFY(clientSocket.pendingOnReceive());
    QCOMPARE(clientSocket.receivedPackets.size(), static_cast<std::size_t>(0));

    clientSocket.setDownloadLimit(4000);
    QTRY_COMPARE_WITH_TIMEOUT(clientSocket.receivedPackets.size(), static_cast<std::size_t>(1), 3000);
    QCOMPARE(clientSocket.receivedPackets[0].data.size(), static_cast<std::size_t>(1000));

    serverSide->close();
    clientSocket.close();
}

QTEST_MAIN(tst_EMSocket)
#include "tst_EMSocket.moc"
