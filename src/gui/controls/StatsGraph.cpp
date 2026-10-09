#include "pch.h"
/// @file StatsGraph.cpp
/// @brief Oscilloscope-style line chart widget — implementation.

#include "controls/StatsGraph.h"

#include "utils/StringUtils.h"

#include <QFontMetrics>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QToolTip>


namespace eMule {

StatsGraph::StatsGraph(int seriesCount, QWidget* parent)
    : QWidget(parent)
    , m_seriesCount(seriesCount)
    , m_series(static_cast<size_t>(seriesCount))
    , m_data(static_cast<size_t>(seriesCount))
{
    setMinimumSize(200, 80);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMouseTracking(true);   // the value tooltip follows the pointer
}

void StatsGraph::setSeriesFilled(int index, bool filled)
{
    if (index < 0 || index >= m_seriesCount)
        return;
    m_series[static_cast<size_t>(index)].filled = filled;
    update();
}

QString StatsGraph::spanCaption(int plotWidth, double sampleIntervalSec)
{
    const auto shown = static_cast<qint64>(plotWidth * sampleIntervalSec);
    return shown > 0 ? formatSecondsHM(shown) : tr("Stopped");
}

QRect StatsGraph::plotRect() const
{
    QFont labelFont;
    labelFont.setPointSize(7);
    const QFontMetrics fm(labelFont);
    const int legendHeight = fm.height() + 8;
    const int yLabelWidth = fm.horizontalAdvance(QStringLiteral("0000.0")) + 6;
    const int topMargin = fm.height() + 4;
    const QRect r = rect();
    return {r.left() + yLabelWidth, r.top() + topMargin, r.width() - yLabelWidth - 4,
            r.height() - topMargin - legendHeight - 2};
}

double StatsGraph::currentYMax() const
{
    if (m_yUpper > 0.0)
        return m_yUpper;
    double dataMax = 0.0;
    for (const auto& series : m_data)
        for (double v : series)
            dataMax = std::max(dataMax, v);
    return niceYMax(dataMax);
}

QString StatsGraph::tooltipAt(const QPoint& pos, const QDateTime& now) const
{
    // MFC COScopeCtrl::OnMouseMove (OScopeCtrl.cpp:842-891): the value is the one the
    // pointer's height stands for, the time the one its column stands for.
    const QRect plot = plotRect();
    if (!plot.contains(pos) || plot.height() <= 0)
        return {};
    const double value = m_yLower + (plot.bottom() - pos.y()) * (currentYMax() - m_yLower) / plot.height();
    const qint64 ago = secondsAgoAt(pos.x(), plot.right(), m_sampleIntervalSec);
    return tr("%1: %2 @ %3 (%4 ago)")
        .arg(m_yUnits)
        .arg(static_cast<qint64>(std::max(value, 0.0)))
        .arg(QLocale().toString(now.addSecs(-ago), QLocale::ShortFormat), formatSecondsLongHM(ago));
}

void StatsGraph::mouseMoveEvent(QMouseEvent* event)
{
    const QString text = tooltipAt(event->position().toPoint(), QDateTime::currentDateTime());
    if (text.isEmpty())
        QToolTip::hideText();
    else
        QToolTip::showText(event->globalPosition().toPoint(), text, this);
    QWidget::mouseMoveEvent(event);
}

void StatsGraph::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
        emit doubleClicked();
    QWidget::mouseDoubleClickEvent(event);
}

void StatsGraph::leaveEvent(QEvent* event)
{
    QToolTip::hideText();
    QWidget::leaveEvent(event);
}

void StatsGraph::setSeriesInfo(int index, const QString& label,
                               const QColor& color, bool filled)
{
    if (index < 0 || index >= m_seriesCount)
        return;
    m_series[static_cast<size_t>(index)] = {label, color, filled};
    update();
}

void StatsGraph::setSeriesColor(int index, const QColor& color)
{
    if (index < 0 || index >= m_seriesCount || !color.isValid())
        return;
    m_series[static_cast<size_t>(index)].color = color;
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
    if (!c.isValid())
        return;   // keep the last good colour rather than painting on an invalid one
    m_bgColor = c;
    update();
}

void StatsGraph::setGridColor(const QColor& c)
{
    if (!c.isValid())
        return;
    m_gridColor = c;
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

    const QRect plotArea = plotRect();

    if (plotArea.width() < 20 || plotArea.height() < 20)
        return;

    // Determine Y range
    const double yMax = currentYMax();

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

    // Vertical grid lines — one per hour the plot spans, counted from "now" at the
    // right edge (MFC m_nXGrids, StatisticsDlg.cpp:2536-2537).
    if (const int pixelsPerHour = static_cast<int>(3600.0 / m_sampleIntervalSec); pixelsPerHour > 0) {
        for (int x = plotArea.right() - pixelsPerHour; x > plotArea.left(); x -= pixelsPerHour)
            p.drawLine(x, plotArea.top(), x, plotArea.bottom());
    }

    // Y-units label at top-left
    if (!m_yUnits.isEmpty()) {
        p.setPen(gridColor);
        p.drawText(QRect(plotArea.left(), r.top(), plotArea.width(), topMargin),
                   Qt::AlignLeft | Qt::AlignVCenter, m_yUnits);
    }

    // The time the plot spans, bottom-right (MFC SetXUnits, StatisticsDlg.cpp:2539-2547)
    {
        const QString timeLabel = spanCaption(plotArea.width(), m_sampleIntervalSec);
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

            // One pixel per sample, newest at the right edge; what does not fit has
            // scrolled out on the left.
            const int first = std::max(0, n - 1 - plotArea.width());
            QVector<QPointF> points;
            points.reserve(n - first);
            for (int i = first; i < n; ++i) {
                const double x = sampleX(plotArea.right(), n, i);
                const double yFrac = std::clamp(series[static_cast<size_t>(i)] / yMax,
                                                0.0, 1.0);
                const double y = plotArea.bottom() - yFrac * plotArea.height();
                points.append(QPointF(x, y));
            }

            if (info.filled) {
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
