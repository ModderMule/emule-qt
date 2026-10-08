/// @file tst_SharedDirWatcher.cpp
/// @brief The shared-directory watcher: settling, overflow, polled roots.

#include "TestHelpers.h"

#include "files/SharedDirWatcher.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTest>

using namespace eMule;

class tst_SharedDirWatcher : public QObject {
    Q_OBJECT

private slots:
    void aBurstOfChangesIsOneReport();
    void aBusyDirectoryIsReportedAnyway();
    void otherDirectoriesAreNotReported();
    void aMissingRootIsPolledAndThenWatched();
    void tooManyDirectoriesAtOnceIsAnOverflow();
    void aNetworkRootIsPolledEvenWhenWatched();
    void aMoveReportsBothDirectories();
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
    QCOMPARE(watcher.polledRoots(), QStringList{dir});
    QSignalSpy changed(&watcher, &SharedDirWatcher::directoryChanged);

    QDir().mkpath(dir);
    touch(dir, QStringLiteral("f"));
    QTRY_VERIFY_WITH_TIMEOUT(!changed.isEmpty(), 5000);
    QVERIFY2(watcher.polledRoots().isEmpty(), "watched once it exists");

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
    QSignalSpy changed(&watcher, &SharedDirWatcher::directoryChanged);
    QSignalSpy overflow(&watcher, &SharedDirWatcher::overflow);

    for (const QString& dir : dirs)
        touch(dir, QStringLiteral("f"));
    QTRY_COMPARE_WITH_TIMEOUT(overflow.count(), 1, 10000);
    QTest::qWait(300);
    QCOMPARE(changed.count(), 0);
}

// A watch on a network share is accepted and then tells little; such a root is
// reported at intervals whatever the watch says.
void tst_SharedDirWatcher::aNetworkRootIsPolledEvenWhenWatched()
{
    eMule::testing::TempDir tmp;
    const QString local = tmp.filePath(QStringLiteral("local"));
    const QString remote = tmp.filePath(QStringLiteral("remote"));
    QDir().mkpath(local);
    QDir().mkpath(remote);

    SharedDirWatcher watcher;
    watcher.setRemoteCheck([&](const QString& dir) { return dir == remote; });
    watcher.setTimings(50, 500, 100);
    watcher.setRoots({local, remote});
    QCOMPARE(watcher.polledRoots(), QStringList{remote});
    QSignalSpy changed(&watcher, &SharedDirWatcher::directoryChanged);

    // Nothing is touched: the reports come from the poll alone, and only for the
    // remote root.
    QTRY_VERIFY_WITH_TIMEOUT(changed.count() >= 2, 5000);
    for (const auto& call : changed)
        QCOMPARE(call.first().toString(), remote);
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

QTEST_GUILESS_MAIN(tst_SharedDirWatcher)
#include "tst_SharedDirWatcher.moc"
