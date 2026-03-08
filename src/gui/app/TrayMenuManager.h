#pragma once

/// @file TrayMenuManager.h
/// @brief System tray context menu with speed controls (ported from MFC TrayMenuBtn).

#include <QMenu>

class QAction;
class QSlider;
class QSpinBox;
class QTimer;

namespace Ipc { class IpcMessage; }

namespace eMule {

class IpcClient;

/// Manages the system tray context menu: speed sliders, connect/disconnect,
/// restore, options, and exit actions.
class TrayMenuManager : public QMenu {
    Q_OBJECT

public:
    explicit TrayMenuManager(QWidget* parent = nullptr);

    void setIpcClient(IpcClient* ipc) { m_ipc = ipc; }

    /// Refresh slider ranges/values and connect action state.
    void updateState(bool ed2kConnected, bool kadRunning, bool ipcConnected);

signals:
    void restoreRequested();
    void connectRequested();
    void disconnectRequested();
    void optionsRequested();
    void exitRequested();

private:
    void buildMenu();
    void syncSliderSpin(QSlider* slider, QSpinBox* spin);
    void sendSpeedChange();

    IpcClient* m_ipc = nullptr;

    QAction* m_connectAction = nullptr;
    QAction* m_disconnectAction = nullptr;
    QSlider* m_upSlider = nullptr;
    QSpinBox* m_upSpin = nullptr;
    QSlider* m_downSlider = nullptr;
    QSpinBox* m_downSpin = nullptr;
    QTimer* m_speedDebounce = nullptr;
};

} // namespace eMule