#include "pch.h"
/// @file BarShader.cpp
/// @brief Port of MFC CBarShader.

#include "controls/BarShader.h"

#include "prefs/Preferences.h"

#include <QPainter>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace eMule {

bool useFlatBar()
{
    return thePrefs.depth3D() <= 0;
}

int barDepth3D()
{
    return std::clamp(thePrefs.depth3D(), 0, 5);
}

BarShader::BarShader(uint64_t fileSize)
{
    setFileSize(fileSize);
}

void BarShader::setFileSize(uint64_t fileSize)
{
    m_fileSize = std::max<uint64_t>(fileSize, 1);
    fill(Qt::black);
}

void BarShader::fill(const QColor& color)
{
    m_spans.clear();
    m_spans[0] = color;
    m_spans[m_fileSize] = Qt::black;
}

void BarShader::fillRange(uint64_t start, uint64_t end, const QColor& color)
{
    end = std::min(end, m_fileSize);
    if (start >= end)
        return;

    // colour of the byte at 'end' carries on after the new span
    const QColor endColor = std::prev(m_spans.upper_bound(end))->second;
    m_spans.erase(m_spans.lower_bound(start), m_spans.upper_bound(end));
    m_spans[start] = color;
    m_spans[end] = endColor;
}

std::vector<float> BarShader::shadeModifiers(int height, int level)
{
    // MFC: depth 2 is the deepest; m_Modifiers[count-1] is always 1
    const int count = std::max(1, (height + 1) / 2);
    if (count < 2)
        return std::vector<float>(static_cast<size_t>(count), 1.0f);

    const int depth = 7 - std::clamp(level, 1, 5);
    const double piOverDepth = std::numbers::pi / depth;
    const double base = piOverDepth * (depth / 2.0 - 1);
    const double increment = piOverDepth / (count - 1);

    std::vector<float> mods(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i)
        mods[static_cast<size_t>(i)] = static_cast<float>(std::sin(base + i * increment));
    return mods;
}

void BarShader::fillBarRect(QPainter& painter, const QRect& rect, const QColor& color,
                            bool flat, int level)
{
    if (rect.isEmpty())
        return;
    if (flat || color.rgb() == qRgb(0, 0, 0)) {
        painter.fillRect(rect, color);
        return;
    }

    // Rows darken symmetrically towards the top and bottom edge
    const std::vector<float> mods = shadeModifiers(rect.height(), level);
    const int top = rect.top();
    const int bottom = rect.top() + rect.height();
    for (size_t i = 0; i < mods.size(); ++i) {
        const float m = mods[i];
        const QColor shaded(int(float(color.red()) * m + .5f), int(float(color.green()) * m + .5f),
                            int(float(color.blue()) * m + .5f));
        const int row = static_cast<int>(i);
        painter.fillRect(rect.left(), top + row, rect.width(), 1, shaded);
        if (bottom - row - 1 != top + row)
            painter.fillRect(rect.left(), bottom - row - 1, rect.width(), 1, shaded);
    }
}

void BarShader::draw(QPainter& painter, const QRect& rect) const
{
    draw(painter, rect, useFlatBar(), barDepth3D());
}

void BarShader::draw(QPainter& painter, const QRect& rect, bool flat, int level) const
{
    const int width = rect.width();
    if (width <= 0 || rect.height() <= 0)
        return;

    struct Span { double start; double end; QColor color; };
    std::vector<Span> spans;
    spans.reserve(m_spans.size());
    for (auto it = m_spans.begin(); std::next(it) != m_spans.end(); ++it)
        spans.push_back({double(it->first), double(std::next(it)->first), it->second});
    if (spans.empty())
        return;

    // Per pixel column: the covering colour, or the weighted average of every span
    // inside it (MFC Draw()'s sub-pixel branch). Equal neighbours merge into one rect.
    const double bytesPerPixel = double(m_fileSize) / width;
    size_t first = 0;
    int runStart = 0;
    QRgb runColor = 0;
    const auto flush = [&](int endColumn) {
        if (endColumn > runStart)
            fillBarRect(painter, QRect(rect.left() + runStart, rect.top(), endColumn - runStart,
                                       rect.height()),
                        QColor(runColor), flat, level);
    };

    for (int px = 0; px < width; ++px) {
        const double b0 = px * bytesPerPixel;
        const double b1 = (px + 1) * bytesPerPixel;
        while (first + 1 < spans.size() && spans[first].end <= b0)
            ++first;

        QRgb rgb;
        if (spans[first].end >= b1 || first + 1 == spans.size()) {
            rgb = spans[first].color.rgb();
        } else {
            double r = 0, g = 0, b = 0;
            for (size_t k = first; k < spans.size() && spans[k].start < b1; ++k) {
                const double w = (std::min(spans[k].end, b1) - std::max(spans[k].start, b0))
                                 / bytesPerPixel;
                r += spans[k].color.red() * w;
                g += spans[k].color.green() * w;
                b += spans[k].color.blue() * w;
            }
            rgb = qRgb(std::clamp(int(r + .5), 0, 255), std::clamp(int(g + .5), 0, 255),
                       std::clamp(int(b + .5), 0, 255));
        }

        if (px == 0) {
            runColor = rgb;
        } else if (rgb != runColor) {
            flush(px);
            runStart = px;
            runColor = rgb;
        }
    }
    flush(width);
}

} // namespace eMule
