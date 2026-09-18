#pragma once

/// @file PartBarPainter.h
/// @brief The part-bar drawing every progress delegate shares.
///
/// eD2K's download and shared lists and the Usenet queue all draw the same MFC
/// bar: equal slices coloured by a status byte, a thin percent strip on top.
/// Only the colour table differs, so that is the one thing passed in.

#include <QByteArray>
#include <QColor>
#include <QPainter>
#include <QRect>

#include <algorithm>

namespace eMule {

/// Fill @p rect with one equal slice per byte of @p parts, each coloured by
/// @p colorOf(byte). The last slice runs to the edge, so rounding leaves no gap.
template <class ColorOf>
void paintPartBar(QPainter& painter, const QRect& rect, const QByteArray& parts, ColorOf colorOf)
{
    const qsizetype count = parts.size();
    if (count <= 0 || rect.width() <= 0 || rect.height() <= 0)
        return;

    const double sliceWidth = double(rect.width()) / double(count);
    for (qsizetype i = 0; i < count; ++i) {
        const int x0 = rect.left() + int(double(i) * sliceWidth);
        const int x1 = i == count - 1 ? rect.right() + 1
                                      : rect.left() + int(double(i + 1) * sliceWidth);
        painter.fillRect(x0, rect.top(), x1 - x0, rect.height(), colorOf(quint8(parts.at(i))));
    }
}

/// MFC's 3 px completion strip along the top of a file bar: light grey track,
/// green share. Skipped when the bar is too short to hold it.
inline void paintProgressStrip(QPainter& painter, const QRect& rect, double percent)
{
    constexpr int kStripHeight = 3;
    if (rect.height() <= kStripHeight)
        return;

    painter.fillRect(QRect(rect.left(), rect.top(), rect.width(), kStripHeight),
                     QColor(224, 224, 224));
    if (percent > 0.0) {
        const int filled = std::min(int(rect.width() * percent / 100.0), rect.width());
        painter.fillRect(QRect(rect.left(), rect.top(), filled, kStripHeight),
                         QColor(0, 224, 0));
    }
}

} // namespace eMule
