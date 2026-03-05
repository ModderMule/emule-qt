/// @file StatisticsPanel.cpp
/// @brief Statistics panel — tree view + oscilloscope graphs — implementation.

#include "panels/StatisticsPanel.h"

#include "app/IpcClient.h"
#include "app/UiState.h"
#include "controls/StatsGraph.h"
#include "prefs/Preferences.h"

#include "IpcMessage.h"
#include "IpcProtocol.h"

#include <QApplication>
#include <QCborMap>
#include <QClipboard>
#include <QDateTime>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMenu>
#include <QSplitter>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

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

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

StatisticsPanel::StatisticsPanel(QWidget* parent)
    : QWidget(parent)
{
    setupUi();

    m_refreshTimer = new QTimer(this);
    connect(m_refreshTimer, &QTimer::timeout, this, &StatisticsPanel::onRefreshTimer);

    // Graph sampling timer — decoupled from IPC polling to match MFC's 3-second interval.
    // 1024 points × 3s = ~51 min visible, matching MFC's ~54 min at 1080px.
    m_graphTimer = new QTimer(this);
    m_graphTimer->setInterval(3000);
    connect(m_graphTimer, &QTimer::timeout, this, &StatisticsPanel::onGraphTimer);
}

void StatisticsPanel::setIpcClient(IpcClient* client)
{
    m_ipc = client;

    if (m_ipc && m_ipc->isConnected()) {
        m_refreshTimer->setInterval(m_ipc->pollingInterval());
        m_refreshTimer->start();
        m_graphTimer->start();
        applySettings();
        onRefreshTimer();
    } else if (m_ipc) {
        connect(m_ipc, &IpcClient::connected, this, [this]() {
            m_refreshTimer->setInterval(m_ipc->pollingInterval());
            m_refreshTimer->start();
            m_graphTimer->start();
            applySettings();
            onRefreshTimer();
        });
        connect(m_ipc, &IpcClient::disconnected, this, [this]() {
            m_refreshTimer->stop();
            m_graphTimer->stop();
            m_lastStats = {};
            m_graphDown->reset();
            m_graphUp->reset();
            m_graphConn->reset();
        });
    } else {
        m_refreshTimer->stop();
        m_graphTimer->stop();
    }
}

void StatisticsPanel::applySettings()
{
    // Graph update interval
    uint32_t graphSec = thePrefs.graphsUpdateSec();
    if (graphSec > 0) {
        m_graphTimer->setInterval(static_cast<int>(graphSec) * 1000);
        if (!m_graphTimer->isActive())
            m_graphTimer->start();
    } else {
        m_graphTimer->stop();
    }

    // Stats tree update interval
    uint32_t statsSec = thePrefs.statsUpdateSec();
    if (statsSec > 0) {
        m_refreshTimer->setInterval(static_cast<int>(statsSec) * 1000);
        if (!m_refreshTimer->isActive() && m_ipc && m_ipc->isConnected())
            m_refreshTimer->start();
    } else {
        m_refreshTimer->stop();
    }

    // Background and grid colors
    const QColor bg(0, 0, 64);
    const QColor grid(192, 192, 255);
    for (auto* graph : {m_graphDown, m_graphUp, m_graphConn}) {
        graph->setBackgroundColor(bg);
        graph->setGridColor(grid);
        graph->setFillAll(thePrefs.fillGraphs());
    }

    // Connections graph Y-axis scale
    auto connMax = static_cast<double>(thePrefs.statsConnectionsMax());
    if (connMax > 0)
        m_graphConn->setYRange(0, connMax);
    else
        m_graphConn->setYRange(0, 0); // auto-scale
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

    // Left side: tree
    auto* treeContainer = new QWidget(this);
    auto* treeLayout = new QVBoxLayout(treeContainer);
    treeLayout->setContentsMargins(2, 2, 2, 2);

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

    // Right side: 3 stacked graphs
    auto* graphSplitter = new QSplitter(Qt::Vertical, this);

    // Graph 1: Download (3 series)
    m_graphDown = new StatsGraph(3, this);
    m_graphDown->setSeriesInfo(0, tr("Session average"), QColor(0, 128, 0));
    m_graphDown->setSeriesInfo(1, tr("Average (3 min)"), QColor(0, 210, 0));
    m_graphDown->setSeriesInfo(2, tr("Current"), QColor(128, 255, 128));
    m_graphDown->setYUnits(tr("KB/s"));
    graphSplitter->addWidget(m_graphDown);

    // Graph 2: Upload (5 series)
    m_graphUp = new StatsGraph(5, this);
    m_graphUp->setSeriesInfo(0, tr("Session average"), QColor(140, 0, 0));
    m_graphUp->setSeriesInfo(1, tr("Average (3 min)"), QColor(200, 0, 0));
    m_graphUp->setSeriesInfo(2, tr("Current"), QColor(255, 128, 128));
    m_graphUp->setSeriesInfo(3, tr("Current (excl. overhead)"), QColor(255, 190, 190));
    m_graphUp->setSeriesInfo(4, tr("Friend slots"), QColor(255, 255, 255));
    m_graphUp->setYUnits(tr("KB/s"));
    graphSplitter->addWidget(m_graphUp);

    // Graph 3: Connections (4 series)
    m_graphConn = new StatsGraph(4, this);
    m_graphConn->setSeriesInfo(0, tr("Active connections"), QColor(150, 150, 255));
    m_graphConn->setSeriesInfo(1, tr("Active uploads"), QColor(255, 255, 128));
    m_graphConn->setSeriesInfo(2, tr("Total uploads"), QColor(192, 0, 192));
    m_graphConn->setSeriesInfo(3, tr("Active downloads"), QColor(255, 255, 255));
    graphSplitter->addWidget(m_graphConn);

    m_hSplitter->addWidget(graphSplitter);

    // Default splitter proportions: 30% tree, 70% graphs
    m_hSplitter->setStretchFactor(0, 3);
    m_hSplitter->setStretchFactor(1, 7);

    theUiState.bindStatsSplitter(m_hSplitter);

    buildTree();
}

// ---------------------------------------------------------------------------
// Tree construction
// ---------------------------------------------------------------------------

void StatisticsPanel::buildTree()
{
    m_tree->clear();

    // Transfer
    auto* transfer = new QTreeWidgetItem(m_tree, {tr("Transfer")});
    transfer->setIcon(0, QIcon(QStringLiteral(":/icons/TransferUpDown.ico")));
    transfer->setExpanded(true);

    m_itemSessionUlDlRatio = new QTreeWidgetItem(transfer, {tr("Session UL:DL Ratio: -")});
    m_itemFriendUlDlRatio = new QTreeWidgetItem(transfer, {tr("Friend Session UL:DL Ratio: -")});
    m_itemCumUlDlRatio = new QTreeWidgetItem(transfer, {tr("Cumulative UL:DL Ratio: -")});

    // Uploads
    auto* uploads = new QTreeWidgetItem(transfer, {tr("Uploads")});
    uploads->setIcon(0, QIcon(QStringLiteral(":/icons/Upload.ico")));
    auto* upSession = new QTreeWidgetItem(uploads, {tr("Session")});
    upSession->setIcon(0, QIcon(QStringLiteral(":/icons/StatisticsDetail.ico")));

    m_itemUpSessionData = new QTreeWidgetItem(upSession, {tr("Uploaded Data: 0 Bytes")});
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

    m_itemUpOverheadTotal = new QTreeWidgetItem(upSession,
                                                {tr("Total Overhead (Packets): 0 Bytes (0)")});
    m_itemUpOverheadFileReq = new QTreeWidgetItem(m_itemUpOverheadTotal,
                                                  {tr("File Request Overhead (Packets): 0 Bytes (0)")});
    m_itemUpOverheadSrcExch = new QTreeWidgetItem(m_itemUpOverheadTotal,
                                                  {tr("Source Exchange Overhead (Packets): 0 Bytes (0)")});
    m_itemUpOverheadServer = new QTreeWidgetItem(m_itemUpOverheadTotal,
                                                 {tr("Server Overhead (Packets): 0 Bytes (0)")});
    m_itemUpOverheadKad = new QTreeWidgetItem(m_itemUpOverheadTotal,
                                              {tr("Kad Overhead (Packets): 0 Bytes (0)")});

    // Downloads
    auto* downloads = new QTreeWidgetItem(transfer, {tr("Downloads")});
    downloads->setIcon(0, QIcon(QStringLiteral(":/icons/Download.ico")));
    auto* downSession = new QTreeWidgetItem(downloads, {tr("Session")});
    downSession->setIcon(0, QIcon(QStringLiteral(":/icons/StatisticsDetail.ico")));

    m_itemDownSessionData = new QTreeWidgetItem(downSession, {tr("Downloaded Data: 0 Bytes")});
    m_itemDownActiveDownloads = new QTreeWidgetItem(downSession,
                                                    {tr("Active Downloads: 0")});
    m_itemDownFoundSources = new QTreeWidgetItem(downSession,
                                                 {tr("Found Sources: 0")});

    m_itemDownOverheadTotal = new QTreeWidgetItem(downSession,
                                                  {tr("Total Overhead (Packets): 0 Bytes (0)")});
    m_itemDownOverheadFileReq = new QTreeWidgetItem(m_itemDownOverheadTotal,
                                                    {tr("File Request Overhead (Packets): 0 Bytes (0)")});
    m_itemDownOverheadSrcExch = new QTreeWidgetItem(m_itemDownOverheadTotal,
                                                    {tr("Source Exchange Overhead (Packets): 0 Bytes (0)")});
    m_itemDownOverheadServer = new QTreeWidgetItem(m_itemDownOverheadTotal,
                                                   {tr("Server Overhead (Packets): 0 Bytes (0)")});
    m_itemDownOverheadKad = new QTreeWidgetItem(m_itemDownOverheadTotal,
                                                {tr("Kad Overhead (Packets): 0 Bytes (0)")});

    // Connection
    auto* connection = new QTreeWidgetItem(m_tree, {tr("Connection")});
    connection->setIcon(0, QIcon(QStringLiteral(":/icons/Connection.ico")));
    auto* connSession = new QTreeWidgetItem(connection, {tr("Session")});
    connSession->setIcon(0, QIcon(QStringLiteral(":/icons/StatisticsDetail.ico")));

    m_itemConnActive = new QTreeWidgetItem(connSession, {tr("Active Connections: 0")});
    m_itemConnPeak = new QTreeWidgetItem(connSession, {tr("Peak Connections: 0")});
    m_itemConnMaxReached = new QTreeWidgetItem(connSession,
                                               {tr("Max Connections Limit Reached: 0")});
    m_itemConnReconnects = new QTreeWidgetItem(connSession, {tr("Reconnects: 0")});
    m_itemConnAverage = new QTreeWidgetItem(connSession,
                                            {tr("Average Connections: 0")});

    // Time Statistics
    m_itemTimeHeader = new QTreeWidgetItem(m_tree, {tr("Time Statistics")});
    m_itemTimeHeader->setIcon(0, QIcon(QStringLiteral(":/icons/StatsTime.ico")));
    m_itemTimeSinceReset = new QTreeWidgetItem(m_itemTimeHeader,
                                               {tr("Time Since Last Reset: -")});

    auto* timeSession = new QTreeWidgetItem(m_itemTimeHeader, {tr("Session")});
    timeSession->setIcon(0, QIcon(QStringLiteral(":/icons/StatisticsDetail.ico")));
    m_itemRuntime = new QTreeWidgetItem(timeSession, {tr("Runtime: 0:00:00")});
    m_itemTransferTime = new QTreeWidgetItem(timeSession, {tr("Transfer Time: 0:00:00")});
    m_itemUploadTime = new QTreeWidgetItem(timeSession, {tr("Upload Time: 0:00:00")});
    m_itemDownloadTime = new QTreeWidgetItem(timeSession, {tr("Download Time: 0:00:00")});
    m_itemServerDuration = new QTreeWidgetItem(timeSession,
                                               {tr("Server Duration: 0:00:00")});

    // Clients
    auto* clients = new QTreeWidgetItem(m_tree, {tr("Clients")});
    clients->setIcon(0, QIcon(QStringLiteral(":/icons/User.ico")));
    m_itemKnownClients = new QTreeWidgetItem(clients, {tr("Known Clients: 0")});
    m_itemBannedClients = new QTreeWidgetItem(clients, {tr("Banned Clients: 0")});
    m_itemFilteredClients = new QTreeWidgetItem(clients, {tr("Filtered Clients: 0")});

    // Servers
    auto* servers = new QTreeWidgetItem(m_tree, {tr("Servers")});
    servers->setIcon(0, QIcon(QStringLiteral(":/icons/Server.ico")));
    m_itemSrvWorking = new QTreeWidgetItem(servers, {tr("Working Servers: 0")});
    m_itemSrvFailed = new QTreeWidgetItem(servers, {tr("Failed Servers: 0")});
    m_itemSrvTotal = new QTreeWidgetItem(servers, {tr("Total: 0")});
    m_itemSrvUsers = new QTreeWidgetItem(servers, {tr("Total Users: 0")});
    m_itemSrvFiles = new QTreeWidgetItem(servers, {tr("Total Files: 0")});
    m_itemSrvLowID = new QTreeWidgetItem(servers, {tr("Low ID Users: 0")});

    // Shared Files
    auto* shared = new QTreeWidgetItem(m_tree, {tr("Shared Files")});
    shared->setIcon(0, QIcon(QStringLiteral(":/icons/SharedFiles.ico")));
    m_itemSharedCount = new QTreeWidgetItem(shared, {tr("Number of Shared Files: 0")});
    m_itemSharedSize = new QTreeWidgetItem(shared, {tr("Total Size: 0 Bytes")});
    m_itemSharedLargest = new QTreeWidgetItem(shared, {tr("Largest Shared File: 0 Bytes")});

    // Expand top-level sections by default
    transfer->setExpanded(true);
    connection->setExpanded(true);
    m_itemTimeHeader->setExpanded(true);
}

// ---------------------------------------------------------------------------
// IPC polling
// ---------------------------------------------------------------------------

void StatisticsPanel::onRefreshTimer()
{
    requestStats();
}

void StatisticsPanel::onGraphTimer()
{
    if (!m_lastStats.isEmpty())
        feedGraphs(m_lastStats);
}

void StatisticsPanel::requestStats()
{
    if (!m_ipc || !m_ipc->isConnected())
        return;

    IpcMessage req(IpcMsgType::GetStats);
    m_ipc->sendRequest(std::move(req), [this](const IpcMessage& resp) {
        if (resp.type() != IpcMsgType::Result || !resp.fieldBool(0))
            return;

        const QCborMap stats = resp.fieldMap(1);
        updateTree(stats);
        m_lastStats = stats;
    });
}

// ---------------------------------------------------------------------------
// Feed graph data
// ---------------------------------------------------------------------------

void StatisticsPanel::feedGraphs(const QCborMap& stats)
{
    // Download graph: session avg, time avg, current
    m_graphDown->appendPoints({
        cborDouble(stats, QLatin1StringView("avgDownSession")),
        cborDouble(stats, QLatin1StringView("avgDownTime")),
        cborDouble(stats, QLatin1StringView("rateDown"))
    });

    // Upload graph: session avg, time avg, current, current data-only, friend
    const double upTotal = cborDouble(stats, QLatin1StringView("rateUp"));
    // Datarate from uploadQueue = data only (no overhead)
    const double upDataOnly = cborDouble(stats, QLatin1StringView("upDatarate")) / 1024.0;
    const double upFriend = cborDouble(stats, QLatin1StringView("upFriendDatarate")) / 1024.0;

    m_graphUp->appendPoints({
        cborDouble(stats, QLatin1StringView("avgUpSession")),
        cborDouble(stats, QLatin1StringView("avgUpTime")),
        upTotal,
        upDataOnly,
        upFriend
    });

    // Connections graph: active connections, active uploads, queue length, active downloads
    m_graphConn->appendPoints({
        static_cast<double>(cborInt(stats, QLatin1StringView("connActive"))),
        static_cast<double>(cborInt(stats, QLatin1StringView("upWaiting"))),
        static_cast<double>(cborInt(stats, QLatin1StringView("upQueueLength"))),
        static_cast<double>(cborInt(stats, QLatin1StringView("downFileCount")))
    });
}

// ---------------------------------------------------------------------------
// Update tree items
// ---------------------------------------------------------------------------

void StatisticsPanel::updateTree(const QCborMap& stats)
{
    const qint64 sent = cborInt(stats, QLatin1StringView("sessionSentBytes"));
    const qint64 recv = cborInt(stats, QLatin1StringView("sessionReceivedBytes"));
    const qint64 sentFriend = cborInt(stats, QLatin1StringView("sessionSentBytesToFriend"));
    const qint64 uptime = cborInt(stats, QLatin1StringView("uptime"));

    // Transfer ratios
    m_itemSessionUlDlRatio->setText(0,
        tr("Session UL:DL Ratio: %1").arg(formatRatio(sent, recv)));
    m_itemFriendUlDlRatio->setText(0,
        tr("Friend Session UL:DL Ratio: %1").arg(formatRatio(sentFriend, recv)));
    m_itemCumUlDlRatio->setText(0,
        tr("Cumulative UL:DL Ratio: %1").arg(formatRatio(sent, recv)));

    // Uploads — Session
    m_itemUpSessionData->setText(0,
        tr("Uploaded Data: %1").arg(formatBytes(sent)));
    m_itemUpSessionFriendData->setText(0,
        tr("Uploaded Data to Friends: %1").arg(formatBytes(sentFriend)));
    m_itemUpActiveUploads->setText(0,
        tr("Active Uploads: %1").arg(cborInt(stats, QLatin1StringView("upWaiting"))));
    m_itemUpWaitingUploads->setText(0,
        tr("Waiting Uploads: %1").arg(cborInt(stats, QLatin1StringView("upQueueLength"))));

    const qint64 upSucc = cborInt(stats, QLatin1StringView("upSuccessful"));
    const qint64 upFail = cborInt(stats, QLatin1StringView("upFailed"));
    const qint64 upTotal = upSucc + upFail;
    m_itemUpSuccessful->setText(0,
        tr("Successful: %1%2").arg(upSucc)
            .arg(upTotal > 0
                 ? QStringLiteral(" (%1%)").arg(100 * upSucc / upTotal)
                 : QString()));
    m_itemUpFailed->setText(0, tr("Failed: %1").arg(upFail));

    if (upSucc > 0)
        m_itemUpAvgPerSession->setText(0,
            tr("Average Upload Per Session: %1").arg(formatBytes(sent / upSucc)));
    m_itemUpAvgTime->setText(0,
        tr("Average Upload Time: %1")
            .arg(formatDuration(cborInt(stats, QLatin1StringView("upAvgTime")))));

    // Upload overhead
    m_itemUpOverheadTotal->setText(0,
        tr("Total Overhead (Packets): %1")
            .arg(formatOverhead(cborInt(stats, QLatin1StringView("upOverheadTotal")),
                                cborInt(stats, QLatin1StringView("upOverheadTotalPackets")))));
    m_itemUpOverheadFileReq->setText(0,
        tr("File Request Overhead (Packets): %1")
            .arg(formatOverhead(cborInt(stats, QLatin1StringView("upOverheadFileReq")),
                                cborInt(stats, QLatin1StringView("upOverheadFileReqPkt")))));
    m_itemUpOverheadSrcExch->setText(0,
        tr("Source Exchange Overhead (Packets): %1")
            .arg(formatOverhead(cborInt(stats, QLatin1StringView("upOverheadSrcExch")),
                                cborInt(stats, QLatin1StringView("upOverheadSrcExchPkt")))));
    m_itemUpOverheadServer->setText(0,
        tr("Server Overhead (Packets): %1")
            .arg(formatOverhead(cborInt(stats, QLatin1StringView("upOverheadServer")),
                                cborInt(stats, QLatin1StringView("upOverheadServerPkt")))));
    m_itemUpOverheadKad->setText(0,
        tr("Kad Overhead (Packets): %1")
            .arg(formatOverhead(cborInt(stats, QLatin1StringView("upOverheadKad")),
                                cborInt(stats, QLatin1StringView("upOverheadKadPkt")))));

    // Downloads — Session
    m_itemDownSessionData->setText(0,
        tr("Downloaded Data: %1").arg(formatBytes(recv)));
    m_itemDownActiveDownloads->setText(0,
        tr("Active Downloads: %1")
            .arg(cborInt(stats, QLatin1StringView("downFileCount"))));
    m_itemDownFoundSources->setText(0,
        tr("Found Sources: %1")
            .arg(cborInt(stats, QLatin1StringView("downFoundSources"))));

    // Download overhead
    m_itemDownOverheadTotal->setText(0,
        tr("Total Overhead (Packets): %1")
            .arg(formatOverhead(cborInt(stats, QLatin1StringView("downOverheadTotal")),
                                cborInt(stats, QLatin1StringView("downOverheadTotalPackets")))));
    m_itemDownOverheadFileReq->setText(0,
        tr("File Request Overhead (Packets): %1")
            .arg(formatOverhead(cborInt(stats, QLatin1StringView("downOverheadFileReq")),
                                cborInt(stats, QLatin1StringView("downOverheadFileReqPkt")))));
    m_itemDownOverheadSrcExch->setText(0,
        tr("Source Exchange Overhead (Packets): %1")
            .arg(formatOverhead(cborInt(stats, QLatin1StringView("downOverheadSrcExch")),
                                cborInt(stats, QLatin1StringView("downOverheadSrcExchPkt")))));
    m_itemDownOverheadServer->setText(0,
        tr("Server Overhead (Packets): %1")
            .arg(formatOverhead(cborInt(stats, QLatin1StringView("downOverheadServer")),
                                cborInt(stats, QLatin1StringView("downOverheadServerPkt")))));
    m_itemDownOverheadKad->setText(0,
        tr("Kad Overhead (Packets): %1")
            .arg(formatOverhead(cborInt(stats, QLatin1StringView("downOverheadKad")),
                                cborInt(stats, QLatin1StringView("downOverheadKadPkt")))));

    // Connection
    m_itemConnActive->setText(0,
        tr("Active Connections: %1").arg(cborInt(stats, QLatin1StringView("connActive"))));
    m_itemConnPeak->setText(0,
        tr("Peak Connections: %1").arg(cborInt(stats, QLatin1StringView("connPeak"))));
    m_itemConnMaxReached->setText(0,
        tr("Max Connections Limit Reached: %1")
            .arg(cborInt(stats, QLatin1StringView("connMaxReached"))));
    m_itemConnReconnects->setText(0,
        tr("Reconnects: %1").arg(cborInt(stats, QLatin1StringView("reconnects"))));
    m_itemConnAverage->setText(0,
        tr("Average Connections: %1")
            .arg(QString::number(cborDouble(stats, QLatin1StringView("connAverage")), 'f', 1)));

    // Time Statistics
    m_itemTimeSinceReset->setText(0,
        tr("Time Since Last Reset: %1").arg(formatDuration(uptime)));
    m_itemRuntime->setText(0,
        tr("Runtime: %1").arg(formatDuration(uptime)));

    const qint64 tTransfer = cborInt(stats, QLatin1StringView("transferTime"));
    const qint64 tUpload = cborInt(stats, QLatin1StringView("uploadTime"));
    const qint64 tDownload = cborInt(stats, QLatin1StringView("downloadTime"));
    const qint64 tServer = cborInt(stats, QLatin1StringView("serverDuration"));

    m_itemTransferTime->setText(0,
        tr("Transfer Time: %1 %2").arg(formatDuration(tTransfer), formatPercent(tTransfer, uptime)));
    m_itemUploadTime->setText(0,
        tr("Upload Time: %1 %2").arg(formatDuration(tUpload), formatPercent(tUpload, uptime)));
    m_itemDownloadTime->setText(0,
        tr("Download Time: %1 %2").arg(formatDuration(tDownload), formatPercent(tDownload, uptime)));
    m_itemServerDuration->setText(0,
        tr("Server Duration: %1 %2").arg(formatDuration(tServer), formatPercent(tServer, uptime)));

    // Clients
    m_itemKnownClients->setText(0,
        tr("Known Clients: %1").arg(cborInt(stats, QLatin1StringView("knownClients"))));
    m_itemBannedClients->setText(0,
        tr("Banned Clients: %1").arg(cborInt(stats, QLatin1StringView("bannedClients"))));
    m_itemFilteredClients->setText(0,
        tr("Filtered Clients: %1").arg(cborInt(stats, QLatin1StringView("filteredClients"))));

    // Servers
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

    // Shared Files
    m_itemSharedCount->setText(0,
        tr("Number of Shared Files: %1").arg(cborInt(stats, QLatin1StringView("sharedCount"))));
    m_itemSharedSize->setText(0,
        tr("Total Size: %1").arg(formatBytes(cborInt(stats, QLatin1StringView("sharedSize")))));
    m_itemSharedLargest->setText(0,
        tr("Largest Shared File: %1")
            .arg(formatBytes(cborInt(stats, QLatin1StringView("sharedLargest")))));
}

// ---------------------------------------------------------------------------
// Context menu
// ---------------------------------------------------------------------------

void StatisticsPanel::onContextMenu(const QPoint& pos)
{
    QMenu menu(this);

    menu.addAction(tr("Reset Statistics"), this, &StatisticsPanel::resetStats);
    menu.addSeparator();

    menu.addAction(tr("Expand Main Sections"), this, [this]() {
        for (int i = 0; i < m_tree->topLevelItemCount(); ++i)
            m_tree->topLevelItem(i)->setExpanded(true);
    });
    menu.addAction(tr("Expand All Sections"), this, [this]() {
        m_tree->expandAll();
    });
    menu.addAction(tr("Collapse All Sections"), this, [this]() {
        m_tree->collapseAll();
    });
    menu.addSeparator();

    menu.addAction(tr("Copy Branch"), this, &StatisticsPanel::copyBranch);
    menu.addAction(tr("Copy All Visible"), this, &StatisticsPanel::copyAllVisible);
    menu.addAction(tr("Copy All Statistics"), this, &StatisticsPanel::copyAllStats);

    menu.exec(m_tree->viewport()->mapToGlobal(pos));
}

void StatisticsPanel::resetStats()
{
    if (!m_ipc || !m_ipc->isConnected())
        return;

    IpcMessage req(IpcMsgType::ResetStats);
    m_ipc->sendRequest(std::move(req), [this](const IpcMessage& resp) {
        if (resp.type() == IpcMsgType::Result && resp.fieldBool(0))
            requestStats(); // refresh tree immediately after reset
    });
}

void StatisticsPanel::copyBranch()
{
    auto* item = m_tree->currentItem();
    if (!item)
        return;

    QString text;
    // Copy this item and all children
    text = treeItemText(item, 0);

    QApplication::clipboard()->setText(text);
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
    // Expand all, copy everything
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

QString StatisticsPanel::formatBytes(qint64 bytes)
{
    if (bytes < 1024)
        return tr("%1 Bytes").arg(bytes);
    if (bytes < 1024 * 1024)
        return QStringLiteral("%1 KB").arg(static_cast<double>(bytes) / 1024.0, 0, 'f', 2);
    if (bytes < 1024LL * 1024 * 1024)
        return QStringLiteral("%1 MB").arg(static_cast<double>(bytes) / (1024.0 * 1024.0), 0, 'f', 2);
    if (bytes < 1024LL * 1024 * 1024 * 1024)
        return QStringLiteral("%1 GB").arg(static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0), 0, 'f', 2);
    return QStringLiteral("%1 TB").arg(static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0 * 1024.0), 0, 'f', 2);
}

QString StatisticsPanel::formatRate(double kbps)
{
    return QStringLiteral("%1 KB/s").arg(kbps, 0, 'f', 1);
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
    return QStringLiteral("%1 (%2)").arg(formatBytes(bytes)).arg(packets);
}

QString StatisticsPanel::formatRatio(qint64 sent, qint64 received)
{
    if (received == 0 && sent == 0)
        return QStringLiteral("-");
    if (received == 0)
        return QStringLiteral("%1:0").arg(QString::number(1.0, 'f', 2));

    const double ratio = static_cast<double>(sent) / static_cast<double>(received);
    return QStringLiteral("1:%1").arg(QString::number(ratio, 'f', 2));
}

QString StatisticsPanel::formatPercent(qint64 part, qint64 whole)
{
    if (whole <= 0)
        return QStringLiteral("(0.0%)");
    const double pct = 100.0 * static_cast<double>(part) / static_cast<double>(whole);
    return QStringLiteral("(%1%)").arg(QString::number(pct, 'f', 1));
}

} // namespace eMule
