#pragma once

/// @file Ed3kKadPanel.h
/// @brief ed3k page of the Kad window: the same layout as the eD2K page (KadPanelBase) over
/// the ed3k node: routing table, lookup graph, lookups, and on the side the node's state,
/// first contact (bootstrap.md §1) and node lists (§2, §3) with the export.

#include "panels/KadPanelBase.h"

#include <QCborMap>

#include <cstdint>

class QButtonGroup;
class QCheckBox;
class QLineEdit;
class QPushButton;
class QRadioButton;
class QSpinBox;
class QTimer;

namespace eMule {

class ContactsGraph;
class KadContactHistogram;
class Ed3kKadContactsModel;
class Ed3kKadLookupsModel;

class Ed3kKadPanel : public KadPanelBase {
    Q_OBJECT

public:
    explicit Ed3kKadPanel(QWidget* parent = nullptr);
    ~Ed3kKadPanel() override;

    void setIpcClient(IpcClient* client) override;

    /// The daemon's preferences landed: reload the node list prefs.
    void refreshPrefs();

signals:
    /// "Edit sources..." was clicked: open the ed3k connection options.
    void editSourcesRequested();

protected:
    void refresh() override;
    void requestLookupHistory() override;
    void autoPickLookup() override;
    void clearAll() override;
    [[nodiscard]] std::uint64_t searchIdAtRow(int sourceRow) const override;

private:
    QWidget* createControlsPanel();
    void requestContacts();
    void requestLookups();
    void requestStatus();
    void applyStatus(const QCborMap& status);
    void onConnectClicked();
    void onBootstrapClicked();
    void onExportClicked();
    void onOpenFileClicked();
    void sendPref(const QString& key, const QCborValue& value);
    void updateControls();

    Ed3kKadContactsModel* m_contactsModel = nullptr;
    Ed3kKadLookupsModel* m_lookupsModel = nullptr;

    QLabel* m_nodeLabel = nullptr;
    QLabel* m_stateLabel = nullptr;
    QPushButton* m_connectBtn = nullptr;
    QButtonGroup* m_bootstrapGroup = nullptr;
    QRadioButton* m_linkRadio = nullptr;
    QRadioButton* m_listsRadio = nullptr;
    QLineEdit* m_linkEdit = nullptr;
    QPushButton* m_bootstrapBtn = nullptr;
    QLabel* m_sourcesLabel = nullptr;
    QCheckBox* m_autoFetchCheck = nullptr;
    QSpinBox* m_maxAgeSpin = nullptr;
    QCheckBox* m_seedEd2kCheck = nullptr;
    QPushButton* m_exportBtn = nullptr;
    ContactsGraph* m_contactsGraph = nullptr;
    KadContactHistogram* m_networkGraph = nullptr;
    QTimer* m_graphTimer = nullptr;

    bool m_available = false;   ///< the core has ed3k
    bool m_running = false;
    std::int64_t m_contacts = 0;
    std::int64_t m_lastConnections = -1;
};

} // namespace eMule
