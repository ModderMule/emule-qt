#include "pch.h"
/// @file TrayMeterIcon.cpp
/// @brief Tray icon meter bar — implementation.

#include "controls/TrayMeterIcon.h"

#include <QPainter>
#include <QRect>

#include <algorithm>

namespace eMule {

int TrayMeterIcon::levelForPercent(int percent)
{
    if (percent <= 0)
        return 0;
    // MFC: (iPercent * 15 / 100) + 1, so any rate above zero shows at least a stub
    // (srchybrid/EmuleDlg.cpp:2025).
    return std::clamp(percent, 1, 100) * (kLevels - 1) / 100 + 1;
}

QPixmap TrayMeterIcon::render(const QIcon& base, int percent, const QColor& barColor,
                              int sizePx)
{
    const int size = std::max(8, sizePx);

    QPixmap canvas(size, size);
    canvas.fill(Qt::transparent);

    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    // Scale explicitly rather than leaving it to QIcon::pixmap(): the tray icons are
    // 16x16 .ico files and QPixmapIconEngine hands back the source size for anything
    // larger, which would leave the meter drawn beside a quarter-size icon.
    QPixmap basePixmap = base.pixmap(QSize(size, size));
    if (!basePixmap.isNull()) {
        basePixmap.setDevicePixelRatio(1.0);
        if (basePixmap.size() != QSize(size, size)) {
            basePixmap = basePixmap.scaled(size, size, Qt::KeepAspectRatio,
                                           Qt::SmoothTransformation);
        }
        painter.drawPixmap((size - basePixmap.width()) / 2,
                           (size - basePixmap.height()) / 2, basePixmap);
    }

    const int level = levelForPercent(percent);
    if (level > 0 && barColor.isValid()) {
        // MFC draws the two rightmost columns of a 16 px icon, rising from the bottom
        // (srchybrid/MeterIcon.cpp:159-167). Both are kept proportional so the bar
        // looks the same at any icon size.
        const int barWidth = std::max(2, size / 8);
        const int barHeight = level * (size - 1) / kLevels + 1;
        painter.fillRect(QRect(size - barWidth, size - barHeight, barWidth, barHeight),
                         barColor);
    }
    painter.end();

    return canvas;
}

} // namespace eMule
