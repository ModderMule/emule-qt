#pragma once

/// @file SearchPanel.h
/// @brief Search tab panel replicating the MFC Search window layout.
///
/// Layout (matching MFC Search screenshots):
///   - Row 1: "Name:" text field
///   - Row 2: Type dropdown + Method dropdown + Reset button
///   - Right column: Start button, scrollable filter area, Cancel button
///   - Tab bar for multiple concurrent searches
///   - Results tree view with sortable columns
///   - Download button at bottom

#include <QWidget>

#include "controls/AbstractListView.h"
#include "dialogs/SearchDetailDialog.h"

#include <QSet>

#include <cstdint>
#include <vector>

class QComboBox;
class QCompleter;
class QLabel;
class QLineEdit;
class QMenu;
class QProgressBar;
class QPushButton;
class QSortFilterProxyModel;
class QSpinBox;
class QStringListModel;
class QTabBar;
class QTimer;
class QTreeView;

namespace eMule {

class DownloadListModel;
class IpcClient;
class SearchResultsModel;

/// Per-search tab state.
struct SearchTab {
    uint32_t searchID = 0;
    QString title;
    int method = 0;  ///< Search method: 0=auto, 1=server, 2=global, 3=kad
    SearchResultsModel* model = nullptr;
    QSortFilterProxyModel* proxy = nullptr;
};

/// Full Search tab page matching the MFC eMule Search window.
class SearchPanel : public QWidget {
    Q_OBJECT

public:
    explicit SearchPanel(QWidget* parent = nullptr);
    ~SearchPanel() override;

    /// Connect this panel to the IPC client for data updates.
    void setIpcClient(IpcClient* client);

    /// Start a search from an external panel (e.g., "Search Related Files").
    ///
    /// With no overrides this fills the search box and runs it exactly as if the user had
    /// pressed Start, so the current filter settings apply. Passing any override instead
    /// sends a fresh request that ignores the filter UI and leaves it untouched — the Qt
    /// equivalent of MFC's StartSearch(SSearchParams*), used by "Search Author's
    /// Collections…" so a stale filter cannot silently discard every hit.
    ///
    /// @param fileType  ED2KFTSTR_* value; empty keeps the type combo's choice.
    /// @param method    SearchType value (3 = Kademlia); -1 keeps the method combo's choice.
    /// @param tabTitle  Caption for the results tab; empty uses @p expression.
    ///                  MFC calls this strSpecialTitle.
    void startSearchFromExternal(const QString& expression,
                                 const QString& fileType = {},
                                 int method = -1,
                                 const QString& tabTitle = {});

    /// Set the stream token for preview streaming (received from daemon GetStats).
    void setStreamToken(const QString& token) { m_streamToken = token; }

    /// Set the download model for preview-eligibility checks.
    void setDownloadModel(DownloadListModel* model) { m_downloadModel = model; }

private slots:
    void onStartSearch();
    void onCancelSearch();
    void onResetFilters();
    void onTabChanged(int index);
    void onTabCloseRequested(int index);
    void onResultContextMenu(const QPoint& pos);
    void onResultDoubleClicked(const QModelIndex& index);
    void onSearchResultPush(const Ipc::IpcMessage& msg);

protected:
    void showEvent(QShowEvent* event) override;

private:
    /// One StartSearch request. Mirrors the daemon's SearchParams field order.
    struct SearchRequest {
        QString expression;
        QString fileType;
        int method = 0;          ///< SearchType value, not the combo index
        qint64 minSize = 0;
        qint64 maxSize = 0;
        int avail = 0;
        QString extension;
        int completeSources = 0;
        QString codec;
        int minBitrate = 0;
        int minLength = 0;
        QString title;
        QString album;
        QString artist;
        QString tabTitle;        ///< empty → use expression (MFC's strSpecialTitle)
    };

    [[nodiscard]] SearchRequest requestFromUi() const;
    void sendSearchRequest(const SearchRequest& req);

    void setupUi();
    QWidget* createSearchBar();
    void setupResultHeader();
    void requestSearchResults(uint32_t searchID);
    void downloadResult(int row);
    [[nodiscard]] QString buildEd2kLink(int proxyRow);
    void copyEd2kLink(int row);
    void closeSearch(int tabIndex);
    void closeAllSearches();
    void switchToTab(int index);
    void updateDownloadButton();
    [[nodiscard]] SearchTab* currentTab();
    [[nodiscard]] QString saveSelection() const;
    void restoreSelection(const QString& key);
    void saveSearches();
    void loadSearches();
    void setupAutoComplete();
    void addToSearchHistory(const QString& expression);
    void sendPreview(const QString& hash);
    void refreshKnownTypes();

    /// Open the MFC-style search-result detail sheet for @p hash in tab @p searchID.
    void fetchAndShowSearchDetails(uint32_t searchID, const QString& hash,
                                   SearchDetailDialog::Page page);

    /// Locate @p hash in the results view (proxy coordinates), or an invalid index
    /// when the tab no longer holds it — including after the user switched tabs,
    /// which swaps the view's model out from under an open dialog.
    [[nodiscard]] QModelIndex resultIndexFor(uint32_t searchID, const QString& hash);

    /// The detail dialog's Prev/Next walk over one search tab's results.
    [[nodiscard]] DetailWalker makeSearchWalker(uint32_t searchID, const QString& hash);

    /// Refetch every search whose results changed since the last drain. No-op while
    /// the panel is hidden; showEvent() drains what accumulated.
    void drainDirtySearches();

    /// Show the global-sweep progress bar only when the tab on screen is the search
    /// doing the sweeping. Called on progress pushes and on every tab switch.
    void updateSweepProgress();


    // Search controls
    QLineEdit* m_nameEdit = nullptr;
    QPushButton* m_startBtn = nullptr;
    QPushButton* m_cancelBtn = nullptr;
    QComboBox* m_typeCombo = nullptr;
    QComboBox* m_methodCombo = nullptr;
    QPushButton* m_resetBtn = nullptr;
    QProgressBar* m_sweepProgress = nullptr;

    // Live ED2K global (UDP) sweep, as reported by PushGlobalSearchProgress.
    uint32_t m_sweepSearchID = 0;   ///< 0 = no sweep running
    int m_sweepAsked = 0;
    int m_sweepTotal = 0;

    // Filter area (collapsible)
    QWidget* m_filterWidget = nullptr;
    QSpinBox* m_minSizeSpin = nullptr;
    QSpinBox* m_maxSizeSpin = nullptr;
    QSpinBox* m_availSpin = nullptr;
    QSpinBox* m_completeSpin = nullptr;
    QLineEdit* m_extensionEdit = nullptr;
    QLineEdit* m_codecEdit = nullptr;
    QSpinBox* m_minBitrateSpin = nullptr;
    QSpinBox* m_minLengthSpin = nullptr;
    QLineEdit* m_titleEdit = nullptr;
    QLineEdit* m_albumEdit = nullptr;
    QLineEdit* m_artistEdit = nullptr;

    // Tab bar + results
    QTabBar* m_tabBar = nullptr;
    ListTreeView* m_resultView = nullptr;
    QLabel* m_statusLabel = nullptr;
    QPushButton* m_downloadBtn = nullptr;
    QPushButton* m_closeAllBtn = nullptr;

    // Context menu
    QMenu* m_contextMenu = nullptr;

    // Per-tab state
    std::vector<SearchTab> m_tabs;

    /// True once the results header has columns and is bound to UiState.
    /// All tabs share m_resultView, so the layout is bound only once.
    bool m_headerBound = false;

    // IPC
    IpcClient* m_ipc = nullptr;

    /// Coalescing window for result refreshes. The daemon pushes one event per
    /// arriving result *and* per source-count bump, and a refresh refetches the
    /// tab's entire result list — so refreshing per push is quadratic in the
    /// result count. A Kad search for "video" produced 1500 pushes over a
    /// 1262-result list in ~10s: 363 MB of CBOR and 1500 model resets, which
    /// starved every other reply on the socket for over a second at a time.
    QTimer* m_resultRefreshTimer = nullptr;

    /// Search IDs that have pending pushes, drained by m_resultRefreshTimer.
    QSet<uint32_t> m_dirtySearchIDs;

    // Preview support
    QString m_streamToken;
    DownloadListModel* m_downloadModel = nullptr;

    // Autocomplete
    QCompleter* m_completer = nullptr;
    QStringListModel* m_historyModel = nullptr;
};

} // namespace eMule
