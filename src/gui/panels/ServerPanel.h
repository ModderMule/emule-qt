#pragma once

/// @file ServerPanel.h
/// @brief Server tab panel replicating the MFC CServerWnd layout.
///
/// Layout (matching Server.png screenshot):
///   - Top: Server list table (QTreeView)
///   - Right: Connect button, New Server form, Update server.met, My Info section
///   - Bottom: Log tabs (Server Info | Log | Verbose) with Reset button
///   - Vertical splitter between server list and log area

#include <QWidget>

class QLabel;
class QLineEdit;
class QMenu;
class QPushButton;
class QSplitter;
class QTimer;
class QTreeView;

namespace eMule {

class IpcClient;
class PanelPoller;
class LogWidget;
class ServerListModel;

/// Full Server tab page matching the MFC eMule Server window.
///
/// Every action goes through the daemon over IPC — this panel holds no core
/// objects. The GUI process has no ServerList or ServerConnect to hand it.
class ServerPanel : public QWidget {
    Q_OBJECT

public:
    explicit ServerPanel(QWidget* parent = nullptr);
    ~ServerPanel() override;

    /// Connect this panel to the IPC client for data updates.
    void setIpcClient(IpcClient* client);

    /// Get the log widget so it can be shared with MainWindow if needed.
    [[nodiscard]] LogWidget* logWidget() const { return m_logWidget; }

private slots:
    void onConnectClicked();
    void onAddServerClicked();
    void onUpdateServerMetClicked();
    void onRefreshTimer();
    void updateConnectButton(bool connected, bool connecting);
    void onServerDoubleClicked(const QModelIndex& index);
    void onServerContextMenu(const QPoint& pos);

private:
    void setupUi();
    QWidget* createServerListPanel();
    QWidget* createControlsPanel();
    void refreshMyInfo();
    void requestServerList();

    /// Apply the manual-order display mode (#24): when useUserSortedServerList is
    /// enabled, show servers in the daemon's list order with column sorting off.
    void applyServerSortMode();
    [[nodiscard]] QString saveSelection() const;
    void restoreSelection(const QString& key);

    void showFindDialog();
    void requestKadStatus();
    void requestServerState();
    void parseAndAddServersFromMet(const QByteArray& data);

    // Models
    ServerListModel* m_serverListModel = nullptr;

    // Context menu
    QMenu* m_serverMenu = nullptr;

    // Views
    QTreeView* m_serverListView = nullptr;
    QLabel* m_serversLabel = nullptr;

    // Right panel controls
    QPushButton* m_connectBtn = nullptr;
    QLineEdit* m_newServerIp = nullptr;
    QLineEdit* m_newServerPort = nullptr;
    QLineEdit* m_newServerName = nullptr;
    QPushButton* m_addServerBtn = nullptr;
    QLineEdit* m_updateUrlEdit = nullptr;
    QPushButton* m_updateBtn = nullptr;

    // My Info section
    QLabel* m_infoLabel = nullptr;

    // Log widget (bottom area with Server Info / Log / Verbose tabs)
    LogWidget* m_logWidget = nullptr;

    // Splitters
    QSplitter* m_vertSplitter = nullptr;

    // Refresh timer
    /// Server list refresh, suspended while this panel is not the visible tab.
    PanelPoller* m_poller = nullptr;

    // IPC client
    IpcClient* m_ipc = nullptr;

    // Kad status (updated via IPC push events)
    bool m_kadRunning    = false;
    bool m_kadConnected  = false;
    bool m_kadFirewalled = false;

    // eD2K status (updated via IPC push events)
    bool m_ed2kConnected  = false;
    bool m_ed2kConnecting = false;
    bool m_ed2kFirewalled = false;   ///< combined ed2k+kad firewall state
    bool m_ed2kLowID      = false;   ///< eD2K-only LowID
    uint32_t m_ed2kClientID = 0;
    QString m_ed2kServerName;
    uint32_t m_ed2kPublicIP = 0;
    QString m_ed2kServerDesc;
    QString m_ed2kServerAddr;
    uint16_t m_ed2kServerPort = 0;
    QString m_ed2kServerVersion;
    uint32_t m_ed2kServerUsers = 0;
    uint32_t m_ed2kServerFiles = 0;
    bool m_ed2kObfuscated = false;

    // Kad extended status
    bool m_kadUdpFirewalled = false;
    bool m_kadUdpVerified = false;
    uint32_t m_kadIP = 0;
    uint16_t m_kadInternPort = 0;
    uint16_t m_kadExternPort = 0;
    uint32_t m_kadId = 0;
    uint32_t m_kadUsers = 0;
    uint32_t m_kadUsersExp = 0;
    uint32_t m_kadFiles = 0;
};

} // namespace eMule
