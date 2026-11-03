#pragma once

/// @file Ed2kKadPanel.h
/// @brief eD2K page of the Kad window, replicating the MFC KademliaWnd layout.
///
/// Layout (matching Kad.png / Kad Distance.png screenshots):
///   - Top-left: QTabWidget with "Contacts" tab and "Search Details" tab
///   - Top-right: Bootstrap/firewall controls + contacts histogram + network graph
///   - Bottom: Current Searches table
///   - Vertical splitter between top and bottom sections

#include "panels/KadPanelBase.h"

#include <cstdint>

class QButtonGroup;
class QLineEdit;
class QPushButton;
class QRadioButton;
class QTimer;

namespace eMule {

class ContactsGraph;
class KadContactHistogram;
class KadContactsModel;
class KadSearchesModel;

class Ed2kKadPanel : public KadPanelBase {
    Q_OBJECT

public:
    explicit Ed2kKadPanel(QWidget* parent = nullptr);
    ~Ed2kKadPanel() override;

    void setIpcClient(IpcClient* client) override;

    /// Reload the nodes.dat URL field after the daemon's preferences landed.
    void refreshNodesUrl();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void refresh() override;
    void requestLookupHistory() override;
    void autoPickLookup() override;
    void clearAll() override;
    [[nodiscard]] std::uint64_t searchIdAtRow(int sourceRow) const override;

private slots:
    void onGraphTimer();
    void onBootstrapClicked();
    void onDisconnectClicked();
    void onRecheckFirewall();

private:
    QWidget* createContactsPanel();
    QWidget* createControlsPanel();
    QWidget* createSearchesPanel();
    void updateBootstrapButton();
    void requestContacts();
    void requestSearches();
    void requestStatus();
    void clearLookupGraph();

    KadContactsModel* m_contactsModel = nullptr;
    KadSearchesModel* m_searchesModel = nullptr;

    // Right panel controls
    QPushButton*  m_recheckFwBtn     = nullptr;
    QPushButton*  m_disconnectBtn    = nullptr;
    QButtonGroup* m_bootstrapGroup   = nullptr;
    QRadioButton* m_bootstrapIpRadio = nullptr;
    QRadioButton* m_bootstrapUrlRadio = nullptr;
    QRadioButton* m_bootstrapClientsRadio = nullptr;
    QLineEdit*    m_ipEdit           = nullptr;
    QLineEdit*    m_portEdit         = nullptr;
    QLineEdit*    m_urlEdit          = nullptr;
    QPushButton*  m_bootstrapBtn     = nullptr;
    ContactsGraph*        m_contactsGraph   = nullptr;
    KadContactHistogram*  m_kadNetworkGraph = nullptr;

    // Graph timer (60 s — responded nodes per minute). Keeps running while hidden:
    // it accumulates history, so a pause would leave a gap in the trace.
    QTimer*  m_graphTimer        = nullptr;
    int64_t  m_lastResponseCount = 0;

    bool       m_kadRunning = false;
    bool       m_kadConnected = false;
    bool       m_downloadingNodes = false;
};

} // namespace eMule
