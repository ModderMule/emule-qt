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
#include <QSize>
#include <QSplitter>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QTreeWidget>

#include <algorithm>
#include <array>
#include <memory>
#include <optional>

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
    /// The ed3k page of the Kad window: sizes of its own.
    void bindEd3kKadSplitter(QSplitter* splitter);
    void bindTransferSplitter(QSplitter* splitter);
    void bindSharedHorzSplitter(QSplitter* splitter);
    void bindSharedVertSplitter(QSplitter* splitter);
    void bindMessagesSplitter(QSplitter* splitter);
    void bindIrcSplitter(QSplitter* splitter);
    void bindStatsSplitter(QSplitter* splitter);
    /// Torrents tab: the list over the Peers/Trackers/Files pane.
    void bindTorrentSplitter(QSplitter* splitter);

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

    /// True once a layout is saved under @p key — false on a fresh install.
    /// Which of a column's two sort values is in use (AbstractListView::setSortValueColumn);
    /// the header blob carries only the column and the direction.
    [[nodiscard]] bool sortValue(const QString& key, int column, bool fallback) const
    {
        const auto it = m_sortValues.constFind(QStringLiteral("%1/%2").arg(key).arg(column));
        return it != m_sortValues.constEnd() ? *it : fallback;
    }
    void setSortValue(const QString& key, int column, bool second)
    {
        m_sortValues[QStringLiteral("%1/%2").arg(key).arg(column)] = second;
        scheduleSave();
    }

    [[nodiscard]] bool hasHeaderState(const QString& key) const
    {
        return !m_headerStates.value(key).isEmpty();
    }

    /// Section count of the saved layout under @p key, -1 when there is none.
    /// Columns past it were added since, and restoreState() leaves them visible.
    [[nodiscard]] int headerStateSectionCount(const QString& key) const;

    /// QHeaderView::restoreState() with a fix for layouts saved with fewer columns
    /// than the model now has: Qt appends the new sections but keeps the old total
    /// length, so they can't be dragged and the next saveState() is rejected (#61).
    static bool restoreHeaderState(QHeaderView* header, const QByteArray& state);

    /// Cache @p header's current state under @p key and schedule a save. For
    /// changes no header signal reports (show/hide a column).
    void captureHeaderState(QHeaderView* header, const QString& key);

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

    /// Size the user left dialog @p key at; invalid when never resized.
    /// See RememberedSizeDialog -- the key is its class name.
    [[nodiscard]] QSize dialogSize(const QString& key) const { return m_dialogSizes.value(key); }
    void setDialogSize(const QString& key, QSize size);

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

    /// Claim .nzb with the desktop at every start, and give it up when off.
    ///
    /// GUI-only, and here rather than in preferences.yml for exactly the reason
    /// lastVersionCheck is: the daemon owns that file and knows nothing about
    /// the desktop it is not running on. It is also the *GUI's* executable path
    /// that gets registered, which the daemon could not supply.
    [[nodiscard]] bool associateNzbFiles() const { return m_associateNzbFiles; }
    void setAssociateNzbFiles(bool on)
    {
        m_associateNzbFiles = on;
        scheduleSave();
    }
    /// Claim .torrent and magnet: the same way (Windows, Linux).
    [[nodiscard]] bool associateTorrentFiles() const { return m_associateTorrentFiles; }
    void setAssociateTorrentFiles(bool on)
    {
        m_associateTorrentFiles = on;
        scheduleSave();
    }

    /// Claim .dlc link containers for the Hoster tab (Windows, Linux). Off by default:
    /// JDownloader usually owns them.
    [[nodiscard]] bool associateDlcFiles() const { return m_associateDlcFiles; }
    void setAssociateDlcFiles(bool on)
    {
        m_associateDlcFiles = on;
        scheduleSave();
    }

    /// Click'n'Load: take links that pages post to 127.0.0.1:9666 into the Hoster tab
    [[nodiscard]] bool clickNLoad() const { return m_clickNLoad; }
    void setClickNLoad(bool on)
    {
        m_clickNLoad = on;
        scheduleSave();
    }
    /// Click'n'Load port, uistate.yml only (no GUI); 9666 is what pages post to
    [[nodiscard]] quint16 clickNLoadPort() const { return m_clickNLoadPort; }

    /// Claim ed2k: and ed3k: links (Windows, Linux). Unset in configs from before
    /// it was stored: savedFileTypes() (Ed2kSchemeHandler) derives it then.
    [[nodiscard]] std::optional<bool> associateEd2kLinks() const { return m_associateEd2kLinks; }
    void setAssociateEd2kLinks(bool on)
    {
        m_associateEd2kLinks = on;
        scheduleSave();
    }

    /// What applyDesktopIntegration() last claimed, bits 1 nzb, 2 torrent, 4 ed2k, 8 dlc.
    /// Unset = never applied. The desktop is only touched when the wanted set differs (#121).
    [[nodiscard]] std::optional<int> appliedFileTypes() const { return m_appliedFileTypes; }
    void setAppliedFileTypes(int bits)
    {
        m_appliedFileTypes = bits;
        scheduleSave();
    }
    /// Linux application menu entry (wizard). Unset = never chosen: the default profile
    /// keeps what is there, a --config profile writes none (#153)
    [[nodiscard]] std::optional<bool> menuEntry() const { return m_menuEntry; }
    void setMenuEntry(bool on)
    {
        m_menuEntry = on;
        scheduleSave();
    }

    /// Clicked eD2K links go straight to Transfers, no "Do you want to download…" (#116)
    [[nodiscard]] bool addLinksWithoutConfirm() const { return m_addLinksWithoutConfirm; }
    void setAddLinksWithoutConfirm(bool on)
    {
        m_addLinksWithoutConfirm = on;
        scheduleSave();
    }

    /// GUI language code, "" = system default. GUI-only: a core with its own
    /// config dir (Docker, remote) keeps preferences.yml out of reach (#75).
    /// Unset until first chosen here; then @p fallback (preferences.yml) applies.
    [[nodiscard]] QString language(const QString& fallback) const
    {
        return m_language.value_or(fallback);
    }
    void setLanguage(const QString& code)
    {
        m_language = code;
        scheduleSave();
    }

    /// MiniMule closes itself three seconds after the pointer left it. Off, as in MFC,
    /// where it is the hidden "MiniMuleAutoClose" key (MiniMule.cpp:115). No option
    /// page offers it; set it in uistate.yml.
    [[nodiscard]] bool miniMuleAutoClose() const { return m_miniMuleAutoClose; }

    /// First start wizard was shown once. Missing key = false, so an upgraded
    /// install gets it once too. Tools > wizard still opens it any time.
    [[nodiscard]] bool firstStartWizardDone() const { return m_firstStartWizardDone; }
    void setFirstStartWizardDone(bool done)
    {
        if (m_firstStartWizardDone == done)
            return;
        m_firstStartWizardDone = done;
        scheduleSave();
    }

    /// GUI starts so far, this one included. The Low ID hint waits for the third.
    [[nodiscard]] int startCount() const { return m_startCount; }
    void countStart()
    {
        ++m_startCount;
        scheduleSave();
    }

    /// "Don't show this again" on the Low ID hint. One-way: save() keeps a true
    /// already on disk, so another GUI window's stale copy cannot undo it (#146).
    [[nodiscard]] bool lowIdHintDisabled() const { return m_lowIdHintDisabled; }
    void setLowIdHintDisabled(bool disabled)
    {
        if (m_lowIdHintDisabled == disabled)
            return;
        m_lowIdHintDisabled = disabled;
        save();   // now, not debounced: the session may not exit cleanly
    }

    /// Country flag next to the client/server icon (IP2Country). Display-only.
    [[nodiscard]] bool showCountryFlags() const { return m_showCountryFlags; }
    void setShowCountryFlags(bool on)
    {
        m_showCountryFlags = on;
        scheduleSave();
    }

    /// Download progress on the taskbar button / Dock (MFC ShowWin7TaskbarGoodies).
    [[nodiscard]] bool showTaskbarProgress() const { return m_showTaskbarProgress; }
    void setShowTaskbarProgress(bool on)
    {
        m_showTaskbarProgress = on;
        scheduleSave();
    }

    /// Status filter row above the Transfers downloads, the Torrents and the Hoster list.
    [[nodiscard]] bool showDownloadStatusFilter() const { return m_showDownloadStatusFilter; }
    void setShowDownloadStatusFilter(bool on)
    {
        m_showDownloadStatusFilter = on;
        scheduleSave();
    }
    /// Downloads list: the columns sorted before break ties of the current one (#84, #174)
    [[nodiscard]] bool downloadSortHistory() const { return m_downloadSortHistory; }
    void setDownloadSortHistory(bool on)
    {
        m_downloadSortHistory = on;
        scheduleSave();
    }
    /// Tie-breakers, newest first
    [[nodiscard]] const QList<std::pair<int, Qt::SortOrder>>& downloadSortHistoryKeys() const
    {
        return m_downloadSortHistoryKeys;
    }
    void setDownloadSortHistoryKeys(const QList<std::pair<int, Qt::SortOrder>>& keys)
    {
        if (keys == m_downloadSortHistoryKeys)
            return;
        m_downloadSortHistoryKeys = keys;
        scheduleSave();
    }
    [[nodiscard]] bool showTorrentStatusFilter() const { return m_showTorrentStatusFilter; }
    void setShowTorrentStatusFilter(bool on)
    {
        m_showTorrentStatusFilter = on;
        scheduleSave();
    }
    [[nodiscard]] bool showHosterStatusFilter() const { return m_showHosterStatusFilter; }
    void setShowHosterStatusFilter(bool on)
    {
        m_showHosterStatusFilter = on;
        scheduleSave();
    }
    /// Transfers tab: the downloads' status filter (CategoryFilterProxy::StatusFilter).
    [[nodiscard]] int downloadStatusFilter() const { return m_downloadStatusFilter; }
    void setDownloadStatusFilter(int filter)
    {
        if (m_downloadStatusFilter == filter)
            return;
        m_downloadStatusFilter = filter;
        scheduleSave();
    }

    /// Search Details graph switches to the active Kad search by itself (MFC AutoShowLookups).
    [[nodiscard]] bool kadAutoShowLookups() const { return m_kadAutoShowLookups; }
    void setKadAutoShowLookups(bool on)
    {
        if (m_kadAutoShowLookups == on)
            return;
        m_kadAutoShowLookups = on;
        scheduleSave();
    }

    /// The command bar beside the download list (MFC ShowDownloadToolbar).
    [[nodiscard]] bool showDownloadToolbar() const { return m_showDownloadToolbar; }
    void setShowDownloadToolbar(bool on)
    {
        m_showDownloadToolbar = on;
        scheduleSave();
    }

    /// Theme: 0 = follow the system, 1 = light, 2 = dark. Not in MFC (issue #18).
    [[nodiscard]] int colorScheme() const { return m_colorScheme; }
    void setColorScheme(int scheme)
    {
        m_colorScheme = std::clamp(scheme, 0, 2);
        scheduleSave();
    }

    /// App font size in percent of the system default, 80-200. Applied at start only.
    [[nodiscard]] int fontScalePercent() const { return m_fontScalePercent; }
    void setFontScalePercent(int percent)
    {
        m_fontScalePercent = std::clamp(percent, 80, 200);
        scheduleSave();
    }

    /// Toolbar speed graph background: 0 = dark (eMule), 1 = light, 2 = follow theme (#140).
    [[nodiscard]] int speedGraphBackground() const { return m_speedGraphBackground; }
    void setSpeedGraphBackground(int mode)
    {
        m_speedGraphBackground = std::clamp(mode, 0, 2);
        scheduleSave();
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

    /// Where the last .torrent/.nzb from a search result was saved (empty = Downloads).
    [[nodiscard]] const QString& metaFileSaveDir() const { return m_metaFileSaveDir; }
    void setMetaFileSaveDir(const QString& dir)
    {
        m_metaFileSaveDir = dir;
        scheduleSave();
    }

    /// Torrents tab: the state filter (TorrentFilterProxy::Filter) and the pane's tab.
    [[nodiscard]] int torrentFilter() const { return m_torrentFilter; }
    void setTorrentFilter(int filter)
    {
        if (m_torrentFilter == filter)
            return;
        m_torrentFilter = filter;
        scheduleSave();
    }
    /// Hoster tab: the state filter (HosterPanel's Filter).
    [[nodiscard]] int hosterFilter() const { return m_hosterFilter; }
    void setHosterFilter(int filter)
    {
        if (m_hosterFilter == filter)
            return;
        m_hosterFilter = filter;
        scheduleSave();
    }
    [[nodiscard]] int torrentPaneTab() const { return m_torrentPaneTab; }
    void setTorrentPaneTab(int tab)
    {
        if (m_torrentPaneTab == tab)
            return;
        m_torrentPaneTab = tab;
        scheduleSave();
    }

    /// Search list: show what a server found on other networks.
    [[nodiscard]] bool searchShowUsenet() const { return m_searchShowUsenet; }
    [[nodiscard]] bool searchShowKad() const { return m_searchShowKad; }
    [[nodiscard]] bool searchShowTorrent() const { return m_searchShowTorrent; }
    [[nodiscard]] bool searchShowWeb() const { return m_searchShowWeb; }
    /// Search list: a release's file rows only when their name holds the keywords.
    [[nodiscard]] bool searchHideRelatedContent() const { return m_searchHideRelatedContent; }
    void setSearchHideRelatedContent(bool on)
    {
        if (m_searchHideRelatedContent == on)
            return;
        m_searchHideRelatedContent = on;
        scheduleSave();
    }
    void setSearchShowNetworks(bool usenet, bool kad, bool torrent, bool web)
    {
        m_searchShowUsenet = usenet;
        m_searchShowKad = kad;
        m_searchShowTorrent = torrent;
        m_searchShowWeb = web;
        scheduleSave();
    }

    /// Search method used last, as a SearchType value; @p fallback before the first search.
    [[nodiscard]] int searchLastMethod(int fallback) const
    {
        return m_searchLastMethod >= 0 ? m_searchLastMethod : fallback;
    }
    void setSearchLastMethod(int type)
    {
        if (m_searchLastMethod == type)
            return;
        m_searchLastMethod = type;
        scheduleSave();
    }

    /// Search box auto completion history, newest first.
    [[nodiscard]] const QStringList& searchHistory() const { return m_searchHistory; }
    void setSearchHistory(const QStringList& history)
    {
        m_searchHistory = history;
        scheduleSave();
    }

    /// Lists shown in the transfer window's two panes (TransferPanel view ids);
    /// @p fallback when never saved.
    [[nodiscard]] int transferTopView(int fallback) const
    {
        return m_transferTopView >= 0 ? m_transferTopView : fallback;
    }
    [[nodiscard]] int transferBottomView(int fallback) const
    {
        return m_transferBottomView >= 0 ? m_transferBottomView : fallback;
    }
    void setTransferViews(int top, int bottom)
    {
        if (m_transferTopView == top && m_transferBottomView == bottom)
            return;
        m_transferTopView = top;
        m_transferBottomView = bottom;
        scheduleSave();
    }

    /// Number of statistics colours, in MFC's index order
    /// (srchybrid/Preferences.h:198 — m_adwStatsColors[15]), plus our own slots
    /// appended after MFC's: 15 is the Download graph's Usenet line, 16/17 the
    /// Download/Upload graphs' BitTorrent lines.
    static constexpr int kStatsColorCount = 20;   // 18, 19: ed3k down/up (not in MFC)

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

    /// One-time move of the keys older builds kept in the platform QSettings store
    /// (~/.config/eMule/GUI.conf, a macOS plist, the registry), which ignored --config.
    void importLegacySettings();

    /// @p state with its total length set to the sum of its section sizes.
    /// Unchanged when it already matches or can't be parsed.
    static QByteArray repairedHeaderState(const QByteArray& state);

    QList<int> m_serverSplitSizes;
    QList<int> m_kadSplitSizes;
    QList<int> m_ed3kKadSplitSizes;
    QList<int> m_transferSplitSizes;
    QList<int> m_sharedHorzSplitSizes;
    QList<int> m_sharedVertSplitSizes;
    QList<int> m_messagesSplitSizes;
    QList<int> m_ircSplitSizes;
    QList<int> m_statsSplitSizes;
    QList<int> m_torrentSplitSizes;
    int m_torrentFilter = 0;
    int m_torrentPaneTab = 0;
    int m_hosterFilter = 0;
    QList<int> m_statsGraphSplitSizes;
    int  m_windowWidth     = 0;
    int  m_windowHeight    = 0;
    bool m_windowMaximized = false;
    int  m_optionsLastPage = 0;
    int64_t m_lastVersionCheck = 0;
    bool m_associateNzbFiles = true;
    bool m_associateTorrentFiles = true;
    bool m_associateDlcFiles = false;
    bool m_clickNLoad = true;
    quint16 m_clickNLoadPort = 9666;
    std::optional<bool> m_associateEd2kLinks;
    std::optional<int> m_appliedFileTypes;
    std::optional<bool> m_menuEntry;
    bool m_addLinksWithoutConfirm = false;
    bool m_miniMuleAutoClose = false;
    bool m_firstStartWizardDone = false;
    std::optional<QString> m_language;   // unset = take preferences.yml's
    int m_startCount = 0;
    bool m_lowIdHintDisabled = false;
    bool m_showCountryFlags = true;
    bool m_showTaskbarProgress = true;
    bool m_showDownloadStatusFilter = false;
    bool m_downloadSortHistory = false;
    QList<std::pair<int, Qt::SortOrder>> m_downloadSortHistoryKeys;
    bool m_showTorrentStatusFilter = true;
    bool m_showHosterStatusFilter = true;
    int m_downloadStatusFilter = 0;
    bool m_showDownloadToolbar = true;
    bool m_kadAutoShowLookups = true;
    int  m_colorScheme = 0;
    int  m_fontScalePercent = 100;
    int  m_speedGraphBackground = 0;
    QList<int> m_toolbarButtonOrder;
    int  m_toolbarButtonStyle = 3;
    QString m_toolbarSkinPath;
    QString m_skinProfilePath;
    QString m_metaFileSaveDir;
    bool m_searchShowUsenet = true;
    bool m_searchShowKad = true;
    bool m_searchShowTorrent = true;
    bool m_searchShowWeb = true;
    bool m_searchHideRelatedContent = true;
    int  m_searchLastMethod = -1;     ///< -1 = never saved
    QStringList m_searchHistory;
    int  m_transferTopView = -1;      ///< -1 = never saved
    int  m_transferBottomView = -1;
    std::array<QColor, kStatsColorCount> m_statsColors = defaultStatsColors();
    QMap<QString, QByteArray> m_headerStates;
    QMap<QString, bool> m_sortValues;
    QMap<QString, QSize> m_dialogSizes;
    QSet<QString> m_statsTreeExpanded;
    QString m_configDir;   ///< Remembered by load() so save() can run without it.
    std::unique_ptr<QTimer> m_saveTimer;   ///< Debounce for scheduleSave().
};

/// Global UI state instance (GUI process only).
extern UiState theUiState;

} // namespace eMule
