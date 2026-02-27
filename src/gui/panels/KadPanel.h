#pragma once

/// @file KadPanel.h
/// @brief Kad tab panel replicating the MFC KademliaWnd layout.
///
/// Layout (matching Kad.png screenshot):
///   - Top-left: Contacts list (QTreeView with hex ID + binary distance)
///   - Top-right: Bootstrap/firewall controls + contacts histogram + network graph
///   - Bottom: Current Searches table
///   - Vertical splitter between top and bottom sections

#include <QWidget>

class QButtonGroup;
class QLabel;
class QLineEdit;
class QPushButton;
class QRadioButton;
class QSplitter;
class QTimer;
class QTreeView;

namespace eMule {

class ContactsGraph;
class IpcClient;
class KadContactsModel;
class KadSearchesModel;

/// Full Kad tab page matching the MFC eMule Kad window.
class KadPanel : public QWidget {
    Q_OBJECT

public:
    explicit KadPanel(QWidget* parent = nullptr);
    ~KadPanel() override;

    /// Connect this panel to the IPC client for data updates.
    void setIpcClient(IpcClient* client);

private slots:
    void onRefreshTimer();
    void onGraphTimer();
    void onBootstrapClicked();
    void onDisconnectClicked();
    void onRecheckFirewall();
    void onBootstrapTypeChanged();
    void onBootstrapInputChanged();

private:
    void setupUi();
    QWidget* createContactsPanel();
    QWidget* createControlsPanel();
    QWidget* createSearchesPanel();
    void updateBootstrapButton();
    void requestContacts();
    void requestSearches();
    void requestStatus();

    // Data models
    KadContactsModel* m_contactsModel = nullptr;
    KadSearchesModel* m_searchesModel = nullptr;

    // Views
    QTreeView* m_contactsView = nullptr;
    QTreeView* m_searchesView = nullptr;
    QLabel* m_contactsLabel = nullptr;
    QLabel* m_searchesLabel = nullptr;

    // Right panel controls
    QPushButton*  m_recheckFwBtn     = nullptr;
    QPushButton*  m_disconnectBtn    = nullptr;
    QButtonGroup* m_bootstrapGroup   = nullptr;
    QRadioButton* m_bootstrapIpRadio = nullptr;
    QRadioButton* m_bootstrapUrlRadio = nullptr;
    QLineEdit*    m_ipEdit           = nullptr;
    QLineEdit*    m_portEdit         = nullptr;
    QLineEdit*    m_urlEdit          = nullptr;
    QPushButton*  m_bootstrapBtn     = nullptr;
    ContactsGraph* m_contactsGraph    = nullptr;
    ContactsGraph* m_kadNetworkGraph  = nullptr;

    // Splitters
    QSplitter* m_vertSplitter = nullptr;

    // Refresh timer (1.4 s — contacts list + searches + status)
    QTimer* m_refreshTimer = nullptr;

    // Graph timer (60 s — hello packet count per minute)
    QTimer*  m_graphTimer     = nullptr;
    int64_t  m_lastHelloCount = 0;

    // IPC link
    IpcClient* m_ipc       = nullptr;
    bool       m_kadRunning = false;
};

} // namespace eMule
