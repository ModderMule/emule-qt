/// @file tst_BandwidthSplit.cpp
/// @brief The global limits divided between eD2K, Usenet and BitTorrent
///        (planDownload/planUpload, transfer/BandwidthSplit).
///
/// Pure arithmetic. Priority eD2K > Usenet > BitTorrent: BitTorrent keeps its share
/// only while the others are busy, and takes the whole line when they are idle.

#include "TestHelpers.h"

#include "transfer/BandwidthSplit.h"

#include <QTest>

#include <algorithm>

using namespace eMule;

namespace {

constexpr qint64 kCeiling = 4000;   // KB/s
constexpr qint64 kKB = 1024;

EngineDemand idle() { return {}; }
EngineDemand busy(qint64 kbPerSec) { return {true, kbPerSec * kKB, kbPerSec * kKB}; }

/// Where the engines settle when each draws min(appetite, its cap) and the plan is
/// recomputed from what they drew. Appetite 0 = idle engine.
struct Settled {
    DownloadPlan plan;
    qint64 ed2kKb = 0;
    qint64 usenetKb = 0;
    qint64 btKb = 0;
};

Settled settleDownload(qint64 ed2kWant, qint64 usenetWant, qint64 btWant, int usenetShare = 50,
                       int btShare = 25)
{
    Settled s;
    DownloadPlanState state;
    for (int round = 0; round < 16; ++round) {
        const EngineDemand ed2k{ed2kWant > 0, s.ed2kKb * kKB, s.ed2kKb * kKB};
        const EngineDemand usenet{usenetWant > 0, s.usenetKb * kKB, s.usenetKb * kKB};
        const EngineDemand bt{btWant > 0, s.btKb * kKB, s.btKb * kKB};
        s.plan = planDownload(kCeiling, usenetShare, btShare, ed2k, usenet, bt, &state);
        const qint64 ed2kCap = s.plan.ed2kBudgetKb >= 0 ? s.plan.ed2kBudgetKb : kCeiling;
        s.ed2kKb = std::min(ed2kWant, ed2kCap);
        s.usenetKb = std::min(usenetWant, s.plan.usenetCapKb);
        s.btKb = std::min(btWant, s.plan.btCapKb);
    }
    return s;
}

} // namespace

class tst_BandwidthSplit : public QObject {
    Q_OBJECT

private slots:
    void unlimitedDividesNothing();
    void combineMergesDemand();
    void btIdleLeavesEd2kAndUsenetAlone();
    void btBorrowsTheWholeLineWhenOthersIdle();
    void allSaturatedHoldTheShares();
    void ed2kAloneRunsFree();
    void btYieldsToEd2kButBorrowsWhatItLeaves();
    void capsNeverZeroOnATinyLine();
    void uploadUnlimitedOrBtIdleHasNoBudget();
    void uploadSaturatedHoldsTheShare();
    void uploadBtBorrowsIdleLine();
};

void tst_BandwidthSplit::unlimitedDividesNothing()
{
    const DownloadPlan p = planDownload(0, 50, 25, busy(500), busy(500), busy(500));
    QCOMPARE(p, DownloadPlan{});
    QCOMPARE(p.ed2kBudgetKb, -1);
    QCOMPARE(p.btCapKb, 0);
}

void tst_BandwidthSplit::combineMergesDemand()
{
    const EngineDemand c = EngineDemand::combine(idle(), busy(10));
    QVERIFY(c.active);
    QCOMPARE(c.rateBytesPerSec, 10 * kKB);
    QVERIFY(!EngineDemand::combine(idle(), idle()).active);
}

void tst_BandwidthSplit::btIdleLeavesEd2kAndUsenetAlone()
{
    // Same as the two-engine split on the whole line
    const DownloadPlan three = planDownload(kCeiling, 50, 25, busy(800), busy(800), idle());
    const PairSplit two = computePairSplit(kCeiling, 50, busy(800), busy(800));
    QCOMPARE(three.ed2kBudgetKb, two.firstCapKb);
    QCOMPARE(three.usenetCapKb, two.secondCapKb);
}

void tst_BandwidthSplit::btBorrowsTheWholeLineWhenOthersIdle()
{
    const Settled s = settleDownload(0, 0, 100000);
    QCOMPARE(s.plan.btCapKb, kCeiling);
    QCOMPARE(s.btKb, kCeiling);
    // idle eD2K is held at the line less BitTorrent's reserve, back within ~2 s
    QCOMPARE(s.plan.ed2kBudgetKb, kCeiling - kCeiling / 4);
}

void tst_BandwidthSplit::allSaturatedHoldTheShares()
{
    const Settled s = settleDownload(100000, 100000, 100000);
    // BitTorrent at its 25 %, the other two halve the rest
    QCOMPARE(s.btKb, kCeiling / 4);
    QCOMPARE(s.ed2kKb + s.usenetKb + s.btKb, kCeiling);
    QVERIFY(std::abs(s.ed2kKb - s.usenetKb) <= 1);
}

void tst_BandwidthSplit::ed2kAloneRunsFree()
{
    const DownloadPlan p = planDownload(kCeiling, 50, 25, busy(3000), idle(), idle());
    QCOMPARE(p.ed2kBudgetKb, -1);
}

void tst_BandwidthSplit::btYieldsToEd2kButBorrowsWhatItLeaves()
{
    // eD2K wants 1000 of 4000: BitTorrent gets the other ~3000, not just its 25 %
    const Settled s = settleDownload(1000, 0, 100000);
    QCOMPARE(s.ed2kKb, 1000);
    QVERIFY2(s.btKb >= kCeiling - 1000 - 1000 / 4 - 1, qPrintable(QString::number(s.btKb)));
    QVERIFY(s.ed2kKb + s.btKb <= kCeiling);
    // eD2K hungry: BitTorrent pushed back to its share
    const Settled hungry = settleDownload(100000, 0, 100000);
    QCOMPARE(hungry.btKb, kCeiling / 4);
    QCOMPARE(hungry.ed2kKb, kCeiling - kCeiling / 4);
}

void tst_BandwidthSplit::capsNeverZeroOnATinyLine()
{
    for (qint64 ceiling : {1, 2, 3}) {
        const DownloadPlan p = planDownload(ceiling, 50, 25, busy(100), busy(100), busy(100));
        QVERIFY(p.ed2kBudgetKb >= 1);
        QVERIFY(p.usenetCapKb >= 1);
        QVERIFY(p.btCapKb >= 1);
        const UploadPlan u = planUpload(ceiling, 25, busy(100), busy(100));
        QVERIFY(u.ed2kBudgetKb >= 1);
        QVERIFY(u.btCapKb >= 1);
    }
}

void tst_BandwidthSplit::uploadUnlimitedOrBtIdleHasNoBudget()
{
    QCOMPARE(planUpload(0, 25, busy(100), busy(100)), UploadPlan{});
    const UploadPlan p = planUpload(1000, 25, busy(900), idle());
    QCOMPARE(p.ed2kBudgetKb, -1);
}

void tst_BandwidthSplit::uploadSaturatedHoldsTheShare()
{
    PairState state;
    qint64 ed2k = 0, bt = 0;
    UploadPlan p;
    for (int round = 0; round < 16; ++round) {
        p = planUpload(1000, 25, {true, ed2k * kKB, ed2k * kKB}, {true, bt * kKB, bt * kKB}, &state);
        ed2k = p.ed2kBudgetKb;
        bt = p.btCapKb;
    }
    QCOMPARE(bt, 250);
    QCOMPARE(ed2k, 750);
}

void tst_BandwidthSplit::uploadBtBorrowsIdleLine()
{
    const UploadPlan p = planUpload(1000, 25, idle(), busy(10));
    QCOMPARE(p.btCapKb, 1000);
}

QTEST_GUILESS_MAIN(tst_BandwidthSplit)
#include "tst_BandwidthSplit.moc"
