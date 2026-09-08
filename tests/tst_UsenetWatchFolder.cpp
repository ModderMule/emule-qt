/// @file tst_UsenetWatchFolder.cpp
/// @brief The .nzb intake folder: what it picks up, and what it refuses to.
///
/// The case that matters most is the settle check. A watcher fires when a file
/// is *created*, which for anything larger than a buffer is well before the
/// writer has finished -- and the damage is not a half-parsed release (the
/// parser rejects a truncated document) but the *move*: a mid-copy file
/// declared invalid and filed into `_failed/` looks exactly like a corrupt
/// download, and the user's .nzb is gone from where they put it.

#include "UsenetPostingHarness.h"

#include "queue/UsenetQueue.h"
#include "queue/UsenetWatchFolder.h"
#include "prefs/Preferences.h"

#include <QDir>
#include <QFile>
#include <QTest>

using namespace eMule;
using namespace eMule::usenet;
using namespace eMule::testing;
using namespace eMule::testing::usenet;

class tst_UsenetWatchFolder : public QObject {
    Q_OBJECT

private slots:
    void aFileStillBeingWrittenIsNotReadUntilItSettles();
    void anAddedNzbIsMovedToProcessed();
    void anUnparseableNzbIsMovedToFailed();
    void aDuplicateIsProcessedNotFailed();
    void filesPresentAtStartupAreAdded();
    void theProcessedAndFailedFoldersAreNotRescanned();
    void aWatchDirInsideTheTempTreeIsRefused();
};

namespace {

QByteArray nzbFor(const QString& messageId)
{
    // Custom delimiter: the subject's `(1/1)"` closes a plain R"( ... )".
    return QByteArray(
               R"(<?xml version="1.0"?><nzb xmlns="http://www.newzbin.com/DTD/2003/nzb">)"
               R"NZB(<file subject="thing (1/1)" poster="p" date="1">)NZB"
               R"(<groups><group>alt.bin</group></groups><segments>)"
               R"(<segment bytes="10" number="1">)")
           + messageId.toUtf8()
           + QByteArray(R"(</segment></segments></file></nzb>)");
}

void writeFile(const QString& path, const QByteArray& data)
{
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(data);
    f.close();
}

/// The settle window is real time, so a test that wants a file consumed has to
/// wait it out. Two scans: the first records the sighting, the second acts.
void settleAndScan(UsenetWatchFolder& watcher)
{
    watcher.scan();
    QTest::qWait(UsenetWatchFolder::kSettleMs + 200);
    watcher.scan();
}

QString processedDir(const QString& root)
{
    return QDir(root).filePath(QLatin1String(UsenetWatchFolder::kProcessedDir));
}

QString failedDir(const QString& root)
{
    return QDir(root).filePath(QLatin1String(UsenetWatchFolder::kFailedDir));
}

/// Config, temp and incoming as *siblings* of the watch folder.
///
/// Deliberately not useTempPrefs(), which puts configDir at the temp root — every
/// path under it would then be "inside the config dir" and the containment
/// assertions below would pass without testing anything.
QString setUpPrefs(const TempDir& tmp)
{
    thePrefs.setConfigDir(tmp.filePath(QStringLiteral("config")));
    thePrefs.setTempDirs({tmp.filePath(QStringLiteral("temp"))});
    thePrefs.setIncomingDir(tmp.filePath(QStringLiteral("incoming")));
    thePrefs.setSharedDirs({});

    const QString watch = tmp.filePath(QStringLiteral("watch"));
    QDir().mkpath(watch);
    return watch;
}

} // namespace

void tst_UsenetWatchFolder::aFileStillBeingWrittenIsNotReadUntilItSettles()
{
    TempDir tmp;
    const QString watch = setUpPrefs(tmp);
    thePrefs.setUsenetWatchDir(watch);

    UsenetQueue queue;
    UsenetWatchFolder watcher(&queue);
    watcher.applyPreferences();

    // A copy in progress: the file exists and is a fragment.
    const QString path = QDir(watch).filePath(QStringLiteral("growing.nzb"));
    writeFile(path, QByteArrayLiteral("<?xml version=\"1.0\"?><nzb>"));
    watcher.scan();

    // Not touched: not queued, and -- the part that matters -- not moved.
    QCOMPARE(queue.items().size(), 0);
    QVERIFY(QFile::exists(path));
    QVERIFY(!QDir(failedDir(watch)).exists()
            || QDir(failedDir(watch)).entryList(QDir::Files).isEmpty());

    // The writer finishes. The clock restarts because the file changed, so a
    // scan straight afterwards still leaves it alone.
    QTest::qWait(UsenetWatchFolder::kSettleMs + 200);
    writeFile(path, nzbFor(QStringLiteral("settled@x")));
    watcher.scan();
    QCOMPARE(queue.items().size(), 0);
    QVERIFY(QFile::exists(path));

    // Once it has held still, it is read.
    QTest::qWait(UsenetWatchFolder::kSettleMs + 200);
    watcher.scan();
    QCOMPARE(queue.items().size(), 1);
    QVERIFY(!QFile::exists(path));
}

void tst_UsenetWatchFolder::anAddedNzbIsMovedToProcessed()
{
    TempDir tmp;
    const QString watch = setUpPrefs(tmp);
    thePrefs.setUsenetWatchDir(watch);

    UsenetQueue queue;
    UsenetWatchFolder watcher(&queue);
    watcher.applyPreferences();

    writeFile(QDir(watch).filePath(QStringLiteral("good.nzb")),
              nzbFor(QStringLiteral("good@x")));
    settleAndScan(watcher);

    QCOMPARE(queue.items().size(), 1);
    QCOMPARE(QDir(processedDir(watch)).entryList(QDir::Files),
             QStringList{QStringLiteral("good.nzb")});
    // Moved, not deleted: a user should be able to see what became of it.
    QVERIFY(!QFile::exists(QDir(watch).filePath(QStringLiteral("good.nzb"))));
}

void tst_UsenetWatchFolder::anUnparseableNzbIsMovedToFailed()
{
    TempDir tmp;
    const QString watch = setUpPrefs(tmp);
    thePrefs.setUsenetWatchDir(watch);

    UsenetQueue queue;
    UsenetWatchFolder watcher(&queue);
    watcher.applyPreferences();

    writeFile(QDir(watch).filePath(QStringLiteral("junk.nzb")),
              QByteArrayLiteral("this is not xml at all"));
    settleAndScan(watcher);

    QCOMPARE(queue.items().size(), 0);
    QCOMPARE(QDir(failedDir(watch)).entryList(QDir::Files),
             QStringList{QStringLiteral("junk.nzb")});
}

void tst_UsenetWatchFolder::aDuplicateIsProcessedNotFailed()
{
    TempDir tmp;
    const QString watch = setUpPrefs(tmp);
    thePrefs.setUsenetWatchDir(watch);

    UsenetQueue queue;
    UsenetWatchFolder watcher(&queue);
    watcher.applyPreferences();

    QString error;
    QVERIFY(!queue.addNzb(nzbFor(QStringLiteral("dupe@x")), QStringLiteral("already"), error)
                 .isEmpty());

    // The same articles under a different filename.
    writeFile(QDir(watch).filePath(QStringLiteral("again.nzb")),
              nzbFor(QStringLiteral("dupe@x")));
    settleAndScan(watcher);

    QCOMPARE(queue.items().size(), 1);
    // "We already have it" is the answer, not a failure. Filing it as failed
    // would be a lie; leaving it in place would make every scan re-read it.
    QCOMPARE(QDir(processedDir(watch)).entryList(QDir::Files),
             QStringList{QStringLiteral("again.nzb")});
    QVERIFY(QDir(failedDir(watch)).entryList(QDir::Files).isEmpty());
}

void tst_UsenetWatchFolder::filesPresentAtStartupAreAdded()
{
    TempDir tmp;
    const QString watch = setUpPrefs(tmp);

    // Dropped in while the daemon was down. No watcher event will ever fire for
    // it, which is why applyPreferences() scans and why there is a rescan timer.
    writeFile(QDir(watch).filePath(QStringLiteral("waiting.nzb")),
              nzbFor(QStringLiteral("waiting@x")));

    thePrefs.setUsenetWatchDir(watch);
    UsenetQueue queue;
    UsenetWatchFolder watcher(&queue);
    watcher.applyPreferences();

    QTest::qWait(UsenetWatchFolder::kSettleMs + 200);
    watcher.scan();

    QCOMPARE(queue.items().size(), 1);
}

void tst_UsenetWatchFolder::theProcessedAndFailedFoldersAreNotRescanned()
{
    TempDir tmp;
    const QString watch = setUpPrefs(tmp);
    thePrefs.setUsenetWatchDir(watch);

    UsenetQueue queue;
    UsenetWatchFolder watcher(&queue);
    watcher.applyPreferences();

    writeFile(QDir(watch).filePath(QStringLiteral("once.nzb")),
              nzbFor(QStringLiteral("once@x")));
    settleAndScan(watcher);
    QCOMPARE(queue.items().size(), 1);

    // The file now lives in _processed. A scanner that reads its own output
    // re-queues every release it ever handled, on every scan, forever.
    for (int i = 0; i < 3; ++i) {
        QTest::qWait(UsenetWatchFolder::kSettleMs + 100);
        watcher.scan();
    }

    QCOMPARE(queue.items().size(), 1);
    QCOMPARE(QDir(processedDir(watch)).entryList(QDir::Files).size(), 1);
}

void tst_UsenetWatchFolder::aWatchDirInsideTheTempTreeIsRefused()
{
    TempDir tmp;
    const QString fine = setUpPrefs(tmp);

    // The daemon writes .usenetpart files under the temp tree while it works. A
    // scanner walking that would be reading files it is itself producing.
    const QString inside = QDir(thePrefs.tempDirs().first()).filePath(QStringLiteral("drop"));
    thePrefs.setUsenetWatchDir(inside);
    QVERIFY(thePrefs.usenetWatchDir().isEmpty());

    thePrefs.setUsenetWatchDir(thePrefs.incomingDir());
    QVERIFY(thePrefs.usenetWatchDir().isEmpty());

    thePrefs.setUsenetWatchDir(QDir(thePrefs.configDir()).filePath(QStringLiteral("sub")));
    QVERIFY(thePrefs.usenetWatchDir().isEmpty());

    // A folder of the user's own is fine.
    thePrefs.setUsenetWatchDir(fine);
    QCOMPARE(thePrefs.usenetWatchDir(), fine);
}

QTEST_MAIN(tst_UsenetWatchFolder)
#include "tst_UsenetWatchFolder.moc"
