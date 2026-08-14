#pragma once

/// @file TrayMeterIcon.h
/// @brief Tray icon with a download-rate meter bar — port of MFC CMeterIcon.
///
/// MFC generates its tray icon on the fly: the connection-state icon with a bar down
/// its right edge whose height is the download rate as a percentage of the configured
/// capacity (srchybrid/MeterIcon.cpp, driven from CemuleDlg::UpdateTrayIcon,
/// srchybrid/EmuleDlg.cpp:2023). Painting into a QPixmap is what makes that portable:
/// the Windows shell, the macOS menu bar and Linux's StatusNotifierItem all accept a
/// pixmap icon, so the same code covers every platform.

#include <QColor>
#include <QIcon>
#include <QPixmap>

namespace eMule {

class TrayMeterIcon {
public:
    /// Steps the bar is quantised to. MFC uses the same count for the cookie that
    /// decides whether the icon is worth regenerating (srchybrid/EmuleDlg.cpp:2025).
    static constexpr int kLevels = 16;

    /// Edge length of the generated icon. The tray icons themselves are 16x16, so this
    /// is a clean 4x — the platform scales it down to whatever the panel asks for.
    static constexpr int kDefaultSize = 64;

    /// Bar step for @p percent: 0 (no bar) through kLevels (full height).
    ///
    /// Callers cache this and skip the repaint when it has not moved — on Linux every
    /// QSystemTrayIcon::setIcon is a DBus round trip, and the rate updates once a
    /// second. MFC's cookie counts down instead of up because it indexes an icon
    /// table; only the granularity matters here.
    [[nodiscard]] static int levelForPercent(int percent);

    /// @p base with the meter bar drawn over it.
    ///
    /// @param percent 0-100; anything <= 0 draws no bar at all.
    /// @param barColor an invalid colour draws no bar, so a caller that has nothing
    ///                 sensible to paint with still gets the plain icon back.
    ///
    /// The bar is snapped to levelForPercent(), so two percentages that share a level
    /// produce identical pixmaps — that is what makes skipping the repaint safe.
    [[nodiscard]] static QPixmap render(const QIcon& base, int percent,
                                        const QColor& barColor,
                                        int sizePx = kDefaultSize);
};

} // namespace eMule
