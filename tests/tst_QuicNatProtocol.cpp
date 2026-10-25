/// @file tst_QuicNatProtocol.cpp
/// @brief EAQN1 proof and EAQC caps codecs of the QUIC NAT-T dialect.

#include "net/quic/QuicNatProtocol.h"

#include <QTest>

using namespace eMule;
using namespace eMule::quic;

namespace {
Hash16 h(uint8 v)
{
    Hash16 x{};
    x.fill(v);
    x[3] = 0x0E;
    return x;
}
} // namespace

class tst_QuicNatProtocol : public QObject {
    Q_OBJECT

private slots:
    void proofLayout()
    {
        const auto p = encodeProof(h(1), h(2));
        QCOMPARE(p.size(), size_t(37));
        QCOMPARE(QByteArray(reinterpret_cast<const char*>(p.data()), 5), QByteArray("EAQN1"));
        QCOMPARE(p[5], uint8(1));
        QCOMPARE(p[21], uint8(2));
    }

    void proofAcceptsUsOrZeroTarget()
    {
        const Hash16 us = h(9), them = h(4);
        auto p = encodeProof(them, us);
        QVERIFY(checkProof(p.data(), p.size(), us, them).ok);
        p = encodeProof(them, Hash16{});
        auto c = checkProof(p.data(), p.size(), us, them);
        QVERIFY(c.ok);
        QCOMPARE(c.sender, them);
        // expected unknown: any sender
        QVERIFY(checkProof(p.data(), p.size(), us, Hash16{}).ok);
    }

    void proofRejects()
    {
        const Hash16 us = h(9), them = h(4);
        auto p = encodeProof(them, h(7));                 // names someone else
        QVERIFY(!checkProof(p.data(), p.size(), us, them).ok);
        p = encodeProof(h(5), us);                        // not who we expected
        QVERIFY(!checkProof(p.data(), p.size(), us, them).ok);
        p = encodeProof(them, us);
        p[0] = 'X';                                       // magic
        QVERIFY(!checkProof(p.data(), p.size(), us, them).ok);
        QVERIFY(!checkProof(p.data(), 36, us, them).ok);  // short
    }

    void capsRoundTrip()
    {
        Caps c;
        c.options = 0x43;
        c.sender = h(1);
        c.expected = h(2);
        c.file = h(3);
        const QByteArray w = encodeCaps(c);
        QCOMPARE(w.size(), qsizetype(54));
        QCOMPARE(w.left(5), QByteArray("EAQC\x01", 5));
        const auto d = decodeCaps(reinterpret_cast<const uint8*>(w.constData()), 54, h(2));
        QVERIFY(d.has_value());
        QCOMPARE(d->options, uint8(0x43));
        QCOMPARE(d->sender, h(1));
        QCOMPARE(d->file, h(3));
    }

    void capsRejects()
    {
        Caps c;
        c.expected = h(2);
        QByteArray w = encodeCaps(c);
        const auto* p = reinterpret_cast<const uint8*>(w.constData());
        QVERIFY(!decodeCaps(p, 54, h(8)));   // meant for another client
        QVERIFY(!decodeCaps(p, 53, h(2)));   // short
        w[4] = 2;
        QVERIFY(!decodeCaps(reinterpret_cast<const uint8*>(w.constData()), 54, h(2)));   // version
        c.expected = Hash16{};
        w = encodeCaps(c);
        QVERIFY(decodeCaps(reinterpret_cast<const uint8*>(w.constData()), 54, h(8)));   // zero = anyone
    }
};

QTEST_GUILESS_MAIN(tst_QuicNatProtocol)
#include "tst_QuicNatProtocol.moc"
