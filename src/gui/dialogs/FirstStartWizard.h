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

#include <optional>

class QCheckBox;
class QDoubleSpinBox;
class QLabel;
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
    enum class StartPage { Ports, Speed };

    explicit FirstStartWizard(IpcClient* ipc, QWidget* parent = nullptr,
                              StartPage startPage = StartPage::Ports);

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
    QWidget* setupPortPage();
    QWidget* setupSpeedPage();
    void setupButtons();
    void showPage(int page);
    void requestDaemonSettings();
    void fillFromSettings(const QCborMap& prefs);
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
    QCheckBox* m_udpDisableCheck = nullptr;
    QPushButton* m_upnpBtn = nullptr;
    QProgressBar* m_upnpProgress = nullptr;
    QLabel* m_upnpStatus = nullptr;
    QTimer* m_upnpTimer = nullptr;
    bool m_upnpRequested = false;

    // Network controls
    QCheckBox* m_kadCheck = nullptr;
    QCheckBox* m_ed2kCheck = nullptr;

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
