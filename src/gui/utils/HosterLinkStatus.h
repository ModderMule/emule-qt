#pragma once

/// @file HosterLinkStatus.h
/// @brief A hoster link's check state as the core sends it (Ed3kHosterJoin::LinkStatus) and
/// its dot: the links dialog of a web search row and the Link Grabber show it.

#include <QColor>
#include <QHash>
#include <QIcon>
#include <QPainter>
#include <QPixmap>

#include <functional>

namespace eMule::gui {

/// Ed3kHosterJoin::LinkStatus, as PushHosterLinkCheck and PushLinkGrabber carry it.
enum HosterLinkStatus { LinkChecking = 0, LinkOnline, LinkOffline, LinkUnknown };

/// 16 px icon, 1x and 2x, drawn by @p paint into a 16 x 16 box.
inline QIcon drawnIcon(const std::function<void(QPainter&)>& paint)
{
    QIcon icon;
    for (const int scale : {1, 2}) {
        QPixmap pm(16 * scale, 16 * scale);
        pm.setDevicePixelRatio(scale);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        paint(p);
        p.end();
        icon.addPixmap(pm);
    }
    return icon;
}

/// The status dot: green online, red offline, grey unknown; none while checking.
inline QIcon linkStatusIcon(int status)
{
    QColor fill;
    switch (status) {
    case LinkOnline: fill = QColor(0x2E, 0xB8, 0x3C); break;
    case LinkOffline: fill = QColor(0xD9, 0x30, 0x25); break;
    case LinkUnknown: fill = QColor(0x9A, 0x9A, 0x9A); break;
    default: return {};
    }
    static QHash<int, QIcon> cache;
    if (const auto it = cache.constFind(status); it != cache.cend())
        return *it;
    return cache[status] = drawnIcon([fill](QPainter& p) {
        p.setPen(QPen(fill.darker(140), 1.0));
        p.setBrush(fill);
        p.drawEllipse(QRectF(3.5, 3.5, 9, 9));
    });
}

} // namespace eMule::gui
