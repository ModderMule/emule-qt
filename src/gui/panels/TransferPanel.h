#pragma once

/// @file TransferPanel.h
/// @brief Transfer tab panel replicating the MFC Transfer window layout.
///
/// Layout (matching MFC screenshots):
///   - Left: vertical action toolbar (CToolbarWnd) with download action buttons
///   - Top pane: bold "Downloads (N)" label + category tabs, download list
///   - Bottom pane: toolbar2 header with 4 client-view buttons, client stack, queue label
///   - Vertical splitter between top and bottom sections

#include <QWidget>

class QAction;
class QLabel;
class QMenu;
class QSortFilterProxyModel;
class QSplitter;
class QStackedWidget;
class QTabBar;
class QTimer;
class QToolBar;
class QTreeView;

namespace eMule {

class ClientListModel;
class DownloadListModel;
class IpcClient;
class TransferToolbar;

/// Full Transfer tab page matching the MFC eMule Transfer window.
class TransferPanel : public QWidget {
    Q_OBJECT

public:
    explicit TransferPanel(QWidget* parent = nullptr);
    ~TransferPanel() override;

    /// Connect this panel to the IPC client for data updates.
    void setIpcClient(IpcClient* client);

    /// Switch to a client sub-tab by index (0=Uploading, 1=Downloading, 2=On Queue, 3=Known).
    void switchToSubTab(int index);

private slots:
    void onRefreshTimer();
    void onDownloadContextMenu(const QPoint& pos);
    void updateActionStates();

private:
    void setupUi();
    QWidget* createDownloadsSection();
    QWidget* createBottomPane();
    QToolBar* createActionToolbar();
    QTreeView* createClientView(ClientListModel* model, const QString& headerKey);
    void requestDownloads();
    void requestUploads();
    void requestDownloadClients();
    void requestKnownClients();
    void sendDownloadAction(const QString& hash, int action);
    void sendSetPriority(const QString& hash, int priority, bool isAuto);
    void sendClearCompleted();
    void copyEd2kLink(const QString& hash);
    [[nodiscard]] QString saveDownloadSelection() const;
    void restoreDownloadSelection(const QString& key);
    void setBottomClientView(int index);
    void updateToolbarLabels();
    void updateCategoryTabs();
    void showPriorityMenu();
    void showFindDialog();
    void onClientContextMenu(QTreeView* view, ClientListModel* model, const QPoint& pos);
    void showClientFindDialog(QTreeView* view);
    void updateClearCompletedState();

    // Data models
    DownloadListModel* m_downloadModel = nullptr;
    ClientListModel* m_uploadingModel  = nullptr;
    ClientListModel* m_downloadingModel = nullptr;
    ClientListModel* m_onQueueModel    = nullptr;
    ClientListModel* m_knownModel      = nullptr;

    // Proxy models
    QSortFilterProxyModel* m_downloadProxy = nullptr;
    QSortFilterProxyModel* m_categoryProxy = nullptr;

    // Views
    QTreeView* m_downloadView = nullptr;
    QTreeView* m_uploadingView = nullptr;
    QTreeView* m_downloadingView = nullptr;
    QTreeView* m_onQueueView = nullptr;
    QTreeView* m_knownView = nullptr;

    // Top pane header label
    QLabel* m_downloadsLabel = nullptr;

    // Left-side action toolbar (MFC CToolbarWnd)
    QToolBar* m_actionToolbar = nullptr;

    // Bottom section header toolbar
    TransferToolbar* m_toolbar2 = nullptr;

    // Category tab bar
    QTabBar* m_categoryTabBar = nullptr;

    // Bottom client stack (4 views: uploading, downloading, on queue, known)
    QStackedWidget* m_bottomClientStack = nullptr;

    // Bottom status label
    QLabel* m_queueLabel = nullptr;

    // Splitter
    QSplitter* m_vertSplitter = nullptr;

    // Refresh timer
    QTimer* m_refreshTimer = nullptr;

    // IPC link
    IpcClient* m_ipc = nullptr;

    // Context menu
    QMenu* m_downloadMenu = nullptr;

    // Actions that require a selected download (greyed out when nothing selected)
    QList<QAction*> m_selectionActions;

    // Toolbar "Clear Completed" action (greyed when no completed downloads)
    QAction* m_clearCompletedAction = nullptr;

    // Cached category set for change detection
    QSet<int64_t> m_categorySet;
};

} // namespace eMule
