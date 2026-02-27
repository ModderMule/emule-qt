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

class KadPanel;
class ServerPanel;

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

    [[nodiscard]] KadPanel* kadPanel() const { return m_kadPanel; }
    [[nodiscard]] ServerPanel* serverPanel() const { return m_serverPanel; }

    /// Update the eD2K status label in the footer.
    void setEd2kStatus(bool connected, bool connecting, bool firewalled);

    /// Update the Kad status label in the footer.
    void setKadStatus(bool running, bool kadConnected);

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onToolbarAction(QAction* action);
    void onConnectToggle();

private:
    void setupToolbar();
    void setupStatusBar();
    void setupPages();

    QStackedWidget* m_pages = nullptr;
    QAction* m_connectAction = nullptr;
    QActionGroup* m_tabGroup = nullptr;

    // Tab panels
    KadPanel* m_kadPanel = nullptr;
    ServerPanel* m_serverPanel = nullptr;
    // ToDo: Add other panels (Transfers, Search, SharedFiles, Messages, IRC, Statistics)

    // Status bar labels
    QLabel* m_statusMsg = nullptr;
    QLabel* m_statusUsers = nullptr;
    QLabel* m_statusUpDown = nullptr;
    QLabel* m_statusEd2k = nullptr;
    QLabel* m_statusKad = nullptr;
};

} // namespace eMule
