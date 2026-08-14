#pragma once

/// @file TransferPanel.h
/// @brief Transfer tab panel replicating the MFC Transfer window layout.
///
/// Layout (matching MFC screenshots):
///   - Left: vertical action toolbar (CToolbarWnd) with download action buttons
///   - Top pane: toolbar1 header with 5 view buttons + category tabs, mounted list
///   - Bottom pane: toolbar2 header with 4 client-view buttons, mounted list, queue label
///   - Vertical splitter between top and bottom sections
///
/// Both panes switch between lists through the same icon strip, as MFC's m_btnWnd1 /
/// m_btnWnd2 do. There is exactly one view per list and it is moved between the panes,
/// so a list can never be in both at once and its columns, sort order and selection
/// follow it across — the same single-control-per-list model the MFC window uses.

#include <QSet>
#include <QWidget>

#include <array>
#include <initializer_list>
#include <vector>

#include "dialogs/FileDetailDialog.h"

class QAction;
class QLabel;
class QMenu;
class QSortFilterProxyModel;
class QSplitter;
class QTabBar;
class QTimer;
class QToolBar;
class QTreeView;
class QVBoxLayout;

namespace eMule {

class ClientListModel;
struct ClientRow;
class DownloadListModel;
struct SourceRow;
class IpcClient;
class PanelPoller;
class TransferToolbar;

/// Full Transfer tab page matching the MFC eMule Transfer window.
class TransferPanel : public QWidget {
    Q_OBJECT

public:
    explicit TransferPanel(QWidget* parent = nullptr);
    ~TransferPanel() override;

    /// Connect this panel to the IPC client for data updates.
    void setIpcClient(IpcClient* client);

    /// Switch the bottom pane by index (0=Uploading, 1=Downloading, 2=On Queue, 3=Known).
    void switchToSubTab(int index);

    /// Switch the top pane by index (0=Downloads, then the four client lists in the
    /// same order as switchToSubTab()).
    void switchToTopView(int index);

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
    /// The four client lists, in toolbar button order — the order of both toolbars
    /// and of m_clients. Uploading and OnQueue are filled by the same GetUploads reply.
    enum ClientView { Uploading = 0, Downloading = 1, OnQueue = 2, Known = 3,
                      ClientViewCount = 4 };

    /// Top-pane button order: the download list first, then the same four client
    /// lists. Downloads is offered by the top pane only.
    enum TopView { Downloads = 0, TopClientFirst = 1 };

    /// Which pane wins when both ask for the same list. The one that asked last does.
    enum class Pane { Top, Bottom };

    /// One client list: its model, its single view, and the icon both toolbars show
    /// for it. Indexed by ClientView.
    struct ClientSlot {
        ClientListModel* model = nullptr;
        QTreeView* view = nullptr;
        const char* icon = nullptr;
    };

    void setupUi();
    QWidget* createDownloadsSection();
    QWidget* createBottomPane();
    QToolBar* createActionToolbar();
    QTreeView* createClientView(ClientListModel* model, const QString& headerKey,
                                std::initializer_list<int> columnWidths);
    void requestDownloads();
    void requestDownloadSources(const QString& hash);
    void requestUploads();
    void requestDownloadClients();
    void requestKnownClients();
    /// Push a fresh snapshot into one client list, preserving its scroll position and
    /// selection across the model reset.
    void applyClients(int clientView, std::vector<ClientRow> rows);
    void sendDownloadAction(const QString& hash, int action);
    void sendDownloadActionBatch(const QStringList& hashes, int action);
    void sendStopDownload(const QString& hash);
    void sendStopDownloadBatch(const QStringList& hashes);
    /// Open a completed download, transparently handling a remote core.
    /// Local core → daemon opens the file locally. Remote core → fetch over
    /// HTTP via the web server (media in the configured player, otherwise the
    /// system default handler / browser).
    void openDownload(const QString& hash);
    void sendOpenFile(const QString& hash);
    void sendOpenFolder(const QString& hash);
    void sendPreview(const QString& hash);
    /// Build the web-server HTTP streaming URL for a download (preview / remote open).
    /// Empty if the web server / stream token is unavailable.
    [[nodiscard]] QString streamUrl(const QString& hash) const;
    void sendSetCategory(const QString& hash, int category);
    void sendSetCategoryBatch(const QStringList& hashes, int category);
    void sendSetPriority(const QString& hash, int priority, bool isAuto);
    void sendSetPriorityBatch(const QStringList& hashes, int priority, bool isAuto);
    void sendClearCompleted();
    void copyEd2kLink(const QString& hash);
    void copyEd2kLinks(const QStringList& hashes);
    void showDownloadDetails(const QString& hash);
    void showComments(const QString& hash);
    void fetchAndShowFileDetails(const QString& hash, FileDetailDialog::Tab tab);
    void fetchAndShowClientDetails(const QString& clientHash, DetailWalker walker = {});
    void searchRelated(const QString& fileName);
    [[nodiscard]] QString saveDownloadSelection() const;
    [[nodiscard]] QStringList saveDownloadSelectionMulti() const;
    [[nodiscard]] QString saveClientSelection(QTreeView* view, ClientListModel* model) const;
    void restoreClientSelection(QTreeView* view, ClientListModel* model, const QString& key);
    void setTopView(int topId);
    void setBottomView(int clientView);
    /// The one implementation behind both switchers: resolves the two panes asking
    /// for the same list, moves the views, and refreshes what is now on screen.
    void applyViews(int topId, int clientView, Pane priority);
    /// Take the pane's current view out of @p layout unless it is the one to keep.
    static void detachView(QVBoxLayout* layout, QTreeView*& slot, QTreeView* keep);
    /// Put @p view into @p layout unless the pane already shows it.
    static void attachView(QVBoxLayout* layout, QTreeView*& slot, QTreeView* view);
    void updateToolbarLabels();
    /// Slot lookup by ClientView — the enumerators are ints, std::array wants size_t.
    [[nodiscard]] ClientSlot& clientSlot(int clientView)
    {
        return m_clients[static_cast<size_t>(clientView)];
    }
    [[nodiscard]] const ClientSlot& clientSlot(int clientView) const
    {
        return m_clients[static_cast<size_t>(clientView)];
    }
    /// Plain list name, for the toolbar button tooltips.
    [[nodiscard]] static QString clientViewName(int clientView);
    /// List name with its current row count, for the toolbar's bold label.
    [[nodiscard]] QString clientLabelText(int clientView) const;
    void updateCategoryTabs();
    void showPriorityMenu();
    void showFindDialog();
    void onClientContextMenu(QTreeView* view, ClientListModel* model, const QPoint& pos);
    void showSourceContextMenu(const SourceRow& src, const QString& parentHash,
                               const QPoint& globalPos);
    void showClientFindDialog(QTreeView* view);
    void updateClearCompletedState();

    // -- Detail-dialog Prev/Next walkers (MFC CListCtrlItemWalk) --------------
    // Each walker anchors on an item *key*, never a QModelIndex: the poll timer
    // resets the flat models and UiState::guardSelectionOnReset() clears the
    // view's current index with them, so the row has to be re-resolved on every
    // step. The download tree needs a row filter because MFC's flat list walks
    // over download rows and source rows separately.
    [[nodiscard]] QModelIndex downloadIndexFor(const QString& fileHash) const;
    [[nodiscard]] QModelIndex sourceIndexFor(const QString& fileHash,
                                             const QString& userHash) const;
    [[nodiscard]] QModelIndex clientIndexFor(const QTreeView* view,
                                             const ClientListModel* model,
                                             const QString& userHash) const;
    [[nodiscard]] DetailWalker makeDownloadWalker(const QString& fileHash);
    [[nodiscard]] DetailWalker makeSourceWalker(const QString& parentHash,
                                                const QString& userHash);
    [[nodiscard]] DetailWalker makeClientWalker(QTreeView* view, ClientListModel* model,
                                                const QString& userHash);

    // Data models
    DownloadListModel* m_downloadModel = nullptr;

    // The four client lists — model, view and icon per list
    std::array<ClientSlot, ClientViewCount> m_clients{};

    // Proxy models
    QSortFilterProxyModel* m_downloadProxy = nullptr;
    QSortFilterProxyModel* m_categoryProxy = nullptr;

    // Download list view — the top pane's first mode, never shown at the bottom
    QTreeView* m_downloadView = nullptr;

    // Left-side action toolbar (MFC CToolbarWnd)
    QToolBar* m_actionToolbar = nullptr;

    // Section header toolbars (MFC m_btnWnd1 / m_btnWnd2)
    TransferToolbar* m_toolbar1 = nullptr;
    TransferToolbar* m_toolbar2 = nullptr;

    // Category tab bar — only meaningful while the top pane shows Downloads
    QTabBar* m_categoryTabBar = nullptr;

    // Pane content areas and the view each currently holds
    QVBoxLayout* m_topContent = nullptr;
    QVBoxLayout* m_bottomContent = nullptr;
    QTreeView* m_topMounted = nullptr;
    QTreeView* m_bottomMounted = nullptr;

    // Selected view per pane: m_topView is a TopView, m_bottomView a ClientView
    int m_topView = TopView::Downloads;
    int m_bottomView = ClientView::Uploading;

    // Bottom status label
    QLabel* m_queueLabel = nullptr;

    /// Waiting-client count from the stats push; -1 until the first one arrives, when
    /// the On Queue model's own count stands in.
    int m_queueCount = -1;

    // Splitter
    QSplitter* m_vertSplitter = nullptr;

    /// Periodic refetch, suspended while this panel is not the visible tab.
    PanelPoller* m_poller = nullptr;

    // IPC link
    IpcClient* m_ipc = nullptr;

    // Context menu
    QMenu* m_downloadMenu = nullptr;

    // Actions that require a selected download (greyed out when nothing selected)
    QList<QAction*> m_selectionActions;

    // Toolbar actions with state-dependent enable/disable
    QAction* m_actPause = nullptr;
    QAction* m_actStop = nullptr;
    QAction* m_actResume = nullptr;
    QAction* m_actCancel = nullptr;

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
