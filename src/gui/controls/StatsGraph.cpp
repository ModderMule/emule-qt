/// @file StatsGraph.cpp
/// @brief Oscilloscope-style line chart widget — implementation.

#include "controls/StatsGraph.h"

#include <QFontMetrics>
#include <QPainter>
#include <QPaintEvent>

#include <algorithm>
#include <cmath>

namespace eMule {

StatsGraph::StatsGraph(int seriesCount, QWidget* parent)
    : QWidget(parent)
    , m_seriesCount(seriesCount)
    , m_series(static_cast<size_t>(seriesCount))
    , m_data(static_cast<size_t>(seriesCount))
{
    setMinimumSize(200, 80);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void StatsGraph::setSeriesInfo(int index, const QString& label,
                               const QColor& color, bool filled)
{
    if (index < 0 || index >= m_seriesCount)
        return;
    m_series[static_cast<size_t>(index)] = {label, color, filled};
    update();
}

void StatsGraph::setYRange(double lower, double upper)
{
    m_yLower = lower;
    m_yUpper = upper;
    update();
}

void StatsGraph::appendPoints(const std::vector<double>& values)
{
    const auto count = std::min(values.size(), m_data.size());
    for (size_t i = 0; i < count; ++i) {
        m_data[i].push_back(values[i]);
        if (static_cast<int>(m_data[i].size()) > kMaxPoints)
            m_data[i].pop_front();
    }
    update();
}

void StatsGraph::reset()
{
    for (auto& d : m_data)
        d.clear();
    update();
}

void StatsGraph::setBackgroundColor(const QColor& c)
{
    m_bgColor = c;
    update();
}

void StatsGraph::setGridColor(const QColor& c)
{
    m_gridColor = c;
    update();
}

void StatsGraph::setFillAll(bool fill)
{
    m_fillAll = fill;
    update();
}

double StatsGraph::niceYMax(double raw)
{
    if (raw <= 0.0)
        return 10.0;

    // Find a nice rounded maximum
    const double magnitude = std::pow(10.0, std::floor(std::log10(raw)));
    const double normalized = raw / magnitude;

    double nice;
    if (normalized <= 1.0)
        nice = 1.0;
    else if (normalized <= 2.0)
        nice = 2.0;
    else if (normalized <= 5.0)
        nice = 5.0;
    else
        nice = 10.0;

    return nice * magnitude;
}

void StatsGraph::paintEvent(QPaintEvent* /*event*/)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRect r = rect();

    // Background — dark navy matching MFC RGB(0,0,64)
    p.fillRect(r, m_bgColor);

    // Layout: left margin for Y labels, bottom margin for legend
    QFont labelFont;
    labelFont.setPointSize(7);
    const QFontMetrics fm(labelFont);

    const int legendHeight = fm.height() + 8;
    const int yLabelWidth = fm.horizontalAdvance(QStringLiteral("0000.0")) + 6;
    const int topMargin = fm.height() + 4; // room for Y-units label

    const QRect plotArea(r.left() + yLabelWidth, r.top() + topMargin,
                         r.width() - yLabelWidth - 4,
                         r.height() - topMargin - legendHeight - 2);

    if (plotArea.width() < 20 || plotArea.height() < 20)
        return;

    // Determine Y range
    double yMax = m_yUpper;
    if (yMax <= 0.0) {
        // Auto-scale: find max across all visible data
        double dataMax = 0.0;
        for (const auto& series : m_data)
            for (double v : series)
                dataMax = std::max(dataMax, v);
        yMax = niceYMax(dataMax);
    }

    // Grid — light blue dotted lines
    const QColor& gridColor = m_gridColor;
    p.setFont(labelFont);

    // Horizontal grid lines
    QPen gridPen(gridColor, 1, Qt::DotLine);
    p.setPen(gridPen);
    for (int i = 0; i <= kGridDivisions; ++i) {
        const int y = plotArea.bottom()
                      - (plotArea.height() * i / kGridDivisions);
        p.drawLine(plotArea.left(), y, plotArea.right(), y);

        // Y-axis label
        const double val = yMax * i / kGridDivisions;
        QString label;
        if (val >= 1000.0)
            label = QString::number(val, 'f', 0);
        else if (val >= 10.0)
            label = QString::number(val, 'f', 1);
        else
            label = QString::number(val, 'f', 2);

        p.setPen(gridColor);
        p.drawText(QRect(r.left(), y - fm.height() / 2, yLabelWidth - 4, fm.height()),
                   Qt::AlignRight | Qt::AlignVCenter, label);
        p.setPen(gridPen);
    }

    // Vertical grid lines — time markers every 10 minutes (matching MFC style)
    const int samplesPerGrid = static_cast<int>(600.0 / m_sampleIntervalSec); // 10 min
    int sampleCount = 0;
    for (const auto& series : m_data)
        sampleCount = std::max(sampleCount, static_cast<int>(series.size()));

    if (sampleCount > 0 && samplesPerGrid > 0) {
        for (int i = samplesPerGrid; i < sampleCount; i += samplesPerGrid) {
            const double xFrac = static_cast<double>(sampleCount - i)
                                 / std::max(1, sampleCount - 1);
            const int x = plotArea.left()
                          + static_cast<int>(xFrac * (plotArea.width() - 1));
            if (x > plotArea.left() && x < plotArea.right())
                p.drawLine(x, plotArea.top(), x, plotArea.bottom());
        }
    }

    // Y-units label at top-left
    if (!m_yUnits.isEmpty()) {
        p.setPen(gridColor);
        p.drawText(QRect(plotArea.left(), r.top(), plotArea.width(), topMargin),
                   Qt::AlignLeft | Qt::AlignVCenter, m_yUnits);
    }

    // Elapsed time label at bottom-right of plot area (matches MFC "XX.XX mins")
    if (sampleCount > 1) {
        const double elapsedMin = sampleCount * m_sampleIntervalSec / 60.0;
        const QString timeLabel = QStringLiteral("%1 mins").arg(elapsedMin, 0, 'f', 2);
        p.setPen(gridColor);
        const int timeLabelW = fm.horizontalAdvance(timeLabel) + 4;
        p.drawText(QRect(plotArea.right() - timeLabelW, plotArea.bottom() - fm.height() - 2,
                         timeLabelW, fm.height()),
                   Qt::AlignRight | Qt::AlignVCenter, timeLabel);
    }

    // Plot area border
    p.setPen(QPen(gridColor, 1, Qt::SolidLine));
    p.drawRect(plotArea);

    // Draw series lines (back to front so series 0 is behind)
    if (yMax > 0.0) {
        for (int si = m_seriesCount - 1; si >= 0; --si) {
            const auto& series = m_data[static_cast<size_t>(si)];
            const auto& info = m_series[static_cast<size_t>(si)];
            const int n = static_cast<int>(series.size());
            if (n < 2)
                continue;

            // Build point array — right-aligned (latest sample at right edge)
            const double xStep = (n > 1)
                ? static_cast<double>(plotArea.width()) / (n - 1)
                : 0.0;

            QVector<QPointF> points;
            points.reserve(n);
            for (int i = 0; i < n; ++i) {
                const double x = plotArea.left() + i * xStep;
                const double yFrac = std::clamp(series[static_cast<size_t>(i)] / yMax,
                                                0.0, 1.0);
                const double y = plotArea.bottom() - yFrac * plotArea.height();
                points.append(QPointF(x, y));
            }

            if ((info.filled || m_fillAll) && n >= 2) {
                // Filled area under the curve
                QVector<QPointF> poly = points;
                poly.append(QPointF(points.last().x(), plotArea.bottom()));
                poly.append(QPointF(points.first().x(), plotArea.bottom()));

                QColor fillColor = info.color;
                fillColor.setAlpha(60);
                p.setPen(Qt::NoPen);
                p.setBrush(fillColor);
                p.drawPolygon(QPolygonF(poly));
            }

            // Line
            QPen seriesPen(info.color, 1.5);
            p.setPen(seriesPen);
            p.setBrush(Qt::NoBrush);
            p.drawPolyline(QPolygonF(points));
        }
    }

    // Legend bar at bottom
    p.setFont(labelFont);
    const int legendY = plotArea.bottom() + 4;
    int legendX = plotArea.left();
    const int swatchSize = fm.height() - 2;
    const int spacing = 12;

    for (int si = 0; si < m_seriesCount; ++si) {
        const auto& info = m_series[static_cast<size_t>(si)];
        if (info.label.isEmpty())
            continue;

        // Color swatch
        p.fillRect(legendX, legendY + 1, swatchSize, swatchSize, info.color);
        legendX += swatchSize + 3;

        // Label text
        p.setPen(gridColor);
        const int textW = fm.horizontalAdvance(info.label);
        p.drawText(legendX, legendY, textW, legendHeight,
                   Qt::AlignLeft | Qt::AlignVCenter, info.label);
        legendX += textW + spacing;
    }
}

} // namespace eMule
