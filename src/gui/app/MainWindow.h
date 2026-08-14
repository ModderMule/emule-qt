#pragma once

/// @file MainWindow.h
/// @brief Main application window with toolbar navigation and status bar.
///
/// Replicates the MFC eMule main window layout: large icon toolbar at the top
/// for switching between tab pages (Kad, Servers, Transfers, etc.),
/// a stacked widget holding the page content, and a status bar at the bottom.

#include "app/SkinEngine.h"

#include <QCloseEvent>
#include <QMainWindow>
#include <QMap>
#include <QWidget>
#include <QPainter>
#include <QMouseEvent>
#include <QSystemTrayIcon>

class QAction;
class QActionGroup;
class QCborMap;
class QLabel;
class QMenu;
class QSoundEffect;
class QStackedWidget;
class QTimer;
class QToolBar;

namespace eMule {

#ifdef Q_OS_WIN
class MiniMuleWidget;
#endif

class IpcClient;
class IrcPanel;
class SpeedGraph;
class TrayMenuManager;
class VersionChecker;
class KadPanel;
class MessagesPanel;
class SearchPanel;
class ServerPanel;
class SharedFilesPanel;
class StatisticsPanel;
class TransferPanel;

/// Identifies each toolbar button for customization persistence.
enum class ToolbarButtonId : int {
    Connect = 0,
    Separator = 1,
    Kad = 10, Servers = 11, Transfers = 12, Search = 13,
    SharedFiles = 14, Messages = 15, IRC = 16, Statistics = 17,
    Options = 20, Tools = 21, Help = 22,
};

/// Small status bar widget showing a world globe with two arrows:
/// left arrow = eD2K status, right arrow = Kad status.
/// Colors: red = disconnected, yellow = firewalled, green = open/connected.
class ConnectionStatusWidget : public QWidget {
    Q_OBJECT

public:
    enum State { Disconnected, Firewalled, Connected };

    explicit ConnectionStatusWidget(QWidget* parent = nullptr);

    void setEd2kState(State s);
    void setKadState(State s);

    [[nodiscard]] QSize sizeHint() const override { return {16, 16}; }
    [[nodiscard]] QSize minimumSizeHint() const override { return {16, 16}; }

signals:
    void doubleClicked();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
    static QColor colorForState(State s);

    State m_ed2kState = Disconnected;
    State m_kadState  = Disconnected;
};

/// Main application window matching the MFC eMule look & feel.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    /// Tab indices matching toolbar button order.
    enum Tab {
        TabKad = 0,
        TabServers,
        TabTransfers,
        TabSearch,
        TabSharedFiles,
        TabMessages,
        TabIRC,
        TabStatistics,
        TabCount
    };

    void switchToTab(Tab tab);

    /// Open the Options dialog, optionally at a specific page index.
    void showOptionsDialog(int page = -1);

    /// Set the IPC client (needed for Options dialog).
    void setIpcClient(IpcClient* ipc);

    [[nodiscard]] bool isEd2kConnected() const { return m_ed2kConnected; }
    [[nodiscard]] bool isKadConnected() const { return m_kadConnected; }

    [[nodiscard]] KadPanel* kadPanel() const { return m_kadPanel; }
    [[nodiscard]] ServerPanel* serverPanel() const { return m_serverPanel; }
    [[nodiscard]] TransferPanel* transferPanel() const { return m_transferPanel; }
    [[nodiscard]] SearchPanel* searchPanel() const { return m_searchPanel; }
    [[nodiscard]] SharedFilesPanel* sharedFilesPanel() const { return m_sharedFilesPanel; }
    [[nodiscard]] MessagesPanel* messagesPanel() const { return m_messagesPanel; }
    [[nodiscard]] IrcPanel* ircPanel() const { return m_ircPanel; }
    [[nodiscard]] StatisticsPanel* statisticsPanel() const { return m_statsPanel; }

    /// Update the eD2K status label in the footer. \p firewalled is the combined
    /// ed2k+kad state (tray icon), \p lowID the per-network eD2K LowID (label colour).
    void setEd2kStatus(bool connected, bool connecting, bool firewalled, bool lowID);

    /// Update the Kad status label in the footer.
    void setKadStatus(bool running, bool kadConnected, bool firewalled);

    /// Update the Users/Files label in the footer with Kad network estimates.
    void setNetworkStats(quint32 users, quint32 files);

    /// Update the Up/Down rate labels in the status bar (values in KB/s).
    void updateTransferRates(double upKBs, double downKBs,
                             double upOverheadKBs, double downOverheadKBs);

    /// Replace the free-text message on the left of the status bar.
    void setStatusMessage(const QString& text);

    /// Show a system tray notification popup (with optional sound).
    void showNotification(const QString& title, const QString& message);

    /// Update MiniMule popup stats (called from rate polling timer).
    void updateMiniMule(int completedCount, qint64 freeBytes);

    /// Bypass minimize-to-tray and promptOnExit, then close and quit.
    void forceQuit();

    /// Run a version check. \p manual reports the "up to date" and "check failed"
    /// outcomes too, which the automatic background check stays silent about, and
    /// ignores both the versionCheckEnabled setting and the interval.
    void checkForUpdates(bool manual);

    /// Start the automatic check: one now if due, then hourly. Idempotent, so the
    /// IPC connected handler can call it again on every reconnect.
    void startVersionChecks();

protected:
    void closeEvent(QCloseEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

private slots:
    void onToolbarAction(QAction* action);
    void onConnectToggle();
    void onTrayIconClicked(QSystemTrayIcon::ActivationReason reason);
    void onOptionsClicked();
    void showNetworkInfo();
    void onClipboardChanged();
    void buildToolsMenu();
    void onOpenIncomingFolder();
    void onImportDownloads();
    void onFirstTimeWizard();
    void onIPFilter();
    void onPasteLinks();
    void onSchedulerToggle();
    void onToolbarContextMenu(const QPoint& pos);
    void onSubmitBugReport();

private:
    void rebuildToolbar();
    void loadToolbarSkin(const QString& path);
    void clearToolbarSkin();
    QString skinsDir() const;
    void applySkinProfile(const QString& path);
    void setupStatusBar();
    void setupPages();
    void updateConnectButton();

    /// Redraw the tray icon: connection-state base plus MFC's download-rate meter bar.
    /// Cheap to call at 1 Hz — it returns early unless the picture would change.
    /// @param force repaint even then (startup, and after a settings change).
    void updateTrayIcon(bool force = false);

    /// Colour of the meter bar: stats colour 11, or the system scheme when that is
    /// left on "auto".
    [[nodiscard]] QColor trayMeterColor() const;

    /// Version, connection state and both rates, as MFC's tray tooltip carries.
    void updateTrayToolTip();

    /// Ask the daemon for toolbar-graph samples newer than m_speedSeq.
    void pollSpeedHistory();
    /// Apply one GetSpeedHistory reply, clearing the widget first if what we hold is
    /// no longer a prefix of the daemon's history.
    void applySpeedHistory(const QCborMap& data);

    QStackedWidget* m_pages = nullptr;
    QToolBar* m_toolbar = nullptr;
    QAction* m_connectAction = nullptr;
    QActionGroup* m_tabGroup = nullptr;
    IpcClient* m_ipc = nullptr;
    QMenu* m_toolsMenu = nullptr;
    QMap<ToolbarButtonId, QAction*> m_toolbarActions;
    QMap<ToolbarButtonId, QIcon> m_skinIcons;

    // Tab panels
    KadPanel* m_kadPanel = nullptr;
    ServerPanel* m_serverPanel = nullptr;
    TransferPanel* m_transferPanel = nullptr;
    SearchPanel* m_searchPanel = nullptr;
    SharedFilesPanel* m_sharedFilesPanel = nullptr;
    MessagesPanel* m_messagesPanel = nullptr;
    IrcPanel* m_ircPanel = nullptr;
    StatisticsPanel* m_statsPanel = nullptr;

    // Status bar labels
    QLabel* m_statusMsg = nullptr;
    QWidget* m_statusUsersWidget = nullptr;   // container for user icon + label
    QLabel* m_statusUsersLabel = nullptr;
    QWidget* m_statusUpDownWidget = nullptr;  // container for up/down icons + labels
    QLabel* m_statusUpLabel = nullptr;
    QLabel* m_statusDownLabel = nullptr;
    QLabel* m_statusEd2k = nullptr;
    QLabel* m_statusKad = nullptr;
    ConnectionStatusWidget* m_connStatus = nullptr;

    // Version checker
    VersionChecker* m_versionChecker = nullptr;

    // Clipboard monitoring (MFC SearchClipboard equivalent)
    QString m_lastClipboardContents;

    // System tray icon for popup notifications
    QSystemTrayIcon* m_trayIcon = nullptr;
    QSoundEffect* m_notifySound = nullptr;

    // Speed graph in toolbar. The samples are the daemon's, fetched by seq — see
    // pollSpeedHistory(). m_speedSeq/m_speedEpoch say which slice of the daemon's
    // history the widget currently holds.
    SpeedGraph* m_speedGraph = nullptr;
    QTimer* m_speedHistoryTimer = nullptr;
    quint32 m_speedSeq = 0;
    quint32 m_speedEpoch = 0;

    // Cached status for world icon
    bool m_ed2kConnected = false;
    bool m_ed2kFirewalled = false;   ///< combined ed2k+kad firewall state (tray icon)
    bool m_ed2kLowID = false;        ///< eD2K-only LowID
    bool m_kadRunning = false;
    bool m_kadConnected = false;
    bool m_kadFirewalled = false;

    // Skin engine
    SkinEngine m_skinEngine;

    // Cached stats for MiniMule popup
    double m_cachedUpKBs = 0.0;
    double m_cachedDownKBs = 0.0;
    int m_cachedCompleted = 0;
    qint64 m_cachedFreeBytes = 0;

    // Tray context menu
    TrayMenuManager* m_trayMenu = nullptr;
    bool m_forceQuit = false;

    // What the tray icon currently shows, so an unchanged picture costs nothing.
    int     m_trayMeterLevel = -1;
    QString m_trayIconPath;
    QColor  m_trayMeterColor;
    QString m_trayToolTip;

#ifdef Q_OS_WIN
    MiniMuleWidget* m_miniMule = nullptr;
#endif
};

} // namespace eMule
