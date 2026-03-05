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

class IpcClient;
class SearchResultsModel;

/// Per-search tab state.
struct SearchTab {
    uint32_t searchID = 0;
    QString title;
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
    void startSearchFromExternal(const QString& expression);

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
    void setupUi();
    QWidget* createSearchBar();
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
    QTreeView* m_resultView = nullptr;
    QLabel* m_statusLabel = nullptr;
    QPushButton* m_downloadBtn = nullptr;
    QPushButton* m_closeAllBtn = nullptr;

    // Context menu
    QMenu* m_contextMenu = nullptr;

    // Per-tab state
    std::vector<SearchTab> m_tabs;

    // IPC
    IpcClient* m_ipc = nullptr;

    // Autocomplete
    QCompleter* m_completer = nullptr;
    QStringListModel* m_historyModel = nullptr;
};

} // namespace eMule
