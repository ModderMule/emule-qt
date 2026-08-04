#pragma once

/// @file UiState.h
/// @brief GUI layout state — persisted to a separate uistate.yml file.
///
/// Keeps splitter positions, window geometry, header states and other
/// layout data in memory.  Reads/writes its own file so the daemon
/// cannot clobber GUI-only state when it saves preferences.yml.

#include <QByteArray>
#include <QHeaderView>
#include <QList>
#include <QMainWindow>
#include <QMap>
#include <QSet>
#include <QSplitter>
#include <QString>
#include <QTimer>
#include <QTreeWidget>

#include <memory>

namespace eMule {

class UiState {
public:
    /// Load saved state from {configDir}/uistate.yml.
    void load(const QString& configDir);

    /// Write current state to {configDir}/uistate.yml.
    void save(const QString& configDir);

    /// Write current state to the directory last passed to load(). No-op when
    /// load() was never called. Lets callers that don't carry the config dir
    /// (e.g. MainWindow::closeEvent) flush the state before the process ends.
    void save();

    /// Write the state a short while after the last change, so a session that
    /// never exits cleanly (crash, kill, logout) still keeps its layout. Called
    /// automatically by every bind* capture; coalesces bursts into one write.
    void scheduleSave();

    /// Restore a splitter from saved sizes, connect it to auto-update.
    void bindServerSplitter(QSplitter* splitter);
    void bindKadSplitter(QSplitter* splitter);
    void bindTransferSplitter(QSplitter* splitter);
    void bindSharedHorzSplitter(QSplitter* splitter);
    void bindSharedVertSplitter(QSplitter* splitter);
    void bindMessagesSplitter(QSplitter* splitter);
    void bindIrcSplitter(QSplitter* splitter);
    void bindStatsSplitter(QSplitter* splitter);

    /// Restore stats tree expansion state, connect to auto-capture on expand/collapse.
    /// Tracks root and first-level sub-items only.
    void bindStatsTree(QTreeWidget* tree);

    /// Restore a header view's column widths/sort order, connect it to auto-update.
    /// Also installs a selection guard on the owning view (see guardSelectionOnReset).
    /// Call this only once the view has a model — a header with no sections cannot
    /// take a restore, and QHeaderView::restoreState() rejects it on a column count
    /// mismatch.
    void bindHeaderView(QHeaderView* header, const QString& key);

    /// Re-apply the cached state for @p key. Call right after a setModel() on an
    /// already-bound view: setModel() clears every section, so the layout has to be
    /// pushed back once the new model's columns exist.
    void applyHeaderState(QHeaderView* header, const QString& key);

    /// Clear a view's selection/current index the moment its model begins a reset,
    /// before the (proxy) persistent-index mapping is torn down. Prevents a deferred
    /// QHeaderView::paintEvent from dereferencing a stale index in
    /// QSortFilterProxyModel::parent(). Safe to call repeatedly on the same view.
    void guardSelectionOnReset(QAbstractItemView* view);

    /// Restore the main window size. Call in the constructor before show().
    void bindMainWindow(QMainWindow* window);

    /// Capture current window geometry. Call from closeEvent().
    void captureMainWindow(QMainWindow* window);

    /// True if the window should be restored maximized.
    [[nodiscard]] bool isWindowMaximized() const { return m_windowMaximized; }

    /// Last selected options dialog page.
    [[nodiscard]] int optionsLastPage() const { return m_optionsLastPage; }
    void setOptionsLastPage(int page) { m_optionsLastPage = page; }

    /// Toolbar button order (empty = default).
    [[nodiscard]] const QList<int>& toolbarButtonOrder() const { return m_toolbarButtonOrder; }
    void setToolbarButtonOrder(const QList<int>& order) { m_toolbarButtonOrder = order; }

    /// Toolbar button style (Qt::ToolButtonStyle values 0-3).
    [[nodiscard]] int toolbarButtonStyle() const { return m_toolbarButtonStyle; }
    void setToolbarButtonStyle(int style) { m_toolbarButtonStyle = style; }

    /// Toolbar skin bitmap path (empty = default icons).
    [[nodiscard]] const QString& toolbarSkinPath() const { return m_toolbarSkinPath; }
    void setToolbarSkinPath(const QString& path) { m_toolbarSkinPath = path; }

    /// Skin profile INI path (empty = default, no skin).
    [[nodiscard]] const QString& skinProfilePath() const { return m_skinProfilePath; }
    void setSkinProfilePath(const QString& path) { m_skinProfilePath = path; }

private:
    /// Restore @p sizes into @p splitter and capture every later drag into it.
    /// Shared by all eight bind*Splitter() entry points.
    void bindSplitter(QSplitter* splitter, QList<int>& sizes);

    QList<int> m_serverSplitSizes;
    QList<int> m_kadSplitSizes;
    QList<int> m_transferSplitSizes;
    QList<int> m_sharedHorzSplitSizes;
    QList<int> m_sharedVertSplitSizes;
    QList<int> m_messagesSplitSizes;
    QList<int> m_ircSplitSizes;
    QList<int> m_statsSplitSizes;
    int  m_windowWidth     = 0;
    int  m_windowHeight    = 0;
    bool m_windowMaximized = false;
    int  m_optionsLastPage = 0;
    QList<int> m_toolbarButtonOrder;
    int  m_toolbarButtonStyle = 3;
    QString m_toolbarSkinPath;
    QString m_skinProfilePath;
    QMap<QString, QByteArray> m_headerStates;
    QSet<QString> m_statsTreeExpanded;
    QString m_configDir;   ///< Remembered by load() so save() can run without it.
    std::unique_ptr<QTimer> m_saveTimer;   ///< Debounce for scheduleSave().
};

/// Global UI state instance (GUI process only).
extern UiState theUiState;

} // namespace eMule
