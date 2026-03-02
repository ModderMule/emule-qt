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
class LogWidget;
class ServerListModel;

class ServerConnect;
class ServerList;

/// Full Server tab page matching the MFC eMule Server window.
class ServerPanel : public QWidget {
    Q_OBJECT

public:
    explicit ServerPanel(QWidget* parent = nullptr);
    ~ServerPanel() override;

    /// Connect this panel to the IPC client for data updates.
    void setIpcClient(IpcClient* client);

    /// Connect this panel to live core objects for data and events.
    void setServerList(ServerList* serverList);
    void setServerConnect(ServerConnect* serverConnect);

    /// Get the log widget so it can be shared with MainWindow if needed.
    [[nodiscard]] LogWidget* logWidget() const { return m_logWidget; }

private slots:
    void onConnectClicked();
    void onAddServerClicked();
    void onUpdateServerMetClicked();
    void onRefreshTimer();
    void onServerListChanged();
    void onConnectedToServer();
    void onDisconnectedFromServer();
    void updateConnectButton(bool connected, bool connecting);
    void onServerMessage(const QString& msg);
    void onServerDoubleClicked(const QModelIndex& index);
    void onServerContextMenu(const QPoint& pos);

private:
    void setupUi();
    QWidget* createServerListPanel();
    QWidget* createControlsPanel();
    void refreshMyInfo();
    void requestServerList();
    [[nodiscard]] QString saveSelection() const;
    void restoreSelection(const QString& key);

    void showFindDialog();

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
    QTimer* m_refreshTimer = nullptr;

    // IPC client
    IpcClient* m_ipc = nullptr;

    // Core links
    ServerList* m_serverList = nullptr;
    ServerConnect* m_serverConnect = nullptr;
};

} // namespace eMule
