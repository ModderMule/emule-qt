#pragma once

/// @file PartBarPainter.h
/// @brief The part-bar drawing every progress delegate shares.
///
/// eD2K's download and shared lists and the Usenet queue all draw the same MFC
/// bar: slices coloured by a status byte, a thin percent strip on top, and
/// optionally the percentage as text (Display > showDwlPercentage). All of it
/// goes through BarShader, so the Display page's flat/round slider applies.

#include "controls/BarShader.h"
#include "utils/Opcodes.h"

#include <QByteArray>
#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QPainter>
#include <QRect>
#include <QString>

#include <algorithm>

namespace eMule {

/// Height of MFC's completion strip along the top of a file bar (PROGRESS_HEIGHT).
inline constexpr int kProgressStripHeight = 3;

/// Fill @p rect with one slice per byte of @p parts, each coloured by
/// @p colorOf(byte). With @p fileSize a slice is one PARTSIZE of the file (the
/// last one shorter), as MFC draws; without, the slices are equal.
template <class ColorOf>
void paintPartBar(QPainter& painter, const QRect& rect, const QByteArray& parts, ColorOf colorOf,
                  uint64_t fileSize = 0)
{
    const qsizetype count = parts.size();
    if (count <= 0 || rect.width() <= 0 || rect.height() <= 0)
        return;

    const bool bySize = fileSize > 0;
    BarShader shader(bySize ? fileSize : uint64_t(count));
    for (qsizetype i = 0; i < count; ++i) {
        const uint64_t start = bySize ? uint64_t(i) * PARTSIZE : uint64_t(i);
        const uint64_t end = bySize ? start + PARTSIZE : start + 1;
        shader.fillRange(start, end, colorOf(quint8(parts.at(i))));
    }
    shader.draw(painter, rect);
}

/// MFC's 3 px completion strip along the top of a file bar. Flat: green on a
/// light grey track. Round: shaded green only, no track (PartFile.cpp DrawStatusBar).
/// Skipped when the bar is too short to hold it.
inline void paintProgressStrip(QPainter& painter, const QRect& rect, double percent)
{
    if (rect.height() <= kProgressStripHeight)
        return;

    const bool flat = useFlatBar();
    const int filled = std::clamp(int(rect.width() * percent / 100.0 + 0.5), 0, rect.width());
    const QRect strip(rect.left(), rect.top(), rect.width(), kProgressStripHeight);
    if (flat) {
        painter.fillRect(strip, QColor(224, 224, 224));
        painter.fillRect(QRect(strip.left(), strip.top(), filled, strip.height()),
                         QColor(0, 150, 0));
    } else {
        BarShader::fillBarRect(painter, QRect(strip.left(), strip.top(), filled, strip.height()),
                               QColor(0, 224, 0), false, barDepth3D());
    }
}

/// MorphXT chunkDots: a 1 px mark on the strip at every part boundary.
inline void paintChunkDots(QPainter& painter, const QRect& rect, uint64_t fileSize)
{
    if (rect.height() <= kProgressStripHeight || fileSize <= PARTSIZE)
        return;

    const bool flat = useFlatBar();
    const QColor dot = flat ? QColor(128, 128, 128) : QColor(255, 255, 255);
    const double w = rect.width();
    for (uint64_t i = PARTSIZE; i < fileSize; i += PARTSIZE) {
        const int x = rect.left() + int(double(i) * w / double(fileSize));
        BarShader::fillBarRect(painter, QRect(x, rect.top(), 1, kProgressStripHeight), dot, flat,
                               barDepth3D());
    }
}

/// MFC's "show percentage" overlay: the cell text in white, centred on the bar.
inline void paintPercentText(QPainter& painter, const QRect& rect, const QFont& font,
                             const QString& text)
{
    if (text.isEmpty() || rect.width() <= 0)
        return;

    painter.setFont(font);
    painter.setPen(Qt::white);
    const QString shown = painter.fontMetrics().elidedText(text, Qt::ElideRight, rect.width());
    painter.drawText(rect, Qt::AlignCenter | Qt::TextSingleLine, shown);
}

} // namespace eMule
