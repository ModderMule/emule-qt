#pragma once

/// @file StatisticsPanel.h
/// @brief Statistics panel — tree view + oscilloscope graphs.
///
/// Left side: QTreeWidget showing live statistics (transfer, connection,
/// time, clients, servers, shared files, total downloads, Usenet).
/// Right side: 3 stacked StatsGraph widgets (Download, Upload, Connections).

#include <QHash>
#include <QList>
#include <QWidget>

#include <span>

class QCborArray;
class QIcon;
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
    static QString formatRate(double kbps);
    static QString formatDuration(qint64 secs);
    static QString formatOverhead(qint64 bytes, qint64 packets);
    static QString formatRatio(qint64 up, qint64 down);
    static QString formatPercent(qint64 part, qint64 whole);

    /// Apply one GetUsenetStats reply to the Usenet branch. Public for tests.
    void applyUsenetStats(const class QCborMap& data);

    /// Apply one GetStats reply to the tree. Public for tests.
    void updateTree(const class QCborMap& stats);

private slots:
    void onContextMenu(const QPoint& pos);

private:
    void setupUi();
    void buildTree();
    void requestStats();
    /// Ask the daemon for graph samples newer than m_statsSeq.
    void requestGraphHistory();
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
    QTreeWidgetItem* m_itemCurrentServerDuration = nullptr;
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

    // --- Usenet ---
    //
    // Data-driven rather than a member per row: the branch shows the same
    // ~70 rows under Session and Cumulative, and the table is what keeps the
    // two scopes, the IPC keys and the labels in one place.

    enum class RowFormat : quint8 { Count, Bytes, Rate, DurationMs };

    struct CounterRow {
        const char* pattern;   ///< QT_TR_NOOP; "%1", or "%1 %2" when shareOf is set
        const char* key;       ///< value key; null for a group heading
        RowFormat format = RowFormat::Count;
        const char* shareOf = nullptr;   ///< key of the 100% figure
        quint8 depth = 0;                ///< nesting below the scope node
        bool liveOnly = false;           ///< a current figure: Session only
    };

    struct CounterItem {
        const CounterRow* row = nullptr;
        QTreeWidgetItem* item = nullptr;
    };

    [[nodiscard]] static std::span<const CounterRow> usenetCounterRows();
    [[nodiscard]] static std::span<const CounterRow> usenetQueueRows();
    [[nodiscard]] static std::span<const CounterRow> httpCacheUploadRows();
    [[nodiscard]] static std::span<const CounterRow> httpCacheDownloadRows();
    void buildUsenetBranch(const QIcon& detailIcon, const QIcon& cumulativeIcon);
    void buildHttpCacheBranch(QTreeWidgetItem* transfer, const QIcon& detailIcon,
                              const QIcon& cumulativeIcon);
    static void buildCounterRows(QTreeWidgetItem* parent, std::span<const CounterRow> rows,
                                 bool includeLive, QList<CounterItem>& out);
    static void fillCounterRows(const QList<CounterItem>& items,
                                const QHash<QString, qint64>& values);
    /// One counter block's CBOR map as a lookup table for fillCounterRows().
    [[nodiscard]] static QHash<QString, qint64> counterValues(const QCborMap& block);
    void updateNewsServers(const QCborArray& servers, qint64 sessionWireBytes);

    QTreeWidgetItem* m_itemUsenet = nullptr;
    QTreeWidgetItem* m_itemUsenetServers = nullptr;
    QList<CounterItem> m_usenetSessionRows;
    QList<CounterItem> m_usenetCumulativeRows;
    QList<CounterItem> m_usenetQueueRows;

    // HTTP Cache: the same two scopes for each direction.
    QList<CounterItem> m_hcUpSessionRows;
    QList<CounterItem> m_hcUpCumulativeRows;
    QList<CounterItem> m_hcDownSessionRows;
    QList<CounterItem> m_hcDownCumulativeRows;

    // From the last GetStats: what Usenet's download time is a share of.
    qint64 m_sessionUptime = 0;
    qint64 m_cumRunTime = 0;
};

} // namespace eMule
