/// @file tst_QueuePositions.cpp
/// @brief One estimated order over the eD2K and ed3k upload queues (transfer/QueuePositions):
///        the On Queue list's Position column.

#include "TestHelpers.h"

#include "transfer/QueuePositions.h"

#include <QTest>

using namespace eMule;

class tst_QueuePositions : public QObject {
    Q_OBJECT

private slots:
    void equalSlotsInterleave();
    void moreSlotsMoveFaster();
    void tiesGoToLongerWait();
    void unknownRankLast();
    void idleQueueCountsOneSlot();
};

void tst_QueuePositions::equalSlotsInterleave()
{
    // eD2K 1,2 and ed3k 1,2 at 3 slots each: the first of either, then the seconds
    const std::vector<QueueWaiter> w{{0, 1, 10}, {0, 2, 5}, {1, 1, 20}, {1, 2, 1}};
    const std::vector<int> slotCounts{3, 3};
    const auto pos = mergedQueuePositions(w, slotCounts);
    QCOMPARE(pos, (std::vector<int>{2, 3, 1, 4}));
}

void tst_QueuePositions::moreSlotsMoveFaster()
{
    // eD2K 4 slots, ed3k 1: eD2K ranks 1-4 come before ed3k's 2nd waiter
    std::vector<QueueWaiter> w;
    for (int r = 1; r <= 4; ++r)
        w.push_back({0, r, 0});
    w.push_back({1, 1, 0});
    w.push_back({1, 2, 0});
    const std::vector<int> slotCounts{4, 1};
    const auto pos = mergedQueuePositions(w, slotCounts);
    // turns: eD2K .25 .5 .75 1; ed3k 1 2 → ed3k 1 ties eD2K 4, equal wait keeps input order
    QCOMPARE(pos, (std::vector<int>{1, 2, 3, 4, 5, 6}));
}

void tst_QueuePositions::tiesGoToLongerWait()
{
    const std::vector<QueueWaiter> w{{0, 1, 100}, {1, 1, 900}};
    const std::vector<int> slotCounts{2, 2};
    QCOMPARE(mergedQueuePositions(w, slotCounts), (std::vector<int>{2, 1}));
}

void tst_QueuePositions::unknownRankLast()
{
    const std::vector<QueueWaiter> w{{0, 0, 999}, {0, 1, 0}, {0, 2, 0}};
    const std::vector<int> slotCounts{1};
    QCOMPARE(mergedQueuePositions(w, slotCounts), (std::vector<int>{3, 1, 2}));
}

void tst_QueuePositions::idleQueueCountsOneSlot()
{
    // ed3k holds no slot: its 1st waiter still counts as one turn away (ties eD2K 1, waited less)
    const std::vector<QueueWaiter> w{{0, 2, 0}, {1, 1, 0}, {0, 1, 50}};
    const std::vector<int> slotCounts{1, 0};
    QCOMPARE(mergedQueuePositions(w, slotCounts), (std::vector<int>{3, 2, 1}));
}

QTEST_GUILESS_MAIN(tst_QueuePositions)
#include "tst_QueuePositions.moc"
