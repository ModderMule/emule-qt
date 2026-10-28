#pragma once

/// @file FirstStartWizard.h
/// @brief First Runtime Wizard dialog matching the MFC eMule wizard.
///
/// Two pages: ports + network selection, then connection speed (MFC's separate
/// connection wizard, with 2026 line types). Shown once on first start and
/// accessible via Tools > "eMule First Runtime Wizard...".

#include "dialogs/ConnectionPresets.h"

#include <QCborMap>
#include <QDialog>
#include <QList>

#include <functional>
#include <utility>

#include <optional>

class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class QTimer;
class QTreeWidget;
class QTreeWidgetItem;

namespace eMule {

class IpcClient;

class FirstStartWizard : public QDialog {
    Q_OBJECT

public:
    /// The page it opens on (the Connection page's wizard button uses Speed).
    enum class StartPage { Ports, Speed };

    explicit FirstStartWizard(IpcClient* ipc, QWidget* parent = nullptr,
                              StartPage startPage = StartPage::Ports);

    /// Cancel takes back the ports a UPnP attempt or a port test pushed to the core
    /// (MFC CPShtWiz1 restores them, PShtWiz1.cpp:821-829).
    void reject() override;

    /// The preference keys Finish wrote (IPC names). Empty until accepted.
    [[nodiscard]] const QCborMap& appliedSettings() const { return m_applied; }

    /// Show a port-mapping status (PushPortMapStatus map); a final one ends the wait.
    void showPortMapStatus(const QCborMap& info);

private slots:
    void onBack();
    void onNext();
    void onUPnPSetup();
    void onUPnPTimeout();
    void onHelp();
    void onSpeedSelectionChanged();
    void onCustomRateEdited();
    void onLimitEdited();

private:
    void setupHeader();
    /// Hand the ports on the page to the core now; remembers what to restore.
    void pushPorts(bool withUPnP, std::function<void(bool ok)> done);
    void onPortTest();
    QWidget* setupPortPage();
    QWidget* setupSpeedPage();
    void setupButtons();
    void showPage(int page);
    void requestDaemonSettings();
    void fillFromSettings(const QCborMap& prefs);
    /// One port field (joined) or TCP and UDP.
    void showPortsJoined();
    /// The UDP port to send: the TCP one when joined, 0 when off.
    [[nodiscard]] int udpPortValue() const;
    [[nodiscard]] bool networksValid();
    [[nodiscard]] std::optional<BandwidthSettings> selectedBandwidth() const;
    [[nodiscard]] std::optional<BandwidthSettings> chosenBandwidth() const;
    void requestPortMapStatus();
    void endUPnPWait(const QString& text, bool failed);
    void finish();

    IpcClient* m_ipc = nullptr;

    // Header
    QLabel* m_titleLabel = nullptr;
    QLabel* m_subtitleLabel = nullptr;
    QStackedWidget* m_pages = nullptr;

    // Port controls
    QSpinBox* m_tcpPortSpin = nullptr;
    QSpinBox* m_udpPortSpin = nullptr;
    bool m_udpOff = false;   ///< udpPort 0 in the config file, no GUI switch
    QLabel* m_portDescLabel = nullptr;
    QLabel* m_tcpLabel = nullptr;
    QLabel* m_udpLabel = nullptr;
    bool m_portsJoined = false;   ///< TCP = UDP: one "TCP/UDP" field (eD2K and ed3k)
    bool m_ed3kJoined = false;    ///< ed3k on that port too: moves with it
    QPushButton* m_upnpBtn = nullptr;
    QProgressBar* m_upnpProgress = nullptr;
    QLabel* m_upnpStatus = nullptr;
    QTimer* m_upnpTimer = nullptr;
    bool m_upnpRequested = false;

    // Network controls
    QCheckBox* m_kadCheck = nullptr;
    QCheckBox* m_ed2kCheck = nullptr;
    QCheckBox* m_extControlsCheck = nullptr;

    QPushButton* m_portTestBtn = nullptr;

    /// What the core had before this wizard pushed ports to it; empty = nothing pushed.
    QCborMap m_portsBefore;

    // Speed controls
    QTreeWidget* m_speedList = nullptr;
    QTreeWidgetItem* m_keepItem = nullptr;
    QTreeWidgetItem* m_recommendedItem = nullptr;
    QTreeWidgetItem* m_customItem = nullptr;
    QDoubleSpinBox* m_customDownSpin = nullptr;
    QDoubleSpinBox* m_customUpSpin = nullptr;
    QSpinBox* m_limitDownSpin = nullptr;
    QSpinBox* m_limitUpSpin = nullptr;
    bool m_limitsEdited = false;   ///< typed limits override the line's derived ones
    BandwidthSettings m_current;   ///< what the daemon runs with now

    // Button row
    QPushButton* m_backBtn = nullptr;
    QPushButton* m_nextBtn = nullptr;

    QCborMap m_applied;
};

} // namespace eMule
