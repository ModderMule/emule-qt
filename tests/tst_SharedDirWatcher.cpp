/// @file tst_SharedDirWatcher.cpp
/// @brief The shared-directory watcher: settling, overflow, polled roots.

#include "TestHelpers.h"

#include "files/SharedDirWatcher.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QSignalSpy>
#include <QTest>
#include <QThread>

#include <atomic>

using namespace eMule;

class tst_SharedDirWatcher : public QObject {
    Q_OBJECT

private slots:
    void aBurstOfChangesIsOneReport();
    void aBusyDirectoryIsReportedAnyway();
    void otherDirectoriesAreNotReported();
    void aMissingRootIsPolledAndThenWatched();
    void tooManyDirectoriesAtOnceIsAnOverflow();
    void aPollOverflowMayResetTheRoots();
    void aNetworkRootIsPolledEvenWhenWatched();
    void aBusyShareSkipsThePoll();
    void aZeroIntervalNeverPolls();
    void aMoveReportsBothDirectories();
    void aSlowOsWatchDoesNotBlockTheCaller();
};

namespace {

void touch(const QString& dir, const QString& name)
{
    QFile f(QDir(dir).filePath(name));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("x");
}

} // namespace

void tst_SharedDirWatcher::aBurstOfChangesIsOneReport()
{
    eMule::testing::TempDir tmp;
    const QString dir = tmp.filePath(QStringLiteral("share"));
    QDir().mkpath(dir);

    SharedDirWatcher watcher;
    // A settle time well above the gaps below, also on a loaded machine where the
    // OS hands the events over late and in clumps.
    watcher.setTimings(1500, 20000, 60000);
    watcher.setRoots({dir});
    QTRY_VERIFY(!watcher.watchesPending());
    QVERIFY(watcher.polledRoots().isEmpty());
    QSignalSpy changed(&watcher, &SharedDirWatcher::directoryChanged);

    for (int i = 0; i < 10; ++i) {
        touch(dir, QStringLiteral("f%1").arg(i));
        QTest::qWait(30);
    }
    QTRY_COMPARE_WITH_TIMEOUT(changed.count(), 1, 15000);
    QCOMPARE(changed.first().first().toString(), dir);
    QTest::qWait(2500);
    QCOMPARE(changed.count(), 1);
}

void tst_SharedDirWatcher::aBusyDirectoryIsReportedAnyway()
{
    eMule::testing::TempDir tmp;
    const QString dir = tmp.filePath(QStringLiteral("share"));
    QDir().mkpath(dir);

    SharedDirWatcher watcher;
    watcher.setTimings(400, 800, 60000);   // never quiet for 400 ms below
    watcher.setRoots({dir});
    QTRY_VERIFY(!watcher.watchesPending());
    QSignalSpy changed(&watcher, &SharedDirWatcher::directoryChanged);

    for (int i = 0; i < 200 && changed.isEmpty(); ++i) {
        touch(dir, QStringLiteral("f%1").arg(i));
        QTest::qWait(100);
    }
    QVERIFY2(!changed.isEmpty(), "held back for ever by a directory that keeps changing");
}

void tst_SharedDirWatcher::otherDirectoriesAreNotReported()
{
    eMule::testing::TempDir tmp;
    const QString dir = tmp.filePath(QStringLiteral("share"));
    const QString other = tmp.filePath(QStringLiteral("other"));
    QDir().mkpath(dir);
    QDir().mkpath(other);

    SharedDirWatcher watcher;
    watcher.setTimings(100, 1000, 60000);
    watcher.setRoots({dir, other});
    watcher.setRoots({dir});   // dropped again
    QTRY_VERIFY(!watcher.watchesPending());
    QCOMPARE(watcher.roots(), QStringList{dir});
    QSignalSpy changed(&watcher, &SharedDirWatcher::directoryChanged);

    touch(other, QStringLiteral("f"));
    QTest::qWait(600);
    QCOMPARE(changed.count(), 0);
}

void tst_SharedDirWatcher::aMissingRootIsPolledAndThenWatched()
{
    eMule::testing::TempDir tmp;
    const QString dir = tmp.filePath(QStringLiteral("later"));

    SharedDirWatcher watcher;
    watcher.setTimings(100, 1000, 150);
    watcher.setRoots({dir});
    QTRY_VERIFY(!watcher.watchesPending());
    QCOMPARE(watcher.polledRoots(), QStringList{dir});
    QSignalSpy changed(&watcher, &SharedDirWatcher::directoryChanged);

    QDir().mkpath(dir);
    touch(dir, QStringLiteral("f"));
    QTRY_VERIFY_WITH_TIMEOUT(!changed.isEmpty(), 5000);
    QTRY_VERIFY2(watcher.polledRoots().isEmpty(), "watched once it exists");

    changed.clear();
    QTest::qWait(300);
    touch(dir, QStringLiteral("g"));
    QTRY_COMPARE_WITH_TIMEOUT(changed.count(), 1, 5000);
}

void tst_SharedDirWatcher::tooManyDirectoriesAtOnceIsAnOverflow()
{
    eMule::testing::TempDir tmp;
    QStringList dirs;
    for (int i = 0; i <= SharedDirWatcher::kMaxPending; ++i) {
        dirs.append(tmp.filePath(QStringLiteral("d%1").arg(i)));
        QDir().mkpath(dirs.last());
    }

    SharedDirWatcher watcher;
    watcher.setTimings(2000, 10000, 60000);
    watcher.setRoots(dirs);
    QTRY_VERIFY(!watcher.watchesPending());
    QSignalSpy changed(&watcher, &SharedDirWatcher::directoryChanged);
    QSignalSpy overflow(&watcher, &SharedDirWatcher::overflow);

    for (const QString& dir : dirs)
        touch(dir, QStringLiteral("f"));
    QTRY_COMPARE_WITH_TIMEOUT(overflow.count(), 1, 10000);
    QTest::qWait(300);
    QCOMPARE(changed.count(), 0);
}

// More changed polled roots than kMaxPending overflow; the owner's reload then
// resets the roots while poll answers are still coming in (#117).
void tst_SharedDirWatcher::aPollOverflowMayResetTheRoots()
{
    eMule::testing::TempDir tmp;
    QStringList dirs;
    for (int i = 0; i <= SharedDirWatcher::kMaxPending + 10; ++i)
        dirs.append(tmp.filePath(QStringLiteral("missing%1").arg(i)));

    SharedDirWatcher watcher;
    watcher.setTimings(100, 1000, 50);
    watcher.setRoots(dirs);
    QTRY_VERIFY(!watcher.watchesPending());
    QCOMPARE(watcher.polledRoots().size(), dirs.size());
    int overflows = 0;
    connect(&watcher, &SharedDirWatcher::overflow, &watcher, [&] {
        ++overflows;
        watcher.setRoots({});        // what SharedFileList::reload() may do
        watcher.setRoots(dirs);
    });
    QTest::qWait(300);   // first listings taken: all missing
    QCOMPARE(overflows, 0);

    for (const QString& dir : dirs) {
        QDir().mkpath(dir);
        touch(dir, QStringLiteral("f"));
    }
    QTRY_VERIFY_WITH_TIMEOUT(overflows >= 1, 5000);
    // the reset roots are asked for again and exist now: watched
    QTRY_VERIFY_WITH_TIMEOUT(watcher.polledRoots().isEmpty(), 5000);
}

// A watch on a network share is accepted and then tells little; such a root is
// listed at intervals whatever the watch says, and reported only when the listing
// changed (#84: a NAS rescanned every poll froze the core).
void tst_SharedDirWatcher::aNetworkRootIsPolledEvenWhenWatched()
{
    eMule::testing::TempDir tmp;
    const QString local = tmp.filePath(QStringLiteral("local"));
    const QString remote = tmp.filePath(QStringLiteral("remote"));
    QDir().mkpath(local);
    QDir().mkpath(remote);
    touch(remote, QStringLiteral("old"));

    SharedDirWatcher watcher;
    watcher.setRemoteCheck([&](const QString& dir) { return dir == remote; });
    watcher.setTimings(50, 500, 100);
    watcher.setRoots({local, remote});
    QTRY_VERIFY(!watcher.watchesPending());
    QCOMPARE(watcher.polledRoots(), QStringList{remote});
    QSignalSpy changed(&watcher, &SharedDirWatcher::directoryChanged);
    QSignalSpy listed(&watcher, &SharedDirWatcher::directoryListed);

    // nothing touched: several polls, no report
    QTest::qWait(800);
    QCOMPARE(changed.count(), 0);
    QCOMPARE(listed.count(), 0);

    // A file rewritten in place moves no directory date, but its own size: the
    // poll sees it and hands its listing over.
    {
        QFile f(QDir(remote).filePath(QStringLiteral("old")));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("longer");
    }
    QTRY_VERIFY_WITH_TIMEOUT(!listed.isEmpty(), 5000);
    QCOMPARE(listed.first().first().toString(), remote);
    const auto listing = listed.first().at(1).value<DirListing>();
    QCOMPARE(listing.size(), 1);
    QCOMPARE(listing.first().info.size(), 6);
    QTRY_VERIFY_WITH_TIMEOUT(!changed.isEmpty(), 5000);
    for (const auto& call : changed)
        QCOMPARE(call.first().toString(), remote);
}

// A share that hangs keeps its listing out on the worker: later polls skip it
// rather than queue more listings behind it.
void tst_SharedDirWatcher::aBusyShareSkipsThePoll()
{
    eMule::testing::TempDir tmp;
    const QString remote = tmp.filePath(QStringLiteral("remote"));
    QDir().mkpath(remote);

    SharedDirWatcher watcher;
    watcher.setRemoteCheck([](const QString&) { return true; });
    std::atomic<int> probes = 0;
    watcher.setBeforeProbeHook([&](const QString&) {
        ++probes;
        QThread::msleep(1500);
    });
    watcher.setTimings(50, 500, 50);
    watcher.setRoots({remote});
    QTRY_VERIFY(!watcher.watchesPending());

    QTest::qWait(1000);   // ~20 poll rounds while the first listing hangs
    QCOMPARE(probes.load(), 1);
    QTRY_VERIFY_WITH_TIMEOUT(probes.load() >= 2, 5000);   // asked again once it answered
}

void tst_SharedDirWatcher::aZeroIntervalNeverPolls()
{
    eMule::testing::TempDir tmp;
    const QString remote = tmp.filePath(QStringLiteral("remote"));
    QDir().mkpath(remote);

    SharedDirWatcher watcher;
    watcher.setRemoteCheck([](const QString&) { return true; });
    std::atomic<int> probes = 0;
    watcher.setBeforeProbeHook([&](const QString&) { ++probes; });
    watcher.setTimings(50, 500, 0);
    watcher.setRoots({remote});
    QTRY_VERIFY(!watcher.watchesPending());
    QTest::qWait(400);
    QCOMPARE(probes.load(), 0);

    // turned on: listed at once, then at the interval
    watcher.setPollInterval(50);
    QTRY_VERIFY_WITH_TIMEOUT(probes.load() >= 2, 5000);
}

// Both ends of a move are reported, in batches — not necessarily the same one:
// the OS may hand over the second directory more than a settle time later.
void tst_SharedDirWatcher::aMoveReportsBothDirectories()
{
    eMule::testing::TempDir tmp;
    const QString from = tmp.filePath(QStringLiteral("from"));
    const QString to = tmp.filePath(QStringLiteral("to"));
    QDir().mkpath(from);
    QDir().mkpath(to);
    touch(from, QStringLiteral("moved"));

    SharedDirWatcher watcher;
    watcher.setTimings(300, 20000, 60000);
    watcher.setRoots({from, to});
    QTRY_VERIFY(!watcher.watchesPending());
    QSignalSpy batches(&watcher, &SharedDirWatcher::directoriesChanged);
    QSignalSpy single(&watcher, &SharedDirWatcher::directoryChanged);

    QVERIFY(QFile::rename(QDir(from).filePath(QStringLiteral("moved")),
                          QDir(to).filePath(QStringLiteral("moved"))));
    const auto reported = [&] {
        QSet<QString> dirs;
        for (const auto& batch : batches)
            for (const QString& dir : batch.first().toStringList())
                dirs.insert(dir);
        return dirs;
    };
    QTRY_COMPARE_WITH_TIMEOUT(reported(), (QSet<QString>{from, to}), 15000);
    // Per-directory signal too. The OS may report a directory a second time after
    // the first settle, so at least once each.
    QVERIFY(single.count() >= 2);
}

// macOS fseventsd took 19-38 s to accept a watch at startup and froze the core:
// setRoots() returns at once, and a late watch reports the root once (changes made
// meanwhile were not seen).
void tst_SharedDirWatcher::aSlowOsWatchDoesNotBlockTheCaller()
{
    eMule::testing::TempDir tmp;
    const QString dir = tmp.filePath(QStringLiteral("share"));
    QDir().mkpath(dir);

    SharedDirWatcher watcher;
    watcher.setTimings(50, 500, 60000);
    watcher.setBeforeAddHook([](const QString&) { QThread::msleep(1500); });
    QSignalSpy changed(&watcher, &SharedDirWatcher::directoryChanged);

    QElapsedTimer timer;
    timer.start();
    watcher.setRoots({dir});
    QVERIFY2(timer.elapsed() < 500, "setRoots() waited for the OS");
    QVERIFY(watcher.watchesPending());
    QVERIFY2(watcher.polledRoots().isEmpty(), "not polled while the worker is busy");

    QTRY_VERIFY_WITH_TIMEOUT(!watcher.watchesPending(), 10000);
    QVERIFY(watcher.polledRoots().isEmpty());
    QTRY_COMPARE_WITH_TIMEOUT(changed.count(), 1, 5000);
    QCOMPARE(changed.first().first().toString(), dir);
}

QTEST_GUILESS_MAIN(tst_SharedDirWatcher)
#include "tst_SharedDirWatcher.moc"
