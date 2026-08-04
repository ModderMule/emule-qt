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

#include <cstdint>
#include <vector>

class QComboBox;
class QCompleter;
class QLabel;
class QLineEdit;
class QMenu;
class QPushButton;
class QSortFilterProxyModel;
class QSpinBox;
class QStringListModel;
class QTabBar;
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
    void onSearchResultPush();

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

    // Search controls
    QLineEdit* m_nameEdit = nullptr;
    QPushButton* m_startBtn = nullptr;
    QPushButton* m_cancelBtn = nullptr;
    QComboBox* m_typeCombo = nullptr;
    QComboBox* m_methodCombo = nullptr;
    QPushButton* m_resetBtn = nullptr;

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

    // Preview support
    QString m_streamToken;
    DownloadListModel* m_downloadModel = nullptr;

    // Autocomplete
    QCompleter* m_completer = nullptr;
    QStringListModel* m_historyModel = nullptr;
};

} // namespace eMule
