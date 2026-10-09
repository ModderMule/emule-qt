/// @file tst_StatisticsPanel.cpp
/// @brief The Statistics window's Usenet and Kademlia branches, fed wire-shaped replies.
///
/// No daemon: the reply is built here in the wire shape IpcClientHandler sends,
/// which is what the panel actually consumes. Set EMULE_STATS_PANEL_SHOT (Usenet)
/// or EMULE_KAD_STATS_SHOT (Kademlia) to a .png path to also save the fully
/// expanded branch for a visual check.

#include "panels/StatisticsPanel.h"

#include "controls/StatsGraph.h"
#include "prefs/NewsServer.h"
#include "prefs/Preferences.h"
#include "stats/NetworkCounters.h"
#include "utils/StringUtils.h"

#include <QApplication>
#include <QCborArray>
#include <QCborMap>
#include <QSignalSpy>
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

/// A GetKadStats reply in the shape ops::kadStats() builds.
QCborMap kadReply(const QCborArray& sessionCountries)
{
    KadCounters s;
    s.contactsAdded = 400;
    s.contactsVerified = 300;
    s.contactsReplaced = 20;
    s.contactsExpired = 35;
    s.peakContacts = 820;
    s.hellosSent = 1000;
    s.hellosReceived = 750;
    s.lookupResponses = 2100;
    s.searchesNode = 60;
    s.searchesKeyword = 10;
    s.searchesSource = 25;
    s.searchesNotes = 5;
    s.publishes = 44;
    s.udpFirewalledNodes = 30;
    s.udpOpenNodes = 90;
    s.tcpFirewalledNodes = 60;
    s.tcpOpenNodes = 60;
    s.connectedMs = 1'800'000;

    KadCounters cum = s;
    cum.contactsAdded = 90'000;
    cum.peakContacts = 1500;

    const QCborMap current{
        {QStringLiteral("running"), true},
        {QStringLiteral("connected"), true},
        {QStringLiteral("firewalled"), false},
        {QStringLiteral("udpFirewalled"), true},
        {QStringLiteral("lanMode"), false},
        {QStringLiteral("contacts"), 800},
        {QStringLiteral("verified"), 600},
        {QStringLiteral("byType"), QCborArray{400, 200, 100, 90, 10}},
        {QStringLiteral("byVersion"), QCborArray{QCborArray{8, 100}, QCborArray{9, 600},
                                                 QCborArray{10, 100}}},
        {QStringLiteral("users"), 250'000},
        {QStringLiteral("files"), 27'000'000},
        {QStringLiteral("indexedKeywords"), 1200},
        {QStringLiteral("activeSearches"), 7},
    };

    const QCborMap seen{
        {QStringLiteral("session"),
         QCborMap{{QStringLiteral("contacted"), 5000},
                  {QStringLiteral("listed"), 42'000},
                  {QStringLiteral("countries"), sessionCountries}}},
        {QStringLiteral("cumulative"),
         QCborMap{{QStringLiteral("contacted"), 310'000},
                  {QStringLiteral("listed"), 2'400'000},
                  {QStringLiteral("countries"),
                   QCborArray{QCborArray{QStringLiteral("CN"), 200'000},
                              QCborArray{QStringLiteral("IT"), 110'000}}}}},
    };

    return QCborMap{
        {QStringLiteral("session"), countersToCbor(s)},
        {QStringLiteral("cumulative"), countersToCbor(cum)},
        {QStringLiteral("current"), current},
        {QStringLiteral("seen"), seen},
    };
}

QTreeWidgetItem* topLevelNamed(QTreeWidget* tree, const QString& name)
{
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        if (tree->topLevelItem(i)->text(0) == name)
            return tree->topLevelItem(i);
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
    void kademliaBranchShowsLiveTableOnlyInSession();
    void kademliaCountersCarrySharesInBothScopes();
    void kademliaFirewalledRowsSayNaWithoutSamples();
    void kademliaCountriesUpdateInPlace();
    void clientsSeenByCountryInBothScopes();
    void copiesFollowTheExpansionState();
    void htmlMarksSectionsAndExportsAPage();
    void expandMainSectionsOpensOnlySections();
    void foundSourcesListsStatesOriginsAndNetworks();
    void mfcsSectionNodesAreSections();
    void projectedAveragesScaleTheCumulativeFigures();
    void uploadRowsAreNotSwapped();
    void sessionParentsCarryTotalsAndShares();
    void downloadSessionOrderAndMeaning();
    void connectionRowsFollowMfc();
    void clientsShowNetworkPortAndIdent();
    void clientSoftwareHasFixedRowsAndMinorVersions();
    void serversSplitWorkingFromAll();
    void sharedFilesOrderAndHashing();
    void diskSpaceShowsShareAndShortfall();
    void graphScrollsOnePixelPerSample();
    void graphTooltipNamesValueAndTime();
    void graphLegendFollowsTheOptions();
    void graphDoubleClickAsksForTheOptions();
    void treeRemembersExpansionThreeLevelsDeep();
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
             QStringLiteral("Current Server Duration: 15:00 Minutes (25.0%)"));
    QCOMPARE(childNamed(session, QStringLiteral("Total Server Duration:"))->text(0),
             QStringLiteral("Total Server Duration: 30:00 Minutes (50.0%)"));
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

void tst_StatisticsPanel::kademliaBranchShowsLiveTableOnlyInSession()
{
    StatisticsPanel panel;
    auto* tree = panel.findChild<QTreeWidget*>();
    panel.applyKadStats(kadReply({}));

    QTreeWidgetItem* kad = topLevelNamed(tree, QStringLiteral("Kademlia"));
    QVERIFY(kad);
    // Not in MFC, so after MFC's branches — and Usenet stays the last one.
    QCOMPARE(tree->indexOfTopLevelItem(kad), tree->topLevelItemCount() - 2);

    QTreeWidgetItem* session = childNamed(kad, QStringLiteral("Session"));
    QTreeWidgetItem* cumulative = childNamed(kad, QStringLiteral("Cumulative"));
    QVERIFY(session && cumulative);
    QCOMPARE(childNamed(session, QStringLiteral("Status"))->text(0),
             QStringLiteral("Status: Connected, UDP firewalled"));

    QTreeWidgetItem* table = childNamed(session, QStringLiteral("Routing Table"));
    QTreeWidgetItem* contacts = childNamed(table, QStringLiteral("Contacts:"));
    QCOMPARE(contacts->text(0), QStringLiteral("Contacts: 800"));
    QCOMPARE(childNamed(contacts, QStringLiteral("IP Verified"))->text(0),
             QStringLiteral("IP Verified: 600 (75.0%)"));
    QCOMPARE(childNamed(contacts, QStringLiteral("Type 0"))->text(0),
             QStringLiteral("Type 0, Alive over 2 Hours: 400 (50.0%)"));
    QCOMPARE(childNamed(contacts, QStringLiteral("Type 4"))->text(0),
             QStringLiteral("Type 4, Dead: 10 (1.3%)"));

    QTreeWidgetItem* versions = childNamed(table, QStringLiteral("By Version"));
    QCOMPARE(versions->childCount(), 3);
    QCOMPARE(versions->child(0)->text(0), QStringLiteral("Version 10: 100 (12.5%)"));
    QCOMPARE(versions->child(1)->text(0), QStringLiteral("Version 9 (eMule 0.50a): 600 (75.0%)"));

    QCOMPARE(childNamed(childNamed(session, QStringLiteral("Network")),
                        QStringLiteral("Estimated Users"))->text(0),
             QStringLiteral("Estimated Users: 250000"));

    // The routing table as it stands has no cumulative form.
    QTreeWidgetItem* cumTable = childNamed(cumulative, QStringLiteral("Routing Table"));
    QVERIFY(!childNamed(cumulative, QStringLiteral("Status")));
    QVERIFY(!childNamed(cumulative, QStringLiteral("Network")));
    QVERIFY(!childNamed(cumTable, QStringLiteral("Contacts:")));
    QVERIFY(!childNamed(cumTable, QStringLiteral("By Version")));
    QCOMPARE(childNamed(cumTable, QStringLiteral("Most Contacts"))->text(0),
             QStringLiteral("Most Contacts: 1500"));
    QCOMPARE(childNamed(cumTable, QStringLiteral("Contacts Added"))->text(0),
             QStringLiteral("Contacts Added: 90000"));
}

void tst_StatisticsPanel::kademliaCountersCarrySharesInBothScopes()
{
    StatisticsPanel panel;
    auto* tree = panel.findChild<QTreeWidget*>();
    panel.applyKadStats(kadReply({}));
    QTreeWidgetItem* kad = topLevelNamed(tree, QStringLiteral("Kademlia"));

    for (const QString& scopeName : {QStringLiteral("Session"), QStringLiteral("Cumulative")}) {
        QTreeWidgetItem* scope = childNamed(kad, scopeName);
        QTreeWidgetItem* nodes = childNamed(scope, QStringLiteral("Nodes"));
        QTreeWidgetItem* firewalled = childNamed(nodes, QStringLiteral("Firewalled (Kad)"));
        QCOMPARE(childNamed(firewalled, QStringLiteral("UDP"))->text(0),
                 QStringLiteral("UDP: 30 (25.0%)"));
        QCOMPARE(childNamed(firewalled, QStringLiteral("TCP"))->text(0),
                 QStringLiteral("TCP: 60 (50.0%)"));

        QTreeWidgetItem* activity = childNamed(scope, QStringLiteral("Activity"));
        QCOMPARE(childNamed(activity, QStringLiteral("Hellos Answered"))->text(0),
                 QStringLiteral("Hellos Answered: 750 (75.0%)"));
        QTreeWidgetItem* searches = childNamed(activity, QStringLiteral("Searches"));
        QCOMPARE(searches->text(0), QStringLiteral("Searches: 100"));
        QCOMPARE(childNamed(searches, QStringLiteral("Source Searches"))->text(0),
                 QStringLiteral("Source Searches: 25 (25.0%)"));
        // No GetStats yet, so no runtime to take a share of.
        QCOMPARE(childNamed(activity, QStringLiteral("Time Connected"))->text(0),
                 QStringLiteral("Time Connected: %1 %2")
                     .arg(StatisticsPanel::formatDuration(1800),
                          StatisticsPanel::formatPercent(1'800'000, 0)));
    }

    QTreeWidgetItem* sesNodes = childNamed(childNamed(kad, QStringLiteral("Session")),
                                           QStringLiteral("Nodes"));
    QCOMPARE(childNamed(sesNodes, QStringLiteral("Nodes Seen"))->text(0),
             QStringLiteral("Nodes Seen: \u22485000"));
    QCOMPARE(childNamed(sesNodes, QStringLiteral("Nodes Heard Of"))->text(0),
             QStringLiteral("Nodes Heard Of: \u224842000"));
    QTreeWidgetItem* cumNodes = childNamed(childNamed(kad, QStringLiteral("Cumulative")),
                                           QStringLiteral("Nodes"));
    QCOMPARE(childNamed(cumNodes, QStringLiteral("Nodes Seen"))->text(0),
             QStringLiteral("Nodes Seen: \u2248310000"));
}

void tst_StatisticsPanel::kademliaFirewalledRowsSayNaWithoutSamples()
{
    // A firewalled node gets no HELLO_REQs, so all four counters stay 0.
    QCborMap reply = kadReply({});
    for (const QString& scopeName : {QStringLiteral("session"), QStringLiteral("cumulative")}) {
        QCborMap counters = reply.value(scopeName).toMap();
        for (const char* key : {"udpFirewalledNodes", "udpOpenNodes", "tcpFirewalledNodes"})
            counters.insert(QString::fromLatin1(key), 0);
        reply.insert(scopeName, counters);
    }

    StatisticsPanel panel;
    auto* tree = panel.findChild<QTreeWidget*>();
    panel.applyKadStats(reply);
    QTreeWidgetItem* kad = topLevelNamed(tree, QStringLiteral("Kademlia"));

    for (const QString& scopeName : {QStringLiteral("Session"), QStringLiteral("Cumulative")}) {
        QTreeWidgetItem* nodes = childNamed(childNamed(kad, scopeName), QStringLiteral("Nodes"));
        QTreeWidgetItem* firewalled = childNamed(nodes, QStringLiteral("Firewalled (Kad)"));
        QCOMPARE(childNamed(firewalled, QStringLiteral("UDP"))->text(0),
                 QStringLiteral("UDP: n/a"));
        // Samples, none of them firewalled: a real 0, not a missing one.
        QCOMPARE(childNamed(firewalled, QStringLiteral("TCP"))->text(0),
                 QStringLiteral("TCP: 0 (0.0%)"));
    }
}

// Clients > Session / Cumulative: distinct clients by user hash, with countries.
void tst_StatisticsPanel::clientsSeenByCountryInBothScopes()
{
    StatisticsPanel panel;
    auto* tree = panel.findChild<QTreeWidget*>();
    QTreeWidgetItem* clients = topLevelNamed(tree, QStringLiteral("Clients"));
    QVERIFY(clients);

    // MFC's rows stay first; the two scopes follow them.
    QCOMPARE(clients->child(0)->text(0), QStringLiteral("Known Clients: 0"));
    QCOMPARE(clients->child(clients->childCount() - 2)->text(0), QStringLiteral("Session"));
    QCOMPARE(clients->child(clients->childCount() - 1)->text(0), QStringLiteral("Cumulative"));

    const auto scopeMap = [](qint64 seen, qint64 identified, const QCborArray& countries) {
        return QCborMap{{QStringLiteral("seen"), seen},
                        {QStringLiteral("identified"), identified},
                        {QStringLiteral("countries"), countries}};
    };
    const auto reply = [&](const QCborArray& sessionCountries) {
        const QCborMap seen{
            {QStringLiteral("session"), scopeMap(200, 150, sessionCountries)},
            {QStringLiteral("cumulative"),
             scopeMap(90000, 81000, {QCborArray{QStringLiteral("ES"), 50000},
                                     QCborArray{QStringLiteral("IT"), 40000}})},
        };
        return QCborMap{{QStringLiteral("seen"), seen}};
    };

    panel.applyClientStats(reply({QCborArray{QStringLiteral("ES"), 120},
                                  QCborArray{QStringLiteral("FR"), 60},
                                  QCborArray{QString(), 20}}));

    QTreeWidgetItem* session = childNamed(clients, QStringLiteral("Session"));
    QCOMPARE(childNamed(session, QStringLiteral("Clients Seen"))->text(0),
             QStringLiteral("Clients Seen: \u2248200"));
    QCOMPARE(childNamed(session, QStringLiteral("Identified"))->text(0),
             QStringLiteral("Identified: \u2248150 (75.0%)"));
    QTreeWidgetItem* countries = childNamed(session, QStringLiteral("By Country"));
    QCOMPARE(countries->childCount(), 3);
    QCOMPARE(countries->child(0)->text(0), QStringLiteral("Spain (ES): \u2248120 (60.0%)"));
    QCOMPARE(countries->child(2)->text(0), QStringLiteral("Unknown: \u224820 (10.0%)"));

    QTreeWidgetItem* cumulative = childNamed(clients, QStringLiteral("Cumulative"));
    QCOMPARE(childNamed(cumulative, QStringLiteral("Identified"))->text(0),
             QStringLiteral("Identified: \u224881000 (90.0%)"));
    QCOMPARE(childNamed(cumulative, QStringLiteral("By Country"))->childCount(), 2);

    // The next poll reuses the rows, and an empty list leaves "By Country" in place.
    QTreeWidgetItem* spain = countries->child(0);
    panel.applyClientStats(reply({QCborArray{QStringLiteral("FR"), 300},
                                  QCborArray{QStringLiteral("ES"), 120}}));
    QCOMPARE(countries->childCount(), 2);
    QCOMPARE(countries->child(1), spain);
    panel.applyClientStats(reply({}));
    QVERIFY(!countries->isHidden());
    QCOMPARE(countries->childCount(), 0);

    // EMULE_CLIENT_STATS_SHOT=<png>: the branch fully expanded, for a visual check.
    if (const QByteArray shot = qgetenv("EMULE_CLIENT_STATS_SHOT"); !shot.isEmpty()) {
        panel.applyClientStats(reply({QCborArray{QStringLiteral("ES"), 120},
                                      QCborArray{QStringLiteral("FR"), 60},
                                      QCborArray{QString(), 20}}));
        panel.resize(1000, 700);
        tree->collapseAll();
        const auto expandAll = [](auto&& self, QTreeWidgetItem* item) -> void {
            item->setExpanded(true);
            for (int i = 0; i < item->childCount(); ++i)
                self(self, item->child(i));
        };
        expandAll(expandAll, clients);
        panel.show();
        QApplication::processEvents();
        tree->scrollToItem(clients, QAbstractItemView::PositionAtTop);
        QApplication::processEvents();
        QVERIFY(panel.grab().save(QString::fromLocal8Bit(shot)));
    }
}

// Rebuilding the list every poll would collapse it and drop the selection.
void tst_StatisticsPanel::kademliaCountriesUpdateInPlace()
{
    StatisticsPanel panel;
    auto* tree = panel.findChild<QTreeWidget*>();
    panel.applyKadStats(kadReply({QCborArray{QStringLiteral("IT"), 3000},
                                  QCborArray{QStringLiteral("DE"), 1500},
                                  QCborArray{QString(), 500}}));

    QTreeWidgetItem* kad = topLevelNamed(tree, QStringLiteral("Kademlia"));
    QTreeWidgetItem* countries = childNamed(
        childNamed(childNamed(kad, QStringLiteral("Session")), QStringLiteral("Nodes")),
        QStringLiteral("By Country"));
    QVERIFY(countries);
    QCOMPARE(countries->childCount(), 3);
    QCOMPARE(countries->child(0)->text(0), QStringLiteral("Italy (IT): \u22483000 (60.0%)"));
    QCOMPARE(countries->child(2)->text(0), QStringLiteral("Unknown: \u2248500 (10.0%)"));

    QTreeWidgetItem* italy = countries->child(0);
    QTreeWidgetItem* germany = countries->child(1);

    // Germany overtakes, France appears, the unknown bucket goes.
    panel.applyKadStats(kadReply({QCborArray{QStringLiteral("DE"), 4000},
                                  QCborArray{QStringLiteral("IT"), 3000},
                                  QCborArray{QStringLiteral("FR"), 1000}}));
    QCOMPARE(countries->childCount(), 3);
    QCOMPARE(countries->child(0), germany);
    QCOMPARE(countries->child(1), italy);
    QCOMPARE(germany->text(0), QStringLiteral("Germany (DE): \u22484000 (50.0%)"));
    QCOMPARE(countries->child(2)->text(0), QStringLiteral("France (FR): \u22481000 (12.5%)"));

    // The cumulative list is its own.
    QTreeWidgetItem* cumCountries = childNamed(
        childNamed(childNamed(kad, QStringLiteral("Cumulative")), QStringLiteral("Nodes")),
        QStringLiteral("By Country"));
    QCOMPARE(cumCountries->childCount(), 2);
    QVERIFY(cumCountries->child(0)->text(0).startsWith(QStringLiteral("China (CN)")));

    // Nothing seen yet: the row stays, just without children.
    panel.applyKadStats(kadReply({}));
    QVERIFY(!countries->isHidden());
    QCOMPARE(countries->childCount(), 0);

    // EMULE_KAD_STATS_SHOT=<png>: the branch fully expanded, for a visual check.
    if (const QByteArray shot = qgetenv("EMULE_KAD_STATS_SHOT"); !shot.isEmpty()) {
        panel.resize(1200, 1500);
        tree->collapseAll();
        const auto expandAll = [](auto&& self, QTreeWidgetItem* item) -> void {
            item->setExpanded(true);
            for (int i = 0; i < item->childCount(); ++i)
                self(self, item->child(i));
        };
        expandAll(expandAll, kad);
        panel.show();
        QApplication::processEvents();
        tree->scrollToItem(kad, QAbstractItemView::PositionAtTop);
        QApplication::processEvents();
        QVERIFY(panel.grab().save(QString::fromLocal8Bit(shot)));
    }
}

// MFC CStatisticsTree::GetText (srchybrid/StatisticsTree.cpp:371-398)
void tst_StatisticsPanel::copiesFollowTheExpansionState()
{
    StatisticsPanel panel;
    auto* tree = panel.findChild<QTreeWidget*>();
    QVERIFY(tree);
    tree->collapseAll();
    QTreeWidgetItem* transfer = tree->topLevelItem(0);
    QVERIFY(transfer->childCount() > 0);

    // collapsed: the bare line, no header, no line end
    QCOMPARE(panel.treeText(true, transfer), transfer->text(0));

    transfer->setExpanded(true);
    const QString branch = panel.treeText(true, transfer);
    QVERIFY(branch.startsWith(QStringLiteral("eMule Qt v")));
    QVERIFY(branch.contains(QStringLiteral("\r\n\r\n") + transfer->text(0) + QStringLiteral("\r\n")));
    // children three spaces in, grandchildren stay out while their parent is closed
    QVERIFY(branch.contains(QStringLiteral("\r\n   ") + transfer->child(0)->text(0)));
    QVERIFY(!branch.contains(QStringLiteral("\r\n      ")));
    // only this branch
    QVERIFY(!branch.contains(tree->topLevelItem(1)->text(0)));

    const QString visible = panel.treeText(true);
    const QString all = panel.treeText(false);
    QVERIFY(visible.contains(tree->topLevelItem(1)->text(0)));
    QVERIFY(all.size() > visible.size());
    QVERIFY(all.contains(QStringLiteral("\r\n      ")));
}

// MFC CStatisticsTree::GetHTML / ExportHTML
void tst_StatisticsPanel::htmlMarksSectionsAndExportsAPage()
{
    StatisticsPanel panel;
    auto* tree = panel.findChild<QTreeWidget*>();
    QVERIFY(tree);
    tree->collapseAll();
    QTreeWidgetItem* transfer = tree->topLevelItem(0);
    transfer->setExpanded(true);
    QVERIFY(StatisticsPanel::isSection(transfer));
    QVERIFY(!StatisticsPanel::isSection(transfer->child(0)));   // a ratio line

    const QString html = panel.treeHtml(true, transfer);
    QVERIFY(html.startsWith(QStringLiteral("<font face=")));
    QVERIFY(html.endsWith(QStringLiteral("</font>")));
    QVERIFY(html.contains(QStringLiteral("<b>%1</b><br>").arg(transfer->text(0).toHtmlEscaped())));
    QVERIFY(html.contains(QStringLiteral("&nbsp;&nbsp;&nbsp;")
                          + transfer->child(0)->text(0).toHtmlEscaped() + QStringLiteral("<br>")));
    QVERIFY(!html.contains(tree->topLevelItem(1)->text(0).toHtmlEscaped()));

    const QString page = panel.exportPageHtml();
    QVERIFY(page.startsWith(QStringLiteral("<!DOCTYPE HTML SYSTEM>")));
    QVERIFY(page.contains(QStringLiteral("function togglevisible(treepart)")));
    // every node is in, the open one shown and a closed one hidden
    QVERIFY(page.contains(QStringLiteral("<div id=\"T1\" style=\"margin-left:18px\">")));
    QVERIFY(page.contains(QStringLiteral("visibility:hidden; position:absolute")));
    QVERIFY(page.contains(tree->topLevelItem(1)->text(0).toHtmlEscaped()));
    // nothing beside the page: the images are embedded
    QVERIFY(page.contains(QStringLiteral("data:image/gif;base64,R0lG")));
    QVERIFY(!page.contains(QStringLiteral("stats_visible.gif")));
}

// MFC CStatisticsTree::ExpandAll(true) (srchybrid/StatisticsTree.cpp:595-615)
void tst_StatisticsPanel::expandMainSectionsOpensOnlySections()
{
    StatisticsPanel panel;
    auto* tree = panel.findChild<QTreeWidget*>();
    QVERIFY(tree);
    tree->expandAll();
    panel.expandMainSections();

    QTreeWidgetItem* transfer = tree->topLevelItem(0);
    QVERIFY(transfer->isExpanded());
    for (int i = 0; i < transfer->childCount(); ++i) {
        QTreeWidgetItem* child = transfer->child(i);
        if (child->childCount() == 0)
            continue;
        QCOMPARE(child->isExpanded(), StatisticsPanel::isSection(child));
        for (int j = 0; j < child->childCount(); ++j) {
            QTreeWidgetItem* grand = child->child(j);
            for (int k = 0; k < grand->childCount(); ++k)
                if (grand->child(k)->childCount() > 0)
                    QCOMPARE(grand->child(k)->isExpanded(),
                             grand->isExpanded() && StatisticsPanel::isSection(grand->child(k)));
        }
    }
}

// MFC StatisticsDlg.cpp:768-830: the breakdown under "Found Sources".
void tst_StatisticsPanel::foundSourcesListsStatesOriginsAndNetworks()
{
    StatisticsPanel panel;
    QCborArray sources;
    for (int i = 1; i <= 20; ++i)
        sources.append(i);
    panel.updateTree(QCborMap{
        {QStringLiteral("downFoundSources"), 200},
        {QStringLiteral("downSources"), sources},
        {QStringLiteral("downDeadSourcesGlobal"), 4},
        {QStringLiteral("downDeadSourcesPerFile"), 3},
    });

    auto* tree = panel.findChild<QTreeWidget*>();
    const QList<QTreeWidgetItem*> hits =
        tree->findItems(QStringLiteral("Found Sources: 200"), Qt::MatchExactly | Qt::MatchRecursive);
    QCOMPARE(hits.size(), 1);
    QStringList rows;
    for (int i = 0; i < hits.first()->childCount(); ++i)
        rows << hits.first()->child(i)->text(0);

    QCOMPARE(rows.size(), 22);
    QCOMPARE(rows.at(0), QStringLiteral("On Queue: 1"));
    QCOMPARE(rows.at(1), QStringLiteral("Queue Full: 2"));
    QCOMPARE(rows.at(11), QStringLiteral("Asked for another file: 12"));
    QCOMPARE(rows.at(12), QStringLiteral("Unknown: 13"));
    QCOMPARE(rows.at(13), QStringLiteral("via eD2K Server: 14"));
    QCOMPARE(rows.at(16), QStringLiteral("via Passive: 17"));
    QVERIFY2(rows.at(17).startsWith(QStringLiteral("eD2K: 18 (9")), qPrintable(rows.at(17)));
    QVERIFY2(rows.at(19).startsWith(QStringLiteral("eD2K/Kad: 20 (10")), qPrintable(rows.at(19)));
    QVERIFY(rows.at(20).startsWith(QStringLiteral("UDP File Re-asks:")));
    QCOMPARE(rows.at(21), QStringLiteral("Dead Sources: 7 (4 + 3)"));
}

// MFC StatisticsDlg.cpp:2804-2834: the bold nodes, which "Expand Main Sections" opens.
void tst_StatisticsPanel::mfcsSectionNodesAreSections()
{
    StatisticsPanel panel;
    auto* tree = panel.findChild<QTreeWidget*>();
    const auto childNamed = [](QTreeWidgetItem* parent, const QString& name) -> QTreeWidgetItem* {
        for (int i = 0; parent && i < parent->childCount(); ++i) {
            if (parent->child(i)->text(0) == name)
                return parent->child(i);
        }
        return nullptr;
    };

    QTreeWidgetItem* connection = topLevelNamed(tree, QStringLiteral("Connection"));
    QVERIFY(connection);
    for (const char* scope : {"Session", "Cumulative"}) {
        QTreeWidgetItem* scopeItem = childNamed(connection, QString::fromLatin1(scope));
        QVERIFY(scopeItem);
        for (const char* name : {"General", "Uploads", "Downloads"}) {
            QTreeWidgetItem* node = childNamed(scopeItem, QString::fromLatin1(name));
            QVERIFY2(node, name);
            QVERIFY2(StatisticsPanel::isSection(node), name);
        }
    }
    for (const char* top : {"Servers", "Shared Files"}) {
        QTreeWidgetItem* records = childNamed(topLevelNamed(tree, QString::fromLatin1(top)),
                                              QStringLiteral("Records"));
        QVERIFY2(records, top);
        QVERIFY2(StatisticsPanel::isSection(records), top);
        // MFC "StatsRecords" is res\\Records.ico, not the cumulative icon
        QCOMPARE(records->icon(0).pixmap(16).toImage(),
                 QIcon(QStringLiteral(":/icons/Records.ico")).pixmap(16).toImage());
        QVERIFY(records->icon(0).pixmap(16).toImage()
                != QIcon(QStringLiteral(":/icons/StatsCumulative.ico")).pixmap(16).toImage());
    }
}

// MFC StatisticsDlg.cpp:1616-1958: the port had no "Projected Averages" at all.
void tst_StatisticsPanel::projectedAveragesScaleTheCumulativeFigures()
{
    QCOMPARE(StatisticsPanel::projected(100, 86400, 43200), 200.0);   // half a day so far
    QCOMPARE(StatisticsPanel::projected(100, 86400, 0), 0.0);         // never reset: nothing
    QCOMPARE(StatisticsPanel::kProjectionPeriods[2], qint64{31556952});

    StatisticsPanel panel;
    auto* tree = panel.findChild<QTreeWidget*>();
    const auto childStartingWith = [](QTreeWidgetItem* parent, const QString& prefix) -> QTreeWidgetItem* {
        for (int i = 0; parent && i < parent->childCount(); ++i) {
            if (parent->child(i)->text(0).startsWith(prefix))
                return parent->child(i);
        }
        return nullptr;
    };

    QTreeWidgetItem* time = topLevelNamed(tree, QStringLiteral("Time Statistics"));
    QTreeWidgetItem* projected = childStartingWith(time, QStringLiteral("Projected Averages"));
    QVERIFY(projected);
    QVERIFY(StatisticsPanel::isSection(projected));
    QCOMPARE(projected->childCount(), 3);

    // One day of statistics: the daily projection is the cumulative figure itself.
    panel.updateTree(QCborMap{
        {QStringLiteral("statsLastReset"), 1'700'000'000},
        {QStringLiteral("timeSinceReset"), 86400},
        {QStringLiteral("cumTotalUp"), 4096},
        {QStringLiteral("cumUpEmule"), 1024},
        {QStringLiteral("cumUpSuccessful"), 6},
        {QStringLiteral("cumUpFailed"), 4},
        {QStringLiteral("cumUpOhTotal"), 2048},
        {QStringLiteral("cumUpOhTotalPkt"), 50},
        {QStringLiteral("cumDownCompletedFiles"), 3},
    });

    QTreeWidgetItem* daily = childStartingWith(projected, QStringLiteral("Daily"));
    QTreeWidgetItem* yearly = childStartingWith(projected, QStringLiteral("Yearly"));
    QVERIFY(daily && yearly);
    QVERIFY(StatisticsPanel::isSection(daily));
    QTreeWidgetItem* up = childStartingWith(daily, QStringLiteral("Uploads"));
    QTreeWidgetItem* down = childStartingWith(daily, QStringLiteral("Downloads"));
    QVERIFY(up && down);
    QVERIFY(StatisticsPanel::isSection(up));

    QTreeWidgetItem* data = childStartingWith(up, QStringLiteral("Uploaded Data"));
    QVERIFY(data);
    QCOMPARE(data->text(0), QStringLiteral("Uploaded Data: %1").arg(formatByteSize(4096)));
    QTreeWidgetItem* emule = childStartingWith(childStartingWith(data, QStringLiteral("Clients")),
                                               QStringLiteral("eMule"));
    QVERIFY(emule);
    QVERIFY2(emule->text(0).startsWith(QStringLiteral("eMule: %1 (25").arg(formatByteSize(1024))),
             qPrintable(emule->text(0)));
    QCOMPARE(childStartingWith(up, QStringLiteral("Upload Sessions"))->text(0),
             QStringLiteral("Upload Sessions: 10"));
    QCOMPARE(childStartingWith(up, QStringLiteral("Total Overhead"))->text(0),
             QStringLiteral("Total Overhead (Packets): %1 (50)").arg(formatByteSize(2048)));
    QCOMPARE(childStartingWith(down, QStringLiteral("Completed Downloads"))->text(0),
             QStringLiteral("Completed Downloads: 3"));

    // A year at that pace
    QTreeWidgetItem* yearUp = childStartingWith(yearly, QStringLiteral("Uploads"));
    QCOMPARE(childStartingWith(yearUp, QStringLiteral("Upload Sessions"))->text(0),
             QStringLiteral("Upload Sessions: %1").arg(10 * 31556952LL / 86400));

    // The same rows under every period
    const std::function<int(QTreeWidgetItem*)> count = [&count](QTreeWidgetItem* item) {
        int n = 1;
        for (int i = 0; i < item->childCount(); ++i)
            n += count(item->child(i));
        return n;
    };
    QCOMPARE(count(daily), count(yearly));
}

namespace {

QTreeWidgetItem* path(QTreeWidget* tree, const QStringList& names)
{
    QTreeWidgetItem* item = topLevelNamed(tree, names.first());
    for (qsizetype i = 1; item && i < names.size(); ++i)
        item = childNamed(item, names.at(i));
    return item;
}

QStringList childTexts(QTreeWidgetItem* parent)
{
    QStringList out;
    for (int i = 0; parent && i < parent->childCount(); ++i)
        out << parent->child(i)->text(0);
    return out;
}

} // namespace

// MFC StatisticsDlg.cpp:1183-1189. upQueueLength is the slot count, upWaiting the
// waiting list; the two rows used to show each other's number.
void tst_StatisticsPanel::uploadRowsAreNotSwapped()
{
    StatisticsPanel panel;
    panel.updateTree(QCborMap{
        {QStringLiteral("upActive"), 3},
        {QStringLiteral("upQueueLength"), 5},
        {QStringLiteral("upWaiting"), 70},
    });
    auto* session = path(panel.findChild<QTreeWidget*>(),
                         {QStringLiteral("Transfer"), QStringLiteral("Uploads"), QStringLiteral("Session")});
    QVERIFY(session);
    const QStringList rows = childTexts(session);
    const auto at = rows.indexOf(QStringLiteral("Active Uploads/Needed to fill Bandwidth: 3"));
    QVERIFY2(at >= 0, qPrintable(rows.join(u'\n')));
    QCOMPARE(rows.at(at + 1), QStringLiteral("Total Uploads: 5"));
    QCOMPARE(rows.at(at + 2), QStringLiteral("Waiting Uploads: 70"));
}

// MFC :1192-1215 and :840-862.
void tst_StatisticsPanel::sessionParentsCarryTotalsAndShares()
{
    StatisticsPanel panel;
    panel.updateTree(QCborMap{
        {QStringLiteral("sessionSentBytes"), 4000},
        {QStringLiteral("upQueueLength"), 1},          // a running upload is a good session
        {QStringLiteral("upSuccessful"), 2},
        {QStringLiteral("upFailed"), 1},
        {QStringLiteral("downSuccessful"), 1},
        {QStringLiteral("downTransferring"), 2},
        {QStringLiteral("downFailed"), 1},
    });
    auto* tree = panel.findChild<QTreeWidget*>();
    auto* up = path(tree, {QStringLiteral("Transfer"), QStringLiteral("Uploads"),
                           QStringLiteral("Session"), QStringLiteral("Upload Sessions")});
    QVERIFY(up);
    QCOMPARE(up->text(0), QStringLiteral("Upload Sessions: 4"));
    QCOMPARE(up->child(0)->text(0), QStringLiteral("Total successful upload sessions: 3 (75.00%)"));
    QCOMPARE(up->child(1)->text(0), QStringLiteral("Total failed upload sessions: 1 (25.00%)"));
    QCOMPARE(up->child(2)->text(0), QStringLiteral("Average Upload Per Session: %1").arg(formatByteSize(1333)));

    auto* down = path(tree, {QStringLiteral("Transfer"), QStringLiteral("Downloads"),
                             QStringLiteral("Session"), QStringLiteral("Download Sessions")});
    QVERIFY(down);
    QCOMPARE(down->text(0), QStringLiteral("Download Sessions: 4"));
    QCOMPARE(down->child(0)->text(0), QStringLiteral("Successful Download Sessions: 3 (75.0%)"));
    QCOMPARE(down->child(1)->text(0), QStringLiteral("Failed Download Sessions: 1 (25.0%)"));

    // Nothing uploaded yet: no division, and no stale figure from the last poll.
    panel.updateTree({});
    QCOMPARE(up->text(0), QStringLiteral("Upload Sessions: 0"));
    QCOMPARE(up->child(1)->text(0), QStringLiteral("Total failed upload sessions: 0 (0.00%)"));
    QCOMPARE(up->child(2)->text(0), QStringLiteral("Average Upload Per Session: Waiting..."));
}

// MFC :757-765: transferring sources, not files; Completed sits second.
void tst_StatisticsPanel::downloadSessionOrderAndMeaning()
{
    StatisticsPanel panel;
    panel.updateTree(QCborMap{
        {QStringLiteral("downFileCount"), 40},
        {QStringLiteral("downTransferring"), 6},
        {QStringLiteral("completedDownloads"), 2},
    });
    auto* session = path(panel.findChild<QTreeWidget*>(),
                         {QStringLiteral("Transfer"), QStringLiteral("Downloads"), QStringLiteral("Session")});
    const QStringList rows = childTexts(session);
    QVERIFY(rows.at(0).startsWith(QStringLiteral("Downloaded Data:")));
    QCOMPARE(rows.at(1), QStringLiteral("Completed Downloads: 2"));
    QCOMPARE(rows.at(2), QStringLiteral("Active Downloads (chunks): 6"));
    QVERIFY(rows.at(3).startsWith(QStringLiteral("Found Sources:")));
    QVERIFY(rows.at(4).startsWith(QStringLiteral("Download Sessions:")));
}

// MFC :1413-1439, :1449, :1471, :1488-1500.
void tst_StatisticsPanel::connectionRowsFollowMfc()
{
    StatisticsPanel panel;
    QCborMap stats{
        {QStringLiteral("reconnects"), 2},
        {QStringLiteral("connActive"), 30},
        {QStringLiteral("connHalfOpen"), 4},
        {QStringLiteral("connComplete"), 20},
        {QStringLiteral("connAverage"), 17.8},
        {QStringLiteral("connPeak"), 55},
        {QStringLiteral("connMaxReached"), 0},
        {QStringLiteral("avgUpSession"), 2048.0},
        {QStringLiteral("cumConnAverage"), 21},
    };
    panel.updateTree(stats);
    auto* tree = panel.findChild<QTreeWidget*>();
    auto* general = path(tree, {QStringLiteral("Connection"), QStringLiteral("Session"),
                                QStringLiteral("General")});
    QCOMPARE(childTexts(general),
             (QStringList{QStringLiteral("Reconnects: 2"),
                          QStringLiteral("Active Connections (estimate): 30 (Half:4 | Compl:20 | Other:6)"),
                          QStringLiteral("Average Connections (estimate): 17"),
                          QStringLiteral("Peak Connections (estimate): 55"),
                          QStringLiteral("Max Connection Limit Reached: 0")}));

    // The limit row is stamped when the count moves and keeps that stamp afterwards.
    stats.insert(QStringLiteral("connMaxReached"), 3);
    panel.updateTree(stats);
    const QString stamped = general->child(4)->text(0);
    QVERIFY2(stamped.startsWith(QStringLiteral("Max Connection Limit Reached: 3 : ")), qPrintable(stamped));
    QVERIFY(stamped.size() > QStringLiteral("Max Connection Limit Reached: 3 : ").size());
    panel.updateTree(stats);
    QCOMPARE(general->child(4)->text(0), stamped);

    auto* uploads = path(tree, {QStringLiteral("Connection"), QStringLiteral("Session"),
                                QStringLiteral("Uploads")});
    QVERIFY(uploads->child(0)->text(0).startsWith(QStringLiteral("Upload Speed:")));
    QVERIFY2(uploads->child(1)->text(0).startsWith(QStringLiteral("Average Uploadrate: 2")),
             qPrintable(uploads->child(1)->text(0)));

    auto* cumGeneral = path(tree, {QStringLiteral("Connection"), QStringLiteral("Cumulative"),
                                   QStringLiteral("General")});
    const QStringList cum = childTexts(cumGeneral);
    QVERIFY(cum.at(0).startsWith(QStringLiteral("Reconnects:")));
    QCOMPARE(cum.at(1), QStringLiteral("Average Connections (estimate): 21"));
    QVERIFY(cum.at(2).startsWith(QStringLiteral("Peak Connections (estimate):")));
    QVERIFY(cum.at(3).startsWith(QStringLiteral("Max Connection Limit Reached:")));
}

// MFC :1985-1986, :2266-2283, :2308, order of :2732-2751.
void tst_StatisticsPanel::clientsShowNetworkPortAndIdent()
{
    StatisticsPanel panel;
    panel.updateTree(QCborMap{
        {QStringLiteral("knownClients"), 200},
        {QStringLiteral("clientCensus"),
         QCborMap{{QStringLiteral("identOk"), 30}, {QStringLiteral("identFailed"), 10},
                  {QStringLiteral("problematic"), 5}, {QStringLiteral("portDefault"), 50},
                  {QStringLiteral("portOther"), 150}, {QStringLiteral("netEd2k"), 120},
                  {QStringLiteral("netKad"), 100}, {QStringLiteral("netBoth"), 60},
                  {QStringLiteral("netUnknown"), 40}}},
    });
    auto* clients = topLevelNamed(panel.findChild<QTreeWidget*>(), QStringLiteral("Clients"));
    QVERIFY(clients);
    QStringList heads;
    for (const QString& row : childTexts(clients))
        heads << row.section(u':', 0, 0);
    const QStringList wanted{QStringLiteral("Known Clients"), QStringLiteral("Client Software"),
                             QStringLiteral("Network"), QStringLiteral("Port"), QStringLiteral("Low ID"),
                             QStringLiteral("Secure Ident (OK "), QStringLiteral("Problematic"),
                             QStringLiteral("Banned Clients"), QStringLiteral("Filtered Clients")};
    QCOMPARE(heads.mid(0, wanted.size()), wanted);

    QCOMPARE(childTexts(childNamed(clients, QStringLiteral("Network"))),
             (QStringList{QStringLiteral("eD2K: 120 (60.0%)"), QStringLiteral("Kad: 100 (50.0%)"),
                          QStringLiteral("eD2K/Kad: 60 (30.0%)"), QStringLiteral("Unknown: 40 (20.0%)")}));
    QCOMPARE(childTexts(childNamed(clients, QStringLiteral("Port"))),
             (QStringList{QStringLiteral("Default: 50 (25.0%)"), QStringLiteral("Other: 150 (75.0%)")}));
    QCOMPARE(childNamed(clients, QStringLiteral("Secure Ident"))->text(0),
             QStringLiteral("Secure Ident (OK : Failed ): 30 (75.0%) : 10 (25.0%)"));
    QCOMPARE(childNamed(clients, QStringLiteral("Problematic"))->text(0),
             QStringLiteral("Problematic: 5 (2.5%)"));
}

// MFC :1995-2077: eight rows always, the four most used versions directly, the rest
// under "Minor". Versions for every software and the mod level are the port's own.
void tst_StatisticsPanel::clientSoftwareHasFixedRowsAndMinorVersions()
{
    QCborArray versions;
    for (int i = 0; i < 6; ++i)
        versions.append(QCborMap{{QStringLiteral("l"), QStringLiteral("v0.%1").arg(60 - i)},
                                 {QStringLiteral("c"), 10 - i}});       // 10,9,8,7,6,5 = 45
    StatisticsPanel panel;
    panel.updateTree(QCborMap{
        {QStringLiteral("knownClients"), 50},
        {QStringLiteral("clientSoftwareStats"),
         QCborArray{QCborMap{{QStringLiteral("n"), QStringLiteral("eMule")}, {QStringLiteral("c"), 45},
                             {QStringLiteral("v"), versions}},
                    QCborMap{{QStringLiteral("n"), QStringLiteral("URL")}, {QStringLiteral("c"), 5}}}},
    });
    auto* soft = path(panel.findChild<QTreeWidget*>(),
                      {QStringLiteral("Clients"), QStringLiteral("Client Software")});
    QVERIFY(soft);
    QCOMPARE(childTexts(soft),
             (QStringList{QStringLiteral("eMule: 45 (90.0%)"), QStringLiteral("eD Hybrid: 0 (0.0%)"),
                          QStringLiteral("eDonkey: 0 (0.0%)"), QStringLiteral("aMule: 0 (0.0%)"),
                          QStringLiteral("MLdonkey: 0 (0.0%)"), QStringLiteral("Shareaza: 0 (0.0%)"),
                          QStringLiteral("eM Compat: 0 (0.0%)"), QStringLiteral("Unknown: 0 (0.0%)"),
                          QStringLiteral("URL: 5 (10.0%)")}));

    QTreeWidgetItem* emule = soft->child(0);
    QCOMPARE(emule->childCount(), 5);
    QCOMPARE(emule->child(0)->text(0), QStringLiteral("v0.60: 10 (22.2%)"));
    QCOMPARE(emule->child(3)->text(0), QStringLiteral("v0.57: 7 (15.6%)"));
    QCOMPARE(emule->child(4)->text(0), QStringLiteral("Minor: 11 (24.4%)"));
    QCOMPARE(childTexts(emule->child(4)),
             (QStringList{QStringLiteral("v0.56: 6 (13.3%)"), QStringLiteral("v0.55: 5 (11.1%)")}));
}

// MFC :2320-2354, ServerList.cpp:347-381.
void tst_StatisticsPanel::serversSplitWorkingFromAll()
{
    StatisticsPanel panel;
    panel.updateTree(QCborMap{
        {QStringLiteral("srvWorking"), 8}, {QStringLiteral("srvFailed"), 2},
        {QStringLiteral("srvTotal"), 10}, {QStringLiteral("srvDeleted"), 3},
        {QStringLiteral("srvUsers"), 1'500'000}, {QStringLiteral("srvLowIDUsers"), 300'000},
        {QStringLiteral("srvFiles"), 90'000'000},
        {QStringLiteral("srvTotalUsers"), 1'600'000}, {QStringLiteral("srvTotalFiles"), 95'000'000},
        {QStringLiteral("srvOccupation"), 42.5},
    });
    auto* servers = topLevelNamed(panel.findChild<QTreeWidget*>(), QStringLiteral("Servers"));
    QVERIFY(servers);
    const QStringList rows = childTexts(servers);
    QCOMPARE(rows.mid(0, 6),
             (QStringList{QStringLiteral("Working Servers: 8"), QStringLiteral("Failed Servers: 2"),
                          QStringLiteral("Deleted Servers: 3"), QStringLiteral("Total: 10"),
                          QStringLiteral("Total Users: %1").arg(formatShortNumber(1'600'000)),
                          QStringLiteral("Total Files: %1").arg(formatShortNumber(95'000'000))}));
    QCOMPARE(childTexts(servers->child(0)),
             (QStringList{QStringLiteral("Users on Working Servers: %1; Low ID: %2 (20.0%)")
                              .arg(formatShortNumber(1'500'000), formatShortNumber(300'000)),
                          QStringLiteral("Files on Working Servers: %1").arg(formatShortNumber(90'000'000)),
                          QStringLiteral("Server Occupation: 42.50%")}));
}

// MFC :2373-2408.
void tst_StatisticsPanel::sharedFilesOrderAndHashing()
{
    StatisticsPanel panel;
    panel.updateTree(QCborMap{
        {QStringLiteral("sharedCount"), 4}, {QStringLiteral("sharedSize"), 4096},
        {QStringLiteral("sharedLargest"), 2048}, {QStringLiteral("sharedHashing"), 2},
    });
    auto* shared = topLevelNamed(panel.findChild<QTreeWidget*>(), QStringLiteral("Shared Files"));
    QVERIFY(shared);
    const QStringList rows = childTexts(shared);
    QCOMPARE(rows.at(0), QStringLiteral("Number of Shared Files: 4 (2 hashing)"));
    QCOMPARE(rows.at(1), QStringLiteral("Average file size: %1").arg(formatByteSize(1024)));
    QCOMPARE(rows.at(2), QStringLiteral("Largest Shared File: %1").arg(formatByteSize(2048)));
    QCOMPARE(rows.at(3), QStringLiteral("Total size of Shared Files: %1").arg(formatByteSize(4096)));

    QStringList records;
    for (const QString& row : childTexts(childNamed(shared, QStringLiteral("Records"))))
        records << row.section(u':', 0, 0);
    QCOMPARE(records, (QStringList{QStringLiteral("Max. Files Ever Shared"),
                                   QStringLiteral("Largest Average File Size"),
                                   QStringLiteral("Largest Shared File"),
                                   QStringLiteral("Largest Share Size")}));

    panel.updateTree(QCborMap{{QStringLiteral("sharedCount"), 4}});
    QCOMPARE(shared->child(0)->text(0), QStringLiteral("Number of Shared Files: 4"));
}

// MFC :2412-2437.
void tst_StatisticsPanel::diskSpaceShowsShareAndShortfall()
{
    StatisticsPanel panel;
    const qint64 gib = 1024LL * 1024 * 1024;
    panel.updateTree(QCborMap{
        {QStringLiteral("totalDownCount"), 3}, {QStringLiteral("totalDownSize"), 8 * gib},
        {QStringLiteral("totalDownDone"), 2 * gib}, {QStringLiteral("totalDownLeft"), 6 * gib},
        {QStringLiteral("totalDownNeeded"), 6 * gib}, {QStringLiteral("freeTempSpace"), 4 * gib},
    });
    auto* disk = topLevelNamed(panel.findChild<QTreeWidget*>(), QStringLiteral("Disk Space"));
    QVERIFY(disk);
    QCOMPARE(childTexts(disk),
             (QStringList{QStringLiteral("Number of Downloads: 3"),
                          QStringLiteral("Total Size of Downloads: %1").arg(formatByteSize(8 * gib)),
                          QStringLiteral("Total Completed Size: %1 (25%)").arg(formatByteSize(2 * gib)),
                          QStringLiteral("Total Size Left to Transfer: %1").arg(formatByteSize(6 * gib)),
                          QStringLiteral("Free Space on Tempdrive: %1 (you need to free %2!)")
                              .arg(formatByteSize(4 * gib), formatByteSize(2 * gib)),
                          QStringLiteral("Additional Space Needed for Downloads: %1")
                              .arg(formatByteSize(6 * gib))}));
}

// MFC OScopeCtrl.cpp:137-139, 598-608: a sample is a pixel and the newest one sits at
// the right edge. The plot used to stretch whatever it had over the full width.
void tst_StatisticsPanel::graphScrollsOnePixelPerSample()
{
    QCOMPARE(StatsGraph::sampleX(500, 10, 9), 500);
    QCOMPARE(StatsGraph::sampleX(500, 10, 8), 499);
    QCOMPARE(StatsGraph::sampleX(500, 10, 0), 491);
    QCOMPARE(StatsGraph::secondsAgoAt(500, 500, 3.0), 0);
    QCOMPARE(StatsGraph::secondsAgoAt(400, 500, 3.0), 300);
    QCOMPARE(StatsGraph::spanCaption(1200, 3.0), formatSecondsHM(3600));
    QCOMPARE(StatsGraph::spanCaption(1200, 0.0), QStringLiteral("Stopped"));

    // Painted: two samples of a filled series colour two columns at the right edge
    // and leave the left of the plot empty.
    StatsGraph graph(1);
    graph.resize(400, 160);
    graph.setBackgroundColor(Qt::black);
    graph.setGridColor(Qt::black);
    graph.setSeriesInfo(0, QString(), Qt::red, true);
    graph.setYRange(0, 10);
    graph.appendPoints({10.0});
    graph.appendPoints({10.0});
    const QImage shot = graph.grab().toImage();
    const QRect plot = graph.plotRect();
    const int midY = plot.center().y();
    QVERIFY(shot.pixelColor(plot.right() - 1, midY).red() > 0);
    QCOMPARE(shot.pixelColor(plot.left() + plot.width() / 2, midY), QColor(Qt::black));
    QCOMPARE(shot.pixelColor(plot.left() + 5, midY), QColor(Qt::black));
}

// MFC OScopeCtrl.cpp:869-886.
void tst_StatisticsPanel::graphTooltipNamesValueAndTime()
{
    StatsGraph graph(1);
    graph.resize(400, 160);
    graph.setYUnits(QStringLiteral("Download Speed"));
    graph.setYRange(0, 100);
    graph.setSampleIntervalSec(3.0);
    const QRect plot = graph.plotRect();
    const QDateTime now(QDate(2026, 10, 9), QTime(12, 0, 0));

    const QString text =
        graph.tooltipAt(QPoint(plot.right() - 100, plot.top() + plot.height() / 2), now);
    QVERIFY2(text.startsWith(QStringLiteral("Download Speed: ")), qPrintable(text));
    const int value = text.section(u' ', 2, 2).toInt();
    QVERIFY2(value >= 48 && value <= 52, qPrintable(text));
    QVERIFY2(text.contains(QLocale().toString(now.addSecs(-300), QLocale::ShortFormat)), qPrintable(text));
    QVERIFY2(text.endsWith(QStringLiteral("(5:00 Minutes ago)")), qPrintable(text));

    QVERIFY(graph.tooltipAt(QPoint(1, 1), now).isEmpty());   // axis labels, not the plot
}

// MFC StatisticsDlg.cpp:549-561: the averaging window is named, and so is each scope.
void tst_StatisticsPanel::graphLegendFollowsTheOptions()
{
    thePrefs.setStatsAverageMinutes(7);
    StatisticsPanel panel;
    const QList<StatsGraph*> graphs = panel.findChildren<StatsGraph*>();
    QCOMPARE(graphs.size(), 3);
    QStringList units;
    for (const StatsGraph* graph : graphs) {
        units << graph->yUnits();
        if (graph->seriesCount() > 4)
            QCOMPARE(graph->seriesLabel(4), QStringLiteral("Friend upload"));
    }
    units.sort();
    QCOMPARE(units, (QStringList{QStringLiteral("Connections"), QStringLiteral("Download Speed"),
                                 QStringLiteral("Upload Speed")}));
    int named = 0;
    for (const StatsGraph* graph : graphs)
        named += graph->seriesLabel(1) == QStringLiteral("Average (7 mins)");
    QCOMPARE(named, 2);

    thePrefs.setStatsAverageMinutes(3);
    panel.applySettings();
    named = 0;
    for (const StatsGraph* graph : graphs)
        named += graph->seriesLabel(1) == QStringLiteral("Average (3 mins)");
    QCOMPARE(named, 2);

    // "Fill graphs" fills one series per scope, as MFC SetBarsPlot.
    thePrefs.setFillGraphs(true);
    panel.applySettings();
    for (const StatsGraph* graph : graphs) {
        int filled = 0;
        for (int i = 0; i < graph->seriesCount(); ++i)
            filled += graph->seriesFilled(i);
        QCOMPARE(filled, 1);
    }
    thePrefs.setFillGraphs(false);
}

void tst_StatisticsPanel::graphDoubleClickAsksForTheOptions()
{
    StatisticsPanel panel;
    QSignalSpy asked(&panel, &StatisticsPanel::graphOptionsRequested);
    StatsGraph* graph = panel.findChildren<StatsGraph*>().first();
    QTest::mouseDClick(graph, Qt::LeftButton, {}, QPoint(50, 50));
    QCOMPARE(asked.size(), 1);
}

void tst_StatisticsPanel::treeRemembersExpansionThreeLevelsDeep()
{
    // Walks Transfer > Downloads > Session > Downloaded Data in a panel's tree.
    const auto path = [](StatisticsPanel& panel) {
        auto* tree = panel.findChild<QTreeWidget*>();
        QList<QTreeWidgetItem*> items{topLevelNamed(tree, QStringLiteral("Transfer"))};
        for (const char* name : {"Downloads", "Session", "Downloaded Data:"})
            items << childNamed(items.last(), QString::fromLatin1(name));
        return items;
    };

    {
        StatisticsPanel panel;
        panel.findChild<QTreeWidget*>()->collapseAll();
        for (QTreeWidgetItem* item : path(panel)) {
            QVERIFY(item);
            item->setExpanded(true);
        }
    }

    StatisticsPanel panel;
    const QList<QTreeWidgetItem*> items = path(panel);
    QVERIFY(items[0]->isExpanded());
    QVERIFY(items[1]->isExpanded());
    QVERIFY(items[2]->isExpanded());    // the level that used to be forgotten
    QVERIFY(!items[3]->isExpanded());   // one deeper is still not kept
    // Collapsing is remembered as well.
    items[2]->setExpanded(false);
    StatisticsPanel again;
    QVERIFY(!path(again)[2]->isExpanded());
    QVERIFY(path(again)[1]->isExpanded());
}

QTEST_MAIN(tst_StatisticsPanel)
#include "tst_StatisticsPanel.moc"
