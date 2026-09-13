#pragma once

/// @file ColorUtils.h
/// @brief Palette-relative colour mixing for painted widgets and list models.
///
/// MFC hard-codes its greys and shades against a white list (RGB 128/192, blue 0,0,255).
/// Mixing with the palette instead gives the same values in light mode and still dims,
/// rather than brightens, on a dark background.

#include <QColor>
#include <QGuiApplication>
#include <QPalette>

namespace eMule {

/// @p a mixed @p weight of the way towards @p b.
[[nodiscard]] inline QColor blend(const QColor& a, const QColor& b, qreal weight)
{
    const auto w = static_cast<float>(weight);   // Qt 6 colour channels are float
    return QColor::fromRgbF(a.redF()   * (1 - w) + b.redF()   * w,
                            a.greenF() * (1 - w) + b.greenF() * w,
                            a.blueF()  * (1 - w) + b.blueF()  * w);
}

/// List text faded @p weight of the way into the list background. 0.5 / 0.75 are MFC's
/// RGB 128 / 192 greys on a white list.
[[nodiscard]] inline QColor dimmedText(qreal weight)
{
    const QPalette pal = QGuiApplication::palette();
    return blend(pal.color(QPalette::Text), pal.color(QPalette::Base), weight);
}

} // namespace eMule
