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

#include <algorithm>
#include <cmath>

namespace eMule {

/// @p a mixed @p weight of the way towards @p b.
[[nodiscard]] inline QColor blend(const QColor& a, const QColor& b, qreal weight)
{
    const auto w = static_cast<float>(weight);   // Qt 6 colour channels are float
    return QColor::fromRgbF(a.redF()   * (1 - w) + b.redF()   * w,
                            a.greenF() * (1 - w) + b.greenF() * w,
                            a.blueF()  * (1 - w) + b.blueF()  * w);
}

/// WCAG contrast ratio of two opaque colours, 1 to 21.
[[nodiscard]] inline qreal contrastRatio(const QColor& a, const QColor& b)
{
    const auto luminance = [](const QColor& c) {
        const auto lin = [](float v) {
            return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f);
        };
        return 0.2126 * lin(c.redF()) + 0.7152 * lin(c.greenF()) + 0.0722 * lin(c.blueF());
    };
    const qreal la = luminance(a);
    const qreal lb = luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

/// @p preferred when it reads on @p background (4.5:1), else black or white.
[[nodiscard]] inline QColor readableTextOn(const QColor& background, const QColor& preferred)
{
    if (contrastRatio(preferred, background) >= 4.5)
        return preferred;
    const QColor black(Qt::black);
    const QColor white(Qt::white);
    return contrastRatio(black, background) >= contrastRatio(white, background) ? black : white;
}

/// List text faded @p weight of the way into the list background. 0.5 / 0.75 are MFC's
/// RGB 128 / 192 greys on a white list.
[[nodiscard]] inline QColor dimmedText(qreal weight)
{
    const QPalette pal = QGuiApplication::palette();
    return blend(pal.color(QPalette::Text), pal.color(QPalette::Base), weight);
}

} // namespace eMule
