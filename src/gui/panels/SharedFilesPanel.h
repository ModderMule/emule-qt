#pragma once

/// @file SharedFilesPanel.h
/// @brief Shared Files tab panel replicating the MFC Shared Files window layout.
///
/// Layout:
///   - Left: folder tree (QTreeWidget) for filtering
///   - Right: file list (QTreeView) with sortable columns
///   - Bottom: tab widget with Statistics / Content / eD2K Links

#include <QWidget>

class QLabel;
class QMenu;
class QProgressBar;
class QPushButton;
class QSplitter;
class QTabWidget;
class QTextEdit;
class QTimer;
class QTreeView;
class QTreeWidget;
class QTreeWidgetItem;

namespace eMule {

class IpcClient;
class SharedFilesModel;
class SharedFilesSortProxy;

/// Full Shared Files tab page matching the MFC eMule Shared Files window.
class SharedFilesPanel : public QWidget {
    Q_OBJECT

public:
    explicit SharedFilesPanel(QWidget* parent = nullptr);
    ~SharedFilesPanel() override;

    /// Connect this panel to the IPC client for data updates.
    void setIpcClient(IpcClient* client);

    /// Access the shared files model (e.g. for checking known hashes).
    [[nodiscard]] SharedFilesModel* sharedFilesModel() const { return m_model; }

private slots:
    void onRefreshTimer();
    void onFolderSelectionChanged();
    void onFileSelectionChanged();
    void onFileContextMenu(const QPoint& pos);
    void onFolderItemExpanded(QTreeWidgetItem* item);

private:
    void setupUi();
    QWidget* createTopSection();
    QWidget* createBottomTabs();
    void requestSharedFiles();
    void sendSetPriority(const QString& hash, int priority, bool isAuto);
    void updateStatsTab();
    void updateEd2kTab();
    void onReloadClicked();
    void showPriorityMenu();
    void showFindDialog();
    void copyEd2kLink();
    [[nodiscard]] QString saveSelection() const;
    void restoreSelection(const QString& key);

    // Models
    SharedFilesModel* m_model = nullptr;
    SharedFilesSortProxy* m_proxy = nullptr;

    // Views
    QTreeView* m_fileView = nullptr;
    QTreeWidget* m_folderTree = nullptr;

    // Header
    QLabel* m_headerLabel = nullptr;
    QPushButton* m_reloadButton = nullptr;

    // Bottom tabs
    QTabWidget* m_bottomTabs = nullptr;

    // Statistics tab widgets — value labels
    QLabel* m_statSessionRequests = nullptr;
    QLabel* m_statSessionAccepted = nullptr;
    QLabel* m_statSessionTransferred = nullptr;
    QLabel* m_statTotalRequests = nullptr;
    QLabel* m_statTotalAccepted = nullptr;
    QLabel* m_statTotalTransferred = nullptr;
    QLabel* m_statPopularity = nullptr;
    QLabel* m_statOnQueue = nullptr;
    QLabel* m_statUploading = nullptr;

    // Statistics tab widgets — percentage progress bars
    QProgressBar* m_barSessionRequests = nullptr;
    QProgressBar* m_barSessionAccepted = nullptr;
    QProgressBar* m_barSessionTransferred = nullptr;
    QProgressBar* m_barTotalRequests = nullptr;
    QProgressBar* m_barTotalAccepted = nullptr;
    QProgressBar* m_barTotalTransferred = nullptr;

    // Cached aggregate totals from IPC for percentage computation
    int64_t m_totalRequests = 0;
    int64_t m_totalAccepted = 0;
    int64_t m_totalTransferred = 0;
    int64_t m_totalAllTimeRequests = 0;
    int64_t m_totalAllTimeAccepted = 0;
    int64_t m_totalAllTimeTransferred = 0;

    // eD2K Links tab
    QTextEdit* m_ed2kText = nullptr;
    QPushButton* m_copyButton = nullptr;

    // Splitters
    QSplitter* m_horzSplitter = nullptr;
    QSplitter* m_vertSplitter = nullptr;

    // Folder tree items
    QTreeWidgetItem* m_allSharedItem = nullptr;
    QTreeWidgetItem* m_incomingItem = nullptr;
    QTreeWidgetItem* m_incompleteItem = nullptr;
    QTreeWidgetItem* m_sharedDirsItem = nullptr;
    QTreeWidgetItem* m_allDirsItem = nullptr;

    // IPC
    IpcClient* m_ipc = nullptr;
    QTimer* m_refreshTimer = nullptr;

    // Context menu
    QMenu* m_contextMenu = nullptr;

    // Cached incoming directory for filtering
    QString m_incomingDir;

    // Filesystem tree helpers
    void populateFilesystemChildren(QTreeWidgetItem* parentItem);
    void addFilesystemChild(QTreeWidgetItem* parent, const QString& path,
                            const QString& displayName);
    [[nodiscard]] static bool hasSubdirectories(const QString& path);
    void initFilesystemRoot();
};

} // namespace eMule
