#pragma once

/// @file SharedFilesPanel.h
/// @brief Shared Files tab panel replicating the MFC Shared Files window layout.
///
/// Layout:
///   - Left: folder tree (QTreeWidget) for filtering
///   - Right: file list (QTreeView) with sortable columns
///   - Bottom: tab widget with Statistics / Content / eD2K Links

#include <QStringList>
#include <QWidget>

#include <functional>
#include <vector>

#include "dialogs/DetailDialog.h"

class QCheckBox;
class QGroupBox;
class QLabel;
class QMenu;
class QProgressBar;
class QPushButton;
class QSplitter;
class QStackedWidget;
class QTabWidget;
class QTextEdit;
class QTimer;
class QTreeView;
class QTreeWidget;
class QTreeWidgetItem;

namespace eMule {

class ArchivePreviewPanel;
class IpcClient;
class PanelPoller;
struct SharedFileRow;
class MediaInfoPanel;
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

signals:
    /// Ask MainWindow to run this search in the Search panel and switch to it.
    /// Emitted by "Search Author's Collections…", which needs an explicit method and
    /// file type rather than whatever the search UI happens to hold.
    void searchRequested(const QString& expression, const QString& fileType,
                         int method, const QString& title);

private slots:
    void onRefreshTimer();
    void onFolderSelectionChanged();
    void onFileSelectionChanged();
    void onFileContextMenu(const QPoint& pos);
    void onFolderContextMenu(const QPoint& pos);
    void onFolderItemExpanded(QTreeWidgetItem* item);

private:
    /// The file list's view state, all of which a model reset destroys.
    struct SelectionState {
        QStringList hashes;    ///< every selected row, in view order
        QString currentHash;   ///< current/anchor row — drives the bottom tabs
        int scrollValue = 0;
    };

    void setupUi();
    QWidget* createTopSection();
    QWidget* createBottomTabs();
    void requestSharedFiles();
    void sendSetPriorityBatch(const QStringList& hashes, int priority, bool isAuto);
    void sendDeleteFilesBatch(const QStringList& hashes);   ///< confirms once, then deletes
    void sendUnshareBatch(const QStringList& hashes);       ///< confirms once, then unshares
    void updateStatsTab();
    void updateContentTab();
    void updateEd2kTab();
    [[nodiscard]] static bool isArchiveFile(const QString& fileType, const QString& fileName);
    void onReloadClicked();
    void showPriorityMenu();
    void showFindDialog();
    void copyEd2kLink();
    void copyEd2kLinks(const QStringList& hashes);
    void rebuildEd2kLink();
    void requestEd2kLinks(const QStringList& hashes, bool hashset, bool sourceHint, bool html,
                          std::function<void(const QStringList& links, bool hintAvailable)> apply);
    [[nodiscard]] const SharedFileRow* currentFile() const;
    [[nodiscard]] QStringList selectedHashes() const;
    [[nodiscard]] std::vector<const SharedFileRow*> rowsForHashes(const QStringList& hashes) const;
    [[nodiscard]] int computePopularityRank(int64_t value,
                                            int64_t (SharedFileRow::*field)) const;
    [[nodiscard]] SelectionState saveSelection() const;
    void restoreSelection(const SelectionState& state);
    void fetchAndShowSharedFileDetails(const QString& hash, int tab);

    /// Locate @p hash in the view (proxy coordinates); invalid when the folder
    /// filter hides it or the file has left the share.
    [[nodiscard]] QModelIndex fileIndexFor(const QString& hash) const;

    /// The detail dialog's Prev/Next walk over the shared-files list. Moving the
    /// view's current row is deliberate: it also drives the bottom Statistics /
    /// Content / eD2K tabs, exactly as MFC's EnsureVisible side effect does.
    [[nodiscard]] DetailWalker makeSharedFileWalker(const QString& hash);
    void sendShareDirsUpdate(const QStringList& dirs);
    static void collectSubdirectories(const QString& root, QStringList& list);

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
    QLabel* m_statPopularity2 = nullptr;
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

    // Content tab (archive preview / media info)
    QStackedWidget* m_contentStack = nullptr;
    ArchivePreviewPanel* m_archivePreview = nullptr;
    MediaInfoPanel* m_mediaInfoPanel = nullptr;

    // eD2K Links tab
    QTextEdit* m_ed2kText = nullptr;
    QPushButton* m_copyButton = nullptr;
    QGroupBox* m_ed2kBasicGroup = nullptr;
    QGroupBox* m_ed2kAdvancedGroup = nullptr;
    QCheckBox* m_ed2kSourceCheck = nullptr;
    QCheckBox* m_ed2kHtmlCheck = nullptr;
    QCheckBox* m_ed2kHashsetCheck = nullptr;
    QCheckBox* m_ed2kHostnameCheck = nullptr;
    int m_ed2kLinkGeneration = 0;   ///< discards GetEd2kLink replies overtaken by a newer request
    int m_ed2kTabIndex = -1;        ///< bottom-tab index of the eD2K Links page
    QStringList m_ed2kLastHashes;   ///< last hashes served, so an unchanged poll costs nothing
    int m_ed2kLastFlags = -1;       ///< hashset/hostname/html bitmask that went with them

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

    /// Periodic refetch, suspended while this panel is not the visible tab.
    PanelPoller* m_poller = nullptr;

    // Context menu
    QMenu* m_contextMenu = nullptr;

    // Cached incoming directory for filtering
    QString m_incomingDir;

    /// True while restoreSelection() drives the selection model, so the resulting
    /// currentChanged/selectionChanged storm does not re-run the bottom tabs.
    bool m_restoringSelection = false;

    /// File path the Content tab is currently showing — re-setting the same one would
    /// restart the media/archive scan on every poll.
    QString m_shownContentPath;

    // Filesystem tree helpers
    void populateFilesystemChildren(QTreeWidgetItem* parentItem);
    void addFilesystemChild(QTreeWidgetItem* parent, const QString& path,
                            const QString& displayName);
    [[nodiscard]] static bool hasSubdirectories(const QString& path);
    void initFilesystemRoot();
};

} // namespace eMule
