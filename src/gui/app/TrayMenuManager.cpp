#include "pch.h"
#include "app/TrayMenuManager.h"

#include "app/IpcClient.h"
#include "IpcMessage.h"
#include "IpcProtocol.h"
#include "prefs/Preferences.h"

#include <QGridLayout>
#include <QIcon>
#include <QLabel>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QTimer>
#include <QWidgetAction>

namespace eMule {

TrayMenuManager::TrayMenuManager(QWidget* parent)
    : QMenu(parent)
{
    buildMenu();
    connect(this, &QMenu::aboutToShow, this, [this]() {
        // Let the owner call updateState() explicitly; this is a fallback.
    });
}

// ---------------------------------------------------------------------------
// Public
// ---------------------------------------------------------------------------

void TrayMenuManager::updateState(bool ed2kConnected, bool kadRunning, bool ipcConnected)
{
    const bool connected = ed2kConnected || kadRunning;

    m_connectAction->setEnabled(ipcConnected && !connected);
    m_disconnectAction->setEnabled(ipcConnected && connected);

    const int maxUp = static_cast<int>(thePrefs.maxGraphUploadRate());
    const int maxDown = static_cast<int>(thePrefs.maxGraphDownloadRate());
    const int curUp = static_cast<int>(thePrefs.maxUpload());
    const int curDown = static_cast<int>(thePrefs.maxDownload());

    {
        QSignalBlocker b1(m_upSlider), b2(m_upSpin);
        m_upSlider->setRange(0, maxUp);
        m_upSpin->setRange(0, maxUp);
        m_upSlider->setValue(curUp);
        m_upSpin->setValue(curUp);
    }
    {
        QSignalBlocker b1(m_downSlider), b2(m_downSpin);
        m_downSlider->setRange(0, maxDown);
        m_downSpin->setRange(0, maxDown);
        m_downSlider->setValue(curDown);
        m_downSpin->setValue(curDown);
    }

    m_upSlider->setEnabled(ipcConnected);
    m_upSpin->setEnabled(ipcConnected);
    m_downSlider->setEnabled(ipcConnected);
    m_downSpin->setEnabled(ipcConnected);
}

// ---------------------------------------------------------------------------
// Private — menu construction
// ---------------------------------------------------------------------------

void TrayMenuManager::buildMenu()
{
    // Header: Speed.ico, "eMule Speed", bold, disabled (matches MFC)
    auto* header = addAction(QIcon(QStringLiteral(":/icons/Speed.ico")),
                             tr("eMule Speed"));
    header->setEnabled(false);
    auto headerFont = header->font();
    headerFont.setBold(true);
    header->setFont(headerFont);

    // Speed controls widget
    auto* speedWidget = new QWidget;
    auto* grid = new QGridLayout(speedWidget);
    grid->setContentsMargins(8, 4, 8, 4);
    grid->setHorizontalSpacing(6);
    grid->setVerticalSpacing(4);

    // Download row first (matches MFC order)
    auto* downIcon = new QLabel;
    downIcon->setPixmap(QIcon(QStringLiteral(":/icons/Download.ico")).pixmap(16, 16));
    auto* downLabel = new QLabel(tr("Download:"));
    m_downSlider = new QSlider(Qt::Horizontal);
    m_downSlider->setMinimumWidth(120);
    m_downSpin = new QSpinBox;
    m_downSpin->setSuffix(tr(" KB/s"));
    m_downSpin->setSpecialValueText(tr("Unlimited"));

    grid->addWidget(downIcon, 0, 0);
    grid->addWidget(downLabel, 0, 1);
    grid->addWidget(m_downSlider, 0, 2);
    grid->addWidget(m_downSpin, 0, 3);

    // Upload row second
    auto* upIcon = new QLabel;
    upIcon->setPixmap(QIcon(QStringLiteral(":/icons/Upload.ico")).pixmap(16, 16));
    auto* upLabel = new QLabel(tr("Upload:"));
    m_upSlider = new QSlider(Qt::Horizontal);
    m_upSlider->setMinimumWidth(120);
    m_upSpin = new QSpinBox;
    m_upSpin->setSuffix(tr(" KB/s"));
    m_upSpin->setSpecialValueText(tr("Unlimited"));

    grid->addWidget(upIcon, 1, 0);
    grid->addWidget(upLabel, 1, 1);
    grid->addWidget(m_upSlider, 1, 2);
    grid->addWidget(m_upSpin, 1, 3);

    auto* speedAction = new QWidgetAction(this);
    speedAction->setDefaultWidget(speedWidget);
    addAction(speedAction);

    // Wire slider ↔ spin bidirectionally
    syncSliderSpin(m_upSlider, m_upSpin);
    syncSliderSpin(m_downSlider, m_downSpin);

    // Debounce timer for IPC speed changes
    m_speedDebounce = new QTimer(this);
    m_speedDebounce->setSingleShot(true);
    m_speedDebounce->setInterval(100);
    connect(m_speedDebounce, &QTimer::timeout, this, &TrayMenuManager::sendSpeedChange);

    auto startDebounce = [this]() { m_speedDebounce->start(); };
    connect(m_upSlider, &QSlider::valueChanged, this, startDebounce);
    connect(m_downSlider, &QSlider::valueChanged, this, startDebounce);
    connect(m_upSpin, &QSpinBox::valueChanged, this, startDebounce);
    connect(m_downSpin, &QSpinBox::valueChanged, this, startDebounce);

    // Set Full Up/Down-Speed: restore both to capacity max
    auto* setFullAction = addAction(
        QIcon(QStringLiteral(":/icons/SpeedMax.ico")), tr("Set Full Up/Down-Speed"));
    connect(setFullAction, &QAction::triggered, this, [this]() {
        const int maxUp = static_cast<int>(thePrefs.maxGraphUploadRate());
        const int maxDown = static_cast<int>(thePrefs.maxGraphDownloadRate());
        {
            QSignalBlocker b1(m_upSlider), b2(m_upSpin);
            m_upSlider->setValue(maxUp);
            m_upSpin->setValue(maxUp);
        }
        {
            QSignalBlocker b1(m_downSlider), b2(m_downSpin);
            m_downSlider->setValue(maxDown);
            m_downSpin->setValue(maxDown);
        }
        sendSpeedChange();
    });

    // Throttle Up/Down-Speed: set both to 1 KB/s
    auto* throttleAction = addAction(
        QIcon(QStringLiteral(":/icons/SpeedMin.ico")), tr("Throttle Up/Down-Speed"));
    connect(throttleAction, &QAction::triggered, this, [this]() {
        {
            QSignalBlocker b1(m_upSlider), b2(m_upSpin);
            m_upSlider->setValue(1);
            m_upSpin->setValue(1);
        }
        {
            QSignalBlocker b1(m_downSlider), b2(m_downSpin);
            m_downSlider->setValue(1);
            m_downSpin->setValue(1);
        }
        sendSpeedChange();
    });

    addSeparator();

    // Connect (enabled when disconnected)
    m_connectAction = addAction(
        QIcon(QStringLiteral(":/icons/ConnectDo.ico")), tr("Connect"));
    connect(m_connectAction, &QAction::triggered,
            this, &TrayMenuManager::connectRequested);

    // Disconnect (enabled when connected)
    m_disconnectAction = addAction(
        QIcon(QStringLiteral(":/icons/ConnectDrop.ico")), tr("Disconnect"));
    connect(m_disconnectAction, &QAction::triggered,
            this, &TrayMenuManager::disconnectRequested);

    addSeparator();

    // Options
    auto* optionsAction = addAction(
        QIcon(QStringLiteral(":/icons/Preferences.ico")), tr("Options"));
    connect(optionsAction, &QAction::triggered, this, &TrayMenuManager::optionsRequested);

    addSeparator();

    // Restore (bold, matches MFC)
    auto* restoreAction = addAction(
        QIcon(QStringLiteral(":/icons/Display.ico")), tr("Restore"));
    auto restoreFont = restoreAction->font();
    restoreFont.setBold(true);
    restoreAction->setFont(restoreFont);
    connect(restoreAction, &QAction::triggered, this, &TrayMenuManager::restoreRequested);

    // Exit
    auto* exitAction = addAction(
        QIcon(QStringLiteral(":/icons/Exit.ico")), tr("Exit"));
    connect(exitAction, &QAction::triggered, this, &TrayMenuManager::exitRequested);
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void TrayMenuManager::syncSliderSpin(QSlider* slider, QSpinBox* spin)
{
    connect(slider, &QSlider::valueChanged, spin, [spin](int v) {
        QSignalBlocker blocker(spin);
        spin->setValue(v);
    });
    connect(spin, &QSpinBox::valueChanged, slider, [slider](int v) {
        QSignalBlocker blocker(slider);
        slider->setValue(v);
    });
}

void TrayMenuManager::sendSpeedChange()
{
    const auto upVal = static_cast<uint32_t>(m_upSpin->value());
    const auto downVal = static_cast<uint32_t>(m_downSpin->value());

    thePrefs.setMaxUpload(upVal);
    thePrefs.setMaxDownload(downVal);

    if (m_ipc && m_ipc->isConnected()) {
        Ipc::IpcMessage req(Ipc::IpcMsgType::SetPreferences);
        req.append(QStringLiteral("maxUpload"));
        req.append(static_cast<qint64>(upVal));
        req.append(QStringLiteral("maxDownload"));
        req.append(static_cast<qint64>(downVal));
        m_ipc->sendRequest(std::move(req));
    }
}

} // namespace eMule