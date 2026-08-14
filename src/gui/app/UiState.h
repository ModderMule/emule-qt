#pragma once

/// @file UiState.h
/// @brief GUI layout state — persisted to a separate uistate.yml file.
///
/// Keeps splitter positions, window geometry, header states and other
/// layout data in memory.  Reads/writes its own file so the daemon
/// cannot clobber GUI-only state when it saves preferences.yml.

#include <QByteArray>
#include <QColor>
#include <QHeaderView>
#include <QList>
#include <QMainWindow>
#include <QMap>
#include <QSet>
#include <QSplitter>
#include <QString>
#include <QTimer>
#include <QTreeWidget>

#include <array>
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

    /// The vertical splitter stacking the three statistics graphs. One QSplitter with
    /// three children holds both dividers, so this covers MFC's SplitterbarPositionStat_HL
    /// *and* _HR (srchybrid/StatisticsDlg.cpp:245-273).
    void bindStatsGraphSplitter(QSplitter* splitter);

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

    /// Epoch seconds of the last completed version check; 0 when none ever ran.
    ///
    /// GUI-only on purpose. The daemon never runs a version check, and it owns
    /// preferences.yml in the normal local setup — so a timestamp kept in thePrefs
    /// could only be persisted by pushing it over SetPreferences, whose handler ends
    /// in emit webServerConfigChanged() and restarts the web server. Recording a
    /// version check is not worth dropping every web session.
    [[nodiscard]] int64_t lastVersionCheck() const { return m_lastVersionCheck; }
    void setLastVersionCheck(int64_t secs)
    {
        m_lastVersionCheck = secs;
        scheduleSave();   // a session that never exits cleanly still keeps it
    }

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

    /// Number of statistics colours, in MFC's index order
    /// (srchybrid/Preferences.h:198 — m_adwStatsColors[15]).
    static constexpr int kStatsColorCount = 15;

    /// MFC's factory palette, indexed exactly as CPreferences::ResetStatsColor does
    /// (srchybrid/Preferences.cpp:1817-1821). Slot 11 (the tray meter bar) is an
    /// invalid QColor: MFC derives it from the taskbar's brightness at startup, and
    /// we follow the system colour scheme instead — see MainWindow::trayMeterColor().
    [[nodiscard]] static const std::array<QColor, kStatsColorCount>& defaultStatsColors();

    /// One statistics colour. Out-of-range indices return an invalid colour.
    [[nodiscard]] QColor statsColor(int index) const;

    [[nodiscard]] const std::array<QColor, kStatsColorCount>& statsColors() const
    {
        return m_statsColors;
    }

    /// Replace the whole palette (the options page edits a copy, then applies it).
    void setStatsColors(const std::array<QColor, kStatsColorCount>& colors);

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
    QList<int> m_statsGraphSplitSizes;
    int  m_windowWidth     = 0;
    int  m_windowHeight    = 0;
    bool m_windowMaximized = false;
    int  m_optionsLastPage = 0;
    int64_t m_lastVersionCheck = 0;
    QList<int> m_toolbarButtonOrder;
    int  m_toolbarButtonStyle = 3;
    QString m_toolbarSkinPath;
    QString m_skinProfilePath;
    std::array<QColor, kStatsColorCount> m_statsColors = defaultStatsColors();
    QMap<QString, QByteArray> m_headerStates;
    QSet<QString> m_statsTreeExpanded;
    QString m_configDir;   ///< Remembered by load() so save() can run without it.
    std::unique_ptr<QTimer> m_saveTimer;   ///< Debounce for scheduleSave().
};

/// Global UI state instance (GUI process only).
extern UiState theUiState;

} // namespace eMule
