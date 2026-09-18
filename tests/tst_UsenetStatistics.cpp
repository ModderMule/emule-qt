/// @file tst_UsenetStatistics.cpp
/// @brief How Usenet engine events become statistics counters.
///
/// The judgement calls, without a socket in sight: which fetch result is a miss,
/// which a provider fault and which our own doing; what the sampler takes as a
/// peak; which adds count. tst_UsenetQueue proves the queue calls these at the
/// right moments against a fake server.

#include "TestHelpers.h"
#include "TestFixtures.h"

#include "post/UsenetPostProcessor.h"
#include "queue/UsenetQueueItem.h"
#include "queue/UsenetStatistics.h"
#include "queue/UsenetWorker.h"

#include <QTest>

using namespace eMule;
using namespace eMule::usenet;
using testing::ScopedStatistics;

namespace {

constexpr const char* kAccount = "acct-1";

UsenetFetchResult resultFrom(NntpError error)
{
    UsenetFetchResult r;
    r.error = error;
    r.accountId = QString::fromLatin1(kAccount);
    r.serverKey = QStringLiteral("news.example.com:563/alice");
    r.rawBytes = 1000;
    r.decodedBytes = error == NntpError::None ? 960 : 0;
    return r;
}

UsenetQueueItem makeItem(UsenetItemStatus status, qint64 encoded, qint64 decoded)
{
    UsenetQueueItem item;
    item.status = status;
    NzbSegment segment;
    segment.bytes = encoded;
    NzbFileInfo file;
    file.segments.append(segment);
    item.nzb.files.append(file);
    UsenetFileState state;
    state.decodedBytes = decoded;
    item.files.append(state);
    return item;
}

/// One file of a release: what the NZB says it costs, what is decoded so far,
/// and whether the user took it out.
struct FileSpec {
    qint64 encoded = 0;
    qint64 decoded = 0;
    bool skipped = false;
    bool neededForRepair = false;
};

/// makeItem() builds a single file, which cannot express a release somebody
/// picked files out of.
UsenetQueueItem makeMultiFileItem(UsenetItemStatus status, const QList<FileSpec>& specs)
{
    UsenetQueueItem item;
    item.status = status;
    for (const FileSpec& spec : specs) {
        NzbSegment segment;
        segment.bytes = spec.encoded;
        NzbFileInfo file;
        file.segments.append(segment);
        item.nzb.files.append(file);

        UsenetFileState state;
        state.decodedBytes = spec.decoded;
        state.skipped = spec.skipped;
        state.neededForRepair = spec.neededForRepair;
        item.files.append(state);
    }
    return item;
}

} // namespace

class tst_UsenetStatistics : public QObject {
    Q_OBJECT

private slots:
    void noteResult_classifies_data();
    void noteResult_classifies();
    void noteResult_withoutStatisticsIsANoOp();
    void tick_countsTimeOnlyWhileReceivingAndPeaksAfterWarmup();
    void noteAdd_countsOriginsAndSkipsFailed();
    void notePostFinished_countsOnlyTerminalVerifies();
    void addPostStageTime_bucketsByStage();
    void summarizeQueue_countsStatusesAndWhatIsLeft();
    void summarizeQueue_leavesOutSkippedFiles();
};

// Which counter one result lands in. Every result charges its wire bytes, the
// same figure the billing meter takes; what else it counts depends on why it ended.
void tst_UsenetStatistics::noteResult_classifies_data()
{
    QTest::addColumn<UsenetFetchResult>("result");
    QTest::addColumn<int>("downloaded");
    QTest::addColumn<int>("notFound");
    QTest::addColumn<int>("corrupt");
    QTest::addColumn<int>("errors");
    QTest::addColumn<int>("probes");
    QTest::addColumn<qint64>("wire");

    QTest::newRow("article") << resultFrom(NntpError::None) << 1 << 0 << 0 << 0 << 0 << qint64(1000);
    QTest::newRow("430 fails over") << resultFrom(NntpError::ArticleNotFound)
                                    << 0 << 1 << 0 << 0 << 0 << qint64(1000);

    // Escalates like a 430 and must be tested before it, or it would be counted
    // as one.
    QTest::newRow("CRC mismatch") << resultFrom(NntpError::ArticleCorrupt)
                                  << 0 << 0 << 1 << 0 << 0 << qint64(1000);

    QTest::newRow("timeout") << resultFrom(NntpError::Timeout) << 0 << 0 << 0 << 1 << 0 << qint64(1000);

    // A settings save stops the workers mid-article. Nobody failed.
    UsenetFetchResult aborted = resultFrom(NntpError::Disconnected);
    aborted.aborted = true;
    QTest::newRow("our own shutdown") << aborted << 0 << 0 << 0 << 0 << 0 << qint64(1000);
    UsenetFetchResult abortedProbe = aborted;
    abortedProbe.probeOnly = true;
    QTest::newRow("probe cut short by us") << abortedProbe << 0 << 0 << 0 << 0 << 0 << qint64(1000);

    // The target file would not open: no connection was ever involved.
    UsenetFetchResult local = resultFrom(NntpError::ProtocolError);
    local.serverKey.clear();
    local.accountId.clear();
    local.rawBytes = 0;
    QTest::newRow("local fault") << local << 0 << 0 << 0 << 0 << 0 << qint64(0);

    UsenetFetchResult starved = resultFrom(NntpError::ServerUnavailable);
    starved.noServerAvailable = true;
    starved.rawBytes = 0;
    QTest::newRow("nothing leasable") << starved << 0 << 0 << 0 << 0 << 0 << qint64(0);

    // A STAT answer, found or not, is a probe and nothing else.
    UsenetFetchResult probe = resultFrom(NntpError::ArticleNotFound);
    probe.probeOnly = true;
    probe.rawBytes = 60;
    QTest::newRow("probe") << probe << 0 << 0 << 0 << 0 << 1 << qint64(60);
}

void tst_UsenetStatistics::noteResult_classifies()
{
    QFETCH(UsenetFetchResult, result);
    QFETCH(int, downloaded);
    QFETCH(int, notFound);
    QFETCH(int, corrupt);
    QFETCH(int, errors);
    QFETCH(int, probes);
    QFETCH(qint64, wire);

    ScopedStatistics stats;
    UsenetStatistics usenet;
    usenet.noteResult(result);

    const UsenetCounters& c = stats->usenetSession();
    QCOMPARE(c.articlesDownloaded, uint64(downloaded));
    QCOMPARE(c.articlesNotFound, uint64(notFound));
    QCOMPARE(c.articlesCorrupt, uint64(corrupt));
    QCOMPARE(c.connectionErrors, uint64(errors));
    QCOMPARE(c.statProbes, uint64(probes));
    QCOMPARE(c.wireBytes, uint64(wire));
    QCOMPARE(c.decodedBytes, uint64(downloaded ? 960 : 0));

    // The per-account figures agree with the engine-wide ones.
    const UsenetServerCounters server = usenet.servers().value(QString::fromLatin1(kAccount));
    QCOMPARE(server.wireBytes, result.accountId.isEmpty() ? uint64(0) : uint64(wire));
    QCOMPARE(server.articles, uint64(downloaded));
    QCOMPARE(server.notFound, uint64(notFound));
    QCOMPARE(server.corrupt, uint64(corrupt));
    QCOMPARE(server.errors, uint64(errors));
}

// A bare queue in a test has no daemon behind it. Counting must not crash, and
// the per-account figures — which live here, not in core — still count.
void tst_UsenetStatistics::noteResult_withoutStatisticsIsANoOp()
{
    UsenetStatistics usenet;
    usenet.noteResult(resultFrom(NntpError::None));
    usenet.tick(250, 1000, 3);
    usenet.bump(&UsenetCounters::articlesMissing);
    QCOMPARE(usenet.servers().value(QString::fromLatin1(kAccount)).articles, uint64(1));
}

void tst_UsenetStatistics::tick_countsTimeOnlyWhileReceivingAndPeaksAfterWarmup()
{
    ScopedStatistics stats;
    UsenetStatistics usenet;
    usenet.restartSampling();

    // The first two seconds are the rate window filling: time counts, peaks do not.
    for (int i = 0; i < 8; ++i)
        usenet.tick(250, 9'000'000, 40);
    QCOMPARE(stats->usenetSession().downloadTimeMs, uint64(2000));
    QCOMPARE(stats->usenetSession().maxDownRate, uint64(0));
    QCOMPARE(stats->usenetSession().peakConnections, uint64(0));

    // Idle: no download time.
    for (int i = 0; i < 4; ++i)
        usenet.tick(250, 0, 0);
    QCOMPARE(stats->usenetSession().downloadTimeMs, uint64(2000));

    // Sampled once a second from here on, and a peak only ever rises.
    for (int i = 0; i < 4; ++i)
        usenet.tick(250, 5'000'000, 12);
    for (int i = 0; i < 4; ++i)
        usenet.tick(250, 2'000'000, 4);
    QCOMPARE(stats->usenetSession().downloadTimeMs, uint64(4000));
    QCOMPARE(stats->usenetSession().maxDownRate, uint64(5'000'000));
    QCOMPARE(stats->usenetSession().peakConnections, uint64(12));
}

void tst_UsenetStatistics::noteAdd_countsOriginsAndSkipsFailed()
{
    ScopedStatistics stats;
    UsenetStatistics usenet;
    usenet.noteAdd(UsenetAddOrigin::File, UsenetAddOutcome::Added);
    usenet.noteAdd(UsenetAddOrigin::Url, UsenetAddOutcome::Added);
    usenet.noteAdd(UsenetAddOrigin::WatchFolder, UsenetAddOutcome::Added);
    usenet.noteAdd(UsenetAddOrigin::Feed, UsenetAddOutcome::Added);
    usenet.noteAdd(UsenetAddOrigin::Feed, UsenetAddOutcome::Duplicate);
    usenet.noteAdd(UsenetAddOrigin::IndexerGrab, UsenetAddOutcome::AlreadyDownloaded);
    usenet.noteAdd(UsenetAddOrigin::WatchFolder, UsenetAddOutcome::Invalid);
    // Retried by the watch folder and by feeds: every retry would count again.
    usenet.noteAdd(UsenetAddOrigin::WatchFolder, UsenetAddOutcome::Failed);

    const UsenetCounters& c = stats->usenetSession();
    QCOMPARE(c.nzbFromFile, uint64(1));
    QCOMPARE(c.nzbFromUrl, uint64(1));
    QCOMPARE(c.nzbFromWatch, uint64(1));
    QCOMPARE(c.nzbFromFeed, uint64(1));
    QCOMPARE(c.nzbFromIndexer, uint64(0));
    QCOMPARE(c.nzbDuplicate, uint64(1));
    QCOMPARE(c.nzbAlreadyDownloaded, uint64(1));
    QCOMPARE(c.nzbInvalid, uint64(1));
}

// A short verify sends the item back for recovery volumes; only the round that
// settles it counts. A repair by par2's rename pass reads Clean, and still counts
// as a repair.
void tst_UsenetStatistics::notePostFinished_countsOnlyTerminalVerifies()
{
    ScopedStatistics stats;
    UsenetStatistics usenet;

    UsenetPostResult shortRound;
    shortRound.par2Outcome = Par2Outcome::NeedMoreBlocks;
    shortRound.needsMoreBlocks = true;
    usenet.notePostFinished(shortRound);
    QCOMPARE(stats->usenetSession().par2Verified, uint64(0));

    UsenetPostResult repaired;
    repaired.par2Outcome = Par2Outcome::Repaired;
    repaired.repaired = true;
    repaired.blocksRepaired = 17;
    repaired.unpackOutcome = UsenetUnpackOutcome::Unpacked;
    usenet.notePostFinished(repaired);

    UsenetPostResult renamedClean;
    renamedClean.par2Outcome = Par2Outcome::Clean;
    renamedClean.repaired = true;
    renamedClean.unpackOutcome = UsenetUnpackOutcome::PasswordRequired;
    usenet.notePostFinished(renamedClean);

    UsenetPostResult hopeless;
    hopeless.par2Outcome = Par2Outcome::RepairFailed;
    usenet.notePostFinished(hopeless);

    UsenetPostResult raw;   // no PAR2, nothing to unpack
    raw.par2Outcome = Par2Outcome::NoPar2Files;
    raw.unpackOutcome = UsenetUnpackOutcome::NothingToUnpack;
    usenet.notePostFinished(raw);

    const UsenetCounters& c = stats->usenetSession();
    QCOMPARE(c.par2Verified, uint64(3));
    QCOMPARE(c.par2Repaired, uint64(2));
    QCOMPARE(c.par2RepairFailed, uint64(1));
    QCOMPARE(c.par2BlocksRepaired, uint64(17));
    QCOMPARE(c.unpackOk, uint64(1));
    QCOMPARE(c.unpackPassword, uint64(1));
    QCOMPARE(c.unpackFailed, uint64(0));
}

void tst_UsenetStatistics::addPostStageTime_bucketsByStage()
{
    ScopedStatistics stats;
    UsenetStatistics usenet;
    usenet.addPostStageTime(PostStage::Verifying, 1500);
    usenet.addPostStageTime(PostStage::Repairing, 700);
    usenet.addPostStageTime(PostStage::Unpacking, 300);
    usenet.addPostStageTime(PostStage::Idle, 10'000);      // waiting in line
    usenet.addPostStageTime(PostStage::Staging, 10'000);   // a rename, not work
    usenet.addPostStageTime(PostStage::Verifying, -5);

    const UsenetCounters& c = stats->usenetSession();
    QCOMPARE(c.verifyMs, uint64(1500));
    QCOMPARE(c.repairMs, uint64(700));
    QCOMPARE(c.unpackMs, uint64(300));
}

void tst_UsenetStatistics::summarizeQueue_countsStatusesAndWhatIsLeft()
{
    const UsenetQueueItem downloading = makeItem(UsenetItemStatus::Downloading, 1000, 400);
    const UsenetQueueItem queued = makeItem(UsenetItemStatus::Queued, 2000, 0);
    const UsenetQueueItem unpacking = makeItem(UsenetItemStatus::Unpacking, 500, 480);
    const UsenetQueueItem complete = makeItem(UsenetItemStatus::Complete, 3000, 2900);

    const UsenetQueueSummary s =
        summarizeQueue({&downloading, &queued, &unpacking, &complete, nullptr});
    QCOMPARE(s.count, 4);
    QCOMPARE(s.downloading, 1);
    QCOMPARE(s.queued, 1);
    QCOMPARE(s.postProcessing, 1);
    QCOMPARE(s.complete, 1);
    QCOMPARE(s.totalBytes, qint64(6500));
    QCOMPARE(s.downloadedBytes, qint64(3780));
    // A finished release's yEnc overhead is not left to download.
    QCOMPARE(s.leftBytes, qint64(600 + 2000 + 20));
}

void tst_UsenetStatistics::summarizeQueue_leavesOutSkippedFiles()
{
    // A file the user took out is not work outstanding. Counting it leaves the
    // queue reporting bytes nobody is ever going to fetch, and a release that
    // can never reach 100%.
    UsenetQueueItem item = makeMultiFileItem(UsenetItemStatus::Downloading,
                                             {{1000, 400}, {2000, 0, /*skipped*/ true}});

    UsenetQueueSummary s = summarizeQueue({&item});
    QCOMPARE(s.count, 1);
    QCOMPARE(s.downloading, 1);
    QCOMPARE(s.totalBytes, qint64(1000));
    QCOMPARE(s.downloadedBytes, qint64(400));
    QCOMPARE(s.leftBytes, qint64(600));

    // Until a repair asks for it back: isSkipped() is skipped && !neededForRepair,
    // so it is being downloaded again and counts again.
    item.files[1].neededForRepair = true;
    s = summarizeQueue({&item});
    QCOMPARE(s.totalBytes, qint64(3000));
    QCOMPARE(s.downloadedBytes, qint64(400));
    QCOMPARE(s.leftBytes, qint64(2600));
}

QTEST_MAIN(tst_UsenetStatistics)
#include "tst_UsenetStatistics.moc"
