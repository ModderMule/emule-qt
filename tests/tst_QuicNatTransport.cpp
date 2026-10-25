/// @file tst_QuicNatTransport.cpp
/// @brief QUIC NAT-T transport: two endpoints over an in-process datagram pipe with real
///        client-UDP obfuscation, loss and reordering. Handshake, EAQN1 proof, bulk data
///        both ways, first-Initial obfuscation, plain fallback, strict mode, caps.

#include "net/EncryptedDatagramSocket.h"
#include "net/StreamTransport.h"
#include "net/quic/QuicNatConnection.h"
#include "net/quic/QuicNatEndpoint.h"
#include "utils/Opcodes.h"

#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QRandomGenerator>
#include <QTest>
#include <QTimer>

#include <memory>

using namespace eMule;
using namespace eMule::quic;

namespace {

Hash16 hashOf(uint8 seed)
{
    Hash16 h{};
    for (size_t i = 0; i < h.size(); ++i)
        h[i] = static_cast<uint8>(seed + i * 7);
    h[5] = 14;
    h[14] = 111;
    return h;
}

/// One side of the pipe. Frames go to the other side after a small random delay.
class PipeIo : public QuicNatIo {
public:
    PipeIo(Endpoint self, Hash16 hash) : m_self(self), m_hash(hash) { m_clock.start(); }

    QuicNatEndpoint* other = nullptr;
    PipeIo* otherIo = nullptr;
    double loss = 0.0;
    bool reorder = false;
    bool strict = false;
    bool obfuscate = true;
    /// The address we key our obfuscation with; a wrong one means the peer cannot decrypt.
    Address claimedPublic;

    struct Wire { QByteArray bytes; uint8 type; bool obfuscated; };
    std::vector<Wire> sent;

    [[nodiscard]] Hash16 ourHash() const override { return m_hash; }
    [[nodiscard]] uint64 nowMs() const override { return static_cast<uint64>(m_clock.elapsed()); }
    [[nodiscard]] bool canObfuscate(const Endpoint&) const override { return obfuscate; }
    [[nodiscard]] bool strictObfuscation() const override { return strict; }
    [[nodiscard]] bool admit(const Address&) const override { return true; }
    [[nodiscard]] uint8 connectOptions() const override { return 0x03; }
    [[nodiscard]] Endpoint localEndpoint(const Endpoint&) const override { return m_self; }

    void sendFrame(uint8 type, const uint8* payload, size_t len, const Endpoint& to,
                   const Hash16* cryptHash) override
    {
        Q_UNUSED(to);
        constexpr size_t kHead = 8 + 15;
        std::vector<uint8> buf(kHead + 2 + len);
        buf[kHead] = OP_UDPRESERVEDPROT2;
        buf[kHead + 1] = type;
        std::memcpy(buf.data() + kHead + 2, payload, len);
        QByteArray wire;
        if (cryptHash) {
            const uint8 pad = static_cast<uint8>(QRandomGenerator::global()->bounded(16));
            const uint32 total = EncryptedDatagramSocket::encryptSendClient(
                buf.data() + kHead - 8 - pad, static_cast<uint32>(2 + len), cryptHash->data(), false, 0, 0,
                claimedPublic.isNull() ? m_self.address() : claimedPublic, pad);
            wire = QByteArray(reinterpret_cast<const char*>(buf.data() + kHead - 8 - pad),
                              static_cast<qsizetype>(total));
        } else {
            wire = QByteArray(reinterpret_cast<const char*>(buf.data() + kHead),
                              static_cast<qsizetype>(2 + len));
        }
        sent.push_back({wire, type, cryptHash != nullptr});
        if (loss > 0 && QRandomGenerator::global()->generateDouble() < loss)
            return;
        const int delay = reorder ? QRandomGenerator::global()->bounded(1, 6) : 0;
        QuicNatEndpoint* target = other;
        PipeIo* targetIo = otherIo;
        const Endpoint from = m_self;
        QTimer::singleShot(delay, target, [target, targetIo, wire, from]() mutable {
            targetIo->deliver(*target, wire, from);
        });
    }

    void deliver(QuicNatEndpoint& ep, QByteArray wire, const Endpoint& from)
    {
        auto* p = reinterpret_cast<uint8*>(wire.data());
        int len = static_cast<int>(wire.size());
        bool obfuscated = false;
        if (p[0] != OP_UDPRESERVEDPROT2) {
            const DecryptResult r = EncryptedDatagramSocket::decryptReceivedClient(
                p, len, from.address(), m_hash.data(), nullptr, 0);
            if (r.length < 0 || r.data == p || r.data[0] != OP_UDPRESERVEDPROT2) {
                ++undecryptable;
                return;
            }
            p = r.data;
            len = r.length;
            obfuscated = true;
        }
        if (len < 2)
            return;
        ep.handleFrame(p[1], p + 2, static_cast<size_t>(len - 2), from, obfuscated);
    }

    int undecryptable = 0;

private:
    Endpoint m_self;
    Hash16 m_hash;
    QElapsedTimer m_clock;
};

/// Collects a stream and reports loss, like the socket would.
class Sink : public StreamTransportSink {
public:
    std::shared_ptr<QuicNatConnection> conn;
    QByteArray got;
    bool lost = false;
    void transportReadable() override
    {
        char buf[65536];
        qint64 n;
        while ((n = conn->read(buf, sizeof(buf))) > 0)
            got.append(buf, n);
    }
    void transportWritten(qint64) override {}
    void transportLost(QAbstractSocket::SocketError, const QString&) override { lost = true; }
};

struct Pair {
    Endpoint epA{Address::fromString(QStringLiteral("10.77.1.5")), 4672};
    Endpoint epB{Address::fromString(QStringLiteral("10.77.2.9")), 5672};
    Hash16 hA = hashOf(1), hB = hashOf(100);
    PipeIo ioA{epA, hA}, ioB{epB, hB};
    QuicNatEndpoint a{ioA}, b{ioB};
    std::shared_ptr<QuicNatConnection> connA, connB;
    QString failA, failB;

    Pair()
    {
        ioA.other = &b; ioA.otherIo = &ioB;
        ioB.other = &a; ioB.otherIo = &ioA;
        a.onEstablished = [this](std::shared_ptr<QuicNatConnection> c, bool) { connA = std::move(c); };
        b.onEstablished = [this](std::shared_ptr<QuicNatConnection> c, bool) { connB = std::move(c); };
        a.onFailed = [this](const Endpoint&, const Hash16&, const QString& r) { failA = r; };
        b.onFailed = [this](const Endpoint&, const Hash16&, const QString& r) { failB = r; };
    }

    bool connect(int timeoutMs = 5000)
    {
        b.expect(epA, hA);
        b.learnPeerHash(epA, hA);
        a.learnPeerHash(epB, hB);
        if (!a.dial(epB, hB))
            return false;
        return QTest::qWaitFor([this] { return connA && connB; }, timeoutMs);
    }
};

QByteArray pattern(qsizetype size, uint8 seed)
{
    QByteArray d(size, Qt::Uninitialized);
    uint32 x = seed * 2654435761u + 1;
    for (qsizetype i = 0; i < size; ++i) {
        x = x * 1103515245u + 12345u;
        d[i] = static_cast<char>(x >> 24);
    }
    return d;
}

/// Writes @p data honouring backpressure, as the throttler would.
void pump(QuicNatConnection& c, const QByteArray& data, qsizetype& off)
{
    while (off < data.size()) {
        const qint64 n = c.write(data.constData() + off, std::min<qint64>(64 * 1024, data.size() - off));
        if (n <= 0)
            return;
        off += n;
    }
}

} // namespace

class tst_QuicNatTransport : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        if (!QuicNatEndpoint::available())
            QSKIP("QUIC NAT-T not built in");
    }

    void handshakeProofAndBulkBothWays()
    {
        Pair p;
        QVERIFY(p.connect());
        QCOMPARE(p.connA->peerHash(), p.hB);
        QCOMPARE(p.connB->peerHash(), p.hA);
        QCOMPARE(p.connB->peerEndpoint(), p.epA);

        Sink sa, sb;
        sa.conn = p.connA; sb.conn = p.connB;
        p.connA->setSink(&sa);
        p.connB->setSink(&sb);
        p.connA->setSendBufferLimit(1024 * 1024);
        p.connB->setSendBufferLimit(1024 * 1024);

        const QByteArray toB = pattern(8 * 1024 * 1024, 3);
        const QByteArray toA = pattern(8 * 1024 * 1024, 9);
        qsizetype offA = 0, offB = 0;
        QElapsedTimer t;
        t.start();
        QVERIFY(QTest::qWaitFor([&] {
            pump(*p.connA, toB, offA);
            pump(*p.connB, toA, offB);
            return sb.got.size() == toB.size() && sa.got.size() == toA.size();
        }, 30000));
        QCOMPARE(QCryptographicHash::hash(sb.got, QCryptographicHash::Md5),
                 QCryptographicHash::hash(toB, QCryptographicHash::Md5));
        QCOMPARE(QCryptographicHash::hash(sa.got, QCryptographicHash::Md5),
                 QCryptographicHash::hash(toA, QCryptographicHash::Md5));
        qInfo("16 MiB both ways in %lld ms", static_cast<long long>(t.elapsed()));
        QVERIFY(!sa.lost && !sb.lost);
    }

    void firstInitialIsObfuscated()
    {
        Pair p;
        QVERIFY(p.connect());
        // key frame first, then the Initial; both obfuscated with B's hash
        const auto& w = p.ioA.sent;
        QVERIFY(w.size() >= 2);
        QCOMPARE(w[0].type, uint8(OP_NATT_FRAME_KEY));
        QCOMPARE(w[1].type, uint8(OP_NATT_FRAME_QUIC));
        for (const auto& f : w) {
            QVERIFY(f.obfuscated);
            QVERIFY(static_cast<uint8>(f.bytes[0]) != OP_UDPRESERVEDPROT2);
        }
        // the first Initial: 1200 QUIC bytes + 2 envelope + 8 header + 0-15 padding
        QVERIFY(w[1].bytes.size() >= 1210 && w[1].bytes.size() <= 1225);
        QCOMPARE(p.ioB.undecryptable, 0);
    }

    void survivesLossAndReordering()
    {
        Pair p;
        p.ioA.loss = p.ioB.loss = 0.05;
        p.ioA.reorder = p.ioB.reorder = true;
        QVERIFY(p.connect(15000));
        Sink sb;
        sb.conn = p.connB;
        p.connB->setSink(&sb);
        const QByteArray data = pattern(2 * 1024 * 1024, 5);
        qsizetype off = 0;
        QVERIFY(QTest::qWaitFor([&] {
            pump(*p.connA, data, off);
            return sb.got.size() == data.size();
        }, 60000));
        QCOMPARE(sb.got, data);
    }

    void handshakeResendRepeatsTheInitial()
    {
        Pair p;
        p.ioA.obfuscate = p.ioB.obfuscate = false;   // plain: resends must be byte-identical
        p.ioA.loss = 1.0;                            // the punch is not open yet
        b_expect(p);
        QVERIFY(p.a.dial(p.epB, p.hB));
        QTest::qWait(700);
        std::vector<QByteArray> quic;
        for (const auto& f : p.ioA.sent)
            if (f.type == OP_NATT_FRAME_QUIC)
                quic.push_back(f.bytes);
        QVERIFY(quic.size() >= 3);
        for (const auto& q : quic)
            QCOMPARE(q, quic.front());
        // a resend alone must open the connection
        p.ioA.loss = 0.0;
        QVERIFY(QTest::qWaitFor([&] { return p.connA && p.connB; }, 5000));
    }

    void unexpectedInitialIsDropped()
    {
        Pair p;
        QVERIFY(p.a.dial(p.epB, p.hB));   // B armed nothing
        QTest::qWait(800);
        QCOMPARE(p.b.connectionCount(), size_t(0));
        QVERIFY(!p.connA);
    }

    void plainFallbackWhenPeerCannotDecrypt()
    {
        Pair p;
        // A keys its obfuscation with an address B does not see: B cannot decrypt A
        p.ioA.claimedPublic = Address::fromString(QStringLiteral("203.0.113.77"));
        QVERIFY(p.connect(15000));
        QVERIFY(p.ioB.undecryptable > 0);
        bool plainSent = false;
        for (const auto& f : p.ioA.sent)
            plainSent |= !f.obfuscated;
        QVERIFY(plainSent);
    }

    void strictNeverSendsPlain()
    {
        Pair p;
        p.ioA.claimedPublic = Address::fromString(QStringLiteral("203.0.113.77"));
        p.ioA.strict = true;
        b_expect(p);
        QVERIFY(p.a.dial(p.epB, p.hB));
        QTest::qWait(3500);
        QVERIFY(!p.connB);
        for (const auto& f : p.ioA.sent)
            QVERIFY(f.obfuscated);
    }

    void strictWithoutObfuscationSendsNothing()
    {
        Pair p;
        p.ioA.strict = true;
        p.ioA.obfuscate = false;
        b_expect(p);
        p.a.dial(p.epB, p.hB);
        QTest::qWait(300);
        QVERIFY(p.ioA.sent.empty());
    }

    void capsAreAnswered()
    {
        Pair p;
        Caps got;
        bool gotAck = false;
        p.a.onCaps = [&](const Endpoint&, const Caps& c, bool ack) { got = c; gotAck = ack; };
        p.a.learnPeerHash(p.epB, p.hB);
        p.a.sendCaps(p.epB, p.hB, hashOf(50), false);
        QVERIFY(QTest::qWaitFor([&] { return gotAck; }, 2000));
        QCOMPARE(got.sender, p.hB);
        QCOMPARE(got.expected, p.hA);
        QCOMPARE(got.file, hashOf(50));
        QVERIFY(got.options & kCapsQuic);
    }

    void closeEndsThePeer()
    {
        Pair p;
        QVERIFY(p.connect());
        Sink sb;
        sb.conn = p.connB;
        p.connB->setSink(&sb);
        p.connA->close();
        QVERIFY(QTest::qWaitFor([&] { return sb.lost || p.connB->finished(); }, 5000));
    }

private:
    static void b_expect(Pair& p)
    {
        p.b.expect(p.epA, p.hA);
        p.b.learnPeerHash(p.epA, p.hA);
        p.a.learnPeerHash(p.epB, p.hB);
    }
};

QTEST_GUILESS_MAIN(tst_QuicNatTransport)
#include "tst_QuicNatTransport.moc"
