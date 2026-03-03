#pragma once

/// @file FirstStartWizard.h
/// @brief First Runtime Wizard dialog matching the MFC eMule wizard.
///
/// Single-page wizard combining the connection ports section and network
/// selection section. Accessible via Tools > "eMule First Runtime Wizard...".

#include <QDialog>

class QCheckBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QTimer;

namespace eMule {

class IpcClient;

class FirstStartWizard : public QDialog {
    Q_OBJECT

public:
    explicit FirstStartWizard(IpcClient* ipc, QWidget* parent = nullptr);

private slots:
    void onFinish();
    void onUPnPSetup();
    void onUPnPTimeout();
    void onHelp();

private:
    void setupHeader();
    void setupPortSection();
    void setupNetworkSection();
    void setupButtons();

    IpcClient* m_ipc = nullptr;

    // Port controls
    QSpinBox* m_tcpPortSpin = nullptr;
    QSpinBox* m_udpPortSpin = nullptr;
    QPushButton* m_upnpBtn = nullptr;
    QProgressBar* m_upnpProgress = nullptr;
    QTimer* m_upnpTimer = nullptr;

    // Network controls
    QCheckBox* m_kadCheck = nullptr;
    QCheckBox* m_ed2kCheck = nullptr;

    // Button row
    QPushButton* m_backBtn = nullptr;
    QPushButton* m_finishBtn = nullptr;
};

} // namespace eMule
