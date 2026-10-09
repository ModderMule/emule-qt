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
#include "utils/CountryFlags.h"
#include "utils/PanelPoller.h"
#include "utils/StringUtils.h"

#include "IpcMessage.h"

#include <QApplication>
#include <QBuffer>
#include <QCborArray>
#include <QCborMap>
#include <QClipboard>
#include <QDateTime>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QSet>
#include <QSplitter>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <tuple>

namespace eMule {

namespace {

/// Draws the section headers bold, as MFC's TVIS_BOLD nodes.
class SectionDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

protected:
    void initStyleOption(QStyleOptionViewItem* option, const QModelIndex& index) const override
    {
        QStyledItemDelegate::initStyleOption(option, index);
        // same rule as StatisticsPanel::isSection()
        if (!option->icon.isNull() && !index.data(Qt::UserRole).isValid())
            option->font.setBold(true);
    }
};

} // namespace

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

int StatisticsPanel::connectionsRatio()
{
    return std::max(1, static_cast<int>(thePrefs.statsConnectionsRatio()));
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
    }
    // MFC fills one trend per scope (SetBarsPlot, StatisticsDlg.cpp:527-544): the
    // current rates and the connection count. Translucent here, solid bars there.
    const bool fill = thePrefs.fillGraphs();
    m_graphDown->setSeriesFilled(2, fill);
    m_graphUp->setSeriesFilled(2, fill);
    m_graphConn->setSeriesFilled(0, fill);

    // MFC IDS_AVG + " (%u mins)" (StatisticsDlg.cpp:549-556); the core averages over it.
    const QString average = tr("Average (%1 mins)").arg(thePrefs.statsAverageMinutes());
    m_graphDown->setSeriesInfo(1, average, theUiState.statsColor(3));
    m_graphUp->setSeriesInfo(1, average, theUiState.statsColor(6));

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

    // The connections line is drawn 1:n so it fits the scale of the others.
    m_graphConn->setSeriesInfo(0, tr("Active connections (1:%1)").arg(connectionsRatio()),
                               theUiState.statsColor(8), fill);
    // Points already drawn carry the old divisor (MFC SetTrendRatio rescales them):
    // start over and let the next poll replay the daemon's history.
    if (m_shownConnRatio != connectionsRatio()) {
        if (m_shownConnRatio != 0) {
            for (auto* graph : {m_graphDown, m_graphUp, m_graphConn})
                graph->reset();
            m_statsSeq = 0;
        }
        m_shownConnRatio = connectionsRatio();
    }

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
    m_tree->setItemDelegate(new SectionDelegate(m_tree));
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
    const QString average = tr("Average (%1 mins)").arg(thePrefs.statsAverageMinutes());
    m_graphDown->setSeriesInfo(1, average, theUiState.statsColor(3));
    m_graphDown->setSeriesInfo(2, tr("Current"), theUiState.statsColor(2));
    m_graphDown->setSeriesInfo(3, tr("Usenet"), theUiState.statsColor(15));
    m_graphDown->setYUnits(tr("Download Speed"));
    graphSplitter->addWidget(m_graphDown);

    m_graphUp = new StatsGraph(5, this);
    m_graphUp->setSeriesInfo(0, tr("Session average"), theUiState.statsColor(7));
    m_graphUp->setSeriesInfo(1, average, theUiState.statsColor(6));
    m_graphUp->setSeriesInfo(2, tr("Current"), theUiState.statsColor(5));
    m_graphUp->setSeriesInfo(3, tr("Current (excl. overhead)"), theUiState.statsColor(14));
    m_graphUp->setSeriesInfo(4, tr("Friend upload"), theUiState.statsColor(13));
    m_graphUp->setYUnits(tr("Upload Speed"));
    graphSplitter->addWidget(m_graphUp);

    m_graphConn = new StatsGraph(4, this);
    m_graphConn->setSeriesInfo(0, tr("Active connections (1:%1)").arg(connectionsRatio()),
                               theUiState.statsColor(8));
    m_graphConn->setSeriesInfo(1, tr("Active uploads"), theUiState.statsColor(10));
    m_graphConn->setSeriesInfo(2, tr("Total uploads"), theUiState.statsColor(9));
    m_graphConn->setSeriesInfo(3, tr("Active downloads"), theUiState.statsColor(12));
    m_graphConn->setYUnits(tr("Connections"));
    // MFC opens the Statistics options from a scope (StatisticsDlg.cpp:2849-2862).
    for (auto* graph : {m_graphDown, m_graphUp, m_graphConn})
        connect(graph, &StatsGraph::doubleClicked, this, &StatisticsPanel::graphOptionsRequested);
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
    // MFC up_S[2..4] (StatisticsDlg.cpp:1183-1189)
    m_itemUpActiveUploads = new QTreeWidgetItem(upSession);
    m_itemUpTotalUploads = new QTreeWidgetItem(upSession);
    m_itemUpWaitingUploads = new QTreeWidgetItem(upSession);

    auto* upSessions = new QTreeWidgetItem(upSession, {tr("Upload Sessions")});
    m_itemUpSessions = upSessions;
    m_itemUpSuccessful = new QTreeWidgetItem(upSessions);
    m_itemUpFailed = new QTreeWidgetItem(upSessions);
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
    m_itemUpCumSessions = upCumSessions;
    m_itemUpCumSuccessful = new QTreeWidgetItem(upCumSessions);
    m_itemUpCumFailed = new QTreeWidgetItem(upCumSessions);
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

    m_itemDownCompletedSes = new QTreeWidgetItem(downSession, {tr("Completed Downloads: 0")});
    m_itemDownActiveDownloads = new QTreeWidgetItem(downSession);
    m_itemDownFoundSources = new QTreeWidgetItem(downSession, {tr("Found Sources: 0")});
    // MFC hangs the per-source breakdown off "Found Sources" (down_sources[] under
    // down_S[3], StatisticsDlg.cpp:2643): by state, by origin, by network, re-asks, dead.
    for (auto*& item : m_itemDownSources)
        item = new QTreeWidgetItem(m_itemDownFoundSources);
    m_itemDownUdpReasks = new QTreeWidgetItem(m_itemDownFoundSources,
                                              {tr("UDP File Re-asks: 0, Failed: 0 (0.0%)")});
    m_itemDownDeadSources = new QTreeWidgetItem(m_itemDownFoundSources);

    auto* downSesSessions = new QTreeWidgetItem(downSession, {tr("Download Sessions")});
    m_itemDownSessions = downSesSessions;
    m_itemDownSesSuccessful = new QTreeWidgetItem(downSesSessions);
    m_itemDownSesFailed = new QTreeWidgetItem(downSesSessions);
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
    m_itemDownCumSessions = downCumSessions;
    m_itemDownCumSuccessful = new QTreeWidgetItem(downCumSessions);
    m_itemDownCumFailed = new QTreeWidgetItem(downCumSessions);
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
    connSesGen->setIcon(0, QIcon(QStringLiteral(":/icons/TransferUpDown.ico")));
    m_itemConnReconnects = new QTreeWidgetItem(connSesGen, {tr("Reconnects: %1").arg(0)});
    m_itemConnActive = new QTreeWidgetItem(connSesGen);
    m_itemConnAverage = new QTreeWidgetItem(connSesGen);
    m_itemConnPeak = new QTreeWidgetItem(connSesGen);
    m_itemConnMaxReached = new QTreeWidgetItem(connSesGen);

    auto* connSesUp = new QTreeWidgetItem(connSession, {tr("Uploads")});
    connSesUp->setIcon(0, QIcon(QStringLiteral(":/icons/Upload.ico")));
    m_itemConnSesUpSpeed = new QTreeWidgetItem(connSesUp, {tr("Upload Speed: 0 KB/s")});
    m_itemConnSesAvgUp = new QTreeWidgetItem(connSesUp);
    m_itemConnSesMaxUp = new QTreeWidgetItem(connSesUp, {tr("Max Upload Rate: 0 KB/s")});
    m_itemConnSesMaxAvgUp = new QTreeWidgetItem(connSesUp, {tr("Max Average Upload Rate: 0 KB/s")});

    auto* connSesDown = new QTreeWidgetItem(connSession, {tr("Downloads")});
    connSesDown->setIcon(0, QIcon(QStringLiteral(":/icons/Download.ico")));
    m_itemConnSesDownSpeed = new QTreeWidgetItem(connSesDown, {tr("Download Speed: 0 KB/s")});
    m_itemConnSesAvgDown = new QTreeWidgetItem(connSesDown);
    m_itemConnSesMaxDown = new QTreeWidgetItem(connSesDown, {tr("Max Download Rate: 0 KB/s")});
    m_itemConnSesMaxAvgDown = new QTreeWidgetItem(connSesDown, {tr("Max Average Download Rate: 0 KB/s")});

    // Connection > Cumulative
    auto* connCum = new QTreeWidgetItem(connection, {tr("Cumulative")});
    connCum->setIcon(0, cumulativeIcon);

    auto* connCumGen = new QTreeWidgetItem(connCum, {tr("General")});
    connCumGen->setIcon(0, QIcon(QStringLiteral(":/icons/TransferUpDown.ico")));
    m_itemConnCumReconnects = new QTreeWidgetItem(connCumGen, {tr("Reconnects: %1").arg(0)});
    m_itemConnCumAverage = new QTreeWidgetItem(connCumGen);
    m_itemConnCumPeak = new QTreeWidgetItem(connCumGen);
    m_itemConnCumMaxReached = new QTreeWidgetItem(connCumGen);

    auto* connCumUp = new QTreeWidgetItem(connCum, {tr("Uploads")});
    connCumUp->setIcon(0, QIcon(QStringLiteral(":/icons/Upload.ico")));
    m_itemConnCumAvgUp = new QTreeWidgetItem(connCumUp, {tr("Average Upload Rate: 0 KB/s")});
    m_itemConnCumMaxUp = new QTreeWidgetItem(connCumUp, {tr("Max Upload Rate: 0 KB/s")});
    m_itemConnCumMaxAvgUp = new QTreeWidgetItem(connCumUp, {tr("Max Average Upload Rate: 0 KB/s")});

    auto* connCumDown = new QTreeWidgetItem(connCum, {tr("Downloads")});
    connCumDown->setIcon(0, QIcon(QStringLiteral(":/icons/Download.ico")));
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

    buildProjectedAverages();

    // ===== Clients =====
    auto* clients = new QTreeWidgetItem(m_tree, {tr("Clients")});
    clients->setIcon(0, QIcon(QStringLiteral(":/icons/User.ico")));
    m_itemKnownClients = new QTreeWidgetItem(clients, {tr("Known Clients: 0")});
    m_itemClientSoftware = new QTreeWidgetItem(clients, {tr("Client Software")});
    // MFC order (StatisticsDlg.cpp:2732-2751): Software, Network, Port, [Firewalled —
    // in the Kademlia branch here], Low ID, Secure Ident, Problematic, Banned, Filtered.
    auto* cliNetwork = new QTreeWidgetItem(clients, {tr("Network")});
    for (auto*& item : m_itemCliNetwork)
        item = new QTreeWidgetItem(cliNetwork);
    auto* cliPort = new QTreeWidgetItem(clients, {tr("Port")});
    for (auto*& item : m_itemCliPort)
        item = new QTreeWidgetItem(cliPort);
    m_itemLowIDClients = new QTreeWidgetItem(clients, {tr("Low ID: 0 (0.0%)")});
    m_itemSecureIdent = new QTreeWidgetItem(clients);
    m_itemProblematic = new QTreeWidgetItem(clients);
    m_itemBannedClients = new QTreeWidgetItem(clients, {tr("Banned Clients: 0")});
    m_itemFilteredClients = new QTreeWidgetItem(clients, {tr("Filtered Clients: 0")});
    {
        const QString estimate =
            tr("Different clients, counted by user hash. An estimate, accurate to about 2%.");
        const QIcon icons[] = {detailIcon, cumulativeIcon};
        const QString labels[] = {tr("Session"), tr("Cumulative")};
        for (int i = 0; i < 2; ++i) {
            ClientSeenItems& items = m_clientSeen[i];
            items.scope = new QTreeWidgetItem(clients, {labels[i]});
            items.scope->setIcon(0, icons[i]);
            items.seen = new QTreeWidgetItem(items.scope);
            items.seen->setToolTip(0, tr("Clients that said hello on a connection.")
                                          + QLatin1Char(' ') + estimate);
            items.identified = new QTreeWidgetItem(items.scope);
            items.identified->setToolTip(0, tr("Clients that proved their user hash with "
                                               "Secure Identification."));
            items.countries = new QTreeWidgetItem(items.scope, {tr("By Country")});
            items.countries->setToolTip(0, tr("Clients by the country of the address they "
                                              "connected from."));
        }
        applyClientStats({});
    }

    // ===== Servers =====
    auto* servers = new QTreeWidgetItem(m_tree, {tr("Servers")});
    servers->setIcon(0, QIcon(QStringLiteral(":/icons/Server.ico")));
    m_itemSrvWorking = new QTreeWidgetItem(servers, {tr("Working Servers: 0")});
    m_itemSrvWorkUsers = new QTreeWidgetItem(m_itemSrvWorking);
    m_itemSrvWorkFiles = new QTreeWidgetItem(m_itemSrvWorking);
    m_itemSrvOccupation = new QTreeWidgetItem(m_itemSrvWorking);
    m_itemSrvFailed = new QTreeWidgetItem(servers, {tr("Failed Servers: 0")});
    m_itemSrvDeleted = new QTreeWidgetItem(servers);
    m_itemSrvTotal = new QTreeWidgetItem(servers, {tr("Total: 0")});
    m_itemSrvUsers = new QTreeWidgetItem(servers, {tr("Total Users: 0")});
    m_itemSrvFiles = new QTreeWidgetItem(servers, {tr("Total Files: 0")});

    auto* srvRecords = new QTreeWidgetItem(servers, {tr("Records")});
    srvRecords->setIcon(0, QIcon(QStringLiteral(":/icons/Records.ico")));
    m_itemSrvRecWorking = new QTreeWidgetItem(srvRecords, {tr("Most Working Servers: 0")});
    m_itemSrvRecUsers = new QTreeWidgetItem(srvRecords, {tr("Most Users Online: 0")});
    m_itemSrvRecFiles = new QTreeWidgetItem(srvRecords, {tr("Most Files Available: 0")});

    // ===== Shared Files =====
    auto* shared = new QTreeWidgetItem(m_tree, {tr("Shared Files")});
    shared->setIcon(0, QIcon(QStringLiteral(":/icons/SharedFiles.ico")));
    m_itemSharedCount = new QTreeWidgetItem(shared, {tr("Number of Shared Files: %1").arg(0)});
    m_itemSharedAvgSize = new QTreeWidgetItem(shared);
    m_itemSharedLargest = new QTreeWidgetItem(shared);
    m_itemSharedSize = new QTreeWidgetItem(shared);

    auto* sharedRecords = new QTreeWidgetItem(shared, {tr("Records")});
    sharedRecords->setIcon(0, QIcon(QStringLiteral(":/icons/Records.ico")));
    m_itemSharedRecCount = new QTreeWidgetItem(sharedRecords);
    m_itemSharedRecAvg = new QTreeWidgetItem(sharedRecords);
    m_itemSharedRecLargest = new QTreeWidgetItem(sharedRecords);
    m_itemSharedRecSize = new QTreeWidgetItem(sharedRecords);

    // ===== Disk Space ===== (MFC IDS_DWTOT, StatisticsDlg.cpp:2412-2437)
    auto* totalDown = new QTreeWidgetItem(m_tree, {tr("Disk Space")});
    totalDown->setIcon(0, QIcon(QStringLiteral(":/icons/HardDisk.ico")));   // as MFC
    m_itemTotalDownCount = new QTreeWidgetItem(totalDown);
    m_itemTotalDownSize = new QTreeWidgetItem(totalDown);
    m_itemTotalDownDone = new QTreeWidgetItem(totalDown);
    m_itemTotalDownLeft = new QTreeWidgetItem(totalDown);
    m_itemTotalDownFreeSpace = new QTreeWidgetItem(totalDown);
    m_itemTotalDownNeeded = new QTreeWidgetItem(totalDown);

    // ===== Kademlia ===== (not in MFC, which shows Kad only as overhead lines)
    buildKademliaBranch(detailIcon, cumulativeIcon);

    // ===== Usenet ===== (not in MFC; last, so MFC's own order stays intact)
    buildUsenetBranch(detailIcon, cumulativeIcon);

    // Restore expansion state from persistent settings (defaults: Transfer, Connection, Time expanded)
    // Rows created empty get their wording from the one place that formats them.
    updateTree({});

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

    IpcMessage kadReq(IpcMsgType::GetKadStats);
    m_ipc->sendRequest(std::move(kadReq), [this](const IpcMessage& resp) {
        if (!resp.isValid())
            return;
        if (resp.type() != IpcMsgType::Result || !resp.fieldBool(0)) {
            m_itemKad->setHidden(true);   // a daemon from before the branch
            return;
        }
        applyKadStats(resp.fieldMap(1));
    });

    IpcMessage clientReq(IpcMsgType::GetClientStats);
    m_ipc->sendRequest(std::move(clientReq), [this](const IpcMessage& resp) {
        if (!resp.isValid())
            return;
        if (resp.type() != IpcMsgType::Result || !resp.fieldBool(0)) {
            for (const ClientSeenItems& items : m_clientSeen)
                items.scope->setHidden(true);   // a daemon from before the census
            return;
        }
        applyClientStats(resp.fieldMap(1));
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
        m_graphConn->appendPoints({static_cast<double>(s.at(10).toInteger()) / connectionsRatio(),
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

double StatisticsPanel::projected(qint64 value, qint64 period, qint64 sinceReset)
{
    return sinceReset > 0 ? static_cast<double>(value) * static_cast<double>(period)
                                / static_cast<double>(sinceReset)
                          : 0.0;
}

void StatisticsPanel::buildProjectedAverages()
{
    auto* root = new QTreeWidgetItem(m_itemTimeHeader, {tr("Projected Averages")});
    root->setIcon(0, QIcon(QStringLiteral(":/icons/StatsProjected.ico")));

    const std::array<QString, 3> periodNames = {tr("Daily"), tr("Monthly"), tr("Yearly")};
    const std::array<const char*, 3> periodIcons = {"StatsDay.ico", "StatsMonth.ico", "StatsYear.ico"};
    using Row = ProjectedRow;

    for (std::size_t p = 0; p < m_projectedRows.size(); ++p) {
        auto& rows = m_projectedRows[p];
        auto* period = new QTreeWidgetItem(root, {periodNames[p]});
        period->setIcon(0, QIcon(QStringLiteral(":/icons/") + QLatin1StringView(periodIcons[p])));

        const auto add = [&rows](QTreeWidgetItem* parent, Row::Kind kind, const QString& label,
                                 const QStringList& keys, const QString& second = {}) {
            auto* item = new QTreeWidgetItem(parent, {label});
            rows.push_back({item, kind, label, keys, second});
            return item;
        };
        const auto group = [](QTreeWidgetItem* parent, const QString& label) {
            return new QTreeWidgetItem(parent, {label});
        };
        const auto addOverhead = [&](QTreeWidgetItem* parent, const QString& prefix) {
            const struct { QString label; const char* key; } parts[] = {
                {tr("Total Overhead (Packets)"), "Total"},
                {tr("File Request Overhead (Packets)"), "FileReq"},
                {tr("Source Exchange Overhead (Packets)"), "SrcExch"},
                {tr("Server Overhead (Packets)"), "Server"},
                {tr("Kad Overhead (Packets)"), "Kad"}};
            QTreeWidgetItem* total = nullptr;
            for (const auto& part : parts) {
                const QString key = prefix + QLatin1StringView(part.key);
                auto* item = add(total ? total : parent, Row::Overhead, part.label, {key},
                                 key + QStringLiteral("Pkt"));
                if (!total)
                    total = item;
            }
        };
        const auto addClients = [&](QTreeWidgetItem* data, const QString& prefix,
                                    std::span<const char* const> labels, const QString& total) {
            static const char* const keys[] = {"Emule", "EDHybrid", "EDonkey", "AMule",
                                               "MLdonkey", "Shareaza", "EMCompat", "URL"};
            auto* clients = group(data, tr("Clients"));
            for (std::size_t i = 0; i < labels.size(); ++i)
                add(clients, Row::BytesShare, QString::fromLatin1(labels[i]),
                    {prefix + QLatin1StringView(keys[i])}, total);
            auto* ports = group(data, tr("Port"));
            add(ports, Row::BytesShare, tr("Default Port 4662"), {prefix + QStringLiteral("Port4662")}, total);
            add(ports, Row::BytesShare, tr("Other Ports"), {prefix + QStringLiteral("PortOther")}, total);
        };

        // Uploads
        auto* up = new QTreeWidgetItem(period, {tr("Uploads")});
        up->setIcon(0, QIcon(QStringLiteral(":/icons/Upload.ico")));
        const QString totalUp = QStringLiteral("cumTotalUp");
        auto* upData = add(up, Row::Bytes, tr("Uploaded Data"), {totalUp});
        addClients(upData, QStringLiteral("cumUp"), kUpClientLabels, totalUp);
        auto* upSource = group(upData, tr("Data Source"));
        add(upSource, Row::BytesShare, tr("Complete File"), {QStringLiteral("cumUpFromFile")}, totalUp);
        add(upSource, Row::BytesShare, tr("Part File"), {QStringLiteral("cumUpFromPartfile")}, totalUp);
        auto* upSessions = add(up, Row::Count, tr("Upload Sessions"),
                               {QStringLiteral("cumUpSuccessful"), QStringLiteral("cumUpFailed")});
        add(upSessions, Row::Count, tr("Successful"), {QStringLiteral("cumUpSuccessful")});
        add(upSessions, Row::Count, tr("Failed"), {QStringLiteral("cumUpFailed")});
        addOverhead(up, QStringLiteral("cumUpOh"));

        // Downloads
        auto* down = new QTreeWidgetItem(period, {tr("Downloads")});
        down->setIcon(0, QIcon(QStringLiteral(":/icons/Download.ico")));
        const QString totalDown = QStringLiteral("cumTotalDown");
        auto* downData = add(down, Row::Bytes, tr("Downloaded Data"), {totalDown});
        addClients(downData, QStringLiteral("cumDown"), kDownClientLabels, totalDown);
        add(down, Row::Count, tr("Completed Downloads"), {QStringLiteral("cumDownCompletedFiles")});
        auto* downSessions = add(down, Row::Count, tr("Download Sessions"),
                                 {QStringLiteral("cumDownSuccessful"), QStringLiteral("cumDownFailed")});
        add(downSessions, Row::Count, tr("Successful"), {QStringLiteral("cumDownSuccessful")});
        add(downSessions, Row::Count, tr("Failed"), {QStringLiteral("cumDownFailed")});
        add(down, Row::Bytes, tr("Gained Due To Compression"), {QStringLiteral("cumCompressionGain")});
        add(down, Row::Bytes, tr("Lost Due To Corruption"), {QStringLiteral("cumCorruptionLoss")});
        add(down, Row::Count, tr("Parts Saved Due To ICH"), {QStringLiteral("cumIchPartsSaved")});
        addOverhead(down, QStringLiteral("cumDownOh"));
    }
    updateProjectedAverages({});
}

void StatisticsPanel::updateProjectedAverages(const QCborMap& stats)
{
    const qint64 sinceReset = stats.value(QLatin1StringView("timeSinceReset")).toInteger();
    const auto sum = [&stats](const QStringList& keys) {
        qint64 total = 0;
        for (const QString& key : keys)
            total += stats.value(key).toInteger();
        return total;
    };
    for (std::size_t p = 0; p < m_projectedRows.size(); ++p) {
        const qint64 period = kProjectionPeriods[p];
        const auto scale = [&](qint64 value) {
            return static_cast<qint64>(projected(value, period, sinceReset));
        };
        for (const ProjectedRow& row : m_projectedRows[p]) {
            const qint64 value = sum(row.keys);
            QString text;
            switch (row.kind) {
            case ProjectedRow::Bytes:
                text = formatByteSize(scale(value));
                break;
            case ProjectedRow::BytesShare:
                // the share is the cumulative one: scaling changes neither side
                text = QStringLiteral("%1 %2").arg(formatByteSize(scale(value)),
                                                   formatPercent(value, stats.value(row.second).toInteger()));
                break;
            case ProjectedRow::Count:
                text = QString::number(scale(value));
                break;
            case ProjectedRow::Overhead:
                text = QStringLiteral("%1 (%2)").arg(formatByteSize(scale(value)))
                           .arg(scale(stats.value(row.second).toInteger()));
                break;
            }
            row.item->setText(0, QStringLiteral("%1: %2").arg(row.label, text));
        }
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
    // upQueueLength is the slot count, upWaiting the waiting list (UploadQueue.h).
    const qint64 upSlots = cborInt(stats, QLatin1StringView("upQueueLength"));
    m_itemUpActiveUploads->setText(0, tr("Active Uploads/Needed to fill Bandwidth: %1")
        .arg(cborInt(stats, QLatin1StringView("upActive"))));
    m_itemUpTotalUploads->setText(0, tr("Total Uploads: %1").arg(upSlots));
    m_itemUpWaitingUploads->setText(0,
        tr("Waiting Uploads: %1").arg(cborInt(stats, QLatin1StringView("upWaiting"))));

    // MFC StatisticsDlg.cpp:1192-1215, 840-862: the parent carries the total, both
    // rows a share, and a session still running counts as a good one.
    const auto sessionRows = [](QTreeWidgetItem* parent, QTreeWidgetItem* okItem,
                                QTreeWidgetItem* failItem, const QString& title,
                                const QString& okText, const QString& failText,
                                qint64 good, qint64 bad, int decimals) {
        const qint64 total = good + bad;
        const double okPct = good > 0 ? 100.0 * static_cast<double>(good) / static_cast<double>(total) : 0.0;
        const double failPct = bad > 0 ? 100.0 - okPct : 0.0;
        parent->setText(0, QStringLiteral("%1: %2").arg(title).arg(total));
        okItem->setText(0, QStringLiteral("%1: %2 (%3%)").arg(okText).arg(good).arg(okPct, 0, 'f', decimals));
        failItem->setText(0, QStringLiteral("%1: %2 (%3%)").arg(failText).arg(bad).arg(failPct, 0, 'f', decimals));
    };
    const QString upOkText = tr("Total successful upload sessions");
    const QString upFailText = tr("Total failed upload sessions");
    const QString downOkText = tr("Successful Download Sessions");
    const QString downFailText = tr("Failed Download Sessions");

    const qint64 upSucc = cborInt(stats, QLatin1StringView("upSuccessful")) + upSlots;
    const qint64 upFail = cborInt(stats, QLatin1StringView("upFailed"));
    sessionRows(m_itemUpSessions, m_itemUpSuccessful, m_itemUpFailed, tr("Upload Sessions"),
                upOkText, upFailText, upSucc, upFail, 2);
    m_itemUpAvgPerSession->setText(0, tr("Average Upload Per Session: %1")
        .arg(upSucc > 0 ? formatByteSize(sent / upSucc) : tr("Waiting...")));
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

    const qint64 cumUpSucc = cborInt(stats, QLatin1StringView("cumUpSuccessful")) + upSlots;
    const qint64 cumUpFail = cborInt(stats, QLatin1StringView("cumUpFailed"));
    sessionRows(m_itemUpCumSessions, m_itemUpCumSuccessful, m_itemUpCumFailed, tr("Upload Sessions"),
                upOkText, upFailText, cumUpSucc, cumUpFail, 2);
    m_itemUpCumAvgPerSession->setText(0, tr("Average Upload Per Session: %1")
        .arg(cumUpSucc > 0 ? formatByteSize(cumTotalUp / cumUpSucc) : tr("Waiting...")));
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

    // Transferring sources, not files (MFC myStats.a[1], StatisticsDlg.cpp:761).
    m_itemDownActiveDownloads->setText(0, tr("Active Downloads (chunks): %1")
        .arg(cborInt(stats, QLatin1StringView("downTransferring"))));
    m_itemDownFoundSources->setText(0,
        tr("Found Sources: %1").arg(cborInt(stats, QLatin1StringView("downFoundSources"))));
    {
        // Row order of StatsSnapshot::downSources (MFC StatisticsDlg.cpp:768-826)
        const std::array<QString, 17> labels = {
            tr("On Queue"), tr("Queue Full"), tr("No needed parts"), tr("Asking"),
            tr("Receiving hashset"), tr("Connecting"), tr("Connecting via server"),
            tr("Too many connections"), tr("Cannot connect LowID to LowID"),
            tr("Problematic"), tr("Banned"), tr("Asked for another file"), tr("Unknown"),
            tr("via eD2K Server"), tr("via Kad"), tr("via Source Exchange"), tr("via Passive")};
        const std::array<QString, 3> networks = {
            QStringLiteral("eD2K"), QStringLiteral("Kad"), QStringLiteral("eD2K/Kad")};
        const QCborArray sources = stats.value(QLatin1StringView("downSources")).toArray();
        const qint64 found = cborInt(stats, QLatin1StringView("downFoundSources"));
        for (std::size_t i = 0; i < m_itemDownSources.size(); ++i) {
            const qint64 n = sources.at(static_cast<qsizetype>(i)).toInteger();
            m_itemDownSources[i]->setText(0, i < labels.size()
                ? QStringLiteral("%1: %2").arg(labels[i]).arg(n)
                : QStringLiteral("%1: %2 %3").arg(networks[i - labels.size()]).arg(n)
                      .arg(formatPercent(n, found)));
        }
        const qint64 deadGlobal = cborInt(stats, QLatin1StringView("downDeadSourcesGlobal"));
        const qint64 deadFiles = cborInt(stats, QLatin1StringView("downDeadSourcesPerFile"));
        m_itemDownDeadSources->setText(0, tr("Dead Sources: %1 (%2 + %3)")
            .arg(formatShortNumber(deadGlobal + deadFiles), formatShortNumber(deadGlobal),
                 formatShortNumber(deadFiles)));
    }
    {
        const qint64 reasks = cborInt(stats, QLatin1StringView("downUdpReasks"));
        const qint64 failed = cborInt(stats, QLatin1StringView("downUdpReasksFailed"));
        m_itemDownUdpReasks->setText(0,
            tr("UDP File Re-asks: %1, Failed: %2 %3")
                .arg(formatShortNumber(reasks), formatShortNumber(failed),
                     formatPercent(failed, reasks)));
    }
    m_itemDownCompletedSes->setText(0,
        tr("Completed Downloads: %1").arg(cborInt(stats, QLatin1StringView("completedDownloads"))));

    // A source delivering right now counts as a good session already
    // (srchybrid/StatisticsDlg.cpp:840).
    const qint64 downRunning = cborInt(stats, QLatin1StringView("downTransferring"));
    {
        const qint64 good = cborInt(stats, QLatin1StringView("downSuccessful")) + downRunning;
        const qint64 bad = cborInt(stats, QLatin1StringView("downFailed"));
        sessionRows(m_itemDownSessions, m_itemDownSesSuccessful, m_itemDownSesFailed,
                    tr("Download Sessions"), downOkText, downFailText, good, bad, 1);
        m_itemDownSesAvgPerSession->setText(0,
            tr("Average Download Per Session: %1").arg(formatByteSize(good > 0 ? recv / good : 0)));
        m_itemDownSesAvgTime->setText(0,
            tr("Average Download Time: %1").arg(formatDuration(cborInt(stats, QLatin1StringView("downAvgTime")))));
    }
    {
        const qint64 good = cborInt(stats, QLatin1StringView("cumDownSuccessful")) + downRunning;
        const qint64 bad = cborInt(stats, QLatin1StringView("cumDownFailed"));
        sessionRows(m_itemDownCumSessions, m_itemDownCumSuccessful, m_itemDownCumFailed,
                    tr("Download Sessions"), downOkText, downFailText, good, bad, 1);
        m_itemDownCumAvgPerSession->setText(0,
            tr("Average Download Per Session: %1").arg(formatByteSize(good > 0 ? cumTotalDown / good : 0)));
        m_itemDownCumAvgTime->setText(0,
            tr("Average Download Time: %1").arg(formatDuration(cborInt(stats, QLatin1StringView("cumDownAvgTime")))));
    }

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
    // MFC StatisticsDlg.cpp:1413-1439. "reconnects" already leaves out the first login.
    {
        const qint64 active = cborInt(stats, QLatin1StringView("connActive"));
        const qint64 half = cborInt(stats, QLatin1StringView("connHalfOpen"));
        const qint64 complete = cborInt(stats, QLatin1StringView("connComplete"));
        m_itemConnReconnects->setText(0,
            tr("Reconnects: %1").arg(cborInt(stats, QLatin1StringView("reconnects"))));
        m_itemConnActive->setText(0,
            tr("Active Connections (estimate): %1 (Half:%2 | Compl:%3 | Other:%4)")
                .arg(active).arg(half).arg(complete).arg(active - half - complete));
        m_itemConnAverage->setText(0, tr("Average Connections (estimate): %1")
            .arg(static_cast<qint64>(cborDouble(stats, QLatin1StringView("connAverage")))));
        m_itemConnPeak->setText(0, tr("Peak Connections (estimate): %1")
            .arg(cborInt(stats, QLatin1StringView("connPeak"))));

        // Stamped when the count moves, as MFC does: the tree shows when it last happened.
        const qint64 reached = cborInt(stats, QLatin1StringView("connMaxReached"));
        if (reached != m_lastMaxConnReached) {
            m_itemConnMaxReached->setText(0, tr("Max Connection Limit Reached: %1 : %2").arg(reached)
                .arg(QLocale().toString(QDateTime::currentDateTime(), QLocale::ShortFormat)));
            m_lastMaxConnReached = reached;
        } else if (reached == 0) {
            m_itemConnMaxReached->setText(0, tr("Max Connection Limit Reached: %1").arg(reached));
        }
    }
    m_itemConnSesAvgUp->setText(0, tr("Average Uploadrate: %1")
        .arg(formatRate(cborDouble(stats, QLatin1StringView("avgUpSession")))));
    m_itemConnSesAvgDown->setText(0, tr("Average Downloadrate: %1")
        .arg(formatRate(cborDouble(stats, QLatin1StringView("avgDownSession")))));

    m_itemConnSesUpSpeed->setText(0, tr("Upload Speed: %1").arg(formatRate(cborDouble(stats, QLatin1StringView("rateUp")))));
    m_itemConnSesMaxUp->setText(0, tr("Max Upload Rate: %1").arg(formatRate(cborDouble(stats, QLatin1StringView("maxUp")))));
    m_itemConnSesMaxAvgUp->setText(0, tr("Max Average Upload Rate: %1").arg(formatRate(cborDouble(stats, QLatin1StringView("maxUpAvg")))));
    m_itemConnSesDownSpeed->setText(0, tr("Download Speed: %1").arg(formatRate(cborDouble(stats, QLatin1StringView("rateDown")))));
    m_itemConnSesMaxDown->setText(0, tr("Max Download Rate: %1").arg(formatRate(cborDouble(stats, QLatin1StringView("maxDown")))));
    m_itemConnSesMaxAvgDown->setText(0, tr("Max Average Download Rate: %1").arg(formatRate(cborDouble(stats, QLatin1StringView("maxDownAvg")))));

    // === Connection — Cumulative ===
    m_itemConnCumReconnects->setText(0,
        tr("Reconnects: %1").arg(cborInt(stats, QLatin1StringView("cumConnReconnects"))));
    m_itemConnCumAverage->setText(0, tr("Average Connections (estimate): %1")
        .arg(cborInt(stats, QLatin1StringView("cumConnAverage"))));
    m_itemConnCumPeak->setText(0, tr("Peak Connections (estimate): %1")
        .arg(cborInt(stats, QLatin1StringView("cumConnPeak"))));
    m_itemConnCumMaxReached->setText(0, tr("Max Connection Limit Reached: %1")
        .arg(cborInt(stats, QLatin1StringView("cumConnMaxLimitReached"))));

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
        updateProjectedAverages(stats);
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
    {
        // MFC StatisticsDlg.cpp:1985-1986, 2266-2283, 2308; counted in ClientList.cpp:112-144
        const QCborMap census = stats.value(QLatin1StringView("clientCensus")).toMap();
        const auto n = [&census](QLatin1StringView key) { return cborInt(census, key); };
        const auto share = [](qint64 part, qint64 whole) {
            return QString::number(whole > 0 ? 100.0 * static_cast<double>(part) / static_cast<double>(whole) : 0.0,
                                   'f', 1);
        };
        const std::array<std::pair<QString, qint64>, 4> nets = {{
            {QStringLiteral("eD2K"), n(QLatin1StringView("netEd2k"))},
            {QStringLiteral("Kad"), n(QLatin1StringView("netKad"))},
            {QStringLiteral("eD2K/Kad"), n(QLatin1StringView("netBoth"))},
            {tr("Unknown"), n(QLatin1StringView("netUnknown"))}}};
        for (std::size_t i = 0; i < nets.size(); ++i)
            m_itemCliNetwork[i]->setText(0, QStringLiteral("%1: %2 (%3%)")
                .arg(nets[i].first).arg(nets[i].second).arg(share(nets[i].second, knownClients)));

        const qint64 portDef = n(QLatin1StringView("portDefault"));
        const qint64 portOther = n(QLatin1StringView("portOther"));
        m_itemCliPort[0]->setText(0, QStringLiteral("%1: %2 (%3%)")
            .arg(tr("Default")).arg(portDef).arg(share(portDef, portDef + portOther)));
        m_itemCliPort[1]->setText(0, QStringLiteral("%1: %2 (%3%)")
            .arg(tr("Other")).arg(portOther).arg(share(portOther, portDef + portOther)));

        const qint64 ok = n(QLatin1StringView("identOk"));
        const qint64 failed = n(QLatin1StringView("identFailed"));
        m_itemSecureIdent->setText(0, QStringLiteral("%1: %2 (%3%) : %4 (%5%)")
            .arg(tr("Secure Ident (OK : Failed )")).arg(ok).arg(share(ok, ok + failed))
            .arg(failed).arg(share(failed, ok + failed)));
        const qint64 problematic = n(QLatin1StringView("problematic"));
        m_itemProblematic->setText(0, QStringLiteral("%1: %2 (%3%)")
            .arg(tr("Problematic")).arg(problematic).arg(share(problematic, knownClients)));
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

        // MFC's eight rows, in its order and shown even at zero (StatisticsDlg.cpp:1995-2011).
        // Anything else the core names (URL sources) follows when it has clients.
        // Kept beyond MFC: versions under every software, and the mod level.
        static const QStringList kFixed = {
            QStringLiteral("eMule"), QStringLiteral("eD Hybrid"), QStringLiteral("eDonkey"),
            QStringLiteral("aMule"), QStringLiteral("MLdonkey"), QStringLiteral("Shareaza"),
            QStringLiteral("eM Compat"), QStringLiteral("Unknown")};
        QList<QCborMap> rows;
        for (const QString& name : kFixed)
            rows.append(QCborMap{{QStringLiteral("n"), name}, {QStringLiteral("c"), 0}});
        for (const auto& softVal : stats.value(QStringLiteral("clientSoftwareStats")).toArray()) {
            const QCborMap softMap = softVal.toMap();
            const QString name = softMap.value(QStringLiteral("n")).toString();
            if (const auto fixed = kFixed.indexOf(name); fixed >= 0)
                rows[fixed] = softMap;
            else if (softMap.value(QStringLiteral("c")).toInteger() > 0)
                rows.append(softMap);
        }
        {
            for (const QCborMap& softMap : std::as_const(rows)) {
                const QString name = softMap.value(QStringLiteral("n")).toString();
                const int count = static_cast<int>(softMap.value(QStringLiteral("c")).toInteger());
                const QString shown = name == QLatin1StringView("Unknown") ? tr("Unknown") : name;

                const double pct = knownClients > 0 ? 100.0 * count / static_cast<double>(knownClients) : 0.0;
                auto* softItem = new QTreeWidgetItem(m_itemClientSoftware,
                    {QStringLiteral("%1: %2 (%3%)").arg(shown).arg(count).arg(pct, 0, 'f', 1)});
                softItem->setData(0, Qt::UserRole, name);

                // Restore expansion
                if (expandedSoftware.contains(name))
                    softItem->setExpanded(true);

                // Versions
                auto verIt = softMap.find(QStringLiteral("v"));
                if (verIt == softMap.end() || !verIt->isArray()) continue;
                const QCborArray verArr = verIt->toArray();

                // The four most used versions directly, the rest under "Minor"
                // (MFC MAX_SUB_CLIENT_VERSIONS / 2). The array arrives sorted by count.
                constexpr int kTopVersions = 4;
                QTreeWidgetItem* minor = nullptr;
                int minorCount = 0;
                int shownVersions = 0;
                for (const auto& verVal : verArr) {
                    if (!verVal.isMap()) continue;
                    const QCborMap verMap = verVal.toMap();
                    const QString label = verMap.value(QStringLiteral("l")).toString();
                    const int verCount = static_cast<int>(verMap.value(QStringLiteral("c")).toInteger());
                    if (verCount == 0) continue;

                    QTreeWidgetItem* verParent = softItem;
                    if (shownVersions++ >= kTopVersions) {
                        if (!minor) {
                            minor = new QTreeWidgetItem(softItem);
                            const QString minorKey = name + QStringLiteral("/#minor");
                            minor->setData(0, Qt::UserRole, minorKey);
                            if (expandedVersions.contains(minorKey))
                                minor->setExpanded(true);
                        }
                        minorCount += verCount;
                        minor->setText(0, QStringLiteral("%1: %2 (%3%)").arg(tr("Minor")).arg(minorCount)
                            .arg(100.0 * minorCount / static_cast<double>(count), 0, 'f', 1));
                        verParent = minor;
                    }

                    const double verPct = 100.0 * verCount / static_cast<double>(count);
                    auto* verItem = new QTreeWidgetItem(verParent,
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
    // MFC StatisticsDlg.cpp:2320-2354; every count through CastItoIShort.
    {
        const auto shortN = [&stats](QLatin1StringView key) {
            return formatShortNumber(cborInt(stats, key));
        };
        const qint64 workUsers = cborInt(stats, QLatin1StringView("srvUsers"));
        const qint64 lowID = cborInt(stats, QLatin1StringView("srvLowIDUsers"));
        m_itemSrvWorking->setText(0, tr("Working Servers: %1").arg(shortN(QLatin1StringView("srvWorking"))));
        m_itemSrvWorkUsers->setText(0, tr("Users on Working Servers: %1; Low ID: %2 (%3%)")
            .arg(formatShortNumber(workUsers), formatShortNumber(lowID))
            .arg(workUsers > 0 ? 100.0 * static_cast<double>(lowID) / static_cast<double>(workUsers) : 0.0,
                 0, 'f', 1));
        m_itemSrvWorkFiles->setText(0,
            tr("Files on Working Servers: %1").arg(shortN(QLatin1StringView("srvFiles"))));
        m_itemSrvOccupation->setText(0, tr("Server Occupation: %1%")
            .arg(cborDouble(stats, QLatin1StringView("srvOccupation")), 0, 'f', 2));
        m_itemSrvFailed->setText(0, tr("Failed Servers: %1").arg(shortN(QLatin1StringView("srvFailed"))));
        m_itemSrvDeleted->setText(0, tr("Deleted Servers: %1").arg(shortN(QLatin1StringView("srvDeleted"))));
        m_itemSrvTotal->setText(0, tr("Total: %1").arg(shortN(QLatin1StringView("srvTotal"))));
        m_itemSrvUsers->setText(0, tr("Total Users: %1").arg(shortN(QLatin1StringView("srvTotalUsers"))));
        m_itemSrvFiles->setText(0, tr("Total Files: %1").arg(shortN(QLatin1StringView("srvTotalFiles"))));

        m_itemSrvRecWorking->setText(0,
            tr("Most Working Servers: %1").arg(shortN(QLatin1StringView("recMaxWorkingServers"))));
        m_itemSrvRecUsers->setText(0,
            tr("Most Users Online: %1").arg(shortN(QLatin1StringView("recMaxUsersOnline"))));
        m_itemSrvRecFiles->setText(0,
            tr("Most Files Available: %1").arg(shortN(QLatin1StringView("recMaxFilesAvail"))));
    }

    // === Shared Files ===
    const qint64 sharedCount = cborInt(stats, QLatin1StringView("sharedCount"));
    const qint64 sharedSize = cborInt(stats, QLatin1StringView("sharedSize"));
    {
        // MFC StatisticsDlg.cpp:2373-2408 (IDS_HASHINGFILESCOUNT while the hasher is busy)
        QString countText = tr("Number of Shared Files: %1").arg(sharedCount);
        if (const qint64 hashing = cborInt(stats, QLatin1StringView("sharedHashing")); hashing > 0)
            countText += tr(" (%1 hashing)").arg(hashing);
        m_itemSharedCount->setText(0, countText);
    }
    m_itemSharedAvgSize->setText(0,
        tr("Average file size: %1").arg(formatByteSize(sharedCount > 0 ? sharedSize / sharedCount : 0)));
    m_itemSharedLargest->setText(0,
        tr("Largest Shared File: %1").arg(formatByteSize(cborInt(stats, QLatin1StringView("sharedLargest")))));
    m_itemSharedSize->setText(0,
        tr("Total size of Shared Files: %1").arg(formatByteSize(sharedSize)));

    m_itemSharedRecCount->setText(0,
        tr("Max. Files Ever Shared: %1").arg(cborInt(stats, QLatin1StringView("recMaxSharedFiles"))));
    m_itemSharedRecAvg->setText(0,
        tr("Largest Average File Size: %1").arg(formatByteSize(cborInt(stats, QLatin1StringView("recMaxAvgFileSize")))));
    m_itemSharedRecLargest->setText(0,
        tr("Largest Shared File: %1").arg(formatByteSize(cborInt(stats, QLatin1StringView("recMaxLargestFile")))));
    m_itemSharedRecSize->setText(0,
        tr("Largest Share Size: %1").arg(formatByteSize(cborInt(stats, QLatin1StringView("recMaxSharedSize")))));

    // === Disk Space === (MFC StatisticsDlg.cpp:2412-2437). Counts every download in
    // the list, not only the running ones: deliberate, 2026-10.
    {
        const qint64 size = cborInt(stats, QLatin1StringView("totalDownSize"));
        const qint64 done = cborInt(stats, QLatin1StringView("totalDownDone"));
        const qint64 needed = cborInt(stats, QLatin1StringView("totalDownNeeded"));
        const qint64 freeSpace = cborInt(stats, QLatin1StringView("freeTempSpace"));
        m_itemTotalDownCount->setText(0,
            tr("Number of Downloads: %1").arg(cborInt(stats, QLatin1StringView("totalDownCount"))));
        m_itemTotalDownSize->setText(0, tr("Total Size of Downloads: %1").arg(formatByteSize(size)));
        m_itemTotalDownDone->setText(0, tr("Total Completed Size: %1 (%2%)").arg(formatByteSize(done))
            .arg(size > 0 ? 100.0 * static_cast<double>(done) / static_cast<double>(size) : 0.0, 0, 'f', 0));
        m_itemTotalDownLeft->setText(0, tr("Total Size Left to Transfer: %1")
            .arg(formatByteSize(cborInt(stats, QLatin1StringView("totalDownLeft")))));
        QString freeText = tr("Free Space on Tempdrive: %1").arg(formatByteSize(freeSpace));
        if (needed > freeSpace)
            freeText += tr(" (you need to free %1!)").arg(formatByteSize(needed - freeSpace));
        m_itemTotalDownFreeSpace->setText(0, freeText);
        m_itemTotalDownNeeded->setText(0,
            tr("Additional Space Needed for Downloads: %1").arg(formatByteSize(needed)));
    }
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

void StatisticsPanel::applyClientStats(const QCborMap& data)
{
    const QCborMap seen = data.value(QStringLiteral("seen")).toMap();
    const QLatin1StringView scopes[] = {QLatin1StringView("session"),
                                        QLatin1StringView("cumulative")};
    for (int i = 0; i < 2; ++i) {
        const ClientSeenItems& items = m_clientSeen[i];
        const QCborMap scope = seen.value(scopes[i]).toMap();
        const qint64 clients = scope.value(QLatin1StringView("seen")).toInteger();
        const qint64 identified = scope.value(QLatin1StringView("identified")).toInteger();
        items.scope->setHidden(false);
        items.seen->setText(0, tr("Clients Seen: ≈%1").arg(clients));
        // Two estimates: the share can come out a little over 100%.
        items.identified->setText(0, tr("Identified: ≈%1 %2")
                                         .arg(identified)
                                         .arg(formatPercent(std::min(identified, clients), clients)));
        updateCountryRows(items.countries, scope.value(QLatin1StringView("countries")).toArray());
    }
}

void StatisticsPanel::applyKadStats(const QCborMap& data)
{
    m_itemKad->setHidden(false);

    const QCborMap current = data.value(QStringLiteral("current")).toMap();
    const QCborMap seen = data.value(QStringLiteral("seen")).toMap();

    const auto scopeValues = [&](const QString& scope, qint64 runtimeSecs) {
        QHash<QString, qint64> v = counterValues(data.value(scope).toMap());
        const QCborMap seenScope = seen.value(scope).toMap();
        v.insert(QStringLiteral("seenContacted"),
                 seenScope.value(QStringLiteral("contacted")).toInteger());
        v.insert(QStringLiteral("seenListed"),
                 seenScope.value(QStringLiteral("listed")).toInteger());
        v.insert(QStringLiteral("udpNodes"), v.value(QStringLiteral("udpFirewalledNodes"))
                                                 + v.value(QStringLiteral("udpOpenNodes")));
        v.insert(QStringLiteral("tcpNodes"), v.value(QStringLiteral("tcpFirewalledNodes"))
                                                 + v.value(QStringLiteral("tcpOpenNodes")));
        v.insert(QStringLiteral("searchesTotal"), v.value(QStringLiteral("searchesNode"))
                                                      + v.value(QStringLiteral("searchesKeyword"))
                                                      + v.value(QStringLiteral("searchesSource"))
                                                      + v.value(QStringLiteral("searchesNotes")));
        v.insert(QStringLiteral("runtimeMs"), runtimeSecs * 1000);
        return v;
    };

    QHash<QString, qint64> session = scopeValues(QStringLiteral("session"), m_sessionUptime);
    for (auto it = current.cbegin(); it != current.cend(); ++it) {
        if (it.value().isInteger())
            session.insert(it.key().toString(), it.value().toInteger());
    }
    const QCborArray byType = current.value(QStringLiteral("byType")).toArray();
    for (qsizetype i = 0; i < byType.size(); ++i)
        session.insert(QStringLiteral("type%1").arg(i), byType.at(i).toInteger());

    fillCounterRows(m_kadSessionRows, session);
    fillCounterRows(m_kadCumulativeRows, scopeValues(QStringLiteral("cumulative"), m_cumRunTime));

    QString status;
    if (!current.value(QStringLiteral("running")).toBool())
        status = tr("Not running");
    else if (!current.value(QStringLiteral("connected")).toBool())
        status = tr("Connecting");
    else if (current.value(QStringLiteral("lanMode")).toBool())
        status = tr("Connected (LAN mode)");
    else {
        const bool tcp = current.value(QStringLiteral("firewalled")).toBool();
        const bool udp = current.value(QStringLiteral("udpFirewalled")).toBool();
        status = tcp && udp ? tr("Connected, TCP and UDP firewalled")
                 : tcp      ? tr("Connected, TCP firewalled")
                 : udp      ? tr("Connected, UDP firewalled")
                            : tr("Connected, open");
    }
    m_itemKadStatus->setText(0, tr("Status: %1").arg(status));

    updateKadVersions(current.value(QStringLiteral("byVersion")).toArray(),
                      session.value(QStringLiteral("contacts")));
    updateCountryRows(m_itemKadSesCountries,
                       seen.value(QStringLiteral("session")).toMap()
                           .value(QStringLiteral("countries")).toArray());
    updateCountryRows(m_itemKadCumCountries,
                       seen.value(QStringLiteral("cumulative")).toMap()
                           .value(QStringLiteral("countries")).toArray());
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

    menu->addAction(tr("Expand Main Sections"), this, &StatisticsPanel::expandMainSections);
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
    menu->addSeparator();

    // MFC's "HTML Features" submenu (srchybrid/StatisticsTree.cpp:144-151)
    QMenu* html = menu->addMenu(QIcon(QStringLiteral(":/icons/Web.ico")), tr("HTML Features"));
    html->addAction(tr("Copy Branch"), this, [this] { copyHtml(true, true); });
    html->addAction(tr("Copy All Visible"), this, [this] { copyHtml(true, false); });
    html->addAction(tr("Copy All Statistics"), this, [this] { copyHtml(false, false); });
    html->addSeparator();
    html->addAction(tr("Export Statistics..."), this, &StatisticsPanel::exportHtml);

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
    if (auto* item = m_tree->currentItem())
        QApplication::clipboard()->setText(treeText(true, item));
}

void StatisticsPanel::copyAllVisible()
{
    QApplication::clipboard()->setText(treeText(true));
}

void StatisticsPanel::copyAllStats()
{
    QApplication::clipboard()->setText(treeText(false));
}

void StatisticsPanel::copyHtml(bool onlyVisible, bool branchOnly)
{
    QTreeWidgetItem* branch = branchOnly ? m_tree->currentItem() : nullptr;
    if (branchOnly && !branch)
        return;
    QApplication::clipboard()->setText(treeHtml(onlyVisible, branch));
}

void StatisticsPanel::exportHtml()
{
    const QString path = QFileDialog::getSaveFileName(this, tr("Export Statistics..."),
        QStringLiteral("eMule Statistics.html"),
        tr("HTML Files (*.html);;All Files (*)"));
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(exportPageHtml().toUtf8()) < 0)
        QMessageBox::warning(this, tr("Export Statistics..."), file.errorString());
}

bool StatisticsPanel::isSection(const QTreeWidgetItem* item)
{
    // The fixed nodes that carry an icon; fed rows (servers, countries) have a key
    return item && !item->icon(0).isNull() && !item->data(0, Qt::UserRole).isValid();
}

void StatisticsPanel::expandMainSections()
{
    m_tree->collapseAll();
    const auto expand = [](auto&& self, QTreeWidgetItem* parent) -> void {
        for (int i = 0; i < parent->childCount(); ++i) {
            QTreeWidgetItem* item = parent->child(i);
            if (item->childCount() > 0 && isSection(item)) {
                item->setExpanded(true);
                self(self, item);
            }
        }
    };
    expand(expand, m_tree->invisibleRootItem());
}

QString StatisticsPanel::treeText(bool onlyVisible, QTreeWidgetItem* branch) const
{
    // A lone leaf or collapsed node is copied bare, without header or line end
    const bool header = !branch || (branch->childCount() > 0 && branch->isExpanded());
    return (header ? headerLine() + QStringLiteral("\r\n\r\n") : QString())
        + itemsText(onlyVisible, branch ? branch->parent() : nullptr, branch, 0, header);
}

QString StatisticsPanel::treeHtml(bool onlyVisible, QTreeWidgetItem* branch) const
{
    return QStringLiteral("<font face=\"Tahoma,Verdana,Courier New,Helvetica\" size=\"2\">\r\n"
                          "<b>%1</b>\r\n<br><br>\r\n").arg(headerLine().toHtmlEscaped())
        + itemsHtml(onlyVisible, branch ? branch->parent() : nullptr, branch, 0)
        + QStringLiteral("</font>");
}

QString StatisticsPanel::exportPageHtml() const
{
    const auto gif = [](const char* name) {
        QFile f(QStringLiteral(":/stats/%1.gif").arg(QLatin1String(name)));
        const QByteArray data = f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
        return QString(QStringLiteral("data:image/gif;base64,") + QString::fromLatin1(data.toBase64()));
    };
    const QString nick = thePrefs.nick().toHtmlEscaped();
    const QString stats = tr("Statistics");
    int nextId = 0;

    // MFC's page (srchybrid/StatisticsTree.cpp:526-571); the toggle images are data
    // URIs held in two script variables instead of files beside the page
    return QStringLiteral(
        "<!DOCTYPE HTML SYSTEM>\r\n"
        "<html>\r\n<head>\r\n"
        "<meta http-equiv=\"Content-Type\" content=\"text/html;charset=utf-8\">\r\n"
        "<title>eMule %1[%2]</title>\r\n"
        "<style type=\"text/css\">\r\n"
        "#pghdr { color: #000F80; font: bold 12pt/14pt Verdana, Courier New, Helvetica; }\r\n"
        "#pghdr2 { color: #000F80; font: bold 10pt/12pt Verdana, Courier New, Helvetica; }\r\n"
        "img { border: 0px; }\r\n"
        "a { text-decoration: none; }\r\n"
        "#sec { color: #000000; font: bold 9pt/11pt Verdana, Courier New, Helvetica; }\r\n"
        "#item { color: #000000; font: normal 8pt/10pt Verdana, Courier New, Helvetica; }\r\n"
        "#bdy { color: #000000; font: normal 8pt/10pt Verdana, Courier New, Helvetica; background-color: #FFFFFF; }\r\n</style>\r\n"
        "<script language=\"JavaScript\" type=\"text/javascript\">\r\n"
        "var imgVisible = \"%5\";\r\n"
        "var imgHidden = \"%6\";\r\n"
        "function togglevisible(treepart)\r\n"
        "{\r\n"
        "var part = document.getElementById(\"T\"+treepart);\r\n"
        "if (part.style.visibility == \"hidden\")\r\n"
        "{\r\n"
        "part.style.position=\"\";\r\n"
        "part.style.visibility=\"\";\r\n"
        "document.getElementById(\"I\"+treepart).src=imgVisible;\r\n"
        "}\r\n"
        "else\r\n"
        "{\r\n"
        "part.style.position=\"absolute\";\r\n"
        "part.style.visibility=\"hidden\";\r\n"
        "document.getElementById(\"I\"+treepart).src=imgHidden;\r\n"
        "}\r\n"
        "}\r\n"
        "</script>\r\n"
        "</head>\r\n"
        "<body id=\"bdy\">\r\n"
        "<span id=\"pghdr\"><b>eMule %1</b></span><br><span id=\"pghdr2\">%3 %2</span>\r\n<br><br>\r\n"
        "%4</body></html>")
        .arg(stats, nick, tr("Name:"), itemsExportHtml(m_tree->invisibleRootItem(), 0, nextId),
             gif("visible"), gif("hidden"));
}

QString StatisticsPanel::headerLine() const
{
    return tr("eMule Qt v%1 %2 [%3]")
        .arg(QApplication::applicationVersion(), tr("Statistics"), thePrefs.nick());
}

// MFC CStatisticsTree::GetText (srchybrid/StatisticsTree.cpp:371-398)
QString StatisticsPanel::itemsText(bool onlyVisible, QTreeWidgetItem* parent, QTreeWidgetItem* only,
                                   int level, bool lineBreaks) const
{
    QString out;
    if (!parent)
        parent = m_tree->invisibleRootItem();
    for (int i = 0; i < parent->childCount(); ++i) {
        QTreeWidgetItem* item = parent->child(i);
        if (only && item != only)
            continue;
        out += QString(3 * level, QLatin1Char(' ')) + item->text(0);
        if (lineBreaks)
            out += QStringLiteral("\r\n");
        if (item->childCount() > 0 && (!onlyVisible || item->isExpanded()))
            out += itemsText(onlyVisible, item, nullptr, level + 1, true);
    }
    return out;
}

// MFC CStatisticsTree::GetHTML (srchybrid/StatisticsTree.cpp:298-333)
QString StatisticsPanel::itemsHtml(bool onlyVisible, QTreeWidgetItem* parent, QTreeWidgetItem* only,
                                   int level) const
{
    QString out;
    if (!parent)
        parent = m_tree->invisibleRootItem();
    for (int i = 0; i < parent->childCount(); ++i) {
        QTreeWidgetItem* item = parent->child(i);
        if (only && item != only)
            continue;
        for (int n = 0; n < level; ++n)
            out += QStringLiteral("&nbsp;&nbsp;&nbsp;");
        if (level == 0)
            out += QLatin1Char('\n');
        const QString text = item->text(0).toHtmlEscaped();
        out += (isSection(item) ? QStringLiteral("<b>%1</b>").arg(text) : text) + QStringLiteral("<br>");
        if (item->childCount() > 0 && (!onlyVisible || item->isExpanded()))
            out += itemsHtml(onlyVisible, item, nullptr, level + 1);
    }
    return out;
}

// MFC CStatisticsTree::GetHTMLForExport (srchybrid/StatisticsTree.cpp:443-509)
QString StatisticsPanel::itemsExportHtml(QTreeWidgetItem* parent, int level, int& nextId) const
{
    static const auto dataUri = [](const QByteArray& bytes, const char* mime) {
        return QString(QStringLiteral("data:%1;base64,").arg(QLatin1String(mime))
                       + QString::fromLatin1(bytes.toBase64()));
    };
    const auto resource = [](const char* name) {
        QFile f(QStringLiteral(":/stats/%1.gif").arg(QLatin1String(name)));
        return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
    };
    static const QString visible = dataUri(resource("visible"), "image/gif");
    static const QString hidden = dataUri(resource("hidden"), "image/gif");
    static const QString space = dataUri(resource("space"), "image/gif");

    QString out;
    const QString tabs(level, QLatin1Char('\t'));
    for (int i = 0; i < parent->childCount(); ++i) {
        QTreeWidgetItem* item = parent->child(i);
        const bool hasChildren = item->childCount() > 0;
        const int id = hasChildren ? ++nextId : 0;

        QString line;
        if (hasChildren) {
            line += QStringLiteral("<a href=\"javascript:togglevisible('%1')\">"
                                   "<img id=\"I%1\" src=\"%2\" align=\"middle\">&nbsp;</a>")
                        .arg(id).arg(item->isExpanded() ? visible : hidden);
        } else {
            line += QStringLiteral("<img src=\"%1\" align=\"middle\">&nbsp;").arg(space);
        }
        // The node's own icon, where MFC writes stats_<image index>.gif
        if (const QIcon icon = item->icon(0); !icon.isNull()) {
            QByteArray png;
            QBuffer buffer(&png);
            buffer.open(QIODevice::WriteOnly);
            icon.pixmap(16, 16).save(&buffer, "PNG");
            line += QStringLiteral("<img src=\"%1\" width=\"16\" height=\"16\" align=\"middle\">&nbsp;")
                        .arg(dataUri(png, "image/png"));
        }
        const QString text = item->text(0).toHtmlEscaped();
        line += isSection(item) ? QStringLiteral("<b>%1</b>").arg(text) : text;

        out += QLatin1Char('\n') + tabs;
        if (level == 0)
            out += QLatin1Char('\n');
        out += line + QStringLiteral("<br>");
        if (hasChildren) {
            const QString div = item->isExpanded()
                ? QStringLiteral("<div id=\"T%1\" style=\"margin-left:18px\">").arg(id)
                : QStringLiteral("<div id=\"T%1\" style=\"margin-left:18px; visibility:hidden; "
                                 "position:absolute\">").arg(id);
            out += QLatin1Char('\n') + tabs + div
                 + QLatin1Char('\n') + tabs + QLatin1Char('\t') + itemsExportHtml(item, level + 1, nextId)
                 + QLatin1Char('\n') + tabs + QStringLiteral("</div>");
        }
    }
    return out;
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
    return formatSecondsLongHM(std::max<qint64>(secs, 0));
}

QString StatisticsPanel::formatOverhead(qint64 bytes, qint64 packets)
{
    return QStringLiteral("%1 (%2)").arg(formatByteSize(bytes), formatShortNumber(packets));
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
// Private — Kademlia branch
// ---------------------------------------------------------------------------

std::span<const StatisticsPanel::CounterRow> StatisticsPanel::kadTableRows()
{
    using F = RowFormat;
    // Contact types as the Kad window's icons show them (kad::Contact::updateType).
    static constexpr CounterRow kRows[] = {
        {QT_TR_NOOP("Contacts: %1"), "contacts", F::Count, nullptr, 0, true},
        {QT_TR_NOOP("IP Verified: %1 %2"), "verified", F::Count, "contacts", 1, true},
        {QT_TR_NOOP("Type 0, Alive over 2 Hours: %1 %2"), "type0", F::Count, "contacts", 1, true},
        {QT_TR_NOOP("Type 1, Alive 1 to 2 Hours: %1 %2"), "type1", F::Count, "contacts", 1, true},
        {QT_TR_NOOP("Type 2, Alive under 1 Hour: %1 %2"), "type2", F::Count, "contacts", 1, true},
        {QT_TR_NOOP("Type 3, New or Not Answering: %1 %2"), "type3", F::Count, "contacts", 1, true},
        {QT_TR_NOOP("Type 4, Dead: %1 %2"), "type4", F::Count, "contacts", 1, true},
        {QT_TR_NOOP("Most Contacts: %1"), "peakContacts"},
        {QT_TR_NOOP("Contacts Added: %1"), "contactsAdded"},
        {QT_TR_NOOP("IP Verified: %1 %2"), "contactsVerified", F::Count, "contactsAdded", 1},
        {QT_TR_NOOP("Replaced a Weaker Contact: %1 %2"), "contactsReplaced", F::Count,
         "contactsAdded", 1},
        {QT_TR_NOOP("Contacts Expired: %1"), "contactsExpired"},
        {QT_TR_NOOP("Contacts Banned: %1"), "contactsBanned"},
    };
    return kRows;
}

std::span<const StatisticsPanel::CounterRow> StatisticsPanel::kadNodeRows()
{
    using F = RowFormat;
    static constexpr CounterRow kRows[] = {
        {QT_TR_NOOP("Nodes Seen: ≈%1"), "seenContacted"},
        {QT_TR_NOOP("Nodes Heard Of: ≈%1"), "seenListed"},
        // MFC's Clients > Firewalled (Kad), srchybrid/StatisticsDlg.cpp:2286.
        {QT_TR_NOOP("Firewalled (Kad)"), nullptr},
        {QT_TR_NOOP("UDP: %1 %2"), "udpFirewalledNodes", F::Count, "udpNodes", 1},
        {QT_TR_NOOP("TCP: %1 %2"), "tcpFirewalledNodes", F::Count, "tcpNodes", 1},
    };
    return kRows;
}

std::span<const StatisticsPanel::CounterRow> StatisticsPanel::kadNetworkRows()
{
    using F = RowFormat;
    static constexpr CounterRow kRows[] = {
        {QT_TR_NOOP("Estimated Users: %1"), "users", F::Count, nullptr, 0, true},
        {QT_TR_NOOP("Estimated Files: %1"), "files", F::Count, nullptr, 0, true},
        {QT_TR_NOOP("Indexed Keywords: %1"), "indexedKeywords", F::Count, nullptr, 0, true},
        {QT_TR_NOOP("Indexed Sources: %1"), "indexedSources", F::Count, nullptr, 0, true},
        {QT_TR_NOOP("Indexed Notes: %1"), "indexedNotes", F::Count, nullptr, 0, true},
        {QT_TR_NOOP("Active Searches: %1"), "activeSearches", F::Count, nullptr, 0, true},
        {QT_TR_NOOP("Banned Addresses: %1"), "safeKadBanned", F::Count, nullptr, 0, true},
    };
    return kRows;
}

std::span<const StatisticsPanel::CounterRow> StatisticsPanel::kadActivityRows()
{
    using F = RowFormat;
    static constexpr CounterRow kRows[] = {
        {QT_TR_NOOP("Time Connected: %1 %2"), "connectedMs", F::DurationMs, "runtimeMs"},
        {QT_TR_NOOP("Hellos Sent: %1"), "hellosSent"},
        {QT_TR_NOOP("Hellos Answered: %1 %2"), "hellosReceived", F::Count, "hellosSent"},
        {QT_TR_NOOP("Lookup Answers: %1"), "lookupResponses"},
        {QT_TR_NOOP("Bootstrap Answers: %1"), "bootstraps"},
        {QT_TR_NOOP("Searches: %1"), "searchesTotal"},
        {QT_TR_NOOP("Node Lookups: %1 %2"), "searchesNode", F::Count, "searchesTotal", 1},
        {QT_TR_NOOP("Keyword Searches: %1 %2"), "searchesKeyword", F::Count, "searchesTotal", 1},
        {QT_TR_NOOP("Source Searches: %1 %2"), "searchesSource", F::Count, "searchesTotal", 1},
        {QT_TR_NOOP("Notes Searches: %1 %2"), "searchesNotes", F::Count, "searchesTotal", 1},
        {QT_TR_NOOP("Publishes: %1"), "publishes"},
    };
    return kRows;
}

void StatisticsPanel::buildKademliaBranch(const QIcon& detailIcon, const QIcon& cumulativeIcon)
{
    // Fixed text down to depth 1, as in the Usenet branch.
    m_itemKad = new QTreeWidgetItem(m_tree, {tr("Kademlia")});
    m_itemKad->setIcon(0, QIcon(QStringLiteral(":/icons/Kad.ico")));

    const QString estimate =
        tr("Different nodes, counted by node ID. An estimate, accurate to about 2%.");

    const auto buildScope = [&](const QString& label, const QIcon& icon, bool live,
                                QList<CounterItem>& rows, QTreeWidgetItem*& countries) {
        auto* scope = new QTreeWidgetItem(m_itemKad, {label});
        scope->setIcon(0, icon);
        rows.clear();

        if (live)
            m_itemKadStatus = new QTreeWidgetItem(scope);

        auto* table = new QTreeWidgetItem(scope, {tr("Routing Table")});
        table->setIcon(0, QIcon(QStringLiteral(":/icons/KadContactList.ico")));
        buildCounterRows(table, kadTableRows(), live, rows);
        if (live)
            m_itemKadVersions = new QTreeWidgetItem(table, {tr("By Version")});

        auto* nodes = new QTreeWidgetItem(scope, {tr("Nodes")});
        nodes->setIcon(0, QIcon(QStringLiteral(":/icons/Contact0.ico")));
        buildCounterRows(nodes, kadNodeRows(), live, rows);
        countries = new QTreeWidgetItem(nodes, {tr("By Country")});
        countries->setToolTip(0, tr("Nodes that sent us a packet, by the country of the "
                                    "address it came from."));

        if (live) {
            auto* network = new QTreeWidgetItem(scope, {tr("Network")});
            network->setIcon(0, QIcon(QStringLiteral(":/icons/KadServer.ico")));
            buildCounterRows(network, kadNetworkRows(), live, rows);
        }

        auto* activity = new QTreeWidgetItem(scope, {tr("Activity")});
        activity->setIcon(0, QIcon(QStringLiteral(":/icons/KadCurrentSearches.ico")));
        buildCounterRows(activity, kadActivityRows(), live, rows);

        for (const auto& [row, item] : rows) {
            const QLatin1StringView key(row->key);
            if (key == QLatin1StringView("seenContacted")) {
                item->setToolTip(0, tr("Nodes that sent us a packet themselves.") + QLatin1Char(' ')
                                        + estimate);
            } else if (key == QLatin1StringView("seenListed")) {
                item->setToolTip(0, tr("Nodes that other nodes named in their answers; most "
                                       "are never contacted.") + QLatin1Char(' ') + estimate);
            }
        }
    };

    buildScope(tr("Session"), detailIcon, true, m_kadSessionRows, m_itemKadSesCountries);
    buildScope(tr("Cumulative"), cumulativeIcon, false, m_kadCumulativeRows, m_itemKadCumCountries);

    applyKadStats({});
}

void StatisticsPanel::updateKadVersions(const QCborArray& versions, qint64 contacts)
{
    // Kad protocol versions and the eMule release that introduced each
    // (KADEMLIA_VERSION*, core/utils/Opcodes.h).
    static constexpr const char* kReleases[] = {
        nullptr, "0.46c", "0.47a", "0.47b", "0.47c", "0.48a", "0.49a beta", "0.49a", "0.49b", "0.50a",
    };

    QList<KeyedRow> rows;
    // Newest first: the tail of old versions is what one scrolls past.
    for (qsizetype i = versions.size() - 1; i >= 0; --i) {
        const QCborArray entry = versions.at(i).toArray();
        const qint64 version = entry.at(0).toInteger();
        const qint64 count = entry.at(1).toInteger();
        const char* release = version >= 0 && version < qint64{std::size(kReleases)}
                                  ? kReleases[version] : nullptr;
        const QString name = release ? tr("Version %1 (eMule %2)").arg(version)
                                           .arg(QLatin1StringView(release))
                                     : tr("Version %1").arg(version);
        rows.append({QString::number(version),
                     tr("%1: %2 %3").arg(name).arg(count).arg(formatPercent(count, contacts)), {}});
    }
    syncKeyedChildren(m_itemKadVersions, rows);
}

void StatisticsPanel::updateCountryRows(QTreeWidgetItem* parent, const QCborArray& countries)
{
    // Shares of the sum, not of the "seen" total: a peer that moved is in two
    // countries, and the rows should still add up to 100%.
    qint64 total = 0;
    for (const auto& value : countries)
        total += value.toArray().at(1).toInteger();

    QList<KeyedRow> rows;
    rows.reserve(countries.size());
    for (const auto& value : countries) {
        const QCborArray entry = value.toArray();
        const QString cc = entry.at(0).toString();
        const qint64 nodes = entry.at(1).toInteger();
        const QString name = cc.isEmpty() ? tr("Unknown") : CountryFlags::tooltip(cc);
        rows.append({cc.isEmpty() ? QStringLiteral("-") : cc,
                     tr("%1: ≈%2 %3").arg(name).arg(nodes).arg(formatPercent(nodes, total)),
                     CountryFlags::showFlags() ? CountryFlags::flag(cc) : QIcon()});
    }
    syncKeyedChildren(parent, rows);
}

void StatisticsPanel::syncKeyedChildren(QTreeWidgetItem* parent, const QList<KeyedRow>& rows)
{
    QHash<QString, QTreeWidgetItem*> existing;
    for (int i = 0; i < parent->childCount(); ++i)
        existing.insert(parent->child(i)->data(0, Qt::UserRole).toString(), parent->child(i));

    for (int i = 0; i < rows.size(); ++i) {
        const KeyedRow& row = rows.at(i);
        QTreeWidgetItem* item = existing.take(row.key);
        if (!item) {
            item = new QTreeWidgetItem;
            item->setData(0, Qt::UserRole, row.key);
            parent->insertChild(i, item);
        } else if (parent->indexOfChild(item) != i) {
            parent->takeChild(parent->indexOfChild(item));
            parent->insertChild(i, item);
        }
        item->setText(0, row.text);
        item->setIcon(0, row.icon);
    }
    qDeleteAll(existing);
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
