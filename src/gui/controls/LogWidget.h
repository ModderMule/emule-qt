#pragma once

/// @file LogWidget.h
/// @brief Tabbed log display matching the MFC "Server Info | Log | Verbose" tabs,
/// plus Leecher, Usenet, Torrent, Hoster, Kad, the three ed3k tabs and IPC.
///
/// Installs a Qt message handler to capture qCInfo/qCWarning/qCDebug output
/// from the core logging categories and routes them to the appropriate tabs.

#include "server/ServerMsgType.h"

#include <QSet>
#include <QWidget>

#include <cstdint>

class QTabBar;
class QStackedWidget;
class QTextBrowser;

namespace eMule {

class LogTextView;

/// Tabbed log widget with Server Info, Log, Verbose, Leecher, Usenet, Torrent, Torrent Verbose,
/// Hoster, Hoster Verbose, Kad, ed3k, ed3k Verbose, ed3k Kad and IPC tabs. Captures Qt logging
/// category output from the core layer. The Leecher tab follows antiLeecher, the Usenet tab
/// usenetEnabled + showUsenetLog, the Torrent tab btEnabled, the Hoster tab hosterDownloadsOn, the
/// ed3k tabs ed3kEnabled, the IPC tab enableIpcLog. A network's Verbose tab also needs its switch
/// (btVerboseLog, hosterVerboseLog, ed3kVerboseLog); debug lines are dropped while it is hidden.
class LogWidget : public QWidget {
    Q_OBJECT

public:
    explicit LogWidget(QWidget* parent = nullptr);
    ~LogWidget() override;

    /// Append one or more lines to the Server Info tab.
    ///
    /// Takes PLAIN TEXT — escaping, link detection and colouring happen here. The
    /// pane is a QTextBrowser, so `append()` parses its argument as HTML: callers
    /// must not pre-format, or a server message containing '<' or '&' is swallowed
    /// (or injects markup) and embedded newlines collapse onto a single line.
    ///
    /// Unlike the log tabs this one carries no timestamp, mirroring the reference's
    /// CemuleDlg::AddServerMessageLine (srchybrid/EmuleDlg.cpp:961-972).
    void appendServerInfo(const QString& text, ServerMsgType type = ServerMsgType::Info);

    /// A run of append*() calls follows as one batch: hold the repaints until
    /// endBatch(). Nests.
    void beginBatch();
    void endBatch();

    /// Append a message to the Log tab.
    /// If @p ts is non-empty it is used as the timestamp; otherwise current time.
    /// @p seqId orders entries (epoch seconds); 0 = use current time.
    void appendLog(const QString& msg, const QString& ts = {}, qint64 seqId = 0);

    /// Append a message to the Verbose tab.
    void appendVerbose(const QString& msg, const QString& ts = {}, qint64 seqId = 0);

    /// Append a message to the Leecher tab (emule.leecher: bans, anti-leecher hits).
    void appendLeecher(const QString& msg, const QString& ts = {}, qint64 seqId = 0);

    /// Route one emule.leecher line: to the Leecher tab when it is shown, else to Verbose
    /// (non-DLP bans still log while DLP is off).
    void routeLeecher(const QString& colored, const QString& ts = {}, qint64 seqId = 0);

    /// Append a message to the Kad tab.
    void appendKad(const QString& msg, const QString& ts = {}, qint64 seqId = 0);

    /// Append a message to the Usenet tab.
    void appendUsenet(const QString& msg, const QString& ts = {}, qint64 seqId = 0);

    /// Route one emule.usenet line: to the Usenet tab when shown, else to
    /// Log/Verbose by severity. Debug lines are dropped when hidden, they would
    /// flood Verbose.
    void routeUsenet(QtMsgType type, const QString& colored,
                     const QString& ts = {}, qint64 seqId = 0);

    /// Append a message to the Torrent tab (libtorrent alerts).
    void appendTorrent(const QString& msg, const QString& ts = {}, qint64 seqId = 0);

    /// Append a debug line to the Torrent Verbose tab.
    void appendTorrentVerbose(const QString& msg, const QString& ts = {}, qint64 seqId = 0);

    /// Route one emule.torrent line: debug to Torrent Verbose (or dropped), the rest to the
    /// Torrent tab when shown, else like routeUsenet().
    void routeTorrent(QtMsgType type, const QString& colored,
                      const QString& ts = {}, qint64 seqId = 0);

    /// Append a message to the Hoster tab.
    void appendHoster(const QString& msg, const QString& ts = {}, qint64 seqId = 0);

    /// Append a debug line to the Hoster Verbose tab.
    void appendHosterVerbose(const QString& msg, const QString& ts = {}, qint64 seqId = 0);

    /// Route one emule.hoster line: debug to Hoster Verbose (or dropped), the rest to the
    /// Hoster tab when shown, else to Log/Verbose by severity.
    void routeHoster(QtMsgType type, const QString& colored,
                     const QString& ts = {}, qint64 seqId = 0);

    /// The ed3k tabs: emule.ed3k (debug lines in ed3k Verbose) and emule.ed3k.kad.
    void appendEd3k(const QString& msg, const QString& ts = {}, qint64 seqId = 0);
    void appendEd3kVerbose(const QString& msg, const QString& ts = {}, qint64 seqId = 0);
    void appendEd3kKad(const QString& msg, const QString& ts = {}, qint64 seqId = 0);

    /// Route one line of an emule.ed3k* category (@p kad: emule.ed3k.kad): to its ed3k
    /// tab when they are shown, else to Log/Verbose by severity (debug lines dropped).
    void routeEd3k(bool kad, QtMsgType type, const QString& colored,
                   const QString& ts = {}, qint64 seqId = 0);

    /// Append an IPC message to the IPC tab.
    /// @p outgoing: true = GUI→daemon (green), false = daemon→GUI (purple).
    void appendIpcMessage(const QString& msg, bool outgoing);

    /// Show or hide the IPC tab.
    void setIpcTabVisible(bool visible);

    /// Show or hide the Usenet tab.
    void setUsenetTabVisible(bool visible);

    /// Show or hide the Torrent tab; Torrent Verbose needs @p verbose too.
    void setTorrentTabVisible(bool visible, bool verbose);

    /// Show or hide the Hoster tab; Hoster Verbose needs @p verbose too.
    void setHosterTabVisible(bool visible, bool verbose);

    /// Show or hide the Leecher tab (follows antiLeecher).
    void setLeecherTabVisible(bool visible);

    /// Show or hide the three ed3k tabs; ed3k Verbose needs @p verbose too.
    void setEd3kTabsVisible(bool visible, bool verbose);

    /// Clear all tabs.
    void clearAll();

    /// Plain text accessors for bug report submission.
    [[nodiscard]] QString logText() const;
    [[nodiscard]] QString verboseText() const;
    [[nodiscard]] QString leecherText() const;
    [[nodiscard]] QString kadText() const;
    [[nodiscard]] QString usenetText() const;
    [[nodiscard]] QString torrentText() const;
    [[nodiscard]] QString torrentVerboseText() const;
    [[nodiscard]] QString hosterText() const;
    [[nodiscard]] QString hosterVerboseText() const;
    [[nodiscard]] QString ed3kText() const;
    [[nodiscard]] QString ed3kVerboseText() const;
    [[nodiscard]] QString ed3kKadText() const;

    /// Set a custom font on all log browser tabs.
    void setCustomFont(const QFont& font);

    /// Install the global message handler to capture core log output.
    void installMessageHandler();

    /// Remove the global message handler.
    void removeMessageHandler();

    /// Open or close the GUI's own log files (emuleqt.log, emuleqt_Verbose.log,
    /// emuleqt_Kad.log, _Usenet, _Ed3k, _Ed3kKad, _Hoster, _Torrent and _Leecher in the config directory) to match the logToDiskGui
    /// pref. The daemon runs DaemonApp::applyLogFileSettings() for its own set
    /// — one switch per process. Safe to call at startup and whenever the pref
    /// changes at runtime.
    static void applyLogFileSettings();

signals:
    /// A link in one of the panes was clicked. Emitted instead of letting
    /// QTextBrowser open it, so ed2k:// can be routed to the in-app importer
    /// rather than handed to the OS.
    ///
    /// Carries the link as PLAIN TEXT, not a QUrl: an eD2K link is not a
    /// representable QUrl, and one built from it stringifies back to an empty
    /// string (see TextLinks.h).
    void linkActivated(const QString& link);

private:
    /// Insert a formatted log line into @p browser in sequence-order.
    /// If seqId >= the last entry's seqId, appends (fast path).
    /// Otherwise binary-searches for the correct position.
    void insertSorted(LogTextView* browser, QList<qint64>& seqIds,
                      qint64 seqId, const QString& html);

    int m_batchDepth = 0;   ///< open beginBatch() calls

    /// A timestamped line into a sorted tab.
    void appendTo(LogTextView* browser, QList<qint64>& seqIds, const QString& msg,
                  const QString& ts, qint64 seqId);
    /// A tab with its own browser; its index.
    int addLogTab(LogTextView*& browser, const QString& icon, const QString& title);

    /// Remove oldest lines from @p browser if it exceeds the configured limit.
    static void trimToLimit(LogTextView* browser, QList<qint64>& seqIds);
    static void trimToLimit(LogTextView* browser);

    /// Write the startup banner (version + version-check link) into the Server
    /// Info pane, as the reference does in CServerWnd::OnInitDialog.
    void writeServerInfoBanner();

    /// @p browser got text: if its tab is not the selected one, mark it unseen
    /// and highlight the tab.
    void noteAppended(QTextBrowser* browser);

    /// Drop every tab's highlight and unseen mark.
    void clearHighlights();

    QTabBar* m_tabBar = nullptr;
    QStackedWidget* m_stack = nullptr;
    LogTextView* m_serverInfoBrowser = nullptr;
    LogTextView* m_logBrowser = nullptr;
    LogTextView* m_verboseBrowser = nullptr;
    LogTextView* m_leecherBrowser = nullptr;
    LogTextView* m_kadBrowser = nullptr;
    LogTextView* m_usenetBrowser = nullptr;
    LogTextView* m_torrentBrowser = nullptr;
    LogTextView* m_torrentVerboseBrowser = nullptr;
    LogTextView* m_hosterBrowser = nullptr;
    LogTextView* m_hosterVerboseBrowser = nullptr;
    LogTextView* m_ed3kBrowser = nullptr;
    LogTextView* m_ed3kVerboseBrowser = nullptr;
    LogTextView* m_ed3kKadBrowser = nullptr;
    LogTextView* m_ipcLogBrowser = nullptr;
    int m_ipcTabIndex = -1;
    int m_usenetTabIndex = -1;
    int m_torrentTabIndex = -1;
    bool m_torrentTabVisible = true;
    int m_torrentVerboseTabIndex = -1;
    bool m_torrentVerboseVisible = false;
    int m_hosterTabIndex = -1;
    bool m_hosterTabVisible = true;
    int m_hosterVerboseTabIndex = -1;
    bool m_hosterVerboseVisible = false;
    int m_leecherTabIndex = -1;
    bool m_leecherTabVisible = true;
    bool m_usenetTabVisible = true;
    int m_ed3kTabIndex = -1;
    int m_ed3kVerboseTabIndex = -1;
    int m_ed3kKadTabIndex = -1;
    bool m_ed3kTabsVisible = true;
    bool m_ed3kVerboseVisible = false;

    /// Tabs with text the user has not looked at yet — the highlighted ones.
    QSet<int> m_unseenTabs;

    /// Parallel sequence-ID lists (one per sorted browser) for ordered insertion.
    QList<qint64> m_logSeqIds;
    QList<qint64> m_verboseSeqIds;
    QList<qint64> m_leecherSeqIds;
    QList<qint64> m_kadSeqIds;
    QList<qint64> m_usenetSeqIds;
    QList<qint64> m_torrentSeqIds;
    QList<qint64> m_torrentVerboseSeqIds;
    QList<qint64> m_hosterSeqIds;
    QList<qint64> m_hosterVerboseSeqIds;
    QList<qint64> m_ed3kSeqIds;
    QList<qint64> m_ed3kVerboseSeqIds;
    QList<qint64> m_ed3kKadSeqIds;

    /// Static instance pointer for the message handler callback.
    static LogWidget* s_instance;

    /// The Qt message handler installed before ours (to chain).
    static QtMessageHandler s_previousHandler;

    /// Our custom message handler.
    static void messageHandler(QtMsgType type, const QMessageLogContext& context,
                               const QString& msg);
};

} // namespace eMule
