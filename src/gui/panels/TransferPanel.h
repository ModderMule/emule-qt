#pragma once

/// @file TransferPanel.h
/// @brief Transfer tab panel replicating the MFC Transfer window layout.
///
/// Layout (matching MFC screenshots):
///   - Left: vertical action toolbar (CToolbarWnd) with download action buttons
///   - Top pane: bold "Downloads (N)" label + category tabs, download list
///   - Bottom pane: toolbar2 header with 4 client-view buttons, client stack, queue label
///   - Vertical splitter between top and bottom sections

#include <QSet>
#include <QWidget>

#include "dialogs/FileDetailDialog.h"

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
struct SourceRow;
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

    /// Access the download list model (e.g. for checking known hashes).
    [[nodiscard]] DownloadListModel* downloadModel() const { return m_downloadModel; }

    /// Set the stream token for preview streaming (received from daemon GetStats).
    void setStreamToken(const QString& token) { m_streamToken = token; }

signals:
    /// Emitted when user requests to search for files related to a download.
    void searchRequested(const QString& expression);

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
    void requestDownloadSources(const QString& hash);
    void requestUploads();
    void requestDownloadClients();
    void requestKnownClients();
    void sendDownloadAction(const QString& hash, int action);
    void sendStopDownload(const QString& hash);
    void sendOpenFile(const QString& hash);
    void sendOpenFolder(const QString& hash);
    void sendPreview(const QString& hash);
    void sendSetCategory(const QString& hash, int category);
    void sendSetPriority(const QString& hash, int priority, bool isAuto);
    void sendClearCompleted();
    void copyEd2kLink(const QString& hash);
    void showDownloadDetails(const QString& hash);
    void showComments(const QString& hash);
    void fetchAndShowFileDetails(const QString& hash, FileDetailDialog::Tab tab);
    void fetchAndShowClientDetails(const QString& clientHash);
    void searchRelated(const QString& fileName);
    [[nodiscard]] QString saveDownloadSelection() const;
    [[nodiscard]] std::pair<QString, QString> saveFullDownloadSelection() const;
    void restoreDownloadSelection(const QString& key);
    void restoreFullDownloadSelection(const QString& fileHash, const QString& sourceHash);
    [[nodiscard]] QString saveClientSelection(QTreeView* view, ClientListModel* model) const;
    void restoreClientSelection(QTreeView* view, ClientListModel* model, const QString& key);
    void setBottomClientView(int index);
    void updateToolbarLabels();
    void updateCategoryTabs();
    void showPriorityMenu();
    void showFindDialog();
    void onClientContextMenu(QTreeView* view, ClientListModel* model, const QPoint& pos);
    void showSourceContextMenu(const SourceRow& src, const QPoint& globalPos);
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

    // Hashes of currently expanded downloads (for source fetching)
    QSet<QString> m_expandedDownloads;

    // Stream token for preview HTTP streaming (from daemon web server)
    QString m_streamToken;
};

} // namespace eMule
