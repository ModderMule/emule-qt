/// @file tst_StatisticsPanel.cpp
/// @brief The Statistics window's Usenet branch, fed a GetUsenetStats reply.
///
/// No daemon: the reply is built here in the wire shape IpcClientHandler sends,
/// which is what the panel actually consumes. Set EMULE_STATS_PANEL_SHOT to a
/// .png path to also save the fully expanded tree for a visual check.

#include "panels/StatisticsPanel.h"

#include "controls/StatsGraph.h"
#include "prefs/NewsServer.h"
#include "prefs/Preferences.h"
#include "stats/NetworkCounters.h"
#include "utils/StringUtils.h"

#include <QApplication>
#include <QCborArray>
#include <QCborMap>
#include <QTest>
#include <QTreeWidget>

using namespace eMule;

namespace {

QCborMap server(const QString& id, const QString& name, NntpQuotaKind kind, qint64 quota,
                qint64 period, qint64 total, int open, const UsenetServerCounters& session)
{
    return QCborMap{
        {QStringLiteral("accountId"), id},
        {QStringLiteral("name"), name},
        {QStringLiteral("host"), QStringLiteral("news.example.com")},
        {QStringLiteral("enabled"), true},
        {QStringLiteral("quotaKind"), int(kind)},
        {QStringLiteral("quotaBytes"), quota},
        {QStringLiteral("periodBytes"), period},
        {QStringLiteral("totalBytes"), total},
        {QStringLiteral("resetsOn"), QStringLiteral("2026-10-01")},
        {QStringLiteral("overQuota"), false},
        {QStringLiteral("openConnections"), open},
        {QStringLiteral("session"), countersToCbor(session)},
    };
}

/// A busy session, in the shape handleGetUsenetStats() sends.
QCborMap reply(bool withBlockAccount = true)
{
    UsenetCounters s;
    s.wireBytes = 1'650'000'000;
    s.decodedBytes = 1'600'000'000;
    s.downloadTimeMs = 3'600'000;
    s.maxDownRate = 6'000'000;
    s.peakConnections = 40;
    s.articlesDownloaded = 22961;
    s.articlesNotFound = 312;
    s.articlesMissing = 14;
    s.articlesCorrupt = 3;
    s.connectionErrors = 41;
    s.itemsCompleted = 7;
    s.completedBytes = 1'580'000'000;
    s.itemsFailed = 1;
    s.par2Verified = 8;
    s.par2Repaired = 2;
    s.par2BlocksRepaired = 184;
    s.recoveryVolumes = 5;
    s.recoveryBytes = 412'000'000;
    s.unpackOk = 6;
    s.unpackFailed = 1;
    s.unpackPassword = 1;
    s.directUnpacks = 4;
    s.verifyMs = 370'000;
    s.repairMs = 182'000;
    s.unpackMs = 320'000;
    s.healthChecks = 9;
    s.healthPassed = 8;
    s.healthPaused = 1;
    s.statProbes = 1240;
    s.nzbFromFile = 5;
    s.nzbFromUrl = 2;
    s.nzbFromWatch = 1;
    s.nzbFromFeed = 3;
    s.nzbFromIndexer = 1;
    s.nzbDuplicate = 2;
    s.nzbAlreadyDownloaded = 1;

    UsenetCounters cum = s;
    cum.wireBytes *= 20;
    cum.decodedBytes *= 20;
    cum.downloadTimeMs *= 20;
    cum.itemsCompleted *= 20;

    IndexerCounters idx;
    idx.searches = 4;
    idx.apiRequests = 20;
    idx.apiErrors = 1;
    idx.nzbFetches = 4;
    idx.feedPolls = 30;
    idx.feedMatches = 5;

    UsenetServerCounters main;
    main.wireBytes = 1'210'000'000;
    main.articles = 17204;
    main.notFound = 290;
    main.errors = 2;
    UsenetServerCounters block;
    block.wireBytes = 440'000'000;
    block.articles = 5757;

    QCborArray servers{server(QStringLiteral("a1"), QStringLiteral("Newshosting"),
                              NntpQuotaKind::Monthly, 500'000'000'000, 412'300'000'000,
                              3'210'400'000'000, 12, main)};
    if (withBlockAccount) {
        servers.append(server(QStringLiteral("b1"), QStringLiteral("Blocknews"),
                              NntpQuotaKind::Block, 250'000'000'000, 120'000'000'000,
                              120'000'000'000, 6, block));
    }

    const QCborMap queue{
        {QStringLiteral("count"), 9},       {QStringLiteral("downloading"), 2},
        {QStringLiteral("queued"), 5},      {QStringLiteral("paused"), 1},
        {QStringLiteral("checking"), 0},    {QStringLiteral("postProcessing"), 1},
        {QStringLiteral("failed"), 0},      {QStringLiteral("complete"), 0},
        {QStringLiteral("totalBytes"), qint64(48'200'000'000)},
        {QStringLiteral("downloadedBytes"), qint64(20'100'000'000)},
        {QStringLiteral("leftBytes"), qint64(28'100'000'000)},
    };
    const QCborMap current{
        {QStringLiteral("running"), true},
        {QStringLiteral("downRate"), 4'410'000},
        {QStringLiteral("limitKb"), 0},
        {QStringLiteral("activeConnections"), 12},
        {QStringLiteral("openConnections"), 18},
        {QStringLiteral("queue"), queue},
    };

    return QCborMap{
        {QStringLiteral("usenet"), QCborMap{
            {QStringLiteral("session"), countersToCbor(s)},
            {QStringLiteral("cumulative"), countersToCbor(cum)},
            {QStringLiteral("current"), current},
            {QStringLiteral("servers"), servers},
        }},
        {QStringLiteral("indexer"), QCborMap{
            {QStringLiteral("session"), countersToCbor(idx)},
            {QStringLiteral("cumulative"), countersToCbor(idx)},
        }},
    };
}

QTreeWidgetItem* childNamed(QTreeWidgetItem* parent, const QString& prefix)
{
    for (int i = 0; parent && i < parent->childCount(); ++i) {
        if (parent->child(i)->text(0).startsWith(prefix))
            return parent->child(i);
    }
    return nullptr;
}

QTreeWidgetItem* usenetRoot(QTreeWidget* tree)
{
    const int last = tree->topLevelItemCount() - 1;
    return last >= 0 ? tree->topLevelItem(last) : nullptr;
}

} // namespace

class tst_StatisticsPanel : public QObject {
    Q_OBJECT

private slots:
    void usenetIsTheLastBranchAndMfcsOrderStays();
    void sessionRowsCarryValuesAndShares();
    void cumulativeLeavesOutTheLiveRows();
    void newsServersAreUpdatedInPlace();
    void ratioReadsFromTheLargerSide();
    void httpCacheSplitsUploadsFromDownloads();
    void timeShowsCurrentAndTotalServerDuration();
    void rateScopesArePinnedToTheGraphMaxima();
};

void tst_StatisticsPanel::usenetIsTheLastBranchAndMfcsOrderStays()
{
    StatisticsPanel panel;
    auto* tree = panel.findChild<QTreeWidget*>();
    QVERIFY(tree);
    QCOMPARE(tree->topLevelItem(0)->text(0), QStringLiteral("Transfer"));

    QTreeWidgetItem* usenet = usenetRoot(tree);
    QVERIFY(usenet);
    QCOMPARE(usenet->text(0), QStringLiteral("Usenet"));
    QStringList children;
    for (int i = 0; i < usenet->childCount(); ++i)
        children << usenet->child(i)->text(0);
    QCOMPARE(children, (QStringList{QStringLiteral("Session"), QStringLiteral("Cumulative"),
                                    QStringLiteral("News Servers"), QStringLiteral("Queue")}));
}

void tst_StatisticsPanel::sessionRowsCarryValuesAndShares()
{
    StatisticsPanel panel;
    panel.applyUsenetStats(reply());
    auto* tree = panel.findChild<QTreeWidget*>();
    QTreeWidgetItem* session = usenetRoot(tree)->child(0);

    QTreeWidgetItem* general = childNamed(session, QStringLiteral("General"));
    QVERIFY(general);
    QCOMPARE(childNamed(general, QStringLiteral("Download Speed:"))->text(0),
             QStringLiteral("Download Speed: %1").arg(formatByteRate(4'410'000)));
    // Wire bytes over the time the engine was receiving.
    QCOMPARE(childNamed(general, QStringLiteral("Average Download Rate:"))->text(0),
             QStringLiteral("Average Download Rate: %1").arg(formatByteRate(1'650'000'000 / 3600)));

    QTreeWidgetItem* traffic = childNamed(general, QStringLiteral("Network Traffic:"));
    QVERIFY(traffic);
    QVERIFY2(traffic->child(0)->text(0).endsWith(QStringLiteral("(3.0%)")),
             qPrintable(traffic->child(0)->text(0)));   // 50 MB of 1.65 GB

    QTreeWidgetItem* post = childNamed(session, QStringLiteral("Post-Processing"));
    QTreeWidgetItem* verified = childNamed(post, QStringLiteral("PAR2 Verified:"));
    QCOMPARE(verified->text(0), QStringLiteral("PAR2 Verified: 8"));
    QCOMPARE(childNamed(verified, QStringLiteral("Repaired:"))->text(0),
             QStringLiteral("Repaired: 2 (25.0%)"));

    QTreeWidgetItem* intake = childNamed(session, QStringLiteral("Intake"));
    QTreeWidgetItem* added = childNamed(intake, QStringLiteral("NZBs Added:"));
    QCOMPARE(added->text(0), QStringLiteral("NZBs Added: 12"));
    QCOMPARE(childNamed(added, QStringLiteral("Feeds:"))->text(0), QStringLiteral("Feeds: 3 (25.0%)"));

    QTreeWidgetItem* queue = usenetRoot(tree)->child(3);
    QCOMPARE(childNamed(queue, QStringLiteral("Number of Downloads:"))->text(0),
             QStringLiteral("Number of Downloads: 9"));

    if (const QByteArray shot = qgetenv("EMULE_STATS_PANEL_SHOT"); !shot.isEmpty()) {
        panel.resize(1200, 1600);
        tree->collapseAll();
        usenetRoot(tree)->setExpanded(true);
        for (int i = 0; i < 4; ++i) {
            QTreeWidgetItem* scope = usenetRoot(tree)->child(i);
            scope->setExpanded(i != 1);   // Cumulative mirrors Session
            for (int j = 0; j < scope->childCount(); ++j) {
                scope->child(j)->setExpanded(true);
                for (int k = 0; k < scope->child(j)->childCount(); ++k)
                    scope->child(j)->child(k)->setExpanded(true);
            }
        }
        panel.show();
        QApplication::processEvents();
        QVERIFY(panel.grab().save(QString::fromLocal8Bit(shot)));
    }
}

void tst_StatisticsPanel::cumulativeLeavesOutTheLiveRows()
{
    StatisticsPanel panel;
    panel.applyUsenetStats(reply());
    auto* tree = panel.findChild<QTreeWidget*>();
    QTreeWidgetItem* general = childNamed(usenetRoot(tree)->child(1), QStringLiteral("General"));
    QVERIFY(general);
    QVERIFY(!childNamed(general, QStringLiteral("Download Speed:")));
    QVERIFY(!childNamed(general, QStringLiteral("Open Connections:")));
    QCOMPARE(childNamed(general, QStringLiteral("Downloaded Data:"))->text(0),
             QStringLiteral("Downloaded Data: %1").arg(formatByteSize(qint64(32'000'000'000))));
}

// Updated in place, keyed by account: expansion and selection must survive a
// poll, and an account that was removed must go.
void tst_StatisticsPanel::newsServersAreUpdatedInPlace()
{
    StatisticsPanel panel;
    panel.applyUsenetStats(reply());
    auto* tree = panel.findChild<QTreeWidget*>();
    QTreeWidgetItem* servers = usenetRoot(tree)->child(2);
    QCOMPARE(servers->childCount(), 2);

    QTreeWidgetItem* main = servers->child(0);
    QCOMPARE(main->text(0), QStringLiteral("Newshosting"));
    QCOMPARE(main->child(0)->text(0), QStringLiteral("Open Connections: 12"));
    // The meter reads like Options: decimal GB, as the provider bills.
    QVERIFY2(main->child(3)->text(0).startsWith(
                 QStringLiteral("This Period: 412.3 GB of 500.0 GB (82.5%)")),
             qPrintable(main->child(3)->text(0)));
    QCOMPARE(main->child(4)->text(0), QStringLiteral("All Time: 3210.4 GB"));
    QCOMPARE(servers->child(1)->child(3)->text(0),
             QStringLiteral("Block: 120.0 GB of 250.0 GB (48.0%)"));

    main->setExpanded(true);
    panel.applyUsenetStats(reply(false));
    QCOMPARE(servers->childCount(), 1);
    QCOMPARE(servers->child(0), main);
    QVERIFY(main->isExpanded());
}

// MFC StatisticsDlg.cpp:623-651. The port used to print "1:5.00" for a net uploader,
// which reads as the opposite.
void tst_StatisticsPanel::ratioReadsFromTheLargerSide()
{
    QCOMPARE(StatisticsPanel::formatRatio(500, 100), QStringLiteral("5.00 : 1"));
    QCOMPARE(StatisticsPanel::formatRatio(100, 500), QStringLiteral("1 : 5.00"));
    QCOMPARE(StatisticsPanel::formatRatio(100, 100), QStringLiteral("1 : 1.00"));
    QCOMPARE(StatisticsPanel::formatRatio(0, 100), QStringLiteral("Waiting..."));
    QCOMPARE(StatisticsPanel::formatRatio(100, 0), QStringLiteral("Waiting..."));

    StatisticsPanel panel;
    auto* transfer = panel.findChild<QTreeWidget*>()->topLevelItem(0);
    QCOMPARE(transfer->child(1)->text(0),
             QStringLiteral("Session UL:DL Ratio (Friends UL excluded): Waiting..."));
}

// The HTTP Cache branch used to be one node under a red upload arrow with the
// two fetch rows mixed in among the publish ones. Now it is split the way
// Transfer itself is, and the download side carries the counters that say why a
// cache is or is not earning its keep.
void tst_StatisticsPanel::httpCacheSplitsUploadsFromDownloads()
{
    HttpCacheCounters session;
    session.bytesPublished = 19'000'000;
    session.chunksPublished = 2;
    session.bytesSaved = 38'000'000;
    session.bytesFetched = 58'400'000;
    session.chunksFetched = 6;
    session.fetchesFailed = 2;
    session.partsCorrupt = 1;
    session.resumes = 3;
    session.offersReceived = 9;
    session.offersDeclined = 3;
    session.kadChunks = 1;

    HttpCacheCounters cumulative = session;
    cumulative.bytesFetched = 900'000'000;
    cumulative.chunksFetched = 96;

    StatisticsPanel panel;
    panel.updateTree(QCborMap{
        {QStringLiteral("httpCacheSession"), countersToCbor(session)},
        {QStringLiteral("httpCacheCumulative"), countersToCbor(cumulative)},
    });

    auto* transfer = panel.findChild<QTreeWidget*>()->topLevelItem(0);
    QTreeWidgetItem* cache = childNamed(transfer, QStringLiteral("HTTP Cache"));
    QVERIFY(cache);

    QTreeWidgetItem* uploads = childNamed(cache, QStringLiteral("Uploads"));
    QTreeWidgetItem* downloads = childNamed(cache, QStringLiteral("Downloads"));
    QVERIFY(uploads && downloads);

    QTreeWidgetItem* upSession = childNamed(uploads, QStringLiteral("Session"));
    QVERIFY(upSession);
    QCOMPARE(childNamed(upSession, QStringLiteral("Published:"))->text(0),
             QStringLiteral("Published: %1").arg(formatByteSize(19'000'000)));
    QCOMPARE(childNamed(upSession, QStringLiteral("Upload Saved:"))->text(0),
             QStringLiteral("Upload Saved: %1").arg(formatByteSize(38'000'000)));
    // The fetch rows belong to the other side now.
    QVERIFY(!childNamed(upSession, QStringLiteral("Fetched:")));

    QTreeWidgetItem* downSession = childNamed(downloads, QStringLiteral("Session"));
    QVERIFY(downSession);
    QCOMPARE(childNamed(downSession, QStringLiteral("Fetched:"))->text(0),
             QStringLiteral("Fetched: %1").arg(formatByteSize(58'400'000)));

    // A share of everything that got as far as being fetched, 6 of 8.
    QTreeWidgetItem* chunks = childNamed(downSession, QStringLiteral("Chunks Fetched:"));
    QVERIFY(chunks);
    QCOMPARE(chunks->text(0), QStringLiteral("Chunks Fetched: 6 (75.0%)"));
    QCOMPARE(childNamed(chunks, QStringLiteral("Failed:"))->text(0),
             QStringLiteral("Failed: 2 (25.0%)"));
    QCOMPARE(childNamed(chunks, QStringLiteral("Failed Hash Check:"))->text(0),
             QStringLiteral("Failed Hash Check: 1"));
    QCOMPARE(childNamed(chunks, QStringLiteral("Resumed:"))->text(0),
             QStringLiteral("Resumed: 3"));

    QTreeWidgetItem* offers = childNamed(downSession, QStringLiteral("Offers Received:"));
    QVERIFY(offers);
    QCOMPARE(offers->text(0), QStringLiteral("Offers Received: 9"));
    QCOMPARE(childNamed(offers, QStringLiteral("Declined:"))->text(0),
             QStringLiteral("Declined: 3 (33.3%)"));
    QCOMPARE(childNamed(downSession, QStringLiteral("Chunks Found in Kad:"))->text(0),
             QStringLiteral("Chunks Found in Kad: 1"));

    // Cumulative is the same rows against the banked block.
    QTreeWidgetItem* downCum = childNamed(downloads, QStringLiteral("Cumulative"));
    QVERIFY(downCum);
    QCOMPARE(childNamed(downCum, QStringLiteral("Fetched:"))->text(0),
             QStringLiteral("Fetched: %1").arg(formatByteSize(900'000'000)));
}

// Both rows MFC has (StatisticsDlg.cpp:1582-1588), and they read 0 for the life
// of the port until the writers behind them were wired up.
void tst_StatisticsPanel::timeShowsCurrentAndTotalServerDuration()
{
    StatisticsPanel panel;
    panel.updateTree(QCborMap{
        {QStringLiteral("uptime"), 3600},
        {QStringLiteral("serverDuration"), 1800},
        {QStringLiteral("currentServerDuration"), 900},
    });

    QTreeWidget* tree = panel.findChild<QTreeWidget*>();
    QTreeWidgetItem* time = nullptr;
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        if (tree->topLevelItem(i)->text(0) == QStringLiteral("Time Statistics"))
            time = tree->topLevelItem(i);
    }
    QVERIFY(time);

    QTreeWidgetItem* session = childNamed(time, QStringLiteral("Session"));
    QVERIFY(session);
    QCOMPARE(childNamed(session, QStringLiteral("Current Server Duration:"))->text(0),
             QStringLiteral("Current Server Duration: 0:15:00 (25.0%)"));
    QCOMPARE(childNamed(session, QStringLiteral("Total Server Duration:"))->text(0),
             QStringLiteral("Total Server Duration: 0:30:00 (50.0%)"));
}

// MFC StatisticsDlg.cpp:166,178: the download and upload scopes use the Connection page's
// graph maxima. Only the connections scope had a fixed range; the rate ones auto-scaled.
void tst_StatisticsPanel::rateScopesArePinnedToTheGraphMaxima()
{
    thePrefs.setMaxGraphDownloadRate(777);
    thePrefs.setMaxGraphUploadRate(333);
    StatisticsPanel panel;
    panel.applySettings();

    QList<double> uppers;
    for (const StatsGraph* graph : panel.findChildren<StatsGraph*>())
        uppers << graph->yUpper();
    QVERIFY(uppers.contains(777.0));
    QVERIFY(uppers.contains(333.0));
}

QTEST_MAIN(tst_StatisticsPanel)
#include "tst_StatisticsPanel.moc"
