#pragma once

/// Painted marks, no image files: the red "!" of unread chats on a toolbar button and the
/// moderator star of ed3k rooms. Header-only, so tests that compile one panel need nothing more.

#include <QIcon>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QtMath>

#include <array>

namespace eMule::BadgeIcons {

/// @p base with a red circle and a white "!" in its bottom right corner, at the usual sizes.
[[nodiscard]] inline QIcon withAlertBadge(const QIcon& base)
{
    QIcon out;
    for (const int size : std::array{16, 24, 32, 48, 64}) {
        QPixmap pm = base.pixmap(size, size);
        if (pm.isNull())
            continue;
        const qreal dpr = pm.devicePixelRatio();
        const qreal w = pm.width() / dpr;
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        const qreal d = std::max<qreal>(7.0, w * 0.5);
        const QRectF circle(w - d, w - d, d, d);
        p.setPen(QPen(Qt::white, std::max<qreal>(1.0, d / 10)));
        p.setBrush(QColor(0xd9, 0x1e, 0x18));
        p.drawEllipse(circle.adjusted(0.5, 0.5, -0.5, -0.5));
        // the "!": a bar and a dot
        p.setPen(Qt::NoPen);
        p.setBrush(Qt::white);
        const qreal bw = d * 0.16;
        const qreal cx = circle.center().x();
        p.drawRoundedRect(QRectF(cx - bw / 2, circle.top() + d * 0.2, bw, d * 0.4), bw / 2, bw / 2);
        p.drawEllipse(QPointF(cx, circle.top() + d * 0.75), bw * 0.6, bw * 0.6);
        p.end();
        out.addPixmap(pm);
    }
    return out.isNull() ? base : out;
}

/// The mark of a room moderator: a gold five-point star, @p size square.
[[nodiscard]] inline QPixmap moderatorMark(int size = 16, qreal dpr = 1.0)
{
    QPixmap pm(QSize(size, size) * dpr);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    QPainterPath star;
    const QPointF c(size / 2.0, size / 2.0 + size * 0.04);
    const qreal outer = size * 0.48;
    const qreal inner = outer * 0.45;
    for (int i = 0; i < 10; ++i) {
        const qreal r = (i % 2) ? inner : outer;
        const qreal a = qDegreesToRadians(-90.0 + i * 36.0);
        const QPointF pt(c.x() + r * std::cos(a), c.y() + r * std::sin(a));
        i == 0 ? star.moveTo(pt) : star.lineTo(pt);
    }
    star.closeSubpath();
    p.setPen(QPen(QColor(0xa0, 0x6a, 0x00), std::max<qreal>(1.0, size / 16.0)));
    p.setBrush(QColor(0xf5, 0xbf, 0x1d));
    p.drawPath(star);
    return pm;
}

} // namespace eMule::BadgeIcons
