#pragma once

/// @file StatisticsPanel.h
/// @brief Statistics panel — tree view + oscilloscope graphs.
///
/// Left side: QTreeWidget showing live statistics (transfer, connection,
/// time, clients, servers, shared files, total downloads).
/// Right side: 3 stacked StatsGraph widgets (Download, Upload, Connections).

#include <QWidget>

class QSplitter;
class QTimer;
class QTreeWidget;
class QTreeWidgetItem;

namespace eMule {

class IpcClient;
class PanelPoller;
class StatsGraph;

/// Statistics panel matching the MFC eMule Statistics tab.
class StatisticsPanel : public QWidget {
    Q_OBJECT

public:
    explicit StatisticsPanel(QWidget* parent = nullptr);

    void setIpcClient(IpcClient* client);

    /// Apply interval, color, and fill settings from preferences to live graphs.
    void applySettings();

    // Formatting helpers
    static QString formatBytes(qint64 bytes);
    static QString formatRate(double kbps);
    static QString formatDuration(qint64 secs);
    static QString formatOverhead(qint64 bytes, qint64 packets);
    static QString formatRatio(qint64 sent, qint64 received);
    static QString formatPercent(qint64 part, qint64 whole);

private slots:
    void onContextMenu(const QPoint& pos);

private:
    void setupUi();
    void buildTree();
    void requestStats();
    /// Ask the daemon for graph samples newer than m_statsSeq.
    void requestGraphHistory();
    void updateTree(const class QCborMap& stats);
    /// Apply one GetStatsHistory reply, clearing the graphs first if what we hold is
    /// no longer a prefix of the daemon's history.
    void applyGraphHistory(const class QCborMap& data);

    // Context menu actions
    /// Build the tree menu, shared by the right-click and the header-bar button.
    /// Deletes itself when it closes; the caller pops it up.
    class QMenu* buildStatsMenu();
    void copyBranch();
    void copyAllVisible();
    void copyAllStats();
    void resetStats();
    void restoreStats();
    QString treeItemText(QTreeWidgetItem* item, int depth) const;

    IpcClient* m_ipc = nullptr;
    PanelPoller* m_treePoller = nullptr;
    PanelPoller* m_graphPoller = nullptr;

    // Which slice of the daemon's sample history the three graphs currently hold.
    quint32 m_statsSeq = 0;
    quint32 m_statsEpoch = 0;

    /// Set from the last stats poll; gates the Restore Statistics menu item.
    bool m_backupAvailable = false;

    // Layout
    QSplitter* m_hSplitter = nullptr;
    class QToolButton* m_menuButton = nullptr;
    class QLabel* m_labelLastReset = nullptr;
    QTreeWidget* m_tree = nullptr;
    StatsGraph* m_graphDown = nullptr;
    StatsGraph* m_graphUp = nullptr;
    StatsGraph* m_graphConn = nullptr;

    // --- Tree items ---

    // Transfer ratios
    QTreeWidgetItem* m_itemSessionUlDlRatio = nullptr;
    QTreeWidgetItem* m_itemFriendUlDlRatio = nullptr;
    QTreeWidgetItem* m_itemCumUlDlRatio = nullptr;

    // Uploads — Session
    QTreeWidgetItem* m_itemUpSessionData = nullptr;
    QTreeWidgetItem* m_itemUpSesClient[7]{};    // eMule..eMCompat
    QTreeWidgetItem* m_itemUpSesPort[2]{};      // 4662, Other
    QTreeWidgetItem* m_itemUpSesSource[2]{};    // File, Partfile
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

    // Uploads — Cumulative
    QTreeWidgetItem* m_itemUpCumData = nullptr;
    QTreeWidgetItem* m_itemUpCumClient[7]{};
    QTreeWidgetItem* m_itemUpCumPort[2]{};
    QTreeWidgetItem* m_itemUpCumSource[2]{};

    // HTTP Cache
    QTreeWidgetItem* m_itemHcSesPublished = nullptr;
    QTreeWidgetItem* m_itemHcSesFetched = nullptr;
    QTreeWidgetItem* m_itemHcSesSaved = nullptr;
    QTreeWidgetItem* m_itemHcSesChunksUp = nullptr;
    QTreeWidgetItem* m_itemHcSesChunksDown = nullptr;
    QTreeWidgetItem* m_itemHcCumPublished = nullptr;
    QTreeWidgetItem* m_itemHcCumFetched = nullptr;
    QTreeWidgetItem* m_itemHcCumSaved = nullptr;
    QTreeWidgetItem* m_itemHcCumChunksUp = nullptr;
    QTreeWidgetItem* m_itemHcCumChunksDown = nullptr;
    QTreeWidgetItem* m_itemUpCumSuccessful = nullptr;
    QTreeWidgetItem* m_itemUpCumFailed = nullptr;
    QTreeWidgetItem* m_itemUpCumAvgPerSession = nullptr;
    QTreeWidgetItem* m_itemUpCumAvgTime = nullptr;
    QTreeWidgetItem* m_itemUpCumOverheadTotal = nullptr;
    QTreeWidgetItem* m_itemUpCumOverheadFileReq = nullptr;
    QTreeWidgetItem* m_itemUpCumOverheadSrcExch = nullptr;
    QTreeWidgetItem* m_itemUpCumOverheadServer = nullptr;
    QTreeWidgetItem* m_itemUpCumOverheadKad = nullptr;

    // Downloads — Session
    QTreeWidgetItem* m_itemDownSessionData = nullptr;
    QTreeWidgetItem* m_itemDownSesClient[8]{};  // eMule..URL
    QTreeWidgetItem* m_itemDownSesPort[2]{};
    QTreeWidgetItem* m_itemDownActiveDownloads = nullptr;
    QTreeWidgetItem* m_itemDownFoundSources = nullptr;
    QTreeWidgetItem* m_itemDownUdpReasks = nullptr;
    QTreeWidgetItem* m_itemDownCompletedSes = nullptr;
    QTreeWidgetItem* m_itemDownSesSuccessful = nullptr;
    QTreeWidgetItem* m_itemDownSesFailed = nullptr;
    QTreeWidgetItem* m_itemDownSesAvgPerSession = nullptr;
    QTreeWidgetItem* m_itemDownSesAvgTime = nullptr;
    QTreeWidgetItem* m_itemDownSesCompression = nullptr;
    QTreeWidgetItem* m_itemDownSesCorruption = nullptr;
    QTreeWidgetItem* m_itemDownSesIchSaved = nullptr;
    QTreeWidgetItem* m_itemDownOverheadTotal = nullptr;
    QTreeWidgetItem* m_itemDownOverheadFileReq = nullptr;
    QTreeWidgetItem* m_itemDownOverheadSrcExch = nullptr;
    QTreeWidgetItem* m_itemDownOverheadServer = nullptr;
    QTreeWidgetItem* m_itemDownOverheadKad = nullptr;

    // Downloads — Cumulative
    QTreeWidgetItem* m_itemDownCumData = nullptr;
    QTreeWidgetItem* m_itemDownCumClient[8]{};
    QTreeWidgetItem* m_itemDownCumPort[2]{};
    QTreeWidgetItem* m_itemDownCumCompleted = nullptr;
    QTreeWidgetItem* m_itemDownCumSuccessful = nullptr;
    QTreeWidgetItem* m_itemDownCumFailed = nullptr;
    QTreeWidgetItem* m_itemDownCumAvgPerSession = nullptr;
    QTreeWidgetItem* m_itemDownCumAvgTime = nullptr;
    QTreeWidgetItem* m_itemDownCumCompression = nullptr;
    QTreeWidgetItem* m_itemDownCumCorruption = nullptr;
    QTreeWidgetItem* m_itemDownCumIchSaved = nullptr;
    QTreeWidgetItem* m_itemDownCumOverheadTotal = nullptr;
    QTreeWidgetItem* m_itemDownCumOverheadFileReq = nullptr;
    QTreeWidgetItem* m_itemDownCumOverheadSrcExch = nullptr;
    QTreeWidgetItem* m_itemDownCumOverheadServer = nullptr;
    QTreeWidgetItem* m_itemDownCumOverheadKad = nullptr;

    // Connection — Session
    QTreeWidgetItem* m_itemConnActive = nullptr;
    QTreeWidgetItem* m_itemConnPeak = nullptr;
    QTreeWidgetItem* m_itemConnMaxReached = nullptr;
    QTreeWidgetItem* m_itemConnReconnects = nullptr;
    QTreeWidgetItem* m_itemConnAverage = nullptr;
    QTreeWidgetItem* m_itemConnSesUpSpeed = nullptr;
    QTreeWidgetItem* m_itemConnSesMaxUp = nullptr;
    QTreeWidgetItem* m_itemConnSesMaxAvgUp = nullptr;
    QTreeWidgetItem* m_itemConnSesDownSpeed = nullptr;
    QTreeWidgetItem* m_itemConnSesMaxDown = nullptr;
    QTreeWidgetItem* m_itemConnSesMaxAvgDown = nullptr;

    // Connection — Cumulative
    QTreeWidgetItem* m_itemConnCumReconnects = nullptr;
    QTreeWidgetItem* m_itemConnCumPeak = nullptr;
    QTreeWidgetItem* m_itemConnCumMaxReached = nullptr;
    QTreeWidgetItem* m_itemConnCumAvgUp = nullptr;
    QTreeWidgetItem* m_itemConnCumMaxUp = nullptr;
    QTreeWidgetItem* m_itemConnCumMaxAvgUp = nullptr;
    QTreeWidgetItem* m_itemConnCumAvgDown = nullptr;
    QTreeWidgetItem* m_itemConnCumMaxDown = nullptr;
    QTreeWidgetItem* m_itemConnCumMaxAvgDown = nullptr;

    // Time Statistics
    QTreeWidgetItem* m_itemTimeHeader = nullptr;
    QTreeWidgetItem* m_itemStatsLastReset = nullptr;
    QTreeWidgetItem* m_itemTimeSinceReset = nullptr;
    // Session
    QTreeWidgetItem* m_itemRuntime = nullptr;
    QTreeWidgetItem* m_itemTransferTime = nullptr;
    QTreeWidgetItem* m_itemUploadTime = nullptr;
    QTreeWidgetItem* m_itemDownloadTime = nullptr;
    QTreeWidgetItem* m_itemServerDuration = nullptr;
    // Cumulative
    QTreeWidgetItem* m_itemCumRuntime = nullptr;
    QTreeWidgetItem* m_itemCumTransferTime = nullptr;
    QTreeWidgetItem* m_itemCumUploadTime = nullptr;
    QTreeWidgetItem* m_itemCumDownloadTime = nullptr;
    QTreeWidgetItem* m_itemCumServerDuration = nullptr;

    // Clients
    QTreeWidgetItem* m_itemKnownClients = nullptr;
    QTreeWidgetItem* m_itemClientSoftware = nullptr;  // dynamic subtree root
    QTreeWidgetItem* m_itemLowIDClients = nullptr;
    QTreeWidgetItem* m_itemBannedClients = nullptr;
    QTreeWidgetItem* m_itemFilteredClients = nullptr;

    // Servers
    QTreeWidgetItem* m_itemSrvWorking = nullptr;
    QTreeWidgetItem* m_itemSrvFailed = nullptr;
    QTreeWidgetItem* m_itemSrvTotal = nullptr;
    QTreeWidgetItem* m_itemSrvUsers = nullptr;
    QTreeWidgetItem* m_itemSrvFiles = nullptr;
    QTreeWidgetItem* m_itemSrvLowID = nullptr;
    QTreeWidgetItem* m_itemSrvRecWorking = nullptr;
    QTreeWidgetItem* m_itemSrvRecUsers = nullptr;
    QTreeWidgetItem* m_itemSrvRecFiles = nullptr;

    // Shared Files
    QTreeWidgetItem* m_itemSharedCount = nullptr;
    QTreeWidgetItem* m_itemSharedSize = nullptr;
    QTreeWidgetItem* m_itemSharedAvgSize = nullptr;
    QTreeWidgetItem* m_itemSharedLargest = nullptr;
    QTreeWidgetItem* m_itemSharedRecCount = nullptr;
    QTreeWidgetItem* m_itemSharedRecSize = nullptr;
    QTreeWidgetItem* m_itemSharedRecAvg = nullptr;
    QTreeWidgetItem* m_itemSharedRecLargest = nullptr;

    // Total Downloads
    QTreeWidgetItem* m_itemTotalDownCount = nullptr;
    QTreeWidgetItem* m_itemTotalDownSize = nullptr;
    QTreeWidgetItem* m_itemTotalDownDone = nullptr;
    QTreeWidgetItem* m_itemTotalDownLeft = nullptr;
    QTreeWidgetItem* m_itemTotalDownFreeSpace = nullptr;
};

} // namespace eMule
