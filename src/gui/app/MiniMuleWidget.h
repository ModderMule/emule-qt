#pragma once

#include <QtCore/qglobal.h>

/// @file MiniMuleWidget.h
/// @brief Compact floating popup showing live stats near the system tray icon.
///
/// Matches the MFC eMule MiniMule window: orange-themed frameless popup with
/// 5 stat rows (Connected, Upload, Download, Completed, Free Space) and
/// 3 action buttons (Restore, Open Incoming, Options).
/// Windows-only — guarded with Q_OS_WIN.

#ifdef Q_OS_WIN

#include <QWidget>

class QLabel;
class QPushButton;
class QSystemTrayIcon;
class QTimer;

namespace eMule {

class MiniMuleWidget : public QWidget {
    Q_OBJECT

public:
    explicit MiniMuleWidget(QSystemTrayIcon* trayIcon, QWidget* parent = nullptr);

    /// Update all stat labels.
    void updateStats(bool connected, double upKBs, double downKBs,
                     int completed, qint64 freeBytes);

    /// Position near the system tray icon and show.
    void showNearTray();

signals:
    void restoreRequested();
    void openIncomingRequested();
    void optionsRequested();

protected:
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    void setupUi();

    QSystemTrayIcon* m_trayIcon = nullptr;
    QTimer* m_autoCloseTimer = nullptr;

    // Stat labels
    QLabel* m_connectedLabel = nullptr;
    QLabel* m_uploadLabel = nullptr;
    QLabel* m_downloadLabel = nullptr;
    QLabel* m_completedLabel = nullptr;
    QLabel* m_freeSpaceLabel = nullptr;

    // Action buttons
    QPushButton* m_restoreBtn = nullptr;
    QPushButton* m_incomingBtn = nullptr;
    QPushButton* m_optionsBtn = nullptr;
};

} // namespace eMule

#endif // Q_OS_WIN
