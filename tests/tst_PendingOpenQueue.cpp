/// @file tst_PendingOpenQueue.cpp
/// @brief What waits for the daemon on a cold start, and how it is let go.
///
/// A link clicked in a browser and a .nzb double-clicked in Finder both reach the
/// GUI before its IPC connection exists. ExternalLinkHandler cannot be linked into a
/// test binary — it reaches MainWindow — so the waiting rules live here, where the
/// three ways they go wrong can be pinned: an item let go twice (which prompts
/// twice), an item that comes back as the wrong kind (a path replayed as a link
/// reaches the eD2K importer, which refuses it, and the file is silently lost), and
/// an unbounded queue.

#include "app/PendingOpenQueue.h"

#include <QTest>

using namespace eMule;

class tst_PendingOpenQueue : public QObject {
    Q_OBJECT

private slots:
    void whatArrivesEarlyWaits();
    void whatWaitedIsReleasedInArrivalOrder();
    void theKindSurvivesTheWait();
    void aReleaseThatReEntersDoesNotReplayAnythingTwice();
    void aFloodIsCappedRatherThanUnbounded();
    void releasingAnEmptyQueueDoesNothing();
};

namespace {

/// Collect what a release hands out, as "kind:value" so both halves are asserted.
QStringList drain(PendingOpenQueue& queue)
{
    QStringList seen;
    queue.release([&seen](const PendingOpen& item) {
        seen << (item.isFile ? QStringLiteral("file:") : QStringLiteral("link:")) + item.value;
    });
    return seen;
}

} // namespace

void tst_PendingOpenQueue::whatArrivesEarlyWaits()
{
    PendingOpenQueue queue;
    QVERIFY(queue.isEmpty());

    QVERIFY(queue.push({QStringLiteral("/tmp/release.nzb"), true}));
    QVERIFY(!queue.isEmpty());
    QCOMPARE(queue.size(), qsizetype(1));
}

void tst_PendingOpenQueue::whatWaitedIsReleasedInArrivalOrder()
{
    PendingOpenQueue queue;
    queue.push({QStringLiteral("ed2k://|file|a|1|AA|/"), false});
    queue.push({QStringLiteral("/tmp/b.nzb"), true});
    queue.push({QStringLiteral("ed2k://|file|c|1|CC|/"), false});

    // Oldest first: opening three files from a file manager should queue them in the
    // order they were named, not backwards.
    QCOMPARE(drain(queue), QStringList({QStringLiteral("link:ed2k://|file|a|1|AA|/"),
                                        QStringLiteral("file:/tmp/b.nzb"),
                                        QStringLiteral("link:ed2k://|file|c|1|CC|/")}));
    QVERIFY(queue.isEmpty());
}

void tst_PendingOpenQueue::theKindSurvivesTheWait()
{
    // The queue used to be a plain QStringList, when only links could wait. A path
    // put through that comes back indistinguishable from a link and is replayed
    // through the eD2K importer, which wants nothing to do with it.
    PendingOpenQueue queue;
    queue.push({QStringLiteral("/tmp/one.nzb"), true});
    queue.push({QStringLiteral("magnet:?xt=urn:btih:0"), false});

    QList<PendingOpen> seen;
    queue.release([&seen](const PendingOpen& item) { seen << item; });

    QCOMPARE(seen.size(), 2);
    QVERIFY(seen.at(0).isFile);
    QCOMPARE(seen.at(0).value, QStringLiteral("/tmp/one.nzb"));
    QVERIFY(!seen.at(1).isFile);
}

void tst_PendingOpenQueue::aReleaseThatReEntersDoesNotReplayAnythingTwice()
{
    // Acting on one item opens dialogs; a modal dialog spins the event loop; the
    // loop can deliver the daemon's connected() again, which calls straight back in
    // here. Emptying after the loop instead of before would hand the same .nzb over
    // a second time and ask "download it again?" twice.
    PendingOpenQueue queue;
    queue.push({QStringLiteral("/tmp/once.nzb"), true});
    queue.push({QStringLiteral("ed2k://|file|d|1|DD|/"), false});

    QStringList seen;
    queue.release([&queue, &seen](const PendingOpen& item) {
        seen << item.value;
        seen << drain(queue);   // the re-entrant release
    });

    QCOMPARE(seen, QStringList({QStringLiteral("/tmp/once.nzb"),
                                QStringLiteral("ed2k://|file|d|1|DD|/")}));
    QVERIFY(queue.isEmpty());
}

void tst_PendingOpenQueue::aFloodIsCappedRatherThanUnbounded()
{
    PendingOpenQueue queue;
    for (qsizetype i = 0; i < PendingOpenQueue::kMax; ++i)
        QVERIFY(queue.push({QStringLiteral("/tmp/%1.nzb").arg(i), true}));

    // Refused, and it says so — the caller logs the drop rather than pretending the
    // file is on its way.
    QVERIFY(!queue.push({QStringLiteral("/tmp/one-too-many.nzb"), true}));
    QCOMPARE(queue.size(), PendingOpenQueue::kMax);

    // The cap drops the newest, keeping what was asked for first.
    const QStringList seen = drain(queue);
    QCOMPARE(seen.size(), PendingOpenQueue::kMax);
    QCOMPARE(seen.first(), QStringLiteral("file:/tmp/0.nzb"));
    QVERIFY(!seen.contains(QStringLiteral("file:/tmp/one-too-many.nzb")));
}

void tst_PendingOpenQueue::releasingAnEmptyQueueDoesNothing()
{
    // Every reconnect calls the release path, and most of them have nothing waiting.
    PendingOpenQueue queue;
    int calls = 0;
    queue.release([&calls](const PendingOpen&) { ++calls; });
    QCOMPARE(calls, 0);
}

QTEST_MAIN(tst_PendingOpenQueue)
#include "tst_PendingOpenQueue.moc"
