#pragma once

/// @file MainWindow.h
/// @brief Main application window with toolbar navigation and status bar.
///
/// Replicates the MFC eMule main window layout: large icon toolbar at the top
/// for switching between tab pages (Kad, Servers, Transfers, etc.),
/// a stacked widget holding the page content, and a status bar at the bottom.

#include <QCloseEvent>
#include <QMainWindow>
#include <QWidget>
#include <QPainter>
#include <QMouseEvent>

class QAction;
class QActionGroup;
class QLabel;
class QMenu;
class QSoundEffect;
class QStackedWidget;
class QSystemTrayIcon;

namespace eMule {

class IpcClient;
class IrcPanel;
class KadPanel;
class MessagesPanel;
class SearchPanel;
class ServerPanel;
class SharedFilesPanel;
class StatisticsPanel;
class TransferPanel;

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

    [[nodiscard]] QSize sizeHint() const override { return {36, 18}; }
    [[nodiscard]] QSize minimumSizeHint() const override { return {36, 18}; }

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
    void setIpcClient(IpcClient* ipc) { m_ipc = ipc; }

    [[nodiscard]] KadPanel* kadPanel() const { return m_kadPanel; }
    [[nodiscard]] ServerPanel* serverPanel() const { return m_serverPanel; }
    [[nodiscard]] TransferPanel* transferPanel() const { return m_transferPanel; }
    [[nodiscard]] SearchPanel* searchPanel() const { return m_searchPanel; }
    [[nodiscard]] SharedFilesPanel* sharedFilesPanel() const { return m_sharedFilesPanel; }
    [[nodiscard]] MessagesPanel* messagesPanel() const { return m_messagesPanel; }
    [[nodiscard]] IrcPanel* ircPanel() const { return m_ircPanel; }
    [[nodiscard]] StatisticsPanel* statisticsPanel() const { return m_statsPanel; }

    /// Update the eD2K status label in the footer.
    void setEd2kStatus(bool connected, bool connecting, bool firewalled);

    /// Update the Kad status label in the footer.
    void setKadStatus(bool running, bool kadConnected, bool firewalled);

    /// Update the Users/Files label in the footer with Kad network estimates.
    void setNetworkStats(quint32 users, quint32 files);

    /// Update the Up/Down rate labels in the status bar (values in KB/s).
    void updateTransferRates(double upKBs, double downKBs,
                             double upOverheadKBs, double downOverheadKBs);

    /// Show a system tray notification popup (with optional sound).
    void showNotification(const QString& title, const QString& message);

protected:
    void closeEvent(QCloseEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

private slots:
    void onToolbarAction(QAction* action);
    void onConnectToggle();
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

private:
    void setupToolbar();
    void setupStatusBar();
    void setupPages();
    void updateConnectButton();

    QStackedWidget* m_pages = nullptr;
    QAction* m_connectAction = nullptr;
    QActionGroup* m_tabGroup = nullptr;
    IpcClient* m_ipc = nullptr;
    QMenu* m_toolsMenu = nullptr;

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

    // Clipboard monitoring (MFC SearchClipboard equivalent)
    QString m_lastClipboardContents;

    // System tray icon for popup notifications
    QSystemTrayIcon* m_trayIcon = nullptr;
    QSoundEffect* m_notifySound = nullptr;

    // Cached status for world icon
    bool m_ed2kConnected = false;
    bool m_ed2kFirewalled = false;
    bool m_kadRunning = false;
    bool m_kadConnected = false;
    bool m_kadFirewalled = false;
};

} // namespace eMule
