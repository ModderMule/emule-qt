#pragma once

/// @file TrayMenuManager.h
/// @brief System tray context menu with speed controls (ported from MFC TrayMenuBtn).

#include <QMenu>

class QAction;

namespace eMule {

class IpcClient;
class SpeedLimitWidget;

/// Manages the system tray context menu: speed sliders, connect/disconnect,
/// restore, options, and exit actions.
class TrayMenuManager : public QMenu {
    Q_OBJECT

public:
    explicit TrayMenuManager(QWidget* parent = nullptr);

    void setIpcClient(IpcClient* ipc);

    /// Refresh slider ranges/values and connect action state.
    void updateState(bool ed2kConnected, bool kadRunning, bool ipcConnected);

protected:
    /// Throttle steps down per click: it keeps the menu open
    void mouseReleaseEvent(QMouseEvent* event) override;

signals:
    void restoreRequested();
    void connectRequested();
    void disconnectRequested();
    void optionsRequested();
    void exitRequested();

private:
    void buildMenu();
    void updateThrottleAction();

    IpcClient* m_ipc = nullptr;

    QAction* m_connectAction = nullptr;
    QAction* m_disconnectAction = nullptr;
    QAction* m_usenetPauseAction = nullptr;
    QAction* m_throttleAction = nullptr;
    SpeedLimitWidget* m_speed = nullptr;
};

} // namespace eMule