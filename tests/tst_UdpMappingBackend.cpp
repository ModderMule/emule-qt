/// @file tst_UdpMappingBackend.cpp
/// @brief Transaction and socket lifetime for the shared PCP / NAT-PMP UDP machinery.
///
/// Not live-labelled and not a router test: `channelFor()` is protected, so the fixture
/// points the IPv4 channel straight at a loopback socket playing gateway. `openChannel()` —
/// the only thing that consults the route table — is reached from `probe()` alone, so
/// `requestMapping()` and `releaseMapping()` run against 127.0.0.1 with no network involved.
///
/// The behaviour under test is the retransmit ladder's socket handling. One socket per
/// attempt is deliberate (the FRITZ!Box answers only the first request per source port,
/// see UdpMappingBackend.h), and the previous attempt's socket has to be handed to the event
/// loop rather than destroyed inline — it is still connected to the backend's slots and can
/// still be woken by a late reply. Destroying it inline crashed tst_PortMapLive in
/// QAbstractSocketPrivate::canReadNotification() about two seconds in, i.e. on the first
/// retransmit.
///
/// The last three cases cover reentrancy — a call inside the read loop destroying the very
/// transaction that loop is walking (issue #5). Read the platform note on
/// `gatewayRefusesEveryProbe()` before trusting a green run of those two: on macOS an ICMP
/// port-unreachable is never reported as a socket error at all, so only a Windows run
/// actually executes `onTransactionError()`.

#include "TestHelpers.h"
#include "net/Address.h"
#include "net/DefaultGateway.h"
#include "portmap/PortMapTypes.h"
#include "portmap/UdpMappingBackend.h"

#include <QElapsedTimer>
#include <QNetworkDatagram>
#include <QSignalSpy>
#include <QTest>
#include <QUdpSocket>

#include <vector>

using namespace eMule;

namespace {

/// Both RFCs put the server on 5351, and UdpMappingBackend hardcodes it.
constexpr quint16 kServerPort = 5351;

// ---------------------------------------------------------------------------
// A loopback stand-in for the router
//
// Records every request and replies only when the test says so, which is what makes
// "answer the superseded attempt" and "answer an attempt that was already retired"
// expressible at all.
// ---------------------------------------------------------------------------

class FakeGateway : public QObject {
    Q_OBJECT

public:
    struct Seen {
        QHostAddress address;
        quint16 port = 0;
        QByteArray payload;
    };

    std::vector<Seen> requests;   ///< every datagram, in arrival order
    int deletes = 0;              ///< how many release payloads arrived

    FakeGateway()
    {
        connect(&m_socket, &QUdpSocket::readyRead, this, &FakeGateway::onReadyRead);
    }

    [[nodiscard]] bool listen()
    {
        return m_socket.bind(QHostAddress::LocalHost, kServerPort);
    }

    /// Leave UDP/5351 unbound, so the host answers a request with an ICMP
    /// port-unreachable instead of a reply. Must be paired with listen() again — the port
    /// is bound once for the whole run, and the other cases need it.
    void stopListening() { m_socket.close(); }

    /// Answer request #index from port 5351 — the backend discards anything from another
    /// source port, so replying from a scratch socket would silently prove nothing.
    void replyTo(std::size_t index, const QByteArray& payload)
    {
        const Seen& seen = requests.at(index);
        m_socket.writeDatagram(payload, seen.address, seen.port);
    }

    /// Distinct source ports across attempts is the whole point of one socket per attempt.
    [[nodiscard]] bool sourcePortsAreDistinct() const
    {
        for (std::size_t i = 0; i < requests.size(); ++i)
            for (std::size_t j = i + 1; j < requests.size(); ++j)
                if (requests[i].port == requests[j].port)
                    return false;
        return true;
    }

private slots:
    void onReadyRead()
    {
        while (m_socket.hasPendingDatagrams()) {
            const QNetworkDatagram datagram = m_socket.receiveDatagram();
            const QByteArray payload = datagram.data();
            if (payload.startsWith('D')) {
                ++deletes;
                continue;
            }
            requests.push_back({ datagram.senderAddress(),
                                 static_cast<quint16>(datagram.senderPort()), payload });
        }
    }

private:
    QUdpSocket m_socket;
};

// ---------------------------------------------------------------------------
// The backend under test
//
// A minimal wire format, because the encode/decode hooks are not what is being tested:
//   request  "R" + transaction id
//   answer   "A" + transaction id + external port (little endian)
//   release  "D"
// Matching on the transaction id is what a real backend does with a PCP nonce, and it is
// what makes a reply to a retired transaction distinguishable from a live one.
// ---------------------------------------------------------------------------

class TestUdpBackend : public UdpMappingBackend {
    Q_OBJECT

public:
    [[nodiscard]] PortMapMethod method() const override { return PortMapMethod::Pcp; }
    [[nodiscard]] bool supports(PortMapFamily family) const override
    {
        return family == PortMapFamily::IPv4;
    }

    /// Skip route discovery and talk to the loopback gateway instead.
    void useLoopbackGateway()
    {
        Channel* channel = channelFor(PortMapFamily::IPv4);
        channel->gateway = GatewayCandidate{ Address::fromString(QStringLiteral("127.0.0.1")),
                                             QString(), 0 };
        channel->localAddress = Address::fromString(QStringLiteral("127.0.0.1"));
        channel->open = true;
    }

    /// Make decodeReply() retire the transaction it was handed, the way the real hooks can:
    /// PcpBackend::decodeReply emits externalAddressLearned and mappingsInvalidated from
    /// inside itself, and those reach PortMapper, which may answer with stop() or reprobe().
    void retireFromDecodeReply() { m_retireFromDecode = true; }

    [[nodiscard]] static QByteArray answerFor(const QByteArray& request, uint16 externalPort)
    {
        QByteArray out(4, '\0');
        out[0] = 'A';
        out[1] = request.size() > 1 ? request.at(1) : '\0';
        out[2] = static_cast<char>(externalPort & 0xFF);
        out[3] = static_cast<char>((externalPort >> 8) & 0xFF);
        return out;
    }

protected:
    [[nodiscard]] QByteArray encodeProbe(const Channel&) override
    {
        return QByteArrayLiteral("P");
    }

    [[nodiscard]] QByteArray encodeMap(const Channel&, const PortMapRequest&, uint32,
                                       Transaction& transaction) override
    {
        QByteArray out(2, '\0');
        out[0] = 'R';
        out[1] = static_cast<char>(transaction.id);
        return out;
    }

    [[nodiscard]] QByteArray encodeDelete(const Channel&, const PortMapping&) override
    {
        return QByteArrayLiteral("D");
    }

    [[nodiscard]] bool decodeReply(std::span<const uint8> datagram,
                                   const Transaction& transaction, PortMapping& mapping,
                                   bool& ok, QString& error) override
    {
        if (m_retireFromDecode) {
            // Returning false sends the caller back around the read loop, which is where
            // the transaction it is walking has just been freed underneath it.
            shutdown();
            return false;
        }
        if (datagram.size() < 4 || datagram[0] != 'A'
            || datagram[1] != static_cast<uint8>(transaction.id)) {
            return false;   // not this transaction's reply
        }
        mapping.externalPort = static_cast<uint16>(datagram[2] | (datagram[3] << 8));
        mapping.externalAddress = Address::fromString(QStringLiteral("203.0.113.7"));
        ok = true;
        error.clear();
        return true;
    }

    [[nodiscard]] bool isVersionMismatch(std::span<const uint8> datagram) const override
    {
        return !datagram.empty() && datagram[0] == 'V';
    }

private:
    bool m_retireFromDecode = false;
};

PortMapRequest makeRequest(uint16 port = 51999)
{
    PortMapRequest request;
    request.purpose = PortMapPurpose::Ed2kTcp;
    request.protocol = PortMapProtocol::Tcp;
    request.family = PortMapFamily::IPv4;
    request.internalPort = port;
    return request;
}

} // namespace

class tst_UdpMappingBackend : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void init();

    void retransmitUsesAFreshSourcePort();
    void lateReplyToASupersededAttemptIsHandled();
    void replyToARetiredAttemptIsIgnored();
    void shutdownWhileInFlightIsClean();
    void exhaustedLadderFailsTheTransaction();
    void releaseSendsTheDatagram();

    void decodeReplyMayRetireItsOwnTransaction();
    void gatewayRefusesEveryProbe();
    void gatewayRefusesAMappingRequest();

private:
    FakeGateway m_gateway;
};

void tst_UdpMappingBackend::initTestCase()
{
    // Bound once for the whole run: the backend only accepts replies from 5351, so there is
    // no way to run these against an ephemeral port instead. A conflict means something else
    // on this host speaks NAT-PMP/PCP, which is a skip rather than a failure.
    if (!m_gateway.listen())
        QSKIP("UDP 5351 is already in use on this host");
}

void tst_UdpMappingBackend::init()
{
    m_gateway.requests.clear();
    m_gateway.deletes = 0;
}

// ---------------------------------------------------------------------------
// One socket per attempt — the FRITZ!Box workaround documented in the header
// ---------------------------------------------------------------------------

void tst_UdpMappingBackend::retransmitUsesAFreshSourcePort()
{
    TestUdpBackend backend;
    backend.useLoopbackGateway();
    backend.requestMapping(makeRequest(), 120);

    // Ladder is 400 / 800 / 1600 ms, so the third attempt lands around 1.2 s.
    QTRY_COMPARE_WITH_TIMEOUT(m_gateway.requests.size(), std::size_t(3), 5000);
    QVERIFY2(m_gateway.sourcePortsAreDistinct(),
             "reusing a source port makes the FRITZ!Box ignore every retransmission");

    backend.shutdown();
}

// ---------------------------------------------------------------------------
// The path that used to destroy a live socket inline
// ---------------------------------------------------------------------------

void tst_UdpMappingBackend::lateReplyToASupersededAttemptIsHandled()
{
    TestUdpBackend backend;
    QSignalSpy spy(&backend, &PortMapBackend::mappingResult);

    backend.useLoopbackGateway();
    backend.requestMapping(makeRequest(), 120);

    // Two attempts means attempt 1's socket has already been replaced by attempt 2's.
    QTRY_COMPARE_WITH_TIMEOUT(m_gateway.requests.size(), std::size_t(2), 5000);

    m_gateway.replyTo(1, TestUdpBackend::answerFor(m_gateway.requests[1].payload, 51999));

    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 3000);
    QVERIFY(spy.at(0).at(1).toBool());
    const auto mapping = spy.at(0).at(0).value<PortMapping>();
    QCOMPARE(mapping.externalPort, uint16(51999));

    backend.shutdown();
}

void tst_UdpMappingBackend::replyToARetiredAttemptIsIgnored()
{
    TestUdpBackend backend;
    QSignalSpy spy(&backend, &PortMapBackend::mappingResult);

    backend.useLoopbackGateway();
    backend.requestMapping(makeRequest(), 120);

    QTRY_COMPARE_WITH_TIMEOUT(m_gateway.requests.size(), std::size_t(2), 5000);

    // Answer the attempt whose socket is already gone. Nothing should come of it — and
    // nothing should crash reaching for it either.
    m_gateway.replyTo(0, TestUdpBackend::answerFor(m_gateway.requests[0].payload, 51999));
    QTest::qWait(400);

    QCOMPARE(spy.count(), 0);

    backend.shutdown();
}

void tst_UdpMappingBackend::shutdownWhileInFlightIsClean()
{
    TestUdpBackend backend;
    QSignalSpy spy(&backend, &PortMapBackend::mappingResult);

    backend.useLoopbackGateway();
    backend.requestMapping(makeRequest(), 120);

    QTRY_COMPARE_WITH_TIMEOUT(m_gateway.requests.size(), std::size_t(1), 5000);

    // "shutdown() returns promptly and emits nothing afterwards" — PortMapBackend.h:13.
    backend.shutdown();
    m_gateway.replyTo(0, TestUdpBackend::answerFor(m_gateway.requests[0].payload, 51999));
    QTest::qWait(400);   // also gives the deleteLater'd sockets a turn to actually go

    QCOMPARE(spy.count(), 0);
}

void tst_UdpMappingBackend::exhaustedLadderFailsTheTransaction()
{
    TestUdpBackend backend;
    QSignalSpy spy(&backend, &PortMapBackend::mappingResult);

    backend.useLoopbackGateway();
    backend.requestMapping(makeRequest(), 120);

    // Silent gateway: 4 attempts at 400 / 800 / 1600 / 3200 ms, so the give-up lands
    // around 6 s. Slow, but the terminal branch of the ladder is exactly the one that
    // must still emit — silence there deadlocks PortMapper's state machine.
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 15000);
    QVERIFY(!spy.at(0).at(1).toBool());
    QVERIFY(!spy.at(0).at(2).toString().isEmpty());

    QCOMPARE(m_gateway.requests.size(), std::size_t(4));
    QVERIFY(m_gateway.sourcePortsAreDistinct());
}

// ---------------------------------------------------------------------------
// The polite early release has to actually leave the host
// ---------------------------------------------------------------------------

void tst_UdpMappingBackend::releaseSendsTheDatagram()
{
    TestUdpBackend backend;
    backend.useLoopbackGateway();

    PortMapping mapping;
    mapping.request = makeRequest();
    mapping.externalPort = 51999;
    mapping.method = PortMapMethod::Pcp;

    // Fire and forget on a socket that dies at the end of the call, immediately after
    // write() buffers the datagram. That reads like a lost-write bug, and it is not:
    // closing the socket pushes the pending write first. Worth a test of its own precisely
    // because releaseMapping() is deliberately unacknowledged — if the datagram silently
    // stopped leaving, nothing downstream would ever notice, and the only symptom would be
    // routers holding stale mappings until their leases expired.
    backend.releaseMapping(mapping);

    QTRY_COMPARE_WITH_TIMEOUT(m_gateway.deletes, 1, 3000);
}

// ---------------------------------------------------------------------------
// Reentrancy — issue #5
//
// The read loop calls out to things that emit, and those emissions can come back and
// destroy the transaction the loop is walking. This is the platform-independent half of
// that bug: it needs no ICMP and no Windows, only a hook that retires the transaction.
// ---------------------------------------------------------------------------

void tst_UdpMappingBackend::decodeReplyMayRetireItsOwnTransaction()
{
    TestUdpBackend backend;
    QSignalSpy spy(&backend, &PortMapBackend::mappingResult);

    backend.useLoopbackGateway();
    backend.retireFromDecodeReply();
    backend.requestMapping(makeRequest(), 120);

    QTRY_COMPARE_WITH_TIMEOUT(m_gateway.requests.size(), std::size_t(1), 5000);

    // The reply reaches decodeReply(), which shuts the backend down and returns false. The
    // loop used to carry on from there through a freed Transaction whose socket unique_ptr
    // had already been release()d — a null dereference here, a use-after-free under ASan,
    // and on Windows the crash that took the daemon down at every startup.
    m_gateway.replyTo(0, TestUdpBackend::answerFor(m_gateway.requests[0].payload, 51999));
    QTest::qWait(400);

    // shutdown() promises silence afterwards (PortMapBackend.h), so the retired
    // transaction must not report anything either.
    QCOMPARE(spy.count(), 0);
}

// ---------------------------------------------------------------------------
// Nothing listening on UDP/5351 — the reporter's router in issue #5
//
// Platform note, and it decides what a green run is worth: Qt only surfaces an ICMP
// port-unreachable on a connected QUdpSocket on Windows, where WSAECONNRESET becomes
// ConnectionRefusedError. On macOS the same condition produces hundreds of thousands of
// readyRead notifications and zero errorOccurred, so these two cases fall through to
// ladder exhaustion and never enter onTransactionError() at all. The Windows-only
// assertion below is the part that proves the refusal path ran: a refusal answers in
// milliseconds, while exhausting the ladder takes ~1.75 s (probe) or ~6 s (mapping).
// ---------------------------------------------------------------------------

void tst_UdpMappingBackend::gatewayRefusesEveryProbe()
{
    m_gateway.stopListening();

    TestUdpBackend backend;
    QSignalSpy spy(&backend, &PortMapBackend::probeFinished);
    backend.useLoopbackGateway();

    QElapsedTimer elapsed;
    elapsed.start();
    backend.probe(3000);

    // Exactly one terminal signal, whichever path got here: silence deadlocks
    // PortMapper's state machine (PortMapBackend.h).
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 15000);
    QVERIFY(!spy.at(0).at(0).toBool());
    QVERIFY(!spy.at(0).at(1).toString().isEmpty());
    qInfo() << "probe gave up after" << elapsed.elapsed() << "ms:"
            << spy.at(0).at(1).toString();

#ifdef Q_OS_WIN
    QVERIFY2(elapsed.elapsed() < 1000,
             "the probe ladder ran to exhaustion, so onTransactionError() never fired — "
             "this case proves nothing about issue #5 on this host");
#endif

    backend.shutdown();
    QTest::qWait(200);
    QCOMPARE(spy.count(), 1);

    QVERIFY(m_gateway.listen());
}

void tst_UdpMappingBackend::gatewayRefusesAMappingRequest()
{
    m_gateway.stopListening();

    TestUdpBackend backend;
    QSignalSpy spy(&backend, &PortMapBackend::mappingResult);
    backend.useLoopbackGateway();

    QElapsedTimer elapsed;
    elapsed.start();
    backend.requestMapping(makeRequest(), 120);

    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 15000);
    QVERIFY(!spy.at(0).at(1).toBool());
    QVERIFY(!spy.at(0).at(2).toString().isEmpty());
    qInfo() << "mapping failed after" << elapsed.elapsed() << "ms:"
            << spy.at(0).at(2).toString();

#ifdef Q_OS_WIN
    QVERIFY2(elapsed.elapsed() < 2000,
             "the request ladder ran to exhaustion, so onTransactionError() never fired — "
             "this case proves nothing about issue #5 on this host");
#endif

    backend.shutdown();
    QTest::qWait(200);
    QCOMPARE(spy.count(), 1);

    QVERIFY(m_gateway.listen());
}

QTEST_MAIN(tst_UdpMappingBackend)
#include "tst_UdpMappingBackend.moc"
