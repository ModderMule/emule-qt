#pragma once

/// @file KadPanel.h
/// @brief Kad tab panel replicating the MFC KademliaWnd layout.
///
/// Layout (matching Kad.png / Kad Distance.png screenshots):
///   - Top-left: QTabWidget with "Contacts" tab and "Search Details" tab
///   - Top-right: Bootstrap/firewall controls + contacts histogram + network graph
///   - Bottom: Current Searches table
///   - Vertical splitter between top and bottom sections

#include <QWidget>

#include <cstdint>

class QButtonGroup;
class QLabel;
class QLineEdit;
class QPushButton;
class QRadioButton;
class QSplitter;
class QTabWidget;
class QTimer;
class QTreeView;

namespace eMule {

class ContactsGraph;
class IpcClient;
class KadContactHistogram;
class KadContactsModel;
class KadLookupGraph;
class KadSearchesModel;

/// Full Kad tab page matching the MFC eMule Kad window.
class KadPanel : public QWidget {
    Q_OBJECT

public:
    explicit KadPanel(QWidget* parent = nullptr);
    ~KadPanel() override;

    /// Connect this panel to the IPC client for data updates.
    void setIpcClient(IpcClient* client);

    /// Switch to a sub-tab by index (0 = Contacts, 1 = Search Details).
    void switchToSubTab(int index);

private slots:
    void onRefreshTimer();
    void onGraphTimer();
    void onBootstrapClicked();
    void onDisconnectClicked();
    void onRecheckFirewall();
    void onBootstrapTypeChanged();
    void onBootstrapInputChanged();
    void onSearchSelectionChanged();

private:
    void setupUi();
    QWidget* createContactsPanel();
    QWidget* createControlsPanel();
    QWidget* createSearchesPanel();
    void updateBootstrapButton();
    void requestContacts();
    void requestSearches();
    void requestStatus();
    void requestLookupHistory();
    uint32_t selectedSearchId() const;

    // Data models
    KadContactsModel* m_contactsModel = nullptr;
    KadSearchesModel* m_searchesModel = nullptr;

    // Views
    QTabWidget* m_topTabWidget = nullptr;
    QTreeView* m_contactsView = nullptr;
    QTreeView* m_searchesView = nullptr;
    QLabel* m_contactsLabel = nullptr;
    QLabel* m_searchesLabel = nullptr;
    KadLookupGraph* m_lookupGraph = nullptr;

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
    ContactsGraph*        m_contactsGraph   = nullptr;
    KadContactHistogram*  m_kadNetworkGraph = nullptr;

    // Splitters
    QSplitter* m_vertSplitter = nullptr;

    // Refresh timer (1.4 s — contacts list + searches + status)
    QTimer* m_refreshTimer = nullptr;

    // Graph timer (60 s — responded nodes per minute)
    QTimer*  m_graphTimer        = nullptr;
    int64_t  m_lastResponseCount = 0;

    // IPC link
    IpcClient* m_ipc       = nullptr;
    bool       m_kadRunning = false;
};

} // namespace eMule
