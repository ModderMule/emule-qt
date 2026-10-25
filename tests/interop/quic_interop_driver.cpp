/// @file quic_interop_driver.cpp
/// @brief Manual QUIC NAT-T interop driver: our real QuicNatEndpoint over localhost UDP
///        against an ngtcp2 + GnuTLS peer (docs/protocol/natt-quic.md).
///
/// Built only with -DEMULE_QUIC_INTEROP_TEST=ON (target quic_interop_driver, not in `all`).
/// Usage: quic_interop_driver dial|expect <localPort> <peerPort> <MiB>
/// Frames are plain `0xB2 <type> <payload>`. Hashes and the data pattern match the peer's.

#include "net/StreamTransport.h"
#include "net/quic/QuicNatConnection.h"
#include "net/quic/QuicNatEndpoint.h"
#include "net/quic/QuicNatIo.h"
#include "utils/Opcodes.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QNetworkDatagram>
#include <QTimer>
#include <QUdpSocket>
#include <QVariant>

#include <cstdio>
#include <memory>

using namespace eMule;
using namespace eMule::quic;

namespace {

const Hash16 kPeerHash = {0x11, 0x12, 0x13, 0x14, 0x15, 0x0e, 0x17, 0x18,
                          0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x6f, 0x20};
const Hash16 kOurHash = {0x21, 0x22, 0x23, 0x24, 0x25, 0x0e, 0x27, 0x28,
                         0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x6f, 0x30};
constexpr uint8 kSeedClientToServer = 0xC1;
constexpr uint8 kSeedServerToClient = 0x5E;

inline uint8 patternByte(uint8 seed, uint64 i)
{
    uint32 x = static_cast<uint32>(i) * 2654435761u ^ (static_cast<uint32>(i >> 32) * 40503u) ^ seed;
    x ^= x >> 15;
    x *= 0x2c1b3c6du;
    x ^= x >> 12;
    return static_cast<uint8>(x);
}

double sinceStart()
{
    static QElapsedTimer t = [] { QElapsedTimer e; e.start(); return e; }();
    return static_cast<double>(t.nsecsElapsed()) / 1e9;
}
#define say(...) (std::fprintf(stderr, "[ours %.3f] ", sinceStart()), std::fprintf(stderr, __VA_ARGS__), \
                  std::fputc('\n', stderr))

/// Client UDP socket stand-in: plain frames only.
class UdpIo : public QuicNatIo {
public:
    explicit UdpIo(QUdpSocket& s, Endpoint self) : m_sock(s), m_self(self) { m_clock.start(); }

    [[nodiscard]] Hash16 ourHash() const override { return kOurHash; }
    [[nodiscard]] uint64 nowMs() const override { return static_cast<uint64>(m_clock.elapsed()); }
    [[nodiscard]] bool canObfuscate(const Endpoint&) const override { return false; }
    [[nodiscard]] bool strictObfuscation() const override { return false; }
    [[nodiscard]] bool admit(const Address&) const override { return true; }
    [[nodiscard]] uint8 connectOptions() const override { return 0x03; }
    [[nodiscard]] Endpoint localEndpoint(const Endpoint&) const override { return m_self; }

    void sendFrame(uint8 type, const uint8* payload, size_t len, const Endpoint& to, const Hash16* cryptHash) override
    {
        Q_UNUSED(cryptHash);   // canObfuscate() is false: always null here
        QByteArray d;
        d.reserve(static_cast<qsizetype>(len + 2));
        d.append(static_cast<char>(OP_UDPRESERVEDPROT2));
        d.append(static_cast<char>(type));
        d.append(reinterpret_cast<const char*>(payload), static_cast<qsizetype>(len));
        m_sock.writeDatagram(d, to.address().toQHostAddress(), to.port());
        ++sent;
    }

    uint64 sent = 0;

private:
    QUdpSocket& m_sock;
    Endpoint m_self;
    QElapsedTimer m_clock;
};

class Exchange : public StreamTransportSink {
public:
    std::shared_ptr<QuicNatConnection> conn;
    uint64 total = 0;
    uint64 sent = 0, recv = 0, bad = 0, written = 0;
    uint8 sendSeed = 0, recvSeed = 0;
    bool lost = false;
    QString lostReason;
    QElapsedTimer t;

    void transportReadable() override
    {
        char buf[65536];
        qint64 n;
        while ((n = conn->read(buf, sizeof(buf))) > 0) {
            for (qint64 i = 0; i < n; ++i)
                if (static_cast<uint8>(buf[i]) != patternByte(recvSeed, recv + static_cast<uint64>(i)))
                    ++bad;
            recv += static_cast<uint64>(n);
        }
    }
    void transportWritten(qint64 bytes) override
    {
        written += static_cast<uint64>(bytes);
        pump();
    }
    void transportLost(QAbstractSocket::SocketError, const QString& reason) override
    {
        lost = true;
        lostReason = reason;
    }

    void pump()
    {
        if (!conn)
            return;
        char chunk[64 * 1024];
        while (sent < total) {
            const auto n = static_cast<qint64>(std::min<uint64>(sizeof(chunk), total - sent));
            for (qint64 i = 0; i < n; ++i)
                chunk[i] = static_cast<char>(patternByte(sendSeed, sent + static_cast<uint64>(i)));
            const qint64 w = conn->write(chunk, n);
            if (w <= 0)
                return;
            sent += static_cast<uint64>(w);
        }
    }

    [[nodiscard]] bool done() const { return recv >= total && sent >= total && conn && conn->pendingSend() == 0; }
};

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (argc < 5) {
        std::fprintf(stderr, "usage: %s dial|expect localPort peerPort MiB\n", argv[0]);
        return 2;
    }
    const bool dial = qstrcmp(argv[1], "dial") == 0;
    const auto localPort = static_cast<uint16>(QByteArray(argv[2]).toUInt());
    const auto peerPort = static_cast<uint16>(QByteArray(argv[3]).toUInt());
    const auto total = static_cast<uint64>(QByteArray(argv[4]).toDouble() * 1024 * 1024);

    if (!QuicNatEndpoint::available()) {
        say("QUIC NAT-T not available");
        return 1;
    }

    QUdpSocket sock;
    if (!sock.bind(QHostAddress::LocalHost, localPort)) {
        say("bind failed: %s", qPrintable(sock.errorString()));
        return 1;
    }
    sock.setSocketOption(QAbstractSocket::ReceiveBufferSizeSocketOption, 4 * 1024 * 1024);
    sock.setSocketOption(QAbstractSocket::SendBufferSizeSocketOption, 4 * 1024 * 1024);
    const Address lo = Address::fromString(QStringLiteral("127.0.0.1"));
    const Endpoint self{lo, localPort};
    const Endpoint peer{lo, peerPort};
    UdpIo io(sock, self);
    QuicNatEndpoint ep(io);

    uint64 recvDgrams = 0;
    QObject::connect(&sock, &QUdpSocket::readyRead, &ep, [&] {
        while (sock.hasPendingDatagrams()) {
            const QNetworkDatagram d = sock.receiveDatagram(6000);
            const QByteArray b = d.data();
            if (b.size() < 2 || static_cast<uint8>(b[0]) != OP_UDPRESERVEDPROT2)
                continue;
            ++recvDgrams;
            const Endpoint from{Address::fromQHostAddress(d.senderAddress()), static_cast<uint16>(d.senderPort())};
            ep.handleFrame(static_cast<uint8>(b[1]), reinterpret_cast<const uint8*>(b.constData() + 2),
                           static_cast<size_t>(b.size() - 2), from, false);
        }
    });

    Exchange ex;
    ex.total = total;
    ex.sendSeed = dial ? kSeedClientToServer : kSeedServerToClient;
    ex.recvSeed = dial ? kSeedServerToClient : kSeedClientToServer;
    QString failed;
    QElapsedTimer startT;
    startT.start();

    ep.onEstablished = [&](std::shared_ptr<QuicNatConnection> c, bool outgoing) {
        say("established (%s) in %lld ms, peer hash ok=%d", outgoing ? "outgoing" : "incoming",
            static_cast<long long>(startT.elapsed()), c->peerHash() == kPeerHash);
        ex.conn = std::move(c);
        ex.conn->setSink(&ex);
        ex.conn->setSendBufferLimit(1024 * 1024);
        ex.t.start();
        ex.transportReadable();
        ex.pump();
    };
    ep.onFailed = [&](const Endpoint&, const Hash16&, const QString& reason) {
        failed = reason;
        say("failed: %s", qPrintable(reason));
    };

    if (dial) {
        if (!ep.dial(peer, kPeerHash)) {
            say("dial refused");
            return 1;
        }
        say("dialled %s", qPrintable(peer.toString()));
    } else {
        ep.expect(peer, kPeerHash);
        say("expecting %s", qPrintable(peer.toString()));
    }

    int rc = 1;
    qint64 doneAt = -1;
    QTimer poll;
    poll.setInterval(5);
    QObject::connect(&poll, &QTimer::timeout, [&] {
        ex.pump();
        if (doneAt < 0 && ex.done()) {
            doneAt = startT.elapsed();
            const double s = static_cast<double>(ex.t.nsecsElapsed()) / 1e9;
            say("DONE: sent %llu recv %llu bad=%llu in %.3f s (%.1f MiB/s each way) dgrams tx=%llu rx=%llu",
                static_cast<unsigned long long>(ex.sent), static_cast<unsigned long long>(ex.recv),
                static_cast<unsigned long long>(ex.bad), s, static_cast<double>(total) / 1048576.0 / s,
                static_cast<unsigned long long>(io.sent), static_cast<unsigned long long>(recvDgrams));
            rc = ex.bad == 0 ? 0 : 3;
        }
        const bool give_up = !failed.isEmpty() || (ex.lost && doneAt < 0) || startT.elapsed() > 120000;
        if (give_up || (doneAt >= 0 && startT.elapsed() - doneAt > 1500)) {
            if (doneAt < 0)
                say("FAIL: %s sent=%llu recv=%llu bad=%llu pending=%lld lost=%s",
                    qPrintable(failed.isEmpty() ? QStringLiteral("timeout") : failed),
                    static_cast<unsigned long long>(ex.sent), static_cast<unsigned long long>(ex.recv),
                    static_cast<unsigned long long>(ex.bad), ex.conn ? static_cast<long long>(ex.conn->pendingSend()) : -1LL,
                    qPrintable(ex.lostReason));
            if (ex.conn)
                ex.conn->close();
            QTimer::singleShot(200, &app, [&app, &rc] { app.exit(rc); });
            poll.stop();
        }
    });
    poll.start();
    const int r = app.exec();
    std::printf("OURS %s %s recv=%llu bad=%llu\n", dial ? "dial" : "expect", r == 0 ? "PASS" : "FAIL",
                static_cast<unsigned long long>(ex.recv), static_cast<unsigned long long>(ex.bad));
    return r;
}
