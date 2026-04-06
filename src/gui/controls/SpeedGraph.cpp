#include "pch.h"
/// @file SpeedGraph.cpp
/// @brief Compact dual-pane speed graph — implementation.

#include "controls/SpeedGraph.h"

#include <QFontMetrics>
#include <QPainter>
#include <QPaintEvent>

#include <algorithm>
#include <cmath>

namespace eMule {

static constexpr int kGridSpacing = 10; // pixels between grid lines

SpeedGraph::SpeedGraph(QWidget* parent)
    : QWidget(parent)
{
    setFixedHeight(50);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

    // Scrolling grid timer — 200ms matches original eMule
    m_gridTimer.setInterval(200);
    connect(&m_gridTimer, &QTimer::timeout, this, [this] {
        m_gridXOffset = (m_gridXOffset + 1) % kGridSpacing;
        update();
    });
    m_gridTimer.start();
}

void SpeedGraph::appendSample(double downKBs, double upKBs)
{
    m_downData.push_back(downKBs);
    if (static_cast<int>(m_downData.size()) > m_maxPoints)
        m_downData.pop_front();

    m_upData.push_back(upKBs);
    if (static_cast<int>(m_upData.size()) > m_maxPoints)
        m_upData.pop_front();

    update();
}

void SpeedGraph::setTimeRangeMinutes(int minutes)
{
    m_maxPoints = std::max(1, minutes) * 60;

    while (static_cast<int>(m_downData.size()) > m_maxPoints)
        m_downData.pop_front();
    while (static_cast<int>(m_upData.size()) > m_maxPoints)
        m_upData.pop_front();

    update();
}

double SpeedGraph::niceYMax(double raw)
{
    if (raw <= 0.0)
        return 10.0;

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

void SpeedGraph::drawGrid(QPainter& p, const QRect& area)
{
    p.setPen(QPen(QColor(75, 75, 75), 1, Qt::SolidLine));

    // Vertical lines — scroll left via m_gridXOffset
    for (int x = area.left() + (kGridSpacing - m_gridXOffset) % kGridSpacing;
         x <= area.right(); x += kGridSpacing)
        p.drawLine(x, area.top(), x, area.bottom());

    // Horizontal lines — static
    for (int y = area.top() + kGridSpacing; y <= area.bottom(); y += kGridSpacing)
        p.drawLine(area.left(), y, area.right(), y);
}

void SpeedGraph::drawHalf(QPainter& p, const QRect& area,
                          const std::deque<double>& data,
                          double yMax, const QColor& color,
                          bool isUpload, const QString& rateText)
{
    if (area.width() < 4 || area.height() < 4)
        return;

    const int n = static_cast<int>(data.size());

    // Arrow + rate label at top-left
    QFont labelFont;
    labelFont.setPointSize(7);
    p.setFont(labelFont);
    const QFontMetrics fm(labelFont);

    const int arrowSize = 6;
    const int labelX = area.left() + 2;
    const int labelY = area.top() + 2;

    // Draw filled triangle arrow
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    if (isUpload) {
        // ▲ pointing up
        const QPointF tri[3] = {
            QPointF(labelX + arrowSize / 2.0, labelY),
            QPointF(labelX, labelY + arrowSize),
            QPointF(labelX + arrowSize, labelY + arrowSize),
        };
        p.drawPolygon(tri, 3);
    } else {
        // ▼ pointing down
        const QPointF tri[3] = {
            QPointF(labelX, labelY),
            QPointF(labelX + arrowSize, labelY),
            QPointF(labelX + arrowSize / 2.0, labelY + arrowSize),
        };
        p.drawPolygon(tri, 3);
    }

    // Rate text after arrow
    p.setPen(QColor(160, 160, 200));
    p.drawText(labelX + arrowSize + 3, labelY, area.width(), fm.height(),
               Qt::AlignLeft | Qt::AlignTop, rateText);

    if (n < 2 || yMax <= 0.0)
        return;

    const double xStep = static_cast<double>(area.width()) / (n - 1);

    // Build filled polygon — data grows upward from bottom of this half
    QVector<QPointF> points;
    points.reserve(n + 2);
    for (int i = 0; i < n; ++i) {
        const double x = area.left() + i * xStep;
        const double yFrac = std::clamp(data[static_cast<size_t>(i)] / yMax, 0.0, 1.0);
        const double y = area.bottom() - yFrac * area.height();
        points.append(QPointF(x, y));
    }
    // Close along bottom edge
    points.append(QPointF(points.last().x(), area.bottom()));
    points.append(QPointF(points.first().x(), area.bottom()));

    // Filled area
    QColor fillColor = color;
    fillColor.setAlpha(100);
    p.setPen(Qt::NoPen);
    p.setBrush(fillColor);
    p.drawPolygon(QPolygonF(points));

    // Line on top of fill
    QVector<QPointF> line(points.begin(), points.begin() + n);
    p.setPen(QPen(color, 1.0));
    p.setBrush(Qt::NoBrush);
    p.drawPolyline(QPolygonF(line));
}

void SpeedGraph::paintEvent(QPaintEvent* /*event*/)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRect r = rect();

    // Dark navy background
    p.fillRect(r, QColor(0, 0, 64));

    // Thin border
    p.setPen(QPen(QColor(80, 80, 128), 1));
    p.drawRect(r.adjusted(0, 0, -1, -1));

    // Split into top (upload) and bottom (download) halves
    const int midY = r.top() + r.height() / 2;
    const QRect upArea(r.left() + 1, r.top() + 1, r.width() - 2, midY - r.top() - 1);
    const QRect downArea(r.left() + 1, midY + 1, r.width() - 2, r.bottom() - midY - 1);

    // Scrolling grid overlay on both halves
    drawGrid(p, upArea);
    drawGrid(p, downArea);

    // Divider line
    p.setPen(QPen(QColor(80, 80, 128), 1));
    p.drawLine(r.left(), midY, r.right(), midY);

    // Compute Y scales with minimum floor
    double upMax = 0.0;
    for (double v : m_upData)
        upMax = std::max(upMax, v);
    const double upScale = std::max(m_minUpScale, niceYMax(upMax));

    double downMax = 0.0;
    for (double v : m_downData)
        downMax = std::max(downMax, v);
    const double downScale = std::max(m_minDownScale, niceYMax(downMax));

    // Format current rate label with unit
    auto fmtRate = [](double val) -> QString {
        if (val >= 1000.0)
            return QStringLiteral("%1 KB/s").arg(val, 0, 'f', 0);
        return QStringLiteral("%1 KB/s").arg(val, 0, 'f', 1);
    };

    const double curUp = m_upData.empty() ? 0.0 : m_upData.back();
    const double curDown = m_downData.empty() ? 0.0 : m_downData.back();

    // Draw upload half (red, top) with ▲ arrow
    drawHalf(p, upArea, m_upData, upScale, QColor(210, 0, 0),
             true, fmtRate(curUp));

    // Draw download half (green, bottom) with ▼ arrow
    drawHalf(p, downArea, m_downData, downScale, QColor(0, 210, 0),
             false, fmtRate(curDown));
}

} // namespace eMule
