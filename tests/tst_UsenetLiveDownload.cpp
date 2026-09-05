/// @file tst_UsenetLiveDownload.cpp
/// @brief The whole Usenet pipeline against a real provider: every .nzb in
///        EMULE_NZB_DIR downloaded, verified, repaired, unpacked and published.
///
/// tst_UsenetLiveFetch proves one file of one NZB by driving NntpSocket and
/// ArticleFetcher by hand. This drives the real UsenetQueue instead — the
/// scheduler, the server ladder, PAR2, direct unpack and the publish step — so
/// it is the first test that sees what a real posting run does to the pipeline
/// as a whole: subjects our parser has to survive, articles that aged out of
/// retention, a recovery set that actually has to be used, and an archive built
/// by somebody else's packer.
///
/// One data row per .nzb in the directory, named after the file, so a failure
/// says which release broke.
///
/// Environment: the provider variables from UsenetLiveEnv.h, plus
///
///   EMULE_NZB_DIR         directory of .nzb files (required)
///   EMULE_NZB_MAX_MB      skip a release larger than this (optional)
///   EMULE_NZB_TIMEOUT_MIN per-release budget, default 45 (optional)
///
/// **Everything it writes, it removes.** Config, scratch and incoming all live
/// inside one TempDir whose destructor runs on a pass, on a QSKIP and on a
/// failed QVERIFY alike; the queue is asked to delete the item's work directory
/// on the way out; and cleanup() checks rather than assumes.
///
/// Labelled "live" and built only under EMULE_LIVE_TESTS.

#include "UsenetLiveEnv.h"

#include "nzb/NzbFile.h"
#include "nzb/NzbInfo.h"
#include "queue/UsenetQueue.h"
#include "queue/UsenetQueueItem.h"

#include "prefs/Preferences.h"

#include <QDeadlineTimer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTest>

using namespace eMule;
using namespace eMule::usenet;
using eMule::testing::TempDir;
using eMule::testing::loadProjectEnv;
using namespace eMule::testing::usenet;

namespace {

/// How long a progress line waits before it gives up and prints anyway. A
/// release takes minutes; a run that says nothing for minutes looks hung.
constexpr int kProgressLogMs = 5000;

constexpr int kDefaultBudgetMinutes = 45;

QString describe(const UsenetQueueItem* item)
{
    if (!item)
        return QStringLiteral("(item gone)");

    QString line = QStringLiteral("  %1  %2%  %3/%4 segments, %5 MB decoded")
                       .arg(describeUsenetItemStatus(item->status))
                       .arg(item->percentComplete())
                       .arg(item->doneSegmentCount())
                       .arg(item->segmentCount())
                       .arg(item->decodedBytes() / (1024 * 1024));

    if (item->isPostProcessing())
        line += QStringLiteral("  [%1% %2]").arg(item->postPercent).arg(item->postDetail);

    return line;
}

int missingSegmentsIn(const UsenetQueueItem* item)
{
    if (!item)
        return 0;

    int missing = 0;
    for (const UsenetFileState& file : item->files)
        missing += file.missingSegments;

    return missing;
}

QFileInfoList publishedFiles()
{
    return QDir(thePrefs.incomingDir())
        .entryInfoList(QDir::Files | QDir::NoDotAndDotDot, QDir::Name);
}

bool nzbHasArchiveVolumes(const NzbInfo& nzb)
{
    for (const NzbFileInfo& file : nzb.files) {
        const QString name = file.fileName.toLower();
        if (name.endsWith(QLatin1String(".rar")) || name.contains(QLatin1String(".part")))
            return true;
    }
    return false;
}

} // namespace

class tst_UsenetLiveDownload : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanup();

    void downloadsRepairsUnpacksAndPublishes_data();
    void downloadsRepairsUnpacksAndPublishes();

private:
    /// The tree the finished row was told to work in. Checked after the row, by
    /// which time TempDir's destructor has run.
    QString m_tmpPath;
};

void tst_UsenetLiveDownload::initTestCase()
{
    // .env fills in whatever the process environment did not already set.
    loadProjectEnv();

    // QtTest reads QTEST_FUNCTION_TIMEOUT from the environment when it builds its
    // watchdog, so ctest supplies it (tests/CMakeLists.txt). Run the binary by
    // hand without it and a long release dies at Qt's 300 s default with the
    // unhelpful "Received a fatal error" — say so rather than let it look like a
    // download failure.
    if (qEnvironmentVariableIsEmpty("QTEST_FUNCTION_TIMEOUT")) {
        qWarning("QTEST_FUNCTION_TIMEOUT is unset: this run will be killed after "
                 "300 s. Run through ctest, or export it.");
    }

    if (!haveLiveProvider())
        QSKIP("Set EMULE_NNTP_HOST to run the live download test");
    if (nzbDir().isEmpty())
        QSKIP("Set EMULE_NZB_DIR to a directory of .nzb files");

    QVERIFY2(QDir(nzbDir()).exists(), qPrintable(QStringLiteral("EMULE_NZB_DIR does not exist: %1")
                                                     .arg(nzbDir())));
}

/// Runs after every row. Cleanup that is only claimed and never checked is how
/// gigabytes quietly survive a failed run.
void tst_UsenetLiveDownload::cleanup()
{
    if (m_tmpPath.isEmpty())
        return;

    const QString path = m_tmpPath;
    m_tmpPath.clear();

    // The provider frees a connection slot some time after we close it. Rows run
    // back to back, so without this the next one races the last one's teardown
    // and is answered "502 Too many connections".
    QTest::qWait(3000);

    if (!QDir(path).exists())
        return;

    QDir(path).removeRecursively();
    QFAIL(qPrintable(QStringLiteral("the run left its working tree behind: %1").arg(path)));
}

void tst_UsenetLiveDownload::downloadsRepairsUnpacksAndPublishes_data()
{
    QTest::addColumn<QString>("nzbPath");

    if (addNzbRows() == 0)
        QSKIP("No .nzb files in EMULE_NZB_DIR");
}

void tst_UsenetLiveDownload::downloadsRepairsUnpacksAndPublishes()
{
    QFETCH(QString, nzbPath);

    NzbInfo nzb;
    QString error;
    QVERIFY2(NzbFile::parseFile(nzbPath, nzb, error), qPrintable(error));
    QVERIFY(!nzb.files.isEmpty());

    const qint64 encoded = nzb.totalEncodedBytes();
    const qint64 cap = nzbSizeCapBytes();
    if (cap > 0 && encoded > cap) {
        QSKIP(qPrintable(QStringLiteral("%1 MB exceeds EMULE_NZB_MAX_MB")
                             .arg(encoded / (1024 * 1024))));
    }

    qInfo().noquote() << QStringLiteral("downloading %1: %2 files, %3 segments, %4 MB encoded")
                             .arg(nzb.name)
                             .arg(nzb.files.size())
                             .arg(nzb.segmentCount())
                             .arg(encoded / (1024 * 1024));

    // Destruction order is the cleanup order: the queue joins its threads first,
    // then prefs are restored, then the tree is removed. Declared here so an
    // early return from a failed QVERIFY unwinds through all three.
    TempDir tmp;
    LivePrefsGuard prefs(tmp);
    m_tmpPath = tmp.path();

    UsenetQueue queue;
    queue.applyServers({providerFromEnv()}, 60);
    // The whole pipeline, exactly as the daemon runs it.
    queue.setPostProcessingOptions({.par2 = true,
                                    .rename = true,
                                    .unpack = true,
                                    .cleanup = true,
                                    .directUnpack = true});
    queue.start();

    QSignalSpy finished(&queue, &UsenetQueue::itemFinished);

    QFile nzbFile(nzbPath);
    QVERIFY2(nzbFile.open(QIODevice::ReadOnly), qPrintable(nzbFile.errorString()));

    const QString id = queue.addNzb(nzbFile.readAll(), QFileInfo(nzbPath).completeBaseName(), error);
    QVERIFY2(!id.isEmpty(), qPrintable(error));

    const int budgetMin = liveEnv("EMULE_NZB_TIMEOUT_MIN").toInt() > 0
                              ? liveEnv("EMULE_NZB_TIMEOUT_MIN").toInt()
                              : kDefaultBudgetMinutes;

    QDeadlineTimer deadline(qint64(budgetMin) * 60 * 1000);
    while (finished.isEmpty() && !deadline.hasExpired()) {
        finished.wait(kProgressLogMs);
        if (finished.isEmpty())
            qInfo().noquote() << describe(queue.findItem(id));
    }

    QVERIFY2(!finished.isEmpty(),
             qPrintable(QStringLiteral("no completion within %1 min — last state:%2")
                            .arg(budgetMin).arg(describe(queue.findItem(id)))));

    const bool success = finished.first().at(1).toBool();
    const QString message = finished.first().at(2).toString();
    const int missing = missingSegmentsIn(queue.findItem(id));

    if (!success) {
        // Retention is a fact about the post, not a defect in the client. An
        // article that no server still carries, with no recovery set able to
        // cover it, is the one failure this test reports as "not applicable".
        if (missing > 0) {
            QSKIP(qPrintable(QStringLiteral("%1 segments have aged out and PAR2 could not "
                                            "cover them: %2").arg(missing).arg(message)));
        }
        QFAIL(qPrintable(message));
    }

    qInfo().noquote() << QStringLiteral("finished: %1").arg(message);

    // -- What was published -------------------------------------------------

    const QFileInfoList published = publishedFiles();
    qint64 publishedBytes = 0;
    for (const QFileInfo& fi : published) {
        publishedBytes += fi.size();
        qInfo().noquote() << QStringLiteral("  published %1 (%2 MB)")
                                 .arg(fi.fileName()).arg(fi.size() / (1024 * 1024));
    }

    QVERIFY2(!published.isEmpty(), "the release finished but nothing reached the incoming dir");

    const bool hadArchives = nzbHasArchiveVolumes(nzb);
    for (const QFileInfo& fi : published) {
        const QString name = fi.fileName().toLower();
        QVERIFY2(!name.endsWith(Preferences::kUsenetPartSuffix),
                 qPrintable(QStringLiteral("still carrying the in-progress suffix: %1")
                                .arg(fi.fileName())));
        // Recovery volumes exist to repair the release, and that job is over by
        // the time anything is published.
        QVERIFY2(!name.endsWith(QLatin1String(".par2")),
                 qPrintable(QStringLiteral("a recovery volume was published: %1")
                                .arg(fi.fileName())));
        if (hadArchives) {
            QVERIFY2(!name.endsWith(QLatin1String(".rar")),
                     qPrintable(QStringLiteral("unpack was on, yet a volume was published: %1")
                                    .arg(fi.fileName())));
        }
    }

    // Not a band around the decoded size — a compressed archive has no
    // predictable ratio. This only catches the failure worth catching: a
    // pipeline that published a stray text file and called the release done.
    QVERIFY2(publishedBytes > encoded / 10,
             qPrintable(QStringLiteral("published only %1 MB of a %2 MB release")
                            .arg(publishedBytes / (1024 * 1024)).arg(encoded / (1024 * 1024))));

    // The work directory is removed by onPostFinished; if it survived, so did
    // a copy of the whole release.
    const QString workDir = QDir(thePrefs.usenetTempDir()).filePath(id);
    QVERIFY2(!QDir(workDir).exists(), qPrintable(QStringLiteral("scratch left behind: %1")
                                                     .arg(workDir)));

    QVERIFY(queue.removeItem(id, /*deleteFiles*/ true));
    queue.stop();
}

QTEST_MAIN(tst_UsenetLiveDownload)
#include "tst_UsenetLiveDownload.moc"
