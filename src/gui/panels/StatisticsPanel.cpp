#include "pch.h"
/// @file StatisticsPanel.cpp
/// @brief Statistics panel — tree view + oscilloscope graphs — implementation.

#include "panels/StatisticsPanel.h"

#include "app/IpcClient.h"
#include "app/UiState.h"
#include "controls/StatsGraph.h"
#include "prefs/NewsServer.h"
#include "prefs/Preferences.h"
#include "stats/NetworkCounters.h"
#include "utils/PanelPoller.h"
#include "utils/StringUtils.h"

#include "IpcMessage.h"

#include <QApplication>
#include <QCborArray>
#include <QCborMap>
#include <QClipboard>
#include <QDateTime>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QSet>
#include <QSplitter>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <tuple>

namespace eMule {

using namespace Ipc;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static double cborDouble(const QCborMap& m, QLatin1StringView key)
{
    auto it = m.find(QString(key));
    if (it == m.end())
        return 0.0;
    if (it->isDouble())
        return it->toDouble();
    return static_cast<double>(it->toInteger());
}

static qint64 cborInt(const QCborMap& m, QLatin1StringView key)
{
    auto it = m.find(QString(key));
    if (it == m.end())
        return 0;
    if (it->isInteger())
        return it->toInteger();
    return static_cast<qint64>(it->toDouble());
}

// Client type labels — shared across session and cumulative trees
static const char* const kUpClientLabels[] = {
    "eMule", "eD Hybrid", "eDonkey", "aMule", "MLdonkey", "Shareaza", "eM Compat"
};
static const char* const kDownClientLabels[] = {
    "eMule", "eD Hybrid", "eDonkey", "aMule", "MLdonkey", "Shareaza", "eM Compat", "URL"
};

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

StatisticsPanel::StatisticsPanel(QWidget* parent)
    : QWidget(parent)
{
    setupUi();

    // Both pollers are gated on this panel being on screen, like every other panel.
    // That used to be unsafe here — the graphs were fed from whatever the last poll
    // returned, so a hidden panel would have drawn a flat line of stale values — but
    // the daemon owns the sampling now, and the next GetStatsHistory replays every
    // sample taken while we were away.
    m_treePoller = new PanelPoller(this, [this] { requestStats(); });
    m_graphPoller = new PanelPoller(this, [this] { requestGraphHistory(); });
    m_graphPoller->setInterval(3000);
}

void StatisticsPanel::setIpcClient(IpcClient* client)
{
    m_ipc = client;

    if (m_ipc && m_ipc->isConnected()) {
        m_treePoller->setInterval(m_ipc->pollingInterval());
        m_treePoller->setEnabled(true);
        m_graphPoller->setEnabled(true);
        applySettings();
    } else if (m_ipc) {
        connect(m_ipc, &IpcClient::connected, this, [this]() {
            m_treePoller->setInterval(m_ipc->pollingInterval());
            m_treePoller->setEnabled(true);
            m_graphPoller->setEnabled(true);
            applySettings();
        });
        connect(m_ipc, &IpcClient::disconnected, this, [this]() {
            m_treePoller->setEnabled(false);
            m_graphPoller->setEnabled(false);
            // Leave the traces on screen: they are the daemon's, and reconnecting to
            // the same daemon resumes them. A daemon that restarted reports a new
            // epoch, which clears them at that point instead.
        });
    } else {
        m_treePoller->setEnabled(false);
        m_graphPoller->setEnabled(false);
    }
}

void StatisticsPanel::applySettings()
{
    // graphsUpdateSec is the daemon's sampling interval; here it only says how often
    // to collect what the daemon has taken. Polling stays on when it is 0 (graphs
    // disabled) — the replies are simply empty, and turning it back on needs no
    // restart of anything.
    const uint32_t graphSec = thePrefs.graphsUpdateSec();
    m_graphPoller->setInterval(static_cast<int>(graphSec > 0 ? graphSec : 3) * 1000);

    const uint32_t statsSec = thePrefs.statsUpdateSec();
    if (statsSec > 0) {
        m_treePoller->setInterval(static_cast<int>(statsSec) * 1000);
        m_treePoller->setEnabled(m_ipc && m_ipc->isConnected());
    } else {
        m_treePoller->setEnabled(false);
    }

    // Colours come from uistate.yml, keyed by MFC's own indices — the mapping is
    // CStatisticsDlg::ApplyStatsColor (srchybrid/StatisticsDlg.cpp:522-543). They are
    // GUI-only state: the daemon owns preferences.yml and has no use for a palette.
    for (auto* graph : {m_graphDown, m_graphUp, m_graphConn}) {
        graph->setBackgroundColor(theUiState.statsColor(0));
        graph->setGridColor(theUiState.statsColor(1));
        graph->setFillAll(thePrefs.fillGraphs());
    }

    m_graphDown->setSeriesColor(0, theUiState.statsColor(4));    // Session average
    m_graphDown->setSeriesColor(1, theUiState.statsColor(3));    // Average
    m_graphDown->setSeriesColor(2, theUiState.statsColor(2));    // Current
    m_graphDown->setSeriesColor(3, theUiState.statsColor(15));   // Usenet (not in MFC)

    m_graphUp->setSeriesColor(0, theUiState.statsColor(7));      // Session average
    m_graphUp->setSeriesColor(1, theUiState.statsColor(6));      // Average
    m_graphUp->setSeriesColor(2, theUiState.statsColor(5));      // Current
    m_graphUp->setSeriesColor(3, theUiState.statsColor(14));     // Current excl. overhead
    m_graphUp->setSeriesColor(4, theUiState.statsColor(13));     // Friend slots

    m_graphConn->setSeriesColor(0, theUiState.statsColor(8));    // Active connections
    m_graphConn->setSeriesColor(1, theUiState.statsColor(10));   // Active uploads
    m_graphConn->setSeriesColor(2, theUiState.statsColor(9));    // Total uploads
    m_graphConn->setSeriesColor(3, theUiState.statsColor(12));   // Active downloads

    auto connMax = static_cast<double>(thePrefs.statsConnectionsMax());
    if (connMax > 0)
        m_graphConn->setYRange(0, connMax);
    else
        m_graphConn->setYRange(0, 0);

    // MFC StatisticsDlg.cpp:166,178: the rate scopes are pinned to the Connection page's
    // graph maxima, not fitted to the data. 0 falls back to auto-scale.
    m_graphDown->setYRange(0, static_cast<double>(thePrefs.maxGraphDownloadRate()));
    m_graphUp->setYRange(0, static_cast<double>(thePrefs.maxGraphUploadRate()));
}

// ---------------------------------------------------------------------------
// UI Setup
// ---------------------------------------------------------------------------

void StatisticsPanel::setupUi()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    m_hSplitter = new QSplitter(Qt::Horizontal, this);
    mainLayout->addWidget(m_hSplitter);

    auto* treeContainer = new QWidget(this);
    auto* treeLayout = new QVBoxLayout(treeContainer);
    treeLayout->setContentsMargins(2, 2, 2, 2);

    // Header bar: MFC puts the tree menu behind a button here and shows the last
    // reset date beside it (IDC_BNMENU / IDC_STATIC_LASTRESET,
    // srchybrid/StatisticsDlg.cpp:2566-2578).
    auto* headerBar = new QWidget(treeContainer);
    auto* headerLayout = new QHBoxLayout(headerBar);
    headerLayout->setContentsMargins(2, 0, 2, 2);
    headerLayout->setSpacing(6);

    m_menuButton = new QToolButton(headerBar);
    m_menuButton->setArrowType(Qt::DownArrow);
    m_menuButton->setAutoRaise(true);
    m_menuButton->setFixedSize(20, 20);   // MFC's is a small square, about text height
    m_menuButton->setToolTip(tr("Statistics Tree"));
    connect(m_menuButton, &QToolButton::clicked, this, [this]() {
        QMenu* menu = buildStatsMenu();
        menu->popup(m_menuButton->mapToGlobal(QPoint(0, m_menuButton->height())));
    });
    headerLayout->addWidget(m_menuButton);

    m_labelLastReset = new QLabel(tr("Statistics last reset: %1").arg(tr("Unknown")), headerBar);
    headerLayout->addWidget(m_labelLastReset, 1);

    treeLayout->addWidget(headerBar);

    // Not a ListTreeWidget: single column with a hidden header, so there is no
    // column layout to persist. Its expansion state is kept by bindStatsTree().
    m_tree = new QTreeWidget(treeContainer);
    m_tree->setHeaderHidden(true);
    m_tree->setColumnCount(1);
    m_tree->setRootIsDecorated(true);
    m_tree->setIndentation(16);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tree, &QTreeWidget::customContextMenuRequested,
            this, &StatisticsPanel::onContextMenu);

    treeLayout->addWidget(m_tree);
    m_hSplitter->addWidget(treeContainer);

    auto* graphSplitter = new QSplitter(Qt::Vertical, this);

    // Labels here, colours from applySettings() — which is also what a change in
    // Options re-runs, so the two can never disagree.
    m_graphDown = new StatsGraph(4, this);
    m_graphDown->setSeriesInfo(0, tr("Session average"), theUiState.statsColor(4));
    m_graphDown->setSeriesInfo(1, tr("Average (3 min)"), theUiState.statsColor(3));
    m_graphDown->setSeriesInfo(2, tr("Current"), theUiState.statsColor(2));
    m_graphDown->setSeriesInfo(3, tr("Usenet"), theUiState.statsColor(15));
    m_graphDown->setYUnits(tr("KB/s"));
    graphSplitter->addWidget(m_graphDown);

    m_graphUp = new StatsGraph(5, this);
    m_graphUp->setSeriesInfo(0, tr("Session average"), theUiState.statsColor(7));
    m_graphUp->setSeriesInfo(1, tr("Average (3 min)"), theUiState.statsColor(6));
    m_graphUp->setSeriesInfo(2, tr("Current"), theUiState.statsColor(5));
    m_graphUp->setSeriesInfo(3, tr("Current (excl. overhead)"), theUiState.statsColor(14));
    m_graphUp->setSeriesInfo(4, tr("Friend slots"), theUiState.statsColor(13));
    m_graphUp->setYUnits(tr("KB/s"));
    graphSplitter->addWidget(m_graphUp);

    m_graphConn = new StatsGraph(4, this);
    m_graphConn->setSeriesInfo(0, tr("Active connections"), theUiState.statsColor(8));
    m_graphConn->setSeriesInfo(1, tr("Active uploads"), theUiState.statsColor(10));
    m_graphConn->setSeriesInfo(2, tr("Total uploads"), theUiState.statsColor(9));
    m_graphConn->setSeriesInfo(3, tr("Active downloads"), theUiState.statsColor(12));
    graphSplitter->addWidget(m_graphConn);

    m_hSplitter->addWidget(graphSplitter);

    m_hSplitter->setStretchFactor(0, 3);
    m_hSplitter->setStretchFactor(1, 7);

    theUiState.bindStatsSplitter(m_hSplitter);
    theUiState.bindStatsGraphSplitter(graphSplitter);

    buildTree();
}

// ---------------------------------------------------------------------------
// Helper: build overhead subtree
// ---------------------------------------------------------------------------

static void buildOverheadItems(QTreeWidgetItem* parent,
                               QTreeWidgetItem*& total,
                               QTreeWidgetItem*& fileReq,
                               QTreeWidgetItem*& srcExch,
                               QTreeWidgetItem*& server,
                               QTreeWidgetItem*& kad)
{
    total   = new QTreeWidgetItem(parent, {QObject::tr("Total Overhead (Packets): 0 Bytes (0)")});
    fileReq = new QTreeWidgetItem(total, {QObject::tr("File Request Overhead (Packets): 0 Bytes (0)")});
    srcExch = new QTreeWidgetItem(total, {QObject::tr("Source Exchange Overhead (Packets): 0 Bytes (0)")});
    server  = new QTreeWidgetItem(total, {QObject::tr("Server Overhead (Packets): 0 Bytes (0)")});
    kad     = new QTreeWidgetItem(total, {QObject::tr("Kad Overhead (Packets): 0 Bytes (0)")});
}

// ---------------------------------------------------------------------------
// Tree construction
// ---------------------------------------------------------------------------

void StatisticsPanel::buildTree()
{
    m_tree->clear();

    const auto detailIcon = QIcon(QStringLiteral(":/icons/StatisticsDetail.ico"));
    // MFC keeps a separate icon for every Cumulative node (StatisticsDlg.cpp:106-128).
    const auto cumulativeIcon = QIcon(QStringLiteral(":/icons/StatsCumulative.ico"));

    // ===== Transfer =====
    auto* transfer = new QTreeWidgetItem(m_tree, {tr("Transfer")});
    transfer->setIcon(0, QIcon(QStringLiteral(":/icons/TransferUpDown.ico")));

    const QString waiting = tr("Waiting...");
    m_itemSessionUlDlRatio = new QTreeWidgetItem(transfer, {tr("Session UL:DL Ratio: %1").arg(waiting)});
    m_itemFriendUlDlRatio = new QTreeWidgetItem(transfer,
        {tr("Session UL:DL Ratio (Friends UL excluded): %1").arg(waiting)});
    m_itemCumUlDlRatio = new QTreeWidgetItem(transfer, {tr("Cumulative UL:DL Ratio: %1").arg(waiting)});

    // --- Uploads ---
    auto* uploads = new QTreeWidgetItem(transfer, {tr("Uploads")});
    uploads->setIcon(0, QIcon(QStringLiteral(":/icons/Upload.ico")));

    // Uploads > Session
    auto* upSession = new QTreeWidgetItem(uploads, {tr("Session")});
    upSession->setIcon(0, detailIcon);

    m_itemUpSessionData = new QTreeWidgetItem(upSession, {tr("Uploaded Data: 0 Bytes")});
    auto* upSesClients = new QTreeWidgetItem(m_itemUpSessionData, {tr("Clients")});
    for (int i = 0; i < 7; ++i)
        m_itemUpSesClient[i] = new QTreeWidgetItem(upSesClients,
            {tr("%1: 0 Bytes").arg(QString::fromLatin1(kUpClientLabels[i]))});
    auto* upSesPorts = new QTreeWidgetItem(m_itemUpSessionData, {tr("Port")});
    m_itemUpSesPort[0] = new QTreeWidgetItem(upSesPorts, {tr("Default Port 4662: 0 Bytes")});
    m_itemUpSesPort[1] = new QTreeWidgetItem(upSesPorts, {tr("Other Ports: 0 Bytes")});
    auto* upSesSrc = new QTreeWidgetItem(m_itemUpSessionData, {tr("Data Source")});
    m_itemUpSesSource[0] = new QTreeWidgetItem(upSesSrc, {tr("Complete File: 0 Bytes")});
    m_itemUpSesSource[1] = new QTreeWidgetItem(upSesSrc, {tr("Part File: 0 Bytes")});

    m_itemUpSessionFriendData = new QTreeWidgetItem(upSession,
                                                    {tr("Uploaded Data to Friends: 0 Bytes")});
    m_itemUpActiveUploads = new QTreeWidgetItem(upSession, {tr("Active Uploads: 0")});
    m_itemUpWaitingUploads = new QTreeWidgetItem(upSession, {tr("Waiting Uploads: 0")});

    auto* upSessions = new QTreeWidgetItem(upSession, {tr("Upload Sessions")});
    m_itemUpSuccessful = new QTreeWidgetItem(upSessions, {tr("Successful: 0")});
    m_itemUpFailed = new QTreeWidgetItem(upSessions, {tr("Failed: 0")});
    m_itemUpAvgPerSession = new QTreeWidgetItem(upSessions,
                                                {tr("Average Upload Per Session: 0 Bytes")});
    m_itemUpAvgTime = new QTreeWidgetItem(upSessions,
                                          {tr("Average Upload Time: 0:00:00")});

    buildOverheadItems(upSession, m_itemUpOverheadTotal, m_itemUpOverheadFileReq,
                       m_itemUpOverheadSrcExch, m_itemUpOverheadServer, m_itemUpOverheadKad);

    // Uploads > Cumulative
    auto* upCum = new QTreeWidgetItem(uploads, {tr("Cumulative")});
    upCum->setIcon(0, cumulativeIcon);

    m_itemUpCumData = new QTreeWidgetItem(upCum, {tr("Uploaded Data: 0 Bytes")});
    auto* upCumClients = new QTreeWidgetItem(m_itemUpCumData, {tr("Clients")});
    for (int i = 0; i < 7; ++i)
        m_itemUpCumClient[i] = new QTreeWidgetItem(upCumClients,
            {tr("%1: 0 Bytes").arg(QString::fromLatin1(kUpClientLabels[i]))});
    auto* upCumPorts = new QTreeWidgetItem(m_itemUpCumData, {tr("Port")});
    m_itemUpCumPort[0] = new QTreeWidgetItem(upCumPorts, {tr("Default Port 4662: 0 Bytes")});
    m_itemUpCumPort[1] = new QTreeWidgetItem(upCumPorts, {tr("Other Ports: 0 Bytes")});
    auto* upCumSrc = new QTreeWidgetItem(m_itemUpCumData, {tr("Data Source")});
    m_itemUpCumSource[0] = new QTreeWidgetItem(upCumSrc, {tr("Complete File: 0 Bytes")});
    m_itemUpCumSource[1] = new QTreeWidgetItem(upCumSrc, {tr("Part File: 0 Bytes")});

    auto* upCumSessions = new QTreeWidgetItem(upCum, {tr("Upload Sessions")});
    m_itemUpCumSuccessful = new QTreeWidgetItem(upCumSessions, {tr("Successful: 0")});
    m_itemUpCumFailed = new QTreeWidgetItem(upCumSessions, {tr("Failed: 0")});
    m_itemUpCumAvgPerSession = new QTreeWidgetItem(upCumSessions,
                                                   {tr("Average Upload Per Session: 0 Bytes")});
    m_itemUpCumAvgTime = new QTreeWidgetItem(upCumSessions,
                                             {tr("Average Upload Time: 0:00:00")});

    buildOverheadItems(upCum, m_itemUpCumOverheadTotal, m_itemUpCumOverheadFileReq,
                       m_itemUpCumOverheadSrcExch, m_itemUpCumOverheadServer, m_itemUpCumOverheadKad);

    // --- Downloads ---
    auto* downloads = new QTreeWidgetItem(transfer, {tr("Downloads")});
    downloads->setIcon(0, QIcon(QStringLiteral(":/icons/Download.ico")));

    // Downloads > Session
    auto* downSession = new QTreeWidgetItem(downloads, {tr("Session")});
    downSession->setIcon(0, detailIcon);

    m_itemDownSessionData = new QTreeWidgetItem(downSession, {tr("Downloaded Data: 0 Bytes")});
    auto* downSesClients = new QTreeWidgetItem(m_itemDownSessionData, {tr("Clients")});
    for (int i = 0; i < 8; ++i)
        m_itemDownSesClient[i] = new QTreeWidgetItem(downSesClients,
            {tr("%1: 0 Bytes").arg(QString::fromLatin1(kDownClientLabels[i]))});
    auto* downSesPorts = new QTreeWidgetItem(m_itemDownSessionData, {tr("Port")});
    m_itemDownSesPort[0] = new QTreeWidgetItem(downSesPorts, {tr("Default Port 4662: 0 Bytes")});
    m_itemDownSesPort[1] = new QTreeWidgetItem(downSesPorts, {tr("Other Ports: 0 Bytes")});

    m_itemDownActiveDownloads = new QTreeWidgetItem(downSession, {tr("Active Downloads: 0")});
    m_itemDownFoundSources = new QTreeWidgetItem(downSession, {tr("Found Sources: 0")});
    // MFC hangs the per-source breakdown off "Found Sources" (down_sources[] under
    // down_S[3], StatisticsDlg.cpp:2643); this is the UDP re-ask line of that group.
    m_itemDownUdpReasks = new QTreeWidgetItem(m_itemDownFoundSources,
                                              {tr("UDP File Re-asks: 0, Failed: 0 (0.0%)")});
    m_itemDownCompletedSes = new QTreeWidgetItem(downSession, {tr("Completed Downloads: 0")});

    auto* downSesSessions = new QTreeWidgetItem(downSession, {tr("Download Sessions")});
    m_itemDownSesSuccessful = new QTreeWidgetItem(downSesSessions, {tr("Successful: 0")});
    m_itemDownSesFailed = new QTreeWidgetItem(downSesSessions, {tr("Failed: 0")});
    m_itemDownSesAvgPerSession = new QTreeWidgetItem(downSesSessions,
                                                     {tr("Average Download Per Session: 0 Bytes")});
    m_itemDownSesAvgTime = new QTreeWidgetItem(downSesSessions,
                                               {tr("Average Download Time: 0:00:00")});

    m_itemDownSesCompression = new QTreeWidgetItem(downSession,
                                                   {tr("Gain Due To Compression: 0 Bytes (0.0%)")});
    m_itemDownSesCorruption = new QTreeWidgetItem(downSession,
                                                  {tr("Lost Due To Corruption: 0 Bytes (0.0%)")});
    m_itemDownSesIchSaved = new QTreeWidgetItem(downSession,
                                                {tr("Parts Saved Due To ICH: 0")});

    buildOverheadItems(downSession, m_itemDownOverheadTotal, m_itemDownOverheadFileReq,
                       m_itemDownOverheadSrcExch, m_itemDownOverheadServer, m_itemDownOverheadKad);

    // Downloads > Cumulative
    auto* downCum = new QTreeWidgetItem(downloads, {tr("Cumulative")});
    downCum->setIcon(0, cumulativeIcon);

    m_itemDownCumData = new QTreeWidgetItem(downCum, {tr("Downloaded Data: 0 Bytes")});
    auto* downCumClients = new QTreeWidgetItem(m_itemDownCumData, {tr("Clients")});
    for (int i = 0; i < 8; ++i)
        m_itemDownCumClient[i] = new QTreeWidgetItem(downCumClients,
            {tr("%1: 0 Bytes").arg(QString::fromLatin1(kDownClientLabels[i]))});
    auto* downCumPorts = new QTreeWidgetItem(m_itemDownCumData, {tr("Port")});
    m_itemDownCumPort[0] = new QTreeWidgetItem(downCumPorts, {tr("Default Port 4662: 0 Bytes")});
    m_itemDownCumPort[1] = new QTreeWidgetItem(downCumPorts, {tr("Other Ports: 0 Bytes")});

    m_itemDownCumCompleted = new QTreeWidgetItem(downCum, {tr("Completed Downloads: 0")});

    auto* downCumSessions = new QTreeWidgetItem(downCum, {tr("Download Sessions")});
    m_itemDownCumSuccessful = new QTreeWidgetItem(downCumSessions, {tr("Successful: 0")});
    m_itemDownCumFailed = new QTreeWidgetItem(downCumSessions, {tr("Failed: 0")});
    m_itemDownCumAvgPerSession = new QTreeWidgetItem(downCumSessions,
                                                     {tr("Average Download Per Session: 0 Bytes")});
    m_itemDownCumAvgTime = new QTreeWidgetItem(downCumSessions,
                                               {tr("Average Download Time: 0:00:00")});

    m_itemDownCumCompression = new QTreeWidgetItem(downCum,
                                                   {tr("Gain Due To Compression: 0 Bytes (0.0%)")});
    m_itemDownCumCorruption = new QTreeWidgetItem(downCum,
                                                  {tr("Lost Due To Corruption: 0 Bytes (0.0%)")});
    m_itemDownCumIchSaved = new QTreeWidgetItem(downCum,
                                                {tr("Parts Saved Due To ICH: 0")});

    buildOverheadItems(downCum, m_itemDownCumOverheadTotal, m_itemDownCumOverheadFileReq,
                       m_itemDownCumOverheadSrcExch, m_itemDownCumOverheadServer, m_itemDownCumOverheadKad);

    buildHttpCacheBranch(transfer, detailIcon, cumulativeIcon);

    // ===== Connection =====
    auto* connection = new QTreeWidgetItem(m_tree, {tr("Connection")});
    connection->setIcon(0, QIcon(QStringLiteral(":/icons/Connection.ico")));

    // Connection > Session
    auto* connSession = new QTreeWidgetItem(connection, {tr("Session")});
    connSession->setIcon(0, detailIcon);

    auto* connSesGen = new QTreeWidgetItem(connSession, {tr("General")});
    m_itemConnActive = new QTreeWidgetItem(connSesGen, {tr("Active Connections: 0")});
    m_itemConnPeak = new QTreeWidgetItem(connSesGen, {tr("Peak Connections: 0")});
    m_itemConnMaxReached = new QTreeWidgetItem(connSesGen, {tr("Max Connections Limit Reached: 0")});
    m_itemConnReconnects = new QTreeWidgetItem(connSesGen, {tr("Reconnects: 0")});
    m_itemConnAverage = new QTreeWidgetItem(connSesGen, {tr("Average Connections: 0.0")});

    auto* connSesUp = new QTreeWidgetItem(connSession, {tr("Uploads")});
    m_itemConnSesUpSpeed = new QTreeWidgetItem(connSesUp, {tr("Upload Speed: 0 KB/s")});
    m_itemConnSesMaxUp = new QTreeWidgetItem(connSesUp, {tr("Max Upload Rate: 0 KB/s")});
    m_itemConnSesMaxAvgUp = new QTreeWidgetItem(connSesUp, {tr("Max Average Upload Rate: 0 KB/s")});

    auto* connSesDown = new QTreeWidgetItem(connSession, {tr("Downloads")});
    m_itemConnSesDownSpeed = new QTreeWidgetItem(connSesDown, {tr("Download Speed: 0 KB/s")});
    m_itemConnSesMaxDown = new QTreeWidgetItem(connSesDown, {tr("Max Download Rate: 0 KB/s")});
    m_itemConnSesMaxAvgDown = new QTreeWidgetItem(connSesDown, {tr("Max Average Download Rate: 0 KB/s")});

    // Connection > Cumulative
    auto* connCum = new QTreeWidgetItem(connection, {tr("Cumulative")});
    connCum->setIcon(0, cumulativeIcon);

    auto* connCumGen = new QTreeWidgetItem(connCum, {tr("General")});
    m_itemConnCumReconnects = new QTreeWidgetItem(connCumGen, {tr("Server Reconnects: 0")});
    m_itemConnCumPeak = new QTreeWidgetItem(connCumGen, {tr("Peak Connections: 0")});
    m_itemConnCumMaxReached = new QTreeWidgetItem(connCumGen, {tr("Connection Limit Reached: 0")});

    auto* connCumUp = new QTreeWidgetItem(connCum, {tr("Uploads")});
    m_itemConnCumAvgUp = new QTreeWidgetItem(connCumUp, {tr("Average Upload Rate: 0 KB/s")});
    m_itemConnCumMaxUp = new QTreeWidgetItem(connCumUp, {tr("Max Upload Rate: 0 KB/s")});
    m_itemConnCumMaxAvgUp = new QTreeWidgetItem(connCumUp, {tr("Max Average Upload Rate: 0 KB/s")});

    auto* connCumDown = new QTreeWidgetItem(connCum, {tr("Downloads")});
    m_itemConnCumAvgDown = new QTreeWidgetItem(connCumDown, {tr("Average Download Rate: 0 KB/s")});
    m_itemConnCumMaxDown = new QTreeWidgetItem(connCumDown, {tr("Max Download Rate: 0 KB/s")});
    m_itemConnCumMaxAvgDown = new QTreeWidgetItem(connCumDown, {tr("Max Average Download Rate: 0 KB/s")});

    // ===== Time Statistics =====
    m_itemTimeHeader = new QTreeWidgetItem(m_tree, {tr("Time Statistics")});
    m_itemTimeHeader->setIcon(0, QIcon(QStringLiteral(":/icons/StatsTime.ico")));
    m_itemStatsLastReset = new QTreeWidgetItem(m_itemTimeHeader,
                                               {tr("Statistics Last Reset: %1").arg(tr("Unknown"))});
    m_itemTimeSinceReset = new QTreeWidgetItem(m_itemTimeHeader,
                                               {tr("Time Since Last Reset: -")});

    auto* timeSession = new QTreeWidgetItem(m_itemTimeHeader, {tr("Session")});
    timeSession->setIcon(0, detailIcon);
    m_itemRuntime = new QTreeWidgetItem(timeSession, {tr("Runtime: 0:00:00")});
    m_itemTransferTime = new QTreeWidgetItem(timeSession, {tr("Transfer Time: 0:00:00")});
    m_itemUploadTime = new QTreeWidgetItem(m_itemTransferTime, {tr("Upload Time: 0:00:00")});
    m_itemDownloadTime = new QTreeWidgetItem(m_itemTransferTime, {tr("Download Time: 0:00:00")});
    // MFC shows both, in this order (StatisticsDlg.cpp:1582-1588).
    m_itemCurrentServerDuration =
        new QTreeWidgetItem(timeSession, {tr("Current Server Duration: 0:00:00")});
    m_itemServerDuration = new QTreeWidgetItem(timeSession, {tr("Total Server Duration: 0:00:00")});

    auto* timeCum = new QTreeWidgetItem(m_itemTimeHeader, {tr("Cumulative")});
    timeCum->setIcon(0, cumulativeIcon);
    m_itemCumRuntime = new QTreeWidgetItem(timeCum, {tr("Run Time: 0:00:00")});
    m_itemCumTransferTime = new QTreeWidgetItem(timeCum, {tr("Transfer Time: 0:00:00")});
    m_itemCumUploadTime = new QTreeWidgetItem(m_itemCumTransferTime, {tr("Upload Time: 0:00:00")});
    m_itemCumDownloadTime = new QTreeWidgetItem(m_itemCumTransferTime, {tr("Download Time: 0:00:00")});
    m_itemCumServerDuration = new QTreeWidgetItem(timeCum, {tr("Total Server Duration: 0:00:00")});

    // ===== Clients =====
    auto* clients = new QTreeWidgetItem(m_tree, {tr("Clients")});
    clients->setIcon(0, QIcon(QStringLiteral(":/icons/User.ico")));
    m_itemKnownClients = new QTreeWidgetItem(clients, {tr("Known Clients: 0")});
    m_itemClientSoftware = new QTreeWidgetItem(clients, {tr("Client Software")});
    // MFC's cligen[4] slot — after the Software/Network/Port/Firewalled groups and
    // before Banned/Filtered (StatisticsDlg.cpp:2748).
    m_itemLowIDClients = new QTreeWidgetItem(clients, {tr("Low ID: 0 (0.0%)")});
    m_itemBannedClients = new QTreeWidgetItem(clients, {tr("Banned Clients: 0")});
    m_itemFilteredClients = new QTreeWidgetItem(clients, {tr("Filtered Clients: 0")});

    // ===== Servers =====
    auto* servers = new QTreeWidgetItem(m_tree, {tr("Servers")});
    servers->setIcon(0, QIcon(QStringLiteral(":/icons/Server.ico")));
    m_itemSrvWorking = new QTreeWidgetItem(servers, {tr("Working Servers: 0")});
    m_itemSrvFailed = new QTreeWidgetItem(servers, {tr("Failed Servers: 0")});
    m_itemSrvTotal = new QTreeWidgetItem(servers, {tr("Total: 0")});
    m_itemSrvUsers = new QTreeWidgetItem(servers, {tr("Total Users: 0")});
    m_itemSrvFiles = new QTreeWidgetItem(servers, {tr("Total Files: 0")});
    m_itemSrvLowID = new QTreeWidgetItem(servers, {tr("Low ID Users: 0")});

    auto* srvRecords = new QTreeWidgetItem(servers, {tr("Records")});
    m_itemSrvRecWorking = new QTreeWidgetItem(srvRecords, {tr("Most Working Servers: 0")});
    m_itemSrvRecUsers = new QTreeWidgetItem(srvRecords, {tr("Most Users Online: 0")});
    m_itemSrvRecFiles = new QTreeWidgetItem(srvRecords, {tr("Most Files Available: 0")});

    // ===== Shared Files =====
    auto* shared = new QTreeWidgetItem(m_tree, {tr("Shared Files")});
    shared->setIcon(0, QIcon(QStringLiteral(":/icons/SharedFiles.ico")));
    m_itemSharedCount = new QTreeWidgetItem(shared, {tr("Number of Shared Files: 0")});
    m_itemSharedSize = new QTreeWidgetItem(shared, {tr("Total Size: 0 Bytes")});
    m_itemSharedAvgSize = new QTreeWidgetItem(shared, {tr("Average File Size: 0 Bytes")});
    m_itemSharedLargest = new QTreeWidgetItem(shared, {tr("Largest Shared File: 0 Bytes")});

    auto* sharedRecords = new QTreeWidgetItem(shared, {tr("Records")});
    m_itemSharedRecCount = new QTreeWidgetItem(sharedRecords, {tr("Most Files Shared: 0")});
    m_itemSharedRecSize = new QTreeWidgetItem(sharedRecords, {tr("Largest Share Size: 0 Bytes")});
    m_itemSharedRecAvg = new QTreeWidgetItem(sharedRecords, {tr("Largest Average File Size: 0 Bytes")});
    m_itemSharedRecLargest = new QTreeWidgetItem(sharedRecords, {tr("Largest File Size: 0 Bytes")});

    // ===== Total Downloads =====
    auto* totalDown = new QTreeWidgetItem(m_tree, {tr("Total Downloads")});
    totalDown->setIcon(0, QIcon(QStringLiteral(":/icons/Download.ico")));
    m_itemTotalDownCount = new QTreeWidgetItem(totalDown, {tr("Number of Downloads: 0")});
    m_itemTotalDownSize = new QTreeWidgetItem(totalDown, {tr("Total Size of Downloads: 0 Bytes")});
    m_itemTotalDownDone = new QTreeWidgetItem(totalDown, {tr("Total Size Downloaded: 0 Bytes")});
    m_itemTotalDownLeft = new QTreeWidgetItem(totalDown, {tr("Total Size Left to Download: 0 Bytes")});
    m_itemTotalDownFreeSpace = new QTreeWidgetItem(totalDown, {tr("Free Space on Drive: 0 Bytes")});

    // ===== Usenet ===== (not in MFC; last, so MFC's own order stays intact)
    buildUsenetBranch(detailIcon, cumulativeIcon);

    // Restore expansion state from persistent settings (defaults: Transfer, Connection, Time expanded)
    theUiState.bindStatsTree(m_tree);
}

// ---------------------------------------------------------------------------
// IPC polling
// ---------------------------------------------------------------------------

void StatisticsPanel::requestStats()
{
    if (!m_ipc || !m_ipc->isConnected())
        return;

    IpcMessage req(IpcMsgType::GetStats);
    m_ipc->sendRequest(std::move(req), [this](const IpcMessage& resp) {
        if (resp.type() != IpcMsgType::Result || !resp.fieldBool(0))
            return;

        updateTree(resp.fieldMap(1));
    });

    // Its own request: the daemon answers GetStats every second for the status
    // bar, and the Usenet branch is only worth building while this panel shows.
    IpcMessage usenetReq(IpcMsgType::GetUsenetStats);
    m_ipc->sendRequest(std::move(usenetReq), [this](const IpcMessage& resp) {
        if (!resp.isValid())
            return;   // dropped: leave the branch on screen, like the rest of the tree
        if (resp.type() != IpcMsgType::Result || !resp.fieldBool(0)) {
            m_itemUsenet->setHidden(true);
            return;
        }
        applyUsenetStats(resp.fieldMap(1));
    });
}

void StatisticsPanel::requestGraphHistory()
{
    if (!m_ipc || !m_ipc->isConnected())
        return;

    IpcMessage req(IpcMsgType::GetStatsHistory);
    req.append(static_cast<qint64>(m_statsSeq));
    m_ipc->sendRequest(std::move(req), [this](const IpcMessage& resp) {
        if (resp.type() != IpcMsgType::Result || !resp.fieldBool(0))
            return;
        applyGraphHistory(resp.fieldMap(1));
    });
}

// ---------------------------------------------------------------------------
// Feed graph data
// ---------------------------------------------------------------------------

void StatisticsPanel::applyGraphHistory(const QCborMap& data)
{
    const auto epoch = static_cast<quint32>(
        data.value(QStringLiteral("epoch")).toInteger());
    const auto oldestSeq = static_cast<quint32>(
        data.value(QStringLiteral("oldestSeq")).toInteger());

    // What we hold is only usable if it is still a prefix of the daemon's history:
    // a different epoch means the daemon restarted or statistics were reset, and an
    // oldestSeq past our own means samples aged out while the panel was hidden.
    if (epoch != m_statsEpoch || oldestSeq > m_statsSeq + 1) {
        m_graphDown->reset();
        m_graphUp->reset();
        m_graphConn->reset();
        m_statsSeq = 0;
        m_statsEpoch = epoch;
    }

    // One sample per graphsUpdateSec on the daemon; the time axis was fixed at 3 s.
    if (const qint64 interval = data.value(QStringLiteral("intervalSec")).toInteger(); interval > 0) {
        for (auto* graph : {m_graphDown, m_graphUp, m_graphConn})
            graph->setSampleIntervalSec(static_cast<double>(interval));
    }

    // Positional unpack of StatsGraphSample, whose field order is MFC's scope order
    // (srchybrid/StatisticsDlg.cpp:569-600) plus the appended Usenet rate.
    constexpr int kFieldCount = 15;
    const QCborArray samples = data.value(QStringLiteral("samples")).toArray();
    for (const auto& v : samples) {
        const QCborArray s = v.toArray();
        if (s.size() < kFieldCount)
            continue;
        m_statsSeq = static_cast<quint32>(s.at(0).toInteger());

        m_graphDown->appendPoints({s.at(2).toDouble(), s.at(3).toDouble(),
                                   s.at(4).toDouble(), s.at(14).toDouble()});
        m_graphUp->appendPoints({s.at(5).toDouble(), s.at(6).toDouble(),
                                 s.at(7).toDouble(), s.at(8).toDouble(),
                                 s.at(9).toDouble()});
        m_graphConn->appendPoints({static_cast<double>(s.at(10).toInteger()),
                                   static_cast<double>(s.at(11).toInteger()),
                                   static_cast<double>(s.at(12).toInteger()),
                                   static_cast<double>(s.at(13).toInteger())});
    }
}

// ---------------------------------------------------------------------------
// Update tree items
// ---------------------------------------------------------------------------

// Helper to set a client breakdown item
static void setClientBreakdown(QTreeWidgetItem* item, const char* label,
                               qint64 bytes, qint64 total)
{
    if (total > 0) {
        const double pct = 100.0 * static_cast<double>(bytes) / static_cast<double>(total);
        item->setText(0, QStringLiteral("%1: %2 (%3%)")
            .arg(QString::fromLatin1(label),
                 formatByteSize(bytes),
                 QString::number(pct, 'f', 1)));
    } else {
        item->setText(0, QStringLiteral("%1: %2")
            .arg(QString::fromLatin1(label), formatByteSize(bytes)));
    }
}

void StatisticsPanel::updateTree(const QCborMap& stats)
{
    const qint64 sent = cborInt(stats, QLatin1StringView("sessionSentBytes"));
    const qint64 recv = cborInt(stats, QLatin1StringView("sessionReceivedBytes"));
    const qint64 sentFriend = cborInt(stats, QLatin1StringView("sessionSentBytesToFriend"));
    const qint64 uptime = cborInt(stats, QLatin1StringView("uptime"));

    const qint64 cumTotalUp = cborInt(stats, QLatin1StringView("cumTotalUp"));
    const qint64 cumTotalDown = cborInt(stats, QLatin1StringView("cumTotalDown"));

    m_sessionUptime = uptime;
    m_cumRunTime = cborInt(stats, QLatin1StringView("cumRunTime"));

    // Transfer ratios. The friend row leaves friend uploads out of the numerator
    // (MFC StatisticsDlg.cpp:633-640).
    m_itemSessionUlDlRatio->setText(0,
        tr("Session UL:DL Ratio: %1").arg(formatRatio(sent, recv)));
    m_itemFriendUlDlRatio->setText(0,
        tr("Session UL:DL Ratio (Friends UL excluded): %1").arg(formatRatio(sent - sentFriend, recv)));
    m_itemCumUlDlRatio->setText(0,
        tr("Cumulative UL:DL Ratio: %1").arg(formatRatio(cumTotalUp, cumTotalDown)));

    // === Uploads — Session ===
    m_itemUpSessionData->setText(0, tr("Uploaded Data: %1").arg(formatByteSize(sent)));

    // Per-client session upload
    static const char* const sesUpKeys[] = {
        "sesUpEmule", "sesUpEDHybrid", "sesUpEDonkey", "sesUpAMule",
        "sesUpMLdonkey", "sesUpShareaza", "sesUpEMCompat"
    };
    for (int i = 0; i < 7; ++i) {
        const qint64 v = cborInt(stats, QLatin1StringView(sesUpKeys[i]));
        setClientBreakdown(m_itemUpSesClient[i], kUpClientLabels[i], v, sent);
    }
    m_itemUpSesPort[0]->setText(0, tr("Default Port 4662: %1 %2")
        .arg(formatByteSize(cborInt(stats, QLatin1StringView("sesUpPort4662"))),
             formatPercent(cborInt(stats, QLatin1StringView("sesUpPort4662")), sent)));
    m_itemUpSesPort[1]->setText(0, tr("Other Ports: %1 %2")
        .arg(formatByteSize(cborInt(stats, QLatin1StringView("sesUpPortOther"))),
             formatPercent(cborInt(stats, QLatin1StringView("sesUpPortOther")), sent)));
    m_itemUpSesSource[0]->setText(0, tr("Complete File: %1 %2")
        .arg(formatByteSize(cborInt(stats, QLatin1StringView("sesUpFromFile"))),
             formatPercent(cborInt(stats, QLatin1StringView("sesUpFromFile")), sent)));
    m_itemUpSesSource[1]->setText(0, tr("Part File: %1 %2")
        .arg(formatByteSize(cborInt(stats, QLatin1StringView("sesUpFromPartfile"))),
             formatPercent(cborInt(stats, QLatin1StringView("sesUpFromPartfile")), sent)));

    m_itemUpSessionFriendData->setText(0,
        tr("Uploaded Data to Friends: %1").arg(formatByteSize(sentFriend)));
    m_itemUpActiveUploads->setText(0,
        tr("Active Uploads: %1").arg(cborInt(stats, QLatin1StringView("upWaiting"))));
    m_itemUpWaitingUploads->setText(0,
        tr("Waiting Uploads: %1").arg(cborInt(stats, QLatin1StringView("upQueueLength"))));

    const qint64 upSucc = cborInt(stats, QLatin1StringView("upSuccessful"));
    const qint64 upFail = cborInt(stats, QLatin1StringView("upFailed"));
    const qint64 upTotal = upSucc + upFail;
    m_itemUpSuccessful->setText(0, tr("Successful: %1%2").arg(upSucc)
        .arg(upTotal > 0 ? QStringLiteral(" (%1%)").arg(100 * upSucc / upTotal) : QString()));
    m_itemUpFailed->setText(0, tr("Failed: %1").arg(upFail));
    if (upSucc > 0)
        m_itemUpAvgPerSession->setText(0,
            tr("Average Upload Per Session: %1").arg(formatByteSize(sent / upSucc)));
    m_itemUpAvgTime->setText(0,
        tr("Average Upload Time: %1").arg(formatDuration(cborInt(stats, QLatin1StringView("upAvgTime")))));

    // Upload session overhead
    auto setOH = [&](QTreeWidgetItem* item, const QString& label, const char* bKey, const char* pKey) {
        item->setText(0, tr("%1: %2").arg(label,
            formatOverhead(cborInt(stats, QLatin1StringView(bKey)),
                          cborInt(stats, QLatin1StringView(pKey)))));
    };
    setOH(m_itemUpOverheadTotal, tr("Total Overhead (Packets)"), "upOverheadTotal", "upOverheadTotalPackets");
    setOH(m_itemUpOverheadFileReq, tr("File Request Overhead (Packets)"), "upOverheadFileReq", "upOverheadFileReqPkt");
    setOH(m_itemUpOverheadSrcExch, tr("Source Exchange Overhead (Packets)"), "upOverheadSrcExch", "upOverheadSrcExchPkt");
    setOH(m_itemUpOverheadServer, tr("Server Overhead (Packets)"), "upOverheadServer", "upOverheadServerPkt");
    setOH(m_itemUpOverheadKad, tr("Kad Overhead (Packets)"), "upOverheadKad", "upOverheadKadPkt");

    // === Uploads — Cumulative ===
    m_itemUpCumData->setText(0, tr("Uploaded Data: %1").arg(formatByteSize(cumTotalUp)));

    static const char* const cumUpKeys[] = {
        "cumUpEmule", "cumUpEDHybrid", "cumUpEDonkey", "cumUpAMule",
        "cumUpMLdonkey", "cumUpShareaza", "cumUpEMCompat"
    };
    for (int i = 0; i < 7; ++i) {
        const qint64 v = cborInt(stats, QLatin1StringView(cumUpKeys[i]));
        setClientBreakdown(m_itemUpCumClient[i], kUpClientLabels[i], v, cumTotalUp);
    }
    m_itemUpCumPort[0]->setText(0, tr("Default Port 4662: %1 %2")
        .arg(formatByteSize(cborInt(stats, QLatin1StringView("cumUpPort4662"))),
             formatPercent(cborInt(stats, QLatin1StringView("cumUpPort4662")), cumTotalUp)));
    m_itemUpCumPort[1]->setText(0, tr("Other Ports: %1 %2")
        .arg(formatByteSize(cborInt(stats, QLatin1StringView("cumUpPortOther"))),
             formatPercent(cborInt(stats, QLatin1StringView("cumUpPortOther")), cumTotalUp)));
    m_itemUpCumSource[0]->setText(0, tr("Complete File: %1 %2")
        .arg(formatByteSize(cborInt(stats, QLatin1StringView("cumUpFromFile"))),
             formatPercent(cborInt(stats, QLatin1StringView("cumUpFromFile")), cumTotalUp)));
    m_itemUpCumSource[1]->setText(0, tr("Part File: %1 %2")
        .arg(formatByteSize(cborInt(stats, QLatin1StringView("cumUpFromPartfile"))),
             formatPercent(cborInt(stats, QLatin1StringView("cumUpFromPartfile")), cumTotalUp)));

    const qint64 cumUpSucc = cborInt(stats, QLatin1StringView("cumUpSuccessful"));
    const qint64 cumUpFail = cborInt(stats, QLatin1StringView("cumUpFailed"));
    const qint64 cumUpSesTotal = cumUpSucc + cumUpFail;
    m_itemUpCumSuccessful->setText(0, tr("Successful: %1%2").arg(cumUpSucc)
        .arg(cumUpSesTotal > 0 ? QStringLiteral(" (%1%)").arg(100 * cumUpSucc / cumUpSesTotal) : QString()));
    m_itemUpCumFailed->setText(0, tr("Failed: %1").arg(cumUpFail));
    if (cumUpSucc > 0)
        m_itemUpCumAvgPerSession->setText(0,
            tr("Average Upload Per Session: %1").arg(formatByteSize(cumTotalUp / cumUpSucc)));
    m_itemUpCumAvgTime->setText(0,
        tr("Average Upload Time: %1").arg(formatDuration(cborInt(stats, QLatin1StringView("cumUpAvgTime")))));

    // === HTTP Cache ===
    // One block per scope, both directions out of it. "Upload Saved" is the
    // number that justifies the feature: bytes peers got that we never had to
    // send, because one published chunk served several.
    for (const auto& [scope, upRows, downRows] : {
             std::tuple{QStringLiteral("httpCacheSession"), &m_hcUpSessionRows, &m_hcDownSessionRows},
             std::tuple{QStringLiteral("httpCacheCumulative"), &m_hcUpCumulativeRows,
                        &m_hcDownCumulativeRows}}) {
        QHash<QString, qint64> v = counterValues(stats.value(scope).toMap());
        // What a fetch outcome is a share of: everything that got as far as
        // being fetched, good or bad. A withdrawn offer never started.
        v.insert(QStringLiteral("fetchesFinished"),
                 v.value(QStringLiteral("chunksFetched"))
                     + v.value(QStringLiteral("fetchesFailed")));
        fillCounterRows(*upRows, v);
        fillCounterRows(*downRows, v);
    }

    setOH(m_itemUpCumOverheadTotal, tr("Total Overhead (Packets)"), "cumUpOhTotal", "cumUpOhTotalPkt");
    setOH(m_itemUpCumOverheadFileReq, tr("File Request Overhead (Packets)"), "cumUpOhFileReq", "cumUpOhFileReqPkt");
    setOH(m_itemUpCumOverheadSrcExch, tr("Source Exchange Overhead (Packets)"), "cumUpOhSrcExch", "cumUpOhSrcExchPkt");
    setOH(m_itemUpCumOverheadServer, tr("Server Overhead (Packets)"), "cumUpOhServer", "cumUpOhServerPkt");
    setOH(m_itemUpCumOverheadKad, tr("Kad Overhead (Packets)"), "cumUpOhKad", "cumUpOhKadPkt");

    // === Downloads — Session ===
    m_itemDownSessionData->setText(0, tr("Downloaded Data: %1").arg(formatByteSize(recv)));

    static const char* const sesDownKeys[] = {
        "sesDownEmule", "sesDownEDHybrid", "sesDownEDonkey", "sesDownAMule",
        "sesDownMLdonkey", "sesDownShareaza", "sesDownEMCompat", "sesDownURL"
    };
    for (int i = 0; i < 8; ++i) {
        const qint64 v = cborInt(stats, QLatin1StringView(sesDownKeys[i]));
        setClientBreakdown(m_itemDownSesClient[i], kDownClientLabels[i], v, recv);
    }
    m_itemDownSesPort[0]->setText(0, tr("Default Port 4662: %1 %2")
        .arg(formatByteSize(cborInt(stats, QLatin1StringView("sesDownPort4662"))),
             formatPercent(cborInt(stats, QLatin1StringView("sesDownPort4662")), recv)));
    m_itemDownSesPort[1]->setText(0, tr("Other Ports: %1 %2")
        .arg(formatByteSize(cborInt(stats, QLatin1StringView("sesDownPortOther"))),
             formatPercent(cborInt(stats, QLatin1StringView("sesDownPortOther")), recv)));

    m_itemDownActiveDownloads->setText(0,
        tr("Active Downloads: %1").arg(cborInt(stats, QLatin1StringView("downFileCount"))));
    m_itemDownFoundSources->setText(0,
        tr("Found Sources: %1").arg(cborInt(stats, QLatin1StringView("downFoundSources"))));
    {
        const qint64 reasks = cborInt(stats, QLatin1StringView("downUdpReasks"));
        const qint64 failed = cborInt(stats, QLatin1StringView("downUdpReasksFailed"));
        m_itemDownUdpReasks->setText(0,
            tr("UDP File Re-asks: %1, Failed: %2 %3")
                .arg(reasks).arg(failed).arg(formatPercent(failed, reasks)));
    }
    m_itemDownCompletedSes->setText(0,
        tr("Completed Downloads: %1").arg(cborInt(stats, QLatin1StringView("completedDownloads"))));

    // Download session compression/corruption
    const qint64 sesCompression = cborInt(stats, QLatin1StringView("sesCompressionGain"));
    const qint64 sesCorruption = cborInt(stats, QLatin1StringView("sesCorruptionLoss"));
    m_itemDownSesCompression->setText(0,
        tr("Gain Due To Compression: %1 %2").arg(formatByteSize(sesCompression), formatPercent(sesCompression, recv)));
    m_itemDownSesCorruption->setText(0,
        tr("Lost Due To Corruption: %1 %2").arg(formatByteSize(sesCorruption), formatPercent(sesCorruption, recv)));
    m_itemDownSesIchSaved->setText(0,
        tr("Parts Saved Due To ICH: %1").arg(cborInt(stats, QLatin1StringView("sesIchPartsSaved"))));

    setOH(m_itemDownOverheadTotal, tr("Total Overhead (Packets)"), "downOverheadTotal", "downOverheadTotalPackets");
    setOH(m_itemDownOverheadFileReq, tr("File Request Overhead (Packets)"), "downOverheadFileReq", "downOverheadFileReqPkt");
    setOH(m_itemDownOverheadSrcExch, tr("Source Exchange Overhead (Packets)"), "downOverheadSrcExch", "downOverheadSrcExchPkt");
    setOH(m_itemDownOverheadServer, tr("Server Overhead (Packets)"), "downOverheadServer", "downOverheadServerPkt");
    setOH(m_itemDownOverheadKad, tr("Kad Overhead (Packets)"), "downOverheadKad", "downOverheadKadPkt");

    // === Downloads — Cumulative ===
    m_itemDownCumData->setText(0, tr("Downloaded Data: %1").arg(formatByteSize(cumTotalDown)));

    static const char* const cumDownKeys[] = {
        "cumDownEmule", "cumDownEDHybrid", "cumDownEDonkey", "cumDownAMule",
        "cumDownMLdonkey", "cumDownShareaza", "cumDownEMCompat", "cumDownURL"
    };
    for (int i = 0; i < 8; ++i) {
        const qint64 v = cborInt(stats, QLatin1StringView(cumDownKeys[i]));
        setClientBreakdown(m_itemDownCumClient[i], kDownClientLabels[i], v, cumTotalDown);
    }
    m_itemDownCumPort[0]->setText(0, tr("Default Port 4662: %1 %2")
        .arg(formatByteSize(cborInt(stats, QLatin1StringView("cumDownPort4662"))),
             formatPercent(cborInt(stats, QLatin1StringView("cumDownPort4662")), cumTotalDown)));
    m_itemDownCumPort[1]->setText(0, tr("Other Ports: %1 %2")
        .arg(formatByteSize(cborInt(stats, QLatin1StringView("cumDownPortOther"))),
             formatPercent(cborInt(stats, QLatin1StringView("cumDownPortOther")), cumTotalDown)));

    m_itemDownCumCompleted->setText(0,
        tr("Completed Downloads: %1").arg(cborInt(stats, QLatin1StringView("cumDownCompletedFiles"))));

    const qint64 cumCompression = cborInt(stats, QLatin1StringView("cumCompressionGain"));
    const qint64 cumCorruption = cborInt(stats, QLatin1StringView("cumCorruptionLoss"));
    m_itemDownCumCompression->setText(0,
        tr("Gain Due To Compression: %1 %2").arg(formatByteSize(cumCompression), formatPercent(cumCompression, cumTotalDown)));
    m_itemDownCumCorruption->setText(0,
        tr("Lost Due To Corruption: %1 %2").arg(formatByteSize(cumCorruption), formatPercent(cumCorruption, cumTotalDown)));
    m_itemDownCumIchSaved->setText(0,
        tr("Parts Saved Due To ICH: %1").arg(cborInt(stats, QLatin1StringView("cumIchPartsSaved"))));

    setOH(m_itemDownCumOverheadTotal, tr("Total Overhead (Packets)"), "cumDownOhTotal", "cumDownOhTotalPkt");
    setOH(m_itemDownCumOverheadFileReq, tr("File Request Overhead (Packets)"), "cumDownOhFileReq", "cumDownOhFileReqPkt");
    setOH(m_itemDownCumOverheadSrcExch, tr("Source Exchange Overhead (Packets)"), "cumDownOhSrcExch", "cumDownOhSrcExchPkt");
    setOH(m_itemDownCumOverheadServer, tr("Server Overhead (Packets)"), "cumDownOhServer", "cumDownOhServerPkt");
    setOH(m_itemDownCumOverheadKad, tr("Kad Overhead (Packets)"), "cumDownOhKad", "cumDownOhKadPkt");

    // === Connection — Session ===
    m_itemConnActive->setText(0,
        tr("Active Connections: %1").arg(cborInt(stats, QLatin1StringView("connActive"))));
    m_itemConnPeak->setText(0,
        tr("Peak Connections: %1").arg(cborInt(stats, QLatin1StringView("connPeak"))));
    m_itemConnMaxReached->setText(0,
        tr("Max Connections Limit Reached: %1").arg(cborInt(stats, QLatin1StringView("connMaxReached"))));
    m_itemConnReconnects->setText(0,
        tr("Reconnects: %1").arg(cborInt(stats, QLatin1StringView("reconnects"))));
    m_itemConnAverage->setText(0,
        tr("Average Connections: %1").arg(QString::number(cborDouble(stats, QLatin1StringView("connAverage")), 'f', 1)));

    m_itemConnSesUpSpeed->setText(0, tr("Upload Speed: %1").arg(formatRate(cborDouble(stats, QLatin1StringView("rateUp")))));
    m_itemConnSesMaxUp->setText(0, tr("Max Upload Rate: %1").arg(formatRate(cborDouble(stats, QLatin1StringView("maxUp")))));
    m_itemConnSesMaxAvgUp->setText(0, tr("Max Average Upload Rate: %1").arg(formatRate(cborDouble(stats, QLatin1StringView("maxUpAvg")))));
    m_itemConnSesDownSpeed->setText(0, tr("Download Speed: %1").arg(formatRate(cborDouble(stats, QLatin1StringView("rateDown")))));
    m_itemConnSesMaxDown->setText(0, tr("Max Download Rate: %1").arg(formatRate(cborDouble(stats, QLatin1StringView("maxDown")))));
    m_itemConnSesMaxAvgDown->setText(0, tr("Max Average Download Rate: %1").arg(formatRate(cborDouble(stats, QLatin1StringView("maxDownAvg")))));

    // === Connection — Cumulative ===
    m_itemConnCumReconnects->setText(0,
        tr("Server Reconnects: %1").arg(cborInt(stats, QLatin1StringView("cumConnReconnects"))));
    m_itemConnCumPeak->setText(0,
        tr("Peak Connections: %1").arg(cborInt(stats, QLatin1StringView("cumConnPeak"))));
    m_itemConnCumMaxReached->setText(0,
        tr("Connection Limit Reached: %1").arg(cborInt(stats, QLatin1StringView("cumConnMaxLimitReached"))));

    m_itemConnCumAvgUp->setText(0, tr("Average Upload Rate: %1").arg(formatRate(cborDouble(stats, QLatin1StringView("cumUpAvg")))));
    m_itemConnCumMaxUp->setText(0, tr("Max Upload Rate: %1").arg(formatRate(cborDouble(stats, QLatin1StringView("maxCumUp")))));
    m_itemConnCumMaxAvgUp->setText(0, tr("Max Average Upload Rate: %1").arg(formatRate(cborDouble(stats, QLatin1StringView("maxCumUpAvg")))));
    m_itemConnCumAvgDown->setText(0, tr("Average Download Rate: %1").arg(formatRate(cborDouble(stats, QLatin1StringView("cumDownAvg")))));
    m_itemConnCumMaxDown->setText(0, tr("Max Download Rate: %1").arg(formatRate(cborDouble(stats, QLatin1StringView("maxCumDown")))));
    m_itemConnCumMaxAvgDown->setText(0, tr("Max Average Download Rate: %1").arg(formatRate(cborDouble(stats, QLatin1StringView("maxCumDownAvg")))));

    // === Time Statistics ===
    // Counted from the last reset, not from session start — MFC shows the two as
    // separate numbers (srchybrid/StatisticsDlg.cpp:1543-1556).
    const qint64 lastReset = cborInt(stats, QLatin1StringView("statsLastReset"));
    const QString lastResetText = lastReset > 0
        ? QLocale().toString(QDateTime::fromSecsSinceEpoch(lastReset), QLocale::ShortFormat)
        : tr("Unknown");
    if (lastReset > 0) {
        m_itemStatsLastReset->setText(0, tr("Statistics Last Reset: %1").arg(lastResetText));
        m_itemTimeSinceReset->setText(0, tr("Time Since Last Reset: %1")
            .arg(formatDuration(cborInt(stats, QLatin1StringView("timeSinceReset")))));
    } else {
        m_itemStatsLastReset->setText(0, tr("Statistics Last Reset: %1").arg(lastResetText));
        m_itemTimeSinceReset->setText(0, tr("Time Since Last Reset: %1").arg(tr("Unknown")));
    }
    // Same date in the header bar; MFC shows it in both places too.
    m_labelLastReset->setText(tr("Statistics last reset: %1").arg(lastResetText));

    // Whether the Restore item in the menu is live. It comes from the poll rather
    // than a request of its own, so the menu can be built synchronously.
    m_backupAvailable = stats.value(QStringLiteral("statsBackupAvailable")).toBool(false);

    // Session
    m_itemRuntime->setText(0, tr("Runtime: %1").arg(formatDuration(uptime)));
    const qint64 tTransfer = cborInt(stats, QLatin1StringView("transferTime"));
    const qint64 tUpload = cborInt(stats, QLatin1StringView("uploadTime"));
    const qint64 tDownload = cborInt(stats, QLatin1StringView("downloadTime"));
    const qint64 tServer = cborInt(stats, QLatin1StringView("serverDuration"));
    const qint64 tServerNow = cborInt(stats, QLatin1StringView("currentServerDuration"));

    m_itemTransferTime->setText(0,
        tr("Transfer Time: %1 %2").arg(formatDuration(tTransfer), formatPercent(tTransfer, uptime)));
    m_itemUploadTime->setText(0,
        tr("Upload Time: %1 %2").arg(formatDuration(tUpload), formatPercent(tUpload, uptime)));
    m_itemDownloadTime->setText(0,
        tr("Download Time: %1 %2").arg(formatDuration(tDownload), formatPercent(tDownload, uptime)));
    m_itemCurrentServerDuration->setText(0,
        tr("Current Server Duration: %1 %2")
            .arg(formatDuration(tServerNow), formatPercent(tServerNow, uptime)));
    m_itemServerDuration->setText(0,
        tr("Total Server Duration: %1 %2").arg(formatDuration(tServer), formatPercent(tServer, uptime)));

    // Cumulative
    const qint64 cumRunTime = cborInt(stats, QLatin1StringView("cumRunTime"));
    const qint64 cumTransfer = cborInt(stats, QLatin1StringView("cumTransferTime"));
    const qint64 cumUpTime = cborInt(stats, QLatin1StringView("cumUploadTime"));
    const qint64 cumDownTime = cborInt(stats, QLatin1StringView("cumDownloadTime"));
    const qint64 cumServer = cborInt(stats, QLatin1StringView("cumServerDuration"));

    m_itemCumRuntime->setText(0, tr("Run Time: %1").arg(formatDuration(cumRunTime)));
    m_itemCumTransferTime->setText(0,
        tr("Transfer Time: %1 %2").arg(formatDuration(cumTransfer), formatPercent(cumTransfer, cumRunTime)));
    m_itemCumUploadTime->setText(0,
        tr("Upload Time: %1 %2").arg(formatDuration(cumUpTime), formatPercent(cumUpTime, cumRunTime)));
    m_itemCumDownloadTime->setText(0,
        tr("Download Time: %1 %2").arg(formatDuration(cumDownTime), formatPercent(cumDownTime, cumRunTime)));
    m_itemCumServerDuration->setText(0,
        tr("Total Server Duration: %1 %2").arg(formatDuration(cumServer), formatPercent(cumServer, cumRunTime)));

    // === Clients ===
    const qint64 knownClients = cborInt(stats, QLatin1StringView("knownClients"));
    m_itemKnownClients->setText(0,
        tr("Known Clients: %1").arg(knownClients));
    {
        const qint64 lowID = cborInt(stats, QLatin1StringView("lowIDClients"));
        m_itemLowIDClients->setText(0,
            tr("Low ID: %1 %2").arg(lowID).arg(formatPercent(lowID, knownClients)));
    }
    m_itemBannedClients->setText(0,
        tr("Banned Clients: %1").arg(cborInt(stats, QLatin1StringView("bannedClients"))));
    m_itemFilteredClients->setText(0,
        tr("Filtered Clients: %1").arg(cborInt(stats, QLatin1StringView("filteredClients"))));

    // Client Software / Version / Mod breakdown — rebuild dynamic subtree
    {
        // Remember expansion state before clearing
        QSet<QString> expandedSoftware;
        QSet<QString> expandedVersions;
        for (int i = 0; i < m_itemClientSoftware->childCount(); ++i) {
            auto* softItem = m_itemClientSoftware->child(i);
            if (softItem->isExpanded()) {
                expandedSoftware.insert(softItem->data(0, Qt::UserRole).toString());
                for (int j = 0; j < softItem->childCount(); ++j) {
                    auto* verItem = softItem->child(j);
                    if (verItem->isExpanded())
                        expandedVersions.insert(verItem->data(0, Qt::UserRole).toString());
                }
            }
        }

        // Clear old children
        while (m_itemClientSoftware->childCount() > 0)
            delete m_itemClientSoftware->takeChild(0);

        // Parse CBOR array
        auto it = stats.find(QStringLiteral("clientSoftwareStats"));
        if (it != stats.end() && it->isArray()) {
            const QCborArray softArr = it->toArray();
            for (const auto& softVal : softArr) {
                if (!softVal.isMap()) continue;
                const QCborMap softMap = softVal.toMap();
                const QString name = softMap.value(QStringLiteral("n")).toString();
                const int count = static_cast<int>(softMap.value(QStringLiteral("c")).toInteger());
                if (count == 0) continue;

                const double pct = knownClients > 0 ? 100.0 * count / static_cast<double>(knownClients) : 0.0;
                auto* softItem = new QTreeWidgetItem(m_itemClientSoftware,
                    {QStringLiteral("%1: %2 (%3%)").arg(name).arg(count).arg(pct, 0, 'f', 1)});
                softItem->setData(0, Qt::UserRole, name);

                // Restore expansion
                if (expandedSoftware.contains(name))
                    softItem->setExpanded(true);

                // Versions
                auto verIt = softMap.find(QStringLiteral("v"));
                if (verIt == softMap.end() || !verIt->isArray()) continue;
                const QCborArray verArr = verIt->toArray();

                for (const auto& verVal : verArr) {
                    if (!verVal.isMap()) continue;
                    const QCborMap verMap = verVal.toMap();
                    const QString label = verMap.value(QStringLiteral("l")).toString();
                    const int verCount = static_cast<int>(verMap.value(QStringLiteral("c")).toInteger());
                    if (verCount == 0) continue;

                    const double verPct = 100.0 * verCount / static_cast<double>(count);
                    auto* verItem = new QTreeWidgetItem(softItem,
                        {QStringLiteral("%1: %2 (%3%)").arg(label).arg(verCount).arg(verPct, 0, 'f', 1)});
                    const QString verKey = name + QLatin1Char('/') + label;
                    verItem->setData(0, Qt::UserRole, verKey);

                    if (expandedVersions.contains(verKey))
                        verItem->setExpanded(true);

                    // Mods
                    auto modIt = verMap.find(QStringLiteral("m"));
                    if (modIt == verMap.end() || !modIt->isArray()) continue;
                    const QCborArray modArr = modIt->toArray();

                    for (const auto& modVal : modArr) {
                        if (!modVal.isMap()) continue;
                        const QCborMap modMap = modVal.toMap();
                        const QString modName = modMap.value(QStringLiteral("n")).toString();
                        const int modCount = static_cast<int>(modMap.value(QStringLiteral("c")).toInteger());
                        if (modCount == 0) continue;

                        const double modPct = 100.0 * modCount / static_cast<double>(verCount);
                        new QTreeWidgetItem(verItem,
                            {QStringLiteral("%1: %2 (%3%)").arg(modName).arg(modCount).arg(modPct, 0, 'f', 1)});
                    }
                }
            }
        }
    }

    // === Servers ===
    m_itemSrvWorking->setText(0,
        tr("Working Servers: %1").arg(cborInt(stats, QLatin1StringView("srvWorking"))));
    m_itemSrvFailed->setText(0,
        tr("Failed Servers: %1").arg(cborInt(stats, QLatin1StringView("srvFailed"))));
    m_itemSrvTotal->setText(0,
        tr("Total: %1").arg(cborInt(stats, QLatin1StringView("srvTotal"))));
    m_itemSrvUsers->setText(0,
        tr("Total Users: %1").arg(cborInt(stats, QLatin1StringView("srvUsers"))));
    m_itemSrvFiles->setText(0,
        tr("Total Files: %1").arg(cborInt(stats, QLatin1StringView("srvFiles"))));
    m_itemSrvLowID->setText(0,
        tr("Low ID Users: %1").arg(cborInt(stats, QLatin1StringView("srvLowIDUsers"))));

    m_itemSrvRecWorking->setText(0,
        tr("Most Working Servers: %1").arg(cborInt(stats, QLatin1StringView("recMaxWorkingServers"))));
    m_itemSrvRecUsers->setText(0,
        tr("Most Users Online: %1").arg(cborInt(stats, QLatin1StringView("recMaxUsersOnline"))));
    m_itemSrvRecFiles->setText(0,
        tr("Most Files Available: %1").arg(cborInt(stats, QLatin1StringView("recMaxFilesAvail"))));

    // === Shared Files ===
    const qint64 sharedCount = cborInt(stats, QLatin1StringView("sharedCount"));
    const qint64 sharedSize = cborInt(stats, QLatin1StringView("sharedSize"));
    m_itemSharedCount->setText(0,
        tr("Number of Shared Files: %1").arg(sharedCount));
    m_itemSharedSize->setText(0,
        tr("Total Size: %1").arg(formatByteSize(sharedSize)));
    m_itemSharedAvgSize->setText(0,
        tr("Average File Size: %1").arg(sharedCount > 0 ? formatByteSize(sharedSize / sharedCount) : formatByteSize(0)));
    m_itemSharedLargest->setText(0,
        tr("Largest Shared File: %1").arg(formatByteSize(cborInt(stats, QLatin1StringView("sharedLargest")))));

    m_itemSharedRecCount->setText(0,
        tr("Most Files Shared: %1").arg(cborInt(stats, QLatin1StringView("recMaxSharedFiles"))));
    m_itemSharedRecSize->setText(0,
        tr("Largest Share Size: %1").arg(formatByteSize(cborInt(stats, QLatin1StringView("recMaxSharedSize")))));
    m_itemSharedRecAvg->setText(0,
        tr("Largest Average File Size: %1").arg(formatByteSize(cborInt(stats, QLatin1StringView("recMaxAvgFileSize")))));
    m_itemSharedRecLargest->setText(0,
        tr("Largest File Size: %1").arg(formatByteSize(cborInt(stats, QLatin1StringView("recMaxLargestFile")))));

    // === Total Downloads ===
    m_itemTotalDownCount->setText(0,
        tr("Number of Downloads: %1").arg(cborInt(stats, QLatin1StringView("totalDownCount"))));
    m_itemTotalDownSize->setText(0,
        tr("Total Size of Downloads: %1").arg(formatByteSize(cborInt(stats, QLatin1StringView("totalDownSize")))));
    m_itemTotalDownDone->setText(0,
        tr("Total Size Downloaded: %1").arg(formatByteSize(cborInt(stats, QLatin1StringView("totalDownDone")))));
    m_itemTotalDownLeft->setText(0,
        tr("Total Size Left to Download: %1").arg(formatByteSize(cborInt(stats, QLatin1StringView("totalDownLeft")))));
    m_itemTotalDownFreeSpace->setText(0,
        tr("Free Space on Drive: %1").arg(formatByteSize(cborInt(stats, QLatin1StringView("freeTempSpace")))));
}

void StatisticsPanel::applyUsenetStats(const QCborMap& data)
{
    m_itemUsenet->setHidden(false);

    const QCborMap usenet = data.value(QStringLiteral("usenet")).toMap();
    const QCborMap indexer = data.value(QStringLiteral("indexer")).toMap();
    const QCborMap current = usenet.value(QStringLiteral("current")).toMap();

    // One flat table per scope: both counter blocks (their keys do not collide),
    // plus the figures derived from them that the rows show or take shares of.
    const auto scopeValues = [&](const QString& scope, qint64 runtimeSecs) {
        QHash<QString, qint64> v = counterValues(usenet.value(scope).toMap());
        v.insert(counterValues(indexer.value(scope).toMap()));
        const qint64 wire = v.value(QStringLiteral("wireBytes"));
        const qint64 timeMs = v.value(QStringLiteral("downloadTimeMs"));
        v.insert(QStringLiteral("overheadBytes"),
                 std::max<qint64>(0, wire - v.value(QStringLiteral("decodedBytes"))));
        v.insert(QStringLiteral("avgDownRate"), timeMs > 0 ? wire * 1000 / timeMs : 0);
        v.insert(QStringLiteral("itemsFinished"), v.value(QStringLiteral("itemsCompleted"))
                                                      + v.value(QStringLiteral("itemsFailed")));
        v.insert(QStringLiteral("postTimeMs"), v.value(QStringLiteral("verifyMs"))
                                                   + v.value(QStringLiteral("repairMs"))
                                                   + v.value(QStringLiteral("unpackMs")));
        v.insert(QStringLiteral("nzbAdded"), v.value(QStringLiteral("nzbFromFile"))
                                                 + v.value(QStringLiteral("nzbFromUrl"))
                                                 + v.value(QStringLiteral("nzbFromWatch"))
                                                 + v.value(QStringLiteral("nzbFromFeed"))
                                                 + v.value(QStringLiteral("nzbFromIndexer")));
        v.insert(QStringLiteral("runtimeMs"), runtimeSecs * 1000);
        return v;
    };

    QHash<QString, qint64> session = scopeValues(QStringLiteral("session"), m_sessionUptime);
    for (const char* live : {"downRate", "activeConnections", "openConnections"}) {
        const QString key = QString::fromLatin1(live);
        session.insert(key, current.value(key).toInteger());
    }
    fillCounterRows(m_usenetSessionRows, session);
    fillCounterRows(m_usenetCumulativeRows, scopeValues(QStringLiteral("cumulative"), m_cumRunTime));

    QHash<QString, qint64> queue;
    const QCborMap queueMap = current.value(QStringLiteral("queue")).toMap();
    for (auto it = queueMap.cbegin(); it != queueMap.cend(); ++it)
        queue.insert(it.key().toString(), it.value().toInteger());
    fillCounterRows(m_usenetQueueRows, queue);

    updateNewsServers(usenet.value(QStringLiteral("servers")).toArray(),
                      session.value(QStringLiteral("wireBytes")));
}

// ---------------------------------------------------------------------------
// Context menu
// ---------------------------------------------------------------------------

// One menu for the right-click and for the header-bar button, so the two cannot
// drift apart — MFC drives both from CStatisticsTree::DoMenu
// (srchybrid/StatisticsTree.cpp:126). The caller pops it up; it deletes itself
// when it closes.
QMenu* StatisticsPanel::buildStatsMenu()
{
    auto* menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);

    menu->addAction(tr("Reset Statistics"), this, &StatisticsPanel::resetStats);
    QAction* restore = menu->addAction(tr("Restore Statistics"), this, &StatisticsPanel::restoreStats);
    // Greyed until a reset has left something to restore, as in MFC, which tests
    // for statbkup.ini (srchybrid/StatisticsTree.cpp:127).
    restore->setEnabled(m_backupAvailable);
    menu->addSeparator();

    menu->addAction(tr("Expand Main Sections"), this, [this]() {
        for (int i = 0; i < m_tree->topLevelItemCount(); ++i)
            m_tree->topLevelItem(i)->setExpanded(true);
    });
    menu->addAction(tr("Expand All Sections"), this, [this]() {
        m_tree->expandAll();
    });
    menu->addAction(tr("Collapse All Sections"), this, [this]() {
        m_tree->collapseAll();
    });
    menu->addSeparator();

    menu->addAction(tr("Copy Branch"), this, &StatisticsPanel::copyBranch);
    menu->addAction(tr("Copy All Visible"), this, &StatisticsPanel::copyAllVisible);
    menu->addAction(tr("Copy All Statistics"), this, &StatisticsPanel::copyAllStats);

    return menu;
}

void StatisticsPanel::onContextMenu(const QPoint& pos)
{
    buildStatsMenu()->popup(m_tree->viewport()->mapToGlobal(pos));
}

void StatisticsPanel::resetStats()
{
    if (!m_ipc || !m_ipc->isConnected())
        return;

    // MFC asks first, and says where the undo lives (IDS_STATS_MBRESET_TXT,
    // srchybrid/emule.rc:3053). It is worth asking: the reset zeroes the
    // cumulative totals in preferences.yml, not just what is on screen.
    if (QMessageBox::question(this, tr("Reset Statistics"),
                              tr("Are you sure you wish to reset your cumulative statistics?\n\n"
                                 "If you change your mind, you can reverse this action by "
                                 "clicking the 'Restore Stats' button."),
                              QMessageBox::Yes | QMessageBox::No,
                              QMessageBox::No) != QMessageBox::Yes)
        return;

    IpcMessage req(IpcMsgType::ResetStats);
    m_ipc->sendRequest(std::move(req), [this](const IpcMessage& resp) {
        if (resp.type() == IpcMsgType::Result && resp.fieldBool(0))
            requestStats();
    });
}

void StatisticsPanel::restoreStats()
{
    if (!m_ipc || !m_ipc->isConnected())
        return;

    if (QMessageBox::question(this, tr("Restore Statistics"),
                              tr("Are you sure you wish to restore your cumulative statistics "
                                 "from the backup file?\n\n"
                                 "Clicking 'Restore Stats' again will reload your current "
                                 "statistics."),
                              QMessageBox::Yes | QMessageBox::No,
                              QMessageBox::No) != QMessageBox::Yes)
        return;

    IpcMessage req(IpcMsgType::RestoreStats);
    m_ipc->sendRequest(std::move(req), [this](const IpcMessage& resp) {
        if (resp.type() == IpcMsgType::Result && resp.fieldBool(0))
            requestStats();  // repaint now rather than at the next poll
    });
}

void StatisticsPanel::copyBranch()
{
    auto* item = m_tree->currentItem();
    if (!item)
        return;
    QApplication::clipboard()->setText(treeItemText(item, 0));
}

void StatisticsPanel::copyAllVisible()
{
    QString text;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        auto* item = m_tree->topLevelItem(i);
        if (!item->isHidden())
            text += treeItemText(item, 0);
    }
    QApplication::clipboard()->setText(text);
}

void StatisticsPanel::copyAllStats()
{
    QString text;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i)
        text += treeItemText(m_tree->topLevelItem(i), 0);
    QApplication::clipboard()->setText(text);
}

QString StatisticsPanel::treeItemText(QTreeWidgetItem* item, int depth) const
{
    QString result;
    const QString indent(depth * 2, QLatin1Char(' '));
    result += indent + item->text(0) + QLatin1Char('\n');

    for (int i = 0; i < item->childCount(); ++i)
        result += treeItemText(item->child(i), depth + 1);

    return result;
}

// ---------------------------------------------------------------------------
// Formatting helpers
// ---------------------------------------------------------------------------

/// The core sends KB/s; MFC's CastItoXBytes(x, true, true) scales it on to MB/s.
QString StatisticsPanel::formatRate(double kbps)
{
    return formatByteRate(kbps * 1024.0);
}

QString StatisticsPanel::formatDuration(qint64 secs)
{
    if (secs < 0) secs = 0;
    const qint64 days = secs / 86400;
    const qint64 hours = (secs % 86400) / 3600;
    const qint64 mins = (secs % 3600) / 60;
    const qint64 s = secs % 60;

    if (days > 0)
        return QStringLiteral("%1 D %2:%3:%4")
            .arg(days)
            .arg(hours, 2, 10, QLatin1Char('0'))
            .arg(mins, 2, 10, QLatin1Char('0'))
            .arg(s, 2, 10, QLatin1Char('0'));

    return QStringLiteral("%1:%2:%3")
        .arg(hours)
        .arg(mins, 2, 10, QLatin1Char('0'))
        .arg(s, 2, 10, QLatin1Char('0'));
}

QString StatisticsPanel::formatOverhead(qint64 bytes, qint64 packets)
{
    return QStringLiteral("%1 (%2)").arg(formatByteSize(bytes)).arg(packets);
}

/// MFC StatisticsDlg.cpp:623-651: the larger side is the multiple, so "5.00 : 1" is a net
/// uploader and "1 : 5.00" a net downloader.
QString StatisticsPanel::formatRatio(qint64 up, qint64 down)
{
    if (up <= 0 || down <= 0)
        return tr("Waiting...");
    const auto u = static_cast<double>(up);
    const auto d = static_cast<double>(down);
    if (d < u)
        return QStringLiteral("%1 : 1").arg(QString::number(u / d, 'f', 2));
    return QStringLiteral("1 : %1").arg(QString::number(d / u, 'f', 2));
}

QString StatisticsPanel::formatPercent(qint64 part, qint64 whole)
{
    if (whole <= 0)
        return QStringLiteral("(0.0%)");
    const double pct = 100.0 * static_cast<double>(part) / static_cast<double>(whole);
    return QStringLiteral("(%1%)").arg(QString::number(pct, 'f', 1));
}

// ---------------------------------------------------------------------------
// Private — Usenet branch
// ---------------------------------------------------------------------------

std::span<const StatisticsPanel::CounterRow> StatisticsPanel::usenetCounterRows()
{
    using F = RowFormat;
    // Keys are the counter walks' (core/stats/NetworkCounters.h) plus the ones
    // applyUsenetStats() derives. A live row must have no children: the
    // Cumulative scope skips it, and would hang them off the wrong parent.
    static constexpr CounterRow kRows[] = {
        {QT_TR_NOOP("General"), nullptr},
        {QT_TR_NOOP("Download Speed: %1"), "downRate", F::Rate, nullptr, 1, true},
        {QT_TR_NOOP("Average Download Rate: %1"), "avgDownRate", F::Rate, nullptr, 1},
        {QT_TR_NOOP("Max Download Rate: %1"), "maxDownRate", F::Rate, nullptr, 1},
        {QT_TR_NOOP("Download Time: %1 %2"), "downloadTimeMs", F::DurationMs, "runtimeMs", 1},
        {QT_TR_NOOP("Open Connections: %1"), "openConnections", F::Count, nullptr, 1, true},
        {QT_TR_NOOP("Active Connections: %1"), "activeConnections", F::Count, nullptr, 1, true},
        {QT_TR_NOOP("Peak Connections: %1"), "peakConnections", F::Count, nullptr, 1},
        {QT_TR_NOOP("Downloaded Data: %1"), "decodedBytes", F::Bytes, nullptr, 1},
        {QT_TR_NOOP("Network Traffic: %1"), "wireBytes", F::Bytes, nullptr, 1},
        {QT_TR_NOOP("Overhead: %1 %2"), "overheadBytes", F::Bytes, "wireBytes", 2},

        {QT_TR_NOOP("Articles"), nullptr},
        {QT_TR_NOOP("Downloaded: %1"), "articlesDownloaded", F::Count, nullptr, 1},
        {QT_TR_NOOP("Not Found on a Server: %1"), "articlesNotFound", F::Count, nullptr, 1},
        {QT_TR_NOOP("Missing on All Servers: %1"), "articlesMissing", F::Count, nullptr, 1},
        // Every yEnc verdict, not only CRC: truncated, malformed and "no binary
        // data" are copies this server cannot serve us either.
        {QT_TR_NOOP("Corrupt (Failed yEnc Check): %1"), "articlesCorrupt", F::Count, nullptr, 1},
        {QT_TR_NOOP("Connection Errors: %1"), "connectionErrors", F::Count, nullptr, 1},

        {QT_TR_NOOP("Downloads"), nullptr},
        {QT_TR_NOOP("Completed Downloads: %1 %2"), "itemsCompleted", F::Count, "itemsFinished", 1},
        {QT_TR_NOOP("Completed Data: %1"), "completedBytes", F::Bytes, nullptr, 2},
        {QT_TR_NOOP("Failed Downloads: %1 %2"), "itemsFailed", F::Count, "itemsFinished", 1},

        {QT_TR_NOOP("Post-Processing"), nullptr},
        {QT_TR_NOOP("PAR2 Verified: %1"), "par2Verified", F::Count, nullptr, 1},
        {QT_TR_NOOP("Repaired: %1 %2"), "par2Repaired", F::Count, "par2Verified", 2},
        {QT_TR_NOOP("Repair Failed: %1 %2"), "par2RepairFailed", F::Count, "par2Verified", 2},
        {QT_TR_NOOP("Blocks Repaired: %1"), "par2BlocksRepaired", F::Count, nullptr, 2},
        {QT_TR_NOOP("Recovery Volumes Fetched: %1"), "recoveryVolumes", F::Count, nullptr, 1},
        {QT_TR_NOOP("Recovery Data: %1"), "recoveryBytes", F::Bytes, nullptr, 2},
        {QT_TR_NOOP("Unpacked: %1"), "unpackOk", F::Count, nullptr, 1},
        {QT_TR_NOOP("Failed: %1"), "unpackFailed", F::Count, nullptr, 2},
        {QT_TR_NOOP("Password Required: %1"), "unpackPassword", F::Count, nullptr, 2},
        {QT_TR_NOOP("Sets Unpacked While Downloading: %1"), "directUnpacks", F::Count, nullptr, 1},
        {QT_TR_NOOP("Time Spent: %1"), "postTimeMs", F::DurationMs, nullptr, 1},
        {QT_TR_NOOP("Verifying: %1 %2"), "verifyMs", F::DurationMs, "postTimeMs", 2},
        {QT_TR_NOOP("Repairing: %1 %2"), "repairMs", F::DurationMs, "postTimeMs", 2},
        {QT_TR_NOOP("Unpacking: %1 %2"), "unpackMs", F::DurationMs, "postTimeMs", 2},

        {QT_TR_NOOP("Health Checks"), nullptr},
        {QT_TR_NOOP("Checks Run: %1"), "healthChecks", F::Count, nullptr, 1},
        {QT_TR_NOOP("Passed: %1 %2"), "healthPassed", F::Count, "healthChecks", 2},
        {QT_TR_NOOP("Paused as Incomplete: %1 %2"), "healthPaused", F::Count, "healthChecks", 2},
        {QT_TR_NOOP("Inconclusive: %1 %2"), "healthInconclusive", F::Count, "healthChecks", 2},
        {QT_TR_NOOP("Articles Probed: %1"), "statProbes", F::Count, nullptr, 1},

        {QT_TR_NOOP("Intake"), nullptr},
        {QT_TR_NOOP("NZBs Added: %1"), "nzbAdded", F::Count, nullptr, 1},
        {QT_TR_NOOP("Files: %1 %2"), "nzbFromFile", F::Count, "nzbAdded", 2},
        {QT_TR_NOOP("URLs: %1 %2"), "nzbFromUrl", F::Count, "nzbAdded", 2},
        {QT_TR_NOOP("Watch Folder: %1 %2"), "nzbFromWatch", F::Count, "nzbAdded", 2},
        {QT_TR_NOOP("Feeds: %1 %2"), "nzbFromFeed", F::Count, "nzbAdded", 2},
        {QT_TR_NOOP("Indexer Searches: %1 %2"), "nzbFromIndexer", F::Count, "nzbAdded", 2},
        {QT_TR_NOOP("Duplicates: %1"), "nzbDuplicate", F::Count, nullptr, 1},
        {QT_TR_NOOP("Already Downloaded: %1"), "nzbAlreadyDownloaded", F::Count, nullptr, 1},
        {QT_TR_NOOP("Invalid NZBs: %1"), "nzbInvalid", F::Count, nullptr, 1},

        {QT_TR_NOOP("Indexers"), nullptr},
        {QT_TR_NOOP("Searches: %1"), "searches", F::Count, nullptr, 1},
        {QT_TR_NOOP("API Requests: %1"), "apiRequests", F::Count, nullptr, 1},
        {QT_TR_NOOP("Errors: %1 %2"), "apiErrors", F::Count, "apiRequests", 2},
        {QT_TR_NOOP("NZBs Fetched: %1"), "nzbFetches", F::Count, nullptr, 1},
        {QT_TR_NOOP("Failed: %1 %2"), "nzbFetchErrors", F::Count, "nzbFetches", 2},
        {QT_TR_NOOP("Feed Polls: %1"), "feedPolls", F::Count, nullptr, 1},
        {QT_TR_NOOP("Feed Matches: %1"), "feedMatches", F::Count, nullptr, 1},
    };
    return kRows;
}

std::span<const StatisticsPanel::CounterRow> StatisticsPanel::usenetQueueRows()
{
    using F = RowFormat;
    // The Usenet twin of Total Downloads, worded the same.
    static constexpr CounterRow kRows[] = {
        {QT_TR_NOOP("Number of Downloads: %1"), "count"},
        {QT_TR_NOOP("Downloading: %1"), "downloading", F::Count, nullptr, 1},
        {QT_TR_NOOP("Queued: %1"), "queued", F::Count, nullptr, 1},
        {QT_TR_NOOP("Paused: %1"), "paused", F::Count, nullptr, 1},
        {QT_TR_NOOP("Checking: %1"), "checking", F::Count, nullptr, 1},
        {QT_TR_NOOP("Post-Processing: %1"), "postProcessing", F::Count, nullptr, 1},
        {QT_TR_NOOP("Failed: %1"), "failed", F::Count, nullptr, 1},
        {QT_TR_NOOP("Completed: %1"), "complete", F::Count, nullptr, 1},
        {QT_TR_NOOP("Total Size of Downloads: %1"), "totalBytes", F::Bytes},
        {QT_TR_NOOP("Total Size Downloaded: %1"), "downloadedBytes", F::Bytes},
        {QT_TR_NOOP("Total Size Left to Download: %1"), "leftBytes", F::Bytes},
    };
    return kRows;
}

void StatisticsPanel::buildUsenetBranch(const QIcon& detailIcon, const QIcon& cumulativeIcon)
{
    // Every label down to depth 1 is fixed text: UiState remembers expansion by
    // it, so a value in one of these would forget the state on every update.
    m_itemUsenet = new QTreeWidgetItem(m_tree, {tr("Usenet")});
    m_itemUsenet->setIcon(0, QIcon(QStringLiteral(":/icons/Usenet.ico")));

    auto* session = new QTreeWidgetItem(m_itemUsenet, {tr("Session")});
    session->setIcon(0, detailIcon);
    m_usenetSessionRows.clear();
    buildCounterRows(session, usenetCounterRows(), true, m_usenetSessionRows);

    auto* cumulative = new QTreeWidgetItem(m_itemUsenet, {tr("Cumulative")});
    cumulative->setIcon(0, cumulativeIcon);
    m_usenetCumulativeRows.clear();
    buildCounterRows(cumulative, usenetCounterRows(), false, m_usenetCumulativeRows);

    // Filled per reply, one child per account (updateNewsServers()).
    m_itemUsenetServers = new QTreeWidgetItem(m_itemUsenet, {tr("News Servers")});
    m_itemUsenetServers->setIcon(0, QIcon(QStringLiteral(":/icons/Server.ico")));

    auto* queue = new QTreeWidgetItem(m_itemUsenet, {tr("Queue")});
    queue->setIcon(0, QIcon(QStringLiteral(":/icons/Download.ico")));
    m_usenetQueueRows.clear();
    buildCounterRows(queue, usenetQueueRows(), true, m_usenetQueueRows);

    const QHash<QString, qint64> zeros;
    fillCounterRows(m_usenetSessionRows, zeros);
    fillCounterRows(m_usenetCumulativeRows, zeros);
    fillCounterRows(m_usenetQueueRows, zeros);
}

// ---------------------------------------------------------------------------
// Private — HTTP Cache branch
// ---------------------------------------------------------------------------

std::span<const StatisticsPanel::CounterRow> StatisticsPanel::httpCacheUploadRows()
{
    using F = RowFormat;
    // Keys are HttpCacheCounters' own field names (core/stats/NetworkCounters.h).
    static constexpr CounterRow kRows[] = {
        {QT_TR_NOOP("Published: %1"), "bytesPublished", F::Bytes},
        {QT_TR_NOOP("Chunks Published: %1"), "chunksPublished"},
        {QT_TR_NOOP("Upload Saved: %1"), "bytesSaved", F::Bytes},
    };
    return kRows;
}

std::span<const StatisticsPanel::CounterRow> StatisticsPanel::httpCacheDownloadRows()
{
    using F = RowFormat;
    static constexpr CounterRow kRows[] = {
        {QT_TR_NOOP("Fetched: %1"), "bytesFetched", F::Bytes},
        {QT_TR_NOOP("Chunks Fetched: %1 %2"), "chunksFetched", F::Count, "fetchesFinished"},
        {QT_TR_NOOP("Failed: %1 %2"), "fetchesFailed", F::Count, "fetchesFinished", 1},
        {QT_TR_NOOP("Failed Hash Check: %1"), "partsCorrupt", F::Count, nullptr, 1},
        {QT_TR_NOOP("Resumed: %1"), "resumes", F::Count, nullptr, 1},
        {QT_TR_NOOP("Offers Received: %1"), "offersReceived"},
        {QT_TR_NOOP("Declined: %1 %2"), "offersDeclined", F::Count, "offersReceived", 1},
        {QT_TR_NOOP("Chunks Found in Kad: %1"), "kadChunks"},
    };
    return kRows;
}

void StatisticsPanel::buildHttpCacheBranch(QTreeWidgetItem* transfer, const QIcon& detailIcon,
                                           const QIcon& cumulativeIcon)
{
    // Its own branch rather than rows inside Uploads and Downloads: a cached
    // chunk is one transfer that shows up on both sides, and "Saved" belongs to
    // neither — it is upstream that never happened. Inside it the two directions
    // are split the way Transfer itself is, arrows included: publishing and
    // fetching share nothing but the cache server.
    auto* httpCache = new QTreeWidgetItem(transfer, {tr("HTTP Cache")});
    httpCache->setIcon(0, QIcon(QStringLiteral(":/icons/TransferUpDown.ico")));

    const auto buildScopes = [&](QTreeWidgetItem* parent, std::span<const CounterRow> rows,
                                 QList<CounterItem>& sessionOut, QList<CounterItem>& cumOut) {
        auto* session = new QTreeWidgetItem(parent, {tr("Session")});
        session->setIcon(0, detailIcon);
        sessionOut.clear();
        buildCounterRows(session, rows, true, sessionOut);

        auto* cumulative = new QTreeWidgetItem(parent, {tr("Cumulative")});
        cumulative->setIcon(0, cumulativeIcon);
        cumOut.clear();
        buildCounterRows(cumulative, rows, false, cumOut);
    };

    auto* uploads = new QTreeWidgetItem(httpCache, {tr("Uploads")});
    uploads->setIcon(0, QIcon(QStringLiteral(":/icons/Upload.ico")));
    buildScopes(uploads, httpCacheUploadRows(), m_hcUpSessionRows, m_hcUpCumulativeRows);

    auto* downloads = new QTreeWidgetItem(httpCache, {tr("Downloads")});
    downloads->setIcon(0, QIcon(QStringLiteral(":/icons/Download.ico")));
    buildScopes(downloads, httpCacheDownloadRows(), m_hcDownSessionRows, m_hcDownCumulativeRows);

    const QHash<QString, qint64> zeros;
    for (const QList<CounterItem>* rows : {&m_hcUpSessionRows, &m_hcUpCumulativeRows,
                                           &m_hcDownSessionRows, &m_hcDownCumulativeRows}) {
        fillCounterRows(*rows, zeros);
    }
}

void StatisticsPanel::buildCounterRows(QTreeWidgetItem* parent, std::span<const CounterRow> rows,
                                       bool includeLive, QList<CounterItem>& out)
{
    std::array<QTreeWidgetItem*, 4> parents{parent};
    for (const CounterRow& row : rows) {
        if (row.liveOnly && !includeLive)
            continue;
        const auto depth = static_cast<size_t>(row.depth);
        if (depth + 1 >= parents.size() || !parents[depth])
            continue;

        auto* item = new QTreeWidgetItem(parents[depth]);
        parents[depth + 1] = item;
        if (row.key)
            out.append({&row, item});
        else
            item->setText(0, tr(row.pattern));
    }
}

QHash<QString, qint64> StatisticsPanel::counterValues(const QCborMap& block)
{
    QHash<QString, qint64> v;
    v.reserve(block.size());
    for (auto it = block.cbegin(); it != block.cend(); ++it)
        v.insert(it.key().toString(), it.value().toInteger());
    return v;
}

void StatisticsPanel::fillCounterRows(const QList<CounterItem>& items,
                                      const QHash<QString, qint64>& values)
{
    for (const auto& [row, item] : items) {
        const qint64 v = values.value(QString::fromLatin1(row->key));
        QString value;
        switch (row->format) {
        case RowFormat::Count:      value = QString::number(v); break;
        case RowFormat::Bytes:      value = formatByteSize(v); break;
        case RowFormat::Rate:       value = formatByteRate(v); break;
        case RowFormat::DurationMs: value = formatDuration(v / 1000); break;
        }

        if (row->shareOf) {
            item->setText(0, tr(row->pattern).arg(
                value, formatPercent(v, values.value(QString::fromLatin1(row->shareOf)))));
        } else {
            item->setText(0, tr(row->pattern).arg(value));
        }
    }
}

void StatisticsPanel::updateNewsServers(const QCborArray& servers, qint64 sessionWireBytes)
{
    // Updated in place, keyed by account id, so expansion and selection survive
    // the poll. Rebuilding like the Client Software subtree would reset both.
    QHash<QString, QTreeWidgetItem*> existing;
    for (int i = 0; i < m_itemUsenetServers->childCount(); ++i) {
        QTreeWidgetItem* node = m_itemUsenetServers->child(i);
        existing.insert(node->data(0, Qt::UserRole).toString(), node);
    }

    QSet<QString> seen;
    for (const auto& value : servers) {
        const QCborMap s = value.toMap();
        const QString id = s.value(QStringLiteral("accountId")).toString();
        if (seen.contains(id))
            continue;
        seen.insert(id);

        QTreeWidgetItem* node = existing.value(id);
        if (!node) {
            node = new QTreeWidgetItem(m_itemUsenetServers);
            node->setData(0, Qt::UserRole, id);
            new QTreeWidgetItem(node);                   // 0 open connections
            new QTreeWidgetItem(node);                   // 1 session traffic
            auto* articles = new QTreeWidgetItem(node);  // 2 articles
            for (int i = 0; i < 3; ++i)
                new QTreeWidgetItem(articles);           //   not found, corrupt, errors
            auto* meter = new QTreeWidgetItem(node);     // 3 billing period
            auto* allTime = new QTreeWidgetItem(node);   // 4 all time
            const QString measured =
                tr("Measured here, not reported by the provider, in decimal GB as "
                   "providers bill. Expect a few percent below the provider's own figure.");
            meter->setToolTip(0, measured);
            allTime->setToolTip(0, measured);
        }

        QString name = s.value(QStringLiteral("name")).toString();
        if (name.isEmpty())
            name = s.value(QStringLiteral("host")).toString();
        node->setText(0, s.value(QStringLiteral("enabled")).toBool(true)
                             ? name : tr("%1 (disabled)").arg(name));

        const auto session =
            countersFromCbor<UsenetServerCounters>(s.value(QStringLiteral("session")).toMap());
        const auto sessionWire = static_cast<qint64>(session.wireBytes);

        node->child(0)->setText(0, tr("Open Connections: %1")
            .arg(s.value(QStringLiteral("openConnections")).toInteger()));
        node->child(1)->setText(0, tr("Session Traffic: %1 %2")
            .arg(formatByteSize(sessionWire), formatPercent(sessionWire, sessionWireBytes)));

        QTreeWidgetItem* articles = node->child(2);
        articles->setText(0, tr("Articles Downloaded: %1").arg(session.articles));
        articles->child(0)->setText(0, tr("Not Found: %1").arg(session.notFound));
        articles->child(1)->setText(0, tr("Corrupt: %1").arg(session.corrupt));
        articles->child(2)->setText(0, tr("Connection Errors: %1").arg(session.errors));

        // The billing meter, worded like Options > Usenet so one account's
        // allowance reads the same in both places.
        const auto kind = static_cast<NntpQuotaKind>(s.value(QStringLiteral("quotaKind")).toInteger());
        const qint64 period = s.value(QStringLiteral("periodBytes")).toInteger();
        const qint64 quota = s.value(QStringLiteral("quotaBytes")).toInteger();
        QString used = quota > 0
            ? tr("%1 of %2 %3").arg(formatQuotaGb(period), formatQuotaGb(quota),
                                    formatPercent(period, quota))
            : formatQuotaGb(period);
        if (kind == NntpQuotaKind::Monthly) {
            const QDate resets = QDate::fromString(
                s.value(QStringLiteral("resetsOn")).toString(), Qt::ISODate);
            if (resets.isValid())
                used += tr(", resets %1").arg(QLocale::system().toString(resets, QLocale::ShortFormat));
        }
        if (s.value(QStringLiteral("overQuota")).toBool(false))
            used += tr(" — spent");

        QTreeWidgetItem* meter = node->child(3);
        meter->setHidden(kind == NntpQuotaKind::None);
        meter->setText(0, kind == NntpQuotaKind::Block ? tr("Block: %1").arg(used)
                                                        : tr("This Period: %1").arg(used));
        node->child(4)->setText(0, tr("All Time: %1")
            .arg(formatQuotaGb(s.value(QStringLiteral("totalBytes")).toInteger())));
    }

    for (auto it = existing.cbegin(); it != existing.cend(); ++it) {
        if (!seen.contains(it.key()))
            delete it.value();
    }
}

} // namespace eMule
