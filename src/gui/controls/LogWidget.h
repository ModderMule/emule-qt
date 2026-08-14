#pragma once

/// @file LogWidget.h
/// @brief Tabbed log display matching the MFC "Server Info | Log | Verbose" tabs.
///
/// Installs a Qt message handler to capture qCInfo/qCWarning/qCDebug output
/// from the core logging categories and routes them to the appropriate tabs.

#include "server/ServerMsgType.h"

#include <QWidget>

#include <cstdint>

class QTabBar;
class QStackedWidget;
class QTextBrowser;

namespace eMule {

/// Tabbed log widget with Server Info, Log, Verbose, Kad, and IPC tabs.
/// Captures Qt logging category output from the core layer.
/// The IPC tab is shown only when enableIpcLog preference is true.
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

    /// Append a message to the Log tab.
    /// If @p ts is non-empty it is used as the timestamp; otherwise current time.
    /// @p seqId orders entries (epoch seconds); 0 = use current time.
    void appendLog(const QString& msg, const QString& ts = {}, qint64 seqId = 0);

    /// Append a message to the Verbose tab.
    void appendVerbose(const QString& msg, const QString& ts = {}, qint64 seqId = 0);

    /// Append a message to the Kad tab.
    void appendKad(const QString& msg, const QString& ts = {}, qint64 seqId = 0);

    /// Append an IPC message to the IPC tab.
    /// @p outgoing: true = GUI→daemon (green), false = daemon→GUI (purple).
    void appendIpcMessage(const QString& msg, bool outgoing);

    /// Show or hide the IPC tab.
    void setIpcTabVisible(bool visible);

    /// Clear all tabs.
    void clearAll();

    /// Plain text accessors for bug report submission.
    [[nodiscard]] QString logText() const;
    [[nodiscard]] QString verboseText() const;
    [[nodiscard]] QString kadText() const;

    /// Set a custom font on all log browser tabs.
    void setCustomFont(const QFont& font);

    /// Install the global message handler to capture core log output.
    void installMessageHandler();

    /// Remove the global message handler.
    void removeMessageHandler();

    /// Open or close the GUI's own log files (emuleqt.log, emuleqt_Verbose.log
    /// and emuleqt_Kad.log in the config directory) to match the logToDiskGui
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
    void insertSorted(QTextBrowser* browser, QList<qint64>& seqIds,
                      qint64 seqId, const QString& html);

    /// Remove oldest lines from @p browser if it exceeds the configured limit.
    void trimToLimit(QTextBrowser* browser, QList<qint64>& seqIds);
    static void trimToLimit(QTextBrowser* browser);

    /// Write the startup banner (version + version-check link) into the Server
    /// Info pane, as the reference does in CServerWnd::OnInitDialog.
    void writeServerInfoBanner();

    /// Draw attention to a tab that is not currently selected.
    void highlightTab(int index);

    QTabBar* m_tabBar = nullptr;
    QStackedWidget* m_stack = nullptr;
    QTextBrowser* m_serverInfoBrowser = nullptr;
    QTextBrowser* m_logBrowser = nullptr;
    QTextBrowser* m_verboseBrowser = nullptr;
    QTextBrowser* m_kadBrowser = nullptr;
    QTextBrowser* m_ipcLogBrowser = nullptr;
    int m_ipcTabIndex = -1;

    /// Parallel sequence-ID lists (one per sorted browser) for ordered insertion.
    QList<qint64> m_logSeqIds;
    QList<qint64> m_verboseSeqIds;
    QList<qint64> m_kadSeqIds;

    /// Static instance pointer for the message handler callback.
    static LogWidget* s_instance;

    /// The Qt message handler installed before ours (to chain).
    static QtMessageHandler s_previousHandler;

    /// Our custom message handler.
    static void messageHandler(QtMsgType type, const QMessageLogContext& context,
                               const QString& msg);
};

} // namespace eMule
