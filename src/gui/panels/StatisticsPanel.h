#pragma once

/// @file StatisticsPanel.h
/// @brief Statistics panel — tree view + oscilloscope graphs.
///
/// Left side: QTreeWidget showing live statistics (transfer, connection,
/// time, clients, servers, shared files).
/// Right side: 3 stacked StatsGraph widgets (Download, Upload, Connections).

#include <QCborMap>
#include <QWidget>

class QSplitter;
class QTimer;
class QTreeWidget;
class QTreeWidgetItem;

namespace eMule {

class IpcClient;
class StatsGraph;

/// Statistics panel matching the MFC eMule Statistics tab.
class StatisticsPanel : public QWidget {
    Q_OBJECT

public:
    explicit StatisticsPanel(QWidget* parent = nullptr);

    void setIpcClient(IpcClient* client);

    /// Apply interval, color, and fill settings from preferences to live graphs.
    void applySettings();

private slots:
    void onRefreshTimer();
    void onGraphTimer();
    void onContextMenu(const QPoint& pos);

private:
    void setupUi();
    void buildTree();
    void requestStats();
    void updateTree(const class QCborMap& stats);
    void feedGraphs(const class QCborMap& stats);

    // Formatting helpers
    static QString formatBytes(qint64 bytes);
    static QString formatRate(double kbps);
    static QString formatDuration(qint64 secs);
    static QString formatOverhead(qint64 bytes, qint64 packets);
    static QString formatRatio(qint64 sent, qint64 received);
    static QString formatPercent(qint64 part, qint64 whole);

    // Context menu actions
    void copyBranch();
    void copyAllVisible();
    void copyAllStats();
    void resetStats();
    QString treeItemText(QTreeWidgetItem* item, int depth) const;

    IpcClient* m_ipc = nullptr;
    QTimer* m_refreshTimer = nullptr;
    QTimer* m_graphTimer = nullptr;    // fires every 3s to feed graph points
    QCborMap m_lastStats;              // cache latest stats for graph sampling

    // Layout
    QSplitter* m_hSplitter = nullptr;
    QTreeWidget* m_tree = nullptr;
    StatsGraph* m_graphDown = nullptr;
    StatsGraph* m_graphUp = nullptr;
    StatsGraph* m_graphConn = nullptr;

    // Tree items that get updated on each tick
    // Transfer
    QTreeWidgetItem* m_itemSessionUlDlRatio = nullptr;
    QTreeWidgetItem* m_itemFriendUlDlRatio = nullptr;
    QTreeWidgetItem* m_itemCumUlDlRatio = nullptr;

    // Uploads — Session
    QTreeWidgetItem* m_itemUpSessionData = nullptr;
    QTreeWidgetItem* m_itemUpSessionFriendData = nullptr;
    QTreeWidgetItem* m_itemUpActiveUploads = nullptr;
    QTreeWidgetItem* m_itemUpWaitingUploads = nullptr;
    QTreeWidgetItem* m_itemUpSuccessful = nullptr;
    QTreeWidgetItem* m_itemUpFailed = nullptr;
    QTreeWidgetItem* m_itemUpAvgPerSession = nullptr;
    QTreeWidgetItem* m_itemUpAvgTime = nullptr;
    QTreeWidgetItem* m_itemUpOverheadTotal = nullptr;
    QTreeWidgetItem* m_itemUpOverheadFileReq = nullptr;
    QTreeWidgetItem* m_itemUpOverheadSrcExch = nullptr;
    QTreeWidgetItem* m_itemUpOverheadServer = nullptr;
    QTreeWidgetItem* m_itemUpOverheadKad = nullptr;

    // Downloads — Session
    QTreeWidgetItem* m_itemDownSessionData = nullptr;
    QTreeWidgetItem* m_itemDownActiveDownloads = nullptr;
    QTreeWidgetItem* m_itemDownFoundSources = nullptr;
    QTreeWidgetItem* m_itemDownOverheadTotal = nullptr;
    QTreeWidgetItem* m_itemDownOverheadFileReq = nullptr;
    QTreeWidgetItem* m_itemDownOverheadSrcExch = nullptr;
    QTreeWidgetItem* m_itemDownOverheadServer = nullptr;
    QTreeWidgetItem* m_itemDownOverheadKad = nullptr;

    // Connection
    QTreeWidgetItem* m_itemConnActive = nullptr;
    QTreeWidgetItem* m_itemConnPeak = nullptr;
    QTreeWidgetItem* m_itemConnMaxReached = nullptr;
    QTreeWidgetItem* m_itemConnReconnects = nullptr;
    QTreeWidgetItem* m_itemConnAverage = nullptr;

    // Time Statistics
    QTreeWidgetItem* m_itemTimeHeader = nullptr;
    QTreeWidgetItem* m_itemTimeSinceReset = nullptr;
    QTreeWidgetItem* m_itemRuntime = nullptr;
    QTreeWidgetItem* m_itemTransferTime = nullptr;
    QTreeWidgetItem* m_itemUploadTime = nullptr;
    QTreeWidgetItem* m_itemDownloadTime = nullptr;
    QTreeWidgetItem* m_itemServerDuration = nullptr;

    // Clients
    QTreeWidgetItem* m_itemKnownClients = nullptr;
    QTreeWidgetItem* m_itemBannedClients = nullptr;
    QTreeWidgetItem* m_itemFilteredClients = nullptr;

    // Servers
    QTreeWidgetItem* m_itemSrvWorking = nullptr;
    QTreeWidgetItem* m_itemSrvFailed = nullptr;
    QTreeWidgetItem* m_itemSrvTotal = nullptr;
    QTreeWidgetItem* m_itemSrvUsers = nullptr;
    QTreeWidgetItem* m_itemSrvFiles = nullptr;
    QTreeWidgetItem* m_itemSrvLowID = nullptr;

    // Shared Files
    QTreeWidgetItem* m_itemSharedCount = nullptr;
    QTreeWidgetItem* m_itemSharedSize = nullptr;
    QTreeWidgetItem* m_itemSharedLargest = nullptr;
};

} // namespace eMule
