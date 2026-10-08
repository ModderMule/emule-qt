/// @file tst_CardinalitySketch.cpp
/// @brief The distinct-count estimator behind the Kad node census.

#include "stats/CardinalitySketch.h"

#include <QBuffer>
#include <QTest>

#include <algorithm>
#include <cmath>
#include <utility>

using namespace eMule;

namespace {

constexpr uint64 kKey0 = 0x0123456789ABCDEFULL;
constexpr uint64 kKey1 = 0xFEDCBA9876543210ULL;

/// A sketch of the identifiers [from, to), hashed the way the census does.
CardinalitySketch sketchOf(uint64 from, uint64 to)
{
    CardinalitySketch s;
    for (uint64 i = from; i < to; ++i)
        s.add(CardinalitySketch::keyedHash(i, ~i, kKey0, kKey1));
    return s;
}

double relativeError(uint64 estimate, uint64 truth)
{
    return std::abs(static_cast<double>(estimate) - static_cast<double>(truth))
           / static_cast<double>(truth);
}

CardinalitySketch roundTrip(const CardinalitySketch& in, bool* ok = nullptr)
{
    QByteArray bytes;
    {
        QDataStream out(&bytes, QIODevice::WriteOnly);
        in.write(out);
    }
    QDataStream stream(bytes);
    CardinalitySketch back;
    const bool good = back.read(stream);
    if (ok)
        *ok = good;
    return back;
}

} // namespace

class tst_CardinalitySketch : public QObject {
    Q_OBJECT

private slots:
    void smallSetsAreExact();
    void estimateStaysWithinFivePercent_data();
    void estimateStaysWithinFivePercent();
    void duplicatesDoNotCount();
    void mergeIsTheUnion();
    void mergeIsIdempotent();
    void serialisationRoundTripsBothModes();
    void malformedInputIsRejected();
    void craftedIdentifiersDoNotInflateASaltedSketch();
};

void tst_CardinalitySketch::smallSetsAreExact()
{
    CardinalitySketch empty;
    QVERIFY(empty.isEmpty());
    QCOMPARE(empty.estimate(), uint64{0});

    for (const uint64 n : {uint64{1}, uint64{17}, uint64{CardinalitySketch::kSparseLimit}}) {
        const CardinalitySketch s = sketchOf(0, n);
        QVERIFY(s.isExact());
        QCOMPARE(s.estimate(), n);
    }
    // One more and it stops keeping the hashes — but must not jump.
    const CardinalitySketch dense = sketchOf(0, CardinalitySketch::kSparseLimit + 1);
    QVERIFY(!dense.isExact());
    QVERIFY(relativeError(dense.estimate(), CardinalitySketch::kSparseLimit + 1) < 0.05);
}

void tst_CardinalitySketch::estimateStaysWithinFivePercent_data()
{
    QTest::addColumn<quint64>("count");
    for (const quint64 n : {500ULL, 1'000ULL, 5'000ULL, 20'000ULL, 100'000ULL, 1'000'000ULL})
        QTest::addRow("%llu", n) << n;
}

// 4096 registers give a standard error of 1.6 %; 5 % is three of those.
void tst_CardinalitySketch::estimateStaysWithinFivePercent()
{
    QFETCH(quint64, count);
    const uint64 estimate = sketchOf(0, count).estimate();
    QVERIFY2(relativeError(estimate, count) < 0.05,
             qPrintable(QStringLiteral("%1 estimated as %2").arg(count).arg(estimate)));
}

void tst_CardinalitySketch::duplicatesDoNotCount()
{
    CardinalitySketch once = sketchOf(0, 10'000);
    CardinalitySketch thrice = sketchOf(0, 10'000);
    thrice.merge(sketchOf(0, 10'000));
    for (uint64 i = 0; i < 10'000; ++i)
        thrice.add(CardinalitySketch::keyedHash(i, ~i, kKey0, kKey1));
    QCOMPARE(thrice, once);
}

void tst_CardinalitySketch::mergeIsTheUnion()
{
    // Overlapping halves, across the sparse / dense boundary in every combination.
    const std::pair<uint64, uint64> sizes[] = {{100, 100}, {100, 5'000}, {5'000, 100}, {5'000, 5'000}};
    for (const auto& [a, b] : sizes) {
        CardinalitySketch merged = sketchOf(0, a);
        merged.merge(sketchOf(a / 2, a / 2 + b));
        QCOMPARE(merged, sketchOf(0, std::max(a, a / 2 + b)));
    }
}

// What lets "banked + session" be flushed absolutely, again and again.
void tst_CardinalitySketch::mergeIsIdempotent()
{
    const CardinalitySketch session = sketchOf(3'000, 9'000);
    CardinalitySketch banked = sketchOf(0, 5'000);
    banked.merge(session);
    const CardinalitySketch once = banked;
    banked.merge(session);
    banked.merge(once);
    QCOMPARE(banked, once);
    QCOMPARE(banked.estimate(), once.estimate());
}

void tst_CardinalitySketch::serialisationRoundTripsBothModes()
{
    for (const uint64 n : {uint64{0}, uint64{40}, uint64{CardinalitySketch::kSparseLimit},
                           uint64{30'000}}) {
        const CardinalitySketch s = sketchOf(0, n);
        bool ok = false;
        QCOMPARE(roundTrip(s, &ok), s);
        QVERIFY(ok);
    }
}

void tst_CardinalitySketch::malformedInputIsRejected()
{
    const auto readBytes = [](const QByteArray& bytes) {
        QDataStream in(bytes);
        CardinalitySketch s;
        const bool ok = s.read(in);
        return std::pair{ok, s.isEmpty()};
    };

    QCOMPARE(readBytes({}), (std::pair{false, true}));                          // nothing
    QCOMPARE(readBytes(QByteArray(1, '\x07')), (std::pair{false, true}));       // unknown mode
    QCOMPARE(readBytes(QByteArray("\x01", 1) + QByteArray(100, '\x01')),        // short registers
             (std::pair{false, true}));
    QCOMPARE(readBytes(QByteArray("\x01", 1)                                    // impossible rank
                       + QByteArray(int(CardinalitySketch::kRegisters), '\x7F')),
             (std::pair{false, true}));

    // A sparse list claiming more entries than the mode ever holds.
    QByteArray tooMany;
    {
        QDataStream out(&tooMany, QIODevice::WriteOnly);
        out << quint8{0} << quint16{60'000};
    }
    QCOMPARE(readBytes(tooMany), (std::pair{false, true}));
}

// Identifiers a peer picked to look like "very rare" hashes (long zero runs)
// would blow a sketch fed raw IDs up by orders of magnitude.
void tst_CardinalitySketch::craftedIdentifiersDoNotInflateASaltedSketch()
{
    // One per register is all it takes.
    constexpr uint64 kCrafted = CardinalitySketch::kRegisters;

    CardinalitySketch raw;
    CardinalitySketch salted;
    for (uint64 i = 0; i < kCrafted; ++i) {
        // Register i, with the maximum rank.
        const uint64 id = i << (64 - CardinalitySketch::kPrecision);
        raw.add(id);
        salted.add(CardinalitySketch::keyedHash(id, i, kKey0, kKey1));
    }
    QVERIFY(raw.estimate() > kCrafted * 1000);          // the attack works on raw IDs
    QVERIFY(relativeError(salted.estimate(), kCrafted) < 0.05);
}

QTEST_APPLESS_MAIN(tst_CardinalitySketch)
#include "tst_CardinalitySketch.moc"
