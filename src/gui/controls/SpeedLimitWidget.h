#pragma once

/// @file SpeedLimitWidget.h
/// @brief Download/Upload limit sliders, shared by the tray menu and the status bar pop-up.
///
/// Ranges run from the throttle floor to the line capacity; both sliders follow MFC's
/// UL:DL ratio while moved (SpeedRules.h). Changes go to the core after a short pause.

#include "prefs/SpeedRules.h"

#include <QWidget>

class QSlider;
class QSpinBox;
class QTimer;

namespace eMule {

class IpcClient;

class SpeedLimitWidget : public QWidget {
    Q_OBJECT

public:
    explicit SpeedLimitWidget(QWidget* parent = nullptr);

    void setIpcClient(IpcClient* ipc);

    /// Re-read capacity and limits from the preferences
    void refresh(bool ipcConnected);

    /// Both limits to the line capacity
    void setFull();
    /// Both limits halved, held at the floors and the ratio
    void throttle();
    [[nodiscard]] bool canThrottle() const;

    /// Limits as shown, KB/s
    [[nodiscard]] SpeedRules::Limits limits() const;

signals:
    /// The limits changed: by the user, Full/Throttle or the core's answer
    void limitsChanged();

private:
    QSlider* addRow(int row, const QString& iconPath, const QString& label, QSpinBox*& spin);
    void onUpMoved(int value);
    void onDownMoved(int value);
    void showLimits(SpeedRules::Limits limits);
    void commit();
    void sendLimits();
    [[nodiscard]] uint32 capUp() const;
    [[nodiscard]] uint32 capDown() const;

    IpcClient* m_ipc = nullptr;
    QSlider* m_downSlider = nullptr;
    QSpinBox* m_downSpin = nullptr;
    QSlider* m_upSlider = nullptr;
    QSpinBox* m_upSpin = nullptr;
    QTimer* m_debounce = nullptr;
};

} // namespace eMule
