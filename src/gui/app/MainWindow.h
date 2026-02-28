#pragma once

/// @file MainWindow.h
/// @brief Main application window with toolbar navigation and status bar.
///
/// Replicates the MFC eMule main window layout: large icon toolbar at the top
/// for switching between tab pages (Kad, Servers, Transfers, etc.),
/// a stacked widget holding the page content, and a status bar at the bottom.

#include <QCloseEvent>
#include <QMainWindow>

class QAction;
class QActionGroup;
class QLabel;
class QStackedWidget;

namespace eMule {

class IpcClient;
class KadPanel;
class ServerPanel;
class TransferPanel;

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

    /// Update the eD2K status label in the footer.
    void setEd2kStatus(bool connected, bool connecting, bool firewalled);

    /// Update the Kad status label in the footer.
    void setKadStatus(bool running, bool kadConnected);

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onToolbarAction(QAction* action);
    void onConnectToggle();
    void onOptionsClicked();

private:
    void setupToolbar();
    void setupStatusBar();
    void setupPages();

    QStackedWidget* m_pages = nullptr;
    QAction* m_connectAction = nullptr;
    QActionGroup* m_tabGroup = nullptr;
    IpcClient* m_ipc = nullptr;

    // Tab panels
    KadPanel* m_kadPanel = nullptr;
    ServerPanel* m_serverPanel = nullptr;
    TransferPanel* m_transferPanel = nullptr;
    // ToDo: Add other panels (Search, SharedFiles, Messages, IRC, Statistics)

    // Status bar labels
    QLabel* m_statusMsg = nullptr;
    QLabel* m_statusUsers = nullptr;
    QLabel* m_statusUpDown = nullptr;
    QLabel* m_statusEd2k = nullptr;
    QLabel* m_statusKad = nullptr;
};

} // namespace eMule
