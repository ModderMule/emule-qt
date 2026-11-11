#pragma once

/// @file SpeedLimitPopup.h
/// @brief Status bar speed pop-up: a reduced MiniMule on every OS.
///
/// The tray menu's speed block (SpeedLimitWidget) with Set Full, Throttle,
/// Pause Usenet and Options. Closes on a click outside it.

#include <QFrame>

class QToolButton;

namespace eMule {

class IpcClient;
class SpeedLimitWidget;

class SpeedLimitPopup : public QFrame {
    Q_OBJECT

public:
    explicit SpeedLimitPopup(QWidget* parent = nullptr);

    void setIpcClient(IpcClient* ipc);

    /// Refresh from the preferences and show right-aligned above @p anchor
    void showAbove(QWidget* anchor);

    [[nodiscard]] SpeedLimitWidget* speedWidget() const { return m_speed; }

signals:
    void optionsRequested();

private:
    QToolButton* addButton(const QString& iconPath, const QString& text);
    void updateButtons();

    IpcClient* m_ipc = nullptr;
    SpeedLimitWidget* m_speed = nullptr;
    QToolButton* m_fullBtn = nullptr;
    QToolButton* m_throttleBtn = nullptr;
    QToolButton* m_usenetBtn = nullptr;
    QToolButton* m_optionsBtn = nullptr;
};

} // namespace eMule
