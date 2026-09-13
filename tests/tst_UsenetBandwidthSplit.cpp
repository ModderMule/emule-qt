/// @file tst_UsenetBandwidthSplit.cpp
/// @brief How maxDownload() is divided between ED2K and Usenet, and the rate
///        window that tells the split what Usenet actually uses.
///
/// No queue, no daemon: UsenetSession::computeDownloadSplit() is pure, so the
/// split cases are arithmetic. The regression this exists for is the one
/// the split shipped with — lending went one way only, so Usenet stayed pinned
/// to its share with ED2K idle and half the line unused.

#include "TestHelpers.h"

#include "UsenetSession.h"
#include "queue/UsenetQueue.h"

#include <QTest>

#include <algorithm>

using namespace eMule;
using namespace eMule::usenet;

namespace {

using Split = UsenetSession::DownloadSplit;
using Demand = UsenetSession::EngineDemand;

constexpr uint32 kCeiling = 3500;   // KB/s
constexpr qint64 kKB = 1024;

Demand idle() { return {}; }
Demand busy(qint64 kbPerSec) { return {true, kbPerSec * kKB}; }

Split split(Demand usenet, Demand ed2k, int percent = 50, uint32 ceiling = kCeiling)
{
    return UsenetSession::computeDownloadSplit(ceiling, percent, usenet, ed2k);
}

/// Where the two engines settle when each draws min(appetite, its cap) and the
/// split is recomputed from what they drew. An appetite of 0 is an idle engine.
struct Settled {
    Split split;
    qint64 usenetKb = 0;    ///< drawn
    qint64 ed2kKb = 0;      ///< drawn
    bool stable = false;    ///< the last round reproduced the one before it
};

Settled settle(qint64 usenetAppetiteKb, qint64 ed2kAppetiteKb, int percent = 50)
{
    Settled s;
    Split previous;
    for (int round = 0; round < 12; ++round) {
        const Demand usenet{usenetAppetiteKb > 0, s.usenetKb * kKB};
        const Demand ed2k{ed2kAppetiteKb > 0, s.ed2kKb * kKB};
        previous = s.split;
        s.split = UsenetSession::computeDownloadSplit(kCeiling, percent, usenet, ed2k);
        s.usenetKb = std::min(usenetAppetiteKb, s.split.usenetKb());
        s.ed2kKb = std::min(ed2kAppetiteKb, s.split.ed2kKb());
    }
    s.stable = s.split == previous;
    return s;
}

} // namespace

class tst_UsenetBandwidthSplit : public QObject {
    Q_OBJECT

private slots:
    void unlimitedCeilingDividesNothing();
    void neitherEngineBusyThrottlesNothing();
    void usenetIsNeverUnlimitedUnderACeiling();
    void bothSaturatedHoldTheConfiguredShare();
    void idleEd2kLendsUsenetTheWholeLine();
    void slowEd2kLendsUsenetWhatItLeaves();
    void slowUsenetLendsEd2kWhatItLeaves();
    void idleUsenetLeavesEd2kTheWholeLine();
    void aBusyEngineKeepsAQuarterOfItsFloor();
    void shareIsClampedAtBothEnds();
    void aOneKilobyteCeilingStillCapsBoth();
    void settlesWithoutOscillating_data();
    void settlesWithoutOscillating();

    void rateWindowStartsAtZero();
    void rateWindowSpreadsAnArticleBurst();
    void rateWindowIsNotDilutedWhileWarmingUp();
    void rateWindowSlidesAndClears();
};

// ---------------------------------------------------------------------------
// The split
// ---------------------------------------------------------------------------

void tst_UsenetBandwidthSplit::unlimitedCeilingDividesNothing()
{
    const Split s = split(busy(5000), busy(5000), 50, 0);
    QCOMPARE(s.usenetLimitBytes, 0);
    QCOMPARE(s.ed2kBudgetKb, -1);
    QVERIFY(!s.isThrottling());
}

void tst_UsenetBandwidthSplit::neitherEngineBusyThrottlesNothing()
{
    const Split s = split(idle(), idle());
    QCOMPARE(s.ed2kBudgetKb, -1);
    QCOMPARE(s.usenetKb(), qint64(kCeiling));
    QVERIFY(!s.isThrottling());
}

void tst_UsenetBandwidthSplit::usenetIsNeverUnlimitedUnderACeiling()
{
    // Usenet's 0 means unlimited, so publishing it under a ceiling would let the
    // engine ignore maxDownload outright. ED2K's -1 is safe: it reads as the
    // ceiling itself.
    const Demand demands[] = {idle(), busy(0), busy(50), busy(1750), busy(9000)};
    for (const Demand& u : demands) {
        for (const Demand& e : demands) {
            for (int percent : {1, 30, 50, 99}) {
                const Split s = split(u, e, percent);
                QVERIFY(s.usenetLimitBytes > 0);
                QVERIFY(s.usenetKb() <= kCeiling);
                QVERIFY(s.ed2kBudgetKb == -1
                        || (s.ed2kBudgetKb >= 1 && s.ed2kBudgetKb <= kCeiling));
            }
        }
    }
}

void tst_UsenetBandwidthSplit::bothSaturatedHoldTheConfiguredShare()
{
    Split s = split(busy(1750), busy(1750));
    QCOMPARE(s.usenetKb(), 1750);
    QCOMPARE(s.ed2kKb(), 1750);

    // Appetite beyond the floor changes nothing: the floor is the ceiling on
    // what either may reserve.
    s = split(busy(9000), busy(9000));
    QCOMPARE(s.usenetKb(), 1750);
    QCOMPARE(s.ed2kKb(), 1750);
    QCOMPARE(s.usenetKb() + s.ed2kKb(), qint64(kCeiling));

    s = split(busy(1050), busy(2450), 30);
    QCOMPARE(s.usenetKb(), 1050);
    QCOMPARE(s.ed2kKb(), 2450);
}

void tst_UsenetBandwidthSplit::idleEd2kLendsUsenetTheWholeLine()
{
    // The regression. Before, Usenet was pinned to 1750 here with nothing else
    // on the line.
    const Split s = split(busy(3500), idle());
    QCOMPARE(s.usenetKb(), qint64(kCeiling));

    // ED2K keeps its floor available, so a download starting there is not held
    // at a trickle until the next tick.
    QCOMPARE(s.ed2kKb(), 1750);
}

void tst_UsenetBandwidthSplit::slowEd2kLendsUsenetWhatItLeaves()
{
    // Using 800 reserves 1000 — the quarter headroom — and Usenet has the rest.
    Split s = split(busy(3500), busy(800));
    QCOMPARE(s.usenetKb(), 2500);
    QCOMPARE(s.ed2kKb(), 1750);

    // Below a quarter of its floor the quarter is what it keeps.
    s = split(busy(3500), busy(50));
    QCOMPARE(s.usenetKb(), 3500 - 1750 / 4);
}

void tst_UsenetBandwidthSplit::slowUsenetLendsEd2kWhatItLeaves()
{
    // The direction that already worked; kept so the rewrite cannot lose it.
    Split s = split(busy(1000), busy(3500));
    QCOMPARE(s.ed2kKb(), 3500 - 1250);
    QCOMPARE(s.usenetKb(), 1750);

    s = split(busy(100), busy(3500));
    QCOMPARE(s.ed2kKb(), 3500 - 1750 / 4);
}

void tst_UsenetBandwidthSplit::idleUsenetLeavesEd2kTheWholeLine()
{
    const Split s = split(idle(), busy(3500));
    QCOMPARE(s.ed2kBudgetKb, -1);   // cleared, so ED2K tracks maxDownload live
    QCOMPARE(s.ed2kKb(), qint64(kCeiling));
    QCOMPARE(s.usenetKb(), 1750);
}

void tst_UsenetBandwidthSplit::aBusyEngineKeepsAQuarterOfItsFloor()
{
    // Both busy, both momentarily at zero — between articles, sources still
    // queueing. Neither may be squeezed below a quarter of its floor.
    const Split s = split(busy(0), busy(0));
    QCOMPARE(s.usenetKb(), 3500 - 1750 / 4);
    QCOMPARE(s.ed2kKb(), 3500 - 1750 / 4);

    for (int percent : {1, 20, 50, 80, 99}) {
        const qint64 usenetFloor = qint64(kCeiling) * percent / 100;
        const qint64 ed2kFloor = kCeiling - usenetFloor;
        const Split hungryUsenet = split(busy(9000), busy(0), percent);
        const Split hungryEd2k = split(busy(0), busy(9000), percent);
        QVERIFY2(hungryUsenet.usenetKb() <= kCeiling - std::max<qint64>(1, ed2kFloor / 4),
                 qPrintable(QString::number(percent)));
        QVERIFY2(hungryEd2k.ed2kKb() <= kCeiling - std::max<qint64>(1, usenetFloor / 4),
                 qPrintable(QString::number(percent)));
    }
}

void tst_UsenetBandwidthSplit::shareIsClampedAtBothEnds()
{
    QCOMPARE(split(busy(9000), busy(9000), 0), split(busy(9000), busy(9000), 1));
    QCOMPARE(split(busy(9000), busy(9000), 150), split(busy(9000), busy(9000), 99));

    const Split low = split(busy(9000), busy(9000), 1);
    QCOMPARE(low.usenetKb(), 35);
    QCOMPARE(low.ed2kKb(), 3465);

    const Split high = split(busy(9000), busy(9000), 99);
    QCOMPARE(high.usenetKb(), 3465);
    QCOMPARE(high.ed2kKb(), 35);
}

void tst_UsenetBandwidthSplit::aOneKilobyteCeilingStillCapsBoth()
{
    // Every cap rounds up to 1: 0 reads as unlimited to both consumers.
    const Split s = split(busy(9000), busy(9000), 50, 1);
    QCOMPARE(s.usenetLimitBytes, kKB);
    QCOMPARE(s.ed2kBudgetKb, 1);
}

void tst_UsenetBandwidthSplit::settlesWithoutOscillating_data()
{
    QTest::addColumn<qint64>("usenetAppetite");
    QTest::addColumn<qint64>("ed2kAppetite");
    QTest::addColumn<qint64>("usenetCap");
    QTest::addColumn<qint64>("ed2kCap");

    // Appetites in KB/s; 0 is an idle engine.
    QTest::newRow("both hungry")      << qint64(9000) << qint64(9000) << qint64(1750) << qint64(1750);
    QTest::newRow("eD2K idle")        << qint64(9000) << qint64(0)    << qint64(3500) << qint64(1750);
    QTest::newRow("eD2K slow")        << qint64(9000) << qint64(800)  << qint64(2500) << qint64(1750);
    QTest::newRow("Usenet near share") << qint64(1500) << qint64(9000) << qint64(1750) << qint64(1750);
    QTest::newRow("Usenet slow")      << qint64(300)  << qint64(9000) << qint64(1750) << qint64(3063);
    QTest::newRow("Usenet idle")      << qint64(0)    << qint64(9000) << qint64(1750) << qint64(3500);
}

void tst_UsenetBandwidthSplit::settlesWithoutOscillating()
{
    QFETCH(qint64, usenetAppetite);
    QFETCH(qint64, ed2kAppetite);
    QFETCH(qint64, usenetCap);
    QFETCH(qint64, ed2kCap);

    const Settled s = settle(usenetAppetite, ed2kAppetite);
    QVERIFY2(s.stable, "the split was still moving after twelve rounds");
    QCOMPARE(s.split.usenetKb(), usenetCap);
    QCOMPARE(s.split.ed2kKb(), ed2kCap);

    // Settled, the line is never oversubscribed — only the round after an
    // engine wakes may briefly be.
    QVERIFY(s.usenetKb + s.ed2kKb <= kCeiling);

    // The share is a floor: an engine that wants its share gets it.
    QVERIFY(s.usenetKb >= std::min<qint64>(usenetAppetite, 1750));
    QVERIFY(s.ed2kKb >= std::min<qint64>(ed2kAppetite, 1750));
}

// ---------------------------------------------------------------------------
// The rate window
// ---------------------------------------------------------------------------

void tst_UsenetBandwidthSplit::rateWindowStartsAtZero()
{
    TickRateWindow w;
    QCOMPARE(w.bytesPerSecond(), 0);
}

void tst_UsenetBandwidthSplit::rateWindowSpreadsAnArticleBurst()
{
    // 750 KB arriving once a second, on a 250 ms tick — the shape a token bucket
    // with a burst allowance produces. A one-tick reading says 3 MB/s or
    // nothing; the split must see 750 KB/s.
    TickRateWindow w;
    for (int second = 0; second < 2; ++second) {
        w.push(0, 250);
        w.push(0, 250);
        w.push(0, 250);
        w.push(750 * kKB, 250);
    }
    QCOMPARE(w.bytesPerSecond(), 750 * kKB);

    // And a quiet tick right after a burst does not read as idle.
    w.push(0, 250);
    QVERIFY(w.bytesPerSecond() > 500 * kKB);
}

void tst_UsenetBandwidthSplit::rateWindowIsNotDilutedWhileWarmingUp()
{
    TickRateWindow w;
    w.push(100 * kKB, 250);
    QCOMPARE(w.bytesPerSecond(), 400 * kKB);
}

void tst_UsenetBandwidthSplit::rateWindowSlidesAndClears()
{
    TickRateWindow w;
    for (int i = 0; i < TickRateWindow::kTicks; ++i)
        w.push(80 * kKB, 250);
    QCOMPARE(w.bytesPerSecond(), 320 * kKB);

    // One old tick drops out for each new one.
    w.push(0, 250);
    QCOMPARE(w.bytesPerSecond(), 280 * kKB);

    for (int i = 0; i < TickRateWindow::kTicks; ++i)
        w.push(0, 250);
    QCOMPARE(w.bytesPerSecond(), 0);

    w.push(80 * kKB, 250);
    w.clear();
    QCOMPARE(w.bytesPerSecond(), 0);
}

QTEST_MAIN(tst_UsenetBandwidthSplit)
#include "tst_UsenetBandwidthSplit.moc"
