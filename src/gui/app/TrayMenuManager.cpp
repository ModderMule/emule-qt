#include "pch.h"
#include "app/TrayMenuManager.h"

#include "app/IpcClient.h"
#include "app/SkinIcons.h"
#include "controls/SpeedLimitWidget.h"

#include <QIcon>
#include <QMouseEvent>
#include <QSignalBlocker>
#include <QWidgetAction>

namespace eMule {

TrayMenuManager::TrayMenuManager(QWidget* parent)
    : QMenu(parent)
{
    buildMenu();
}

// ---------------------------------------------------------------------------
// Public
// ---------------------------------------------------------------------------

void TrayMenuManager::setIpcClient(IpcClient* ipc)
{
    m_ipc = ipc;
    m_speed->setIpcClient(ipc);
    if (!m_ipc)
        return;
    // The menu can be open while another client pauses the engine.
    connect(m_ipc, &IpcClient::usenetEnginePausedChanged, m_usenetPauseAction,
            &QAction::setChecked, Qt::UniqueConnection);
}

void TrayMenuManager::updateState(bool ed2kConnected, bool kadRunning, bool ipcConnected)
{
    const bool connected = ed2kConnected || kadRunning;

    m_connectAction->setEnabled(ipcConnected && !connected);
    m_disconnectAction->setEnabled(ipcConnected && connected);

    m_usenetPauseAction->setEnabled(ipcConnected);
    {
        QSignalBlocker block(m_usenetPauseAction);
        m_usenetPauseAction->setChecked(m_ipc && m_ipc->usenetEnginePaused());
    }

    m_speed->refresh(ipcConnected);
    updateThrottleAction();
}

void TrayMenuManager::mouseReleaseEvent(QMouseEvent* event)
{
    if (QAction* action = actionAt(event->position().toPoint());
        action && action == m_throttleAction && action->isEnabled()) {
        action->trigger();
        return;
    }
    QMenu::mouseReleaseEvent(event);
}

// ---------------------------------------------------------------------------
// Private — menu construction
// ---------------------------------------------------------------------------

void TrayMenuManager::buildMenu()
{
    // Header: Speed.ico, "eMule Speed", bold, disabled (matches MFC)
    auto* header = addAction(skinIcon(QStringLiteral(":/icons/Speed.ico")),
                             tr("eMule Speed"));
    header->setEnabled(false);
    auto headerFont = header->font();
    headerFont.setBold(true);
    header->setFont(headerFont);

    // Download/Upload sliders
    m_speed = new SpeedLimitWidget;
    auto* speedAction = new QWidgetAction(this);
    speedAction->setDefaultWidget(m_speed);
    addAction(speedAction);
    connect(m_speed, &SpeedLimitWidget::limitsChanged, this, &TrayMenuManager::updateThrottleAction);

    // Set Full Up/Down-Speed: both to the line capacity
    auto* setFullAction = addAction(
        skinIcon(QStringLiteral(":/icons/SpeedMax.ico")), tr("Set Full Up/Down-Speed"));
    connect(setFullAction, &QAction::triggered, m_speed, &SpeedLimitWidget::setFull);

    // Throttle Up/Down-Speed: halved per click, down to 10 up / 40 down and the ratio
    m_throttleAction = addAction(
        skinIcon(QStringLiteral(":/icons/SpeedMin.ico")), tr("Throttle Up/Down-Speed"));
    connect(m_throttleAction, &QAction::triggered, m_speed, &SpeedLimitWidget::throttle);

    addSeparator();

    // The whole Usenet engine, checked while paused. triggered(), not toggled():
    // the state follows the daemon's push, and only a click should send.
    m_usenetPauseAction = addAction(
        skinIcon(QStringLiteral(":/icons/Pause.ico")), tr("Pause Usenet"));
    m_usenetPauseAction->setCheckable(true);
    connect(m_usenetPauseAction, &QAction::triggered, this, [this](bool checked) {
        if (m_ipc)
            m_ipc->requestUsenetPause(checked);
    });

    addSeparator();

    // Connect (enabled when disconnected)
    m_connectAction = addAction(
        skinIcon(QStringLiteral(":/icons/ConnectDo.ico")), tr("Connect"));
    connect(m_connectAction, &QAction::triggered,
            this, &TrayMenuManager::connectRequested);

    // Disconnect (enabled when connected)
    m_disconnectAction = addAction(
        skinIcon(QStringLiteral(":/icons/ConnectDrop.ico")), tr("Disconnect"));
    connect(m_disconnectAction, &QAction::triggered,
            this, &TrayMenuManager::disconnectRequested);

    addSeparator();

    // Options
    auto* optionsAction = addAction(
        skinIcon(QStringLiteral(":/icons/Preferences.ico")), tr("Options"));
    connect(optionsAction, &QAction::triggered, this, &TrayMenuManager::optionsRequested);

    addSeparator();

    // Restore (bold, matches MFC)
    auto* restoreAction = addAction(
        skinIcon(QStringLiteral(":/icons/Display.ico")), tr("Restore"));
    auto restoreFont = restoreAction->font();
    restoreFont.setBold(true);
    restoreAction->setFont(restoreFont);
    connect(restoreAction, &QAction::triggered, this, &TrayMenuManager::restoreRequested);

    // Exit
    auto* exitAction = addAction(
        skinIcon(QStringLiteral(":/icons/Exit.ico")), tr("Exit"));
    connect(exitAction, &QAction::triggered, this, &TrayMenuManager::exitRequested);
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void TrayMenuManager::updateThrottleAction()
{
    m_throttleAction->setEnabled(m_ipc && m_ipc->isConnected() && m_speed->canThrottle());
}

} // namespace eMule