#include "controls/ContactsGraph.h"

#include <QFontMetrics>
#include <QPainter>
#include <QPaintEvent>

#include <algorithm>

namespace eMule {

ContactsGraph::ContactsGraph(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(140, 80);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void ContactsGraph::addSample(int contacts)
{
    m_samples.push_back(contacts);
    if (static_cast<int>(m_samples.size()) > kMaxSamples)
        m_samples.pop_front();

    m_peakValue = std::max(1, *std::max_element(m_samples.begin(), m_samples.end()));
    update();
}

void ContactsGraph::clearSamples()
{
    m_samples.clear();
    m_peakValue = 1;
    update();
}

QSize ContactsGraph::minimumSizeHint() const
{
    return {140, 80};
}

QSize ContactsGraph::sizeHint() const
{
    return {180, 100};
}

void ContactsGraph::paintEvent(QPaintEvent* /*event*/)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);

    const QRect r = rect();

    // Background
    p.fillRect(r, Qt::white);

    // Border
    p.setPen(QPen(QColor(180, 180, 180), 1));
    p.drawRect(r.adjusted(0, 0, -1, -1));

    if (m_samples.empty())
        return;

    // Leave margin for axis labels
    const int labelW = 25;
    const int labelH = 14;
    const QRect plotArea(r.left() + labelW, r.top() + 4,
                         r.width() - labelW - 4, r.height() - labelH - 4);

    if (plotArea.width() < 10 || plotArea.height() < 10)
        return;

    // Y-axis scale: round up peak to nearest 5
    const int yMax = ((m_peakValue + 4) / 5) * 5;

    // Draw Y-axis labels
    QFont labelFont;
    labelFont.setPointSize(7);
    p.setFont(labelFont);
    p.setPen(Qt::darkGray);
    p.drawText(QRect(0, plotArea.top() - 6, labelW - 2, 12),
               Qt::AlignRight | Qt::AlignVCenter, QString::number(yMax));
    p.drawText(QRect(0, plotArea.bottom() - 6, labelW - 2, 12),
               Qt::AlignRight | Qt::AlignVCenter, QStringLiteral("0"));

    // Draw "Contacts" title
    p.setPen(Qt::black);
    QFont titleFont;
    titleFont.setPointSize(7);
    titleFont.setBold(true);
    p.setFont(titleFont);
    p.drawText(QRect(plotArea.left(), r.top(), plotArea.width(), 12),
               Qt::AlignRight, tr("Contacts"));

    // X-axis label
    p.setFont(labelFont);
    p.setPen(Qt::darkGray);
    p.drawText(QRect(plotArea.left(), plotArea.bottom() + 1, plotArea.width(), labelH),
               Qt::AlignCenter, tr("Time"));

    // Grid lines
    p.setPen(QPen(QColor(220, 220, 220), 1, Qt::DotLine));
    for (int i = 1; i < 4; ++i) {
        const int y = plotArea.bottom() - (plotArea.height() * i / 4);
        p.drawLine(plotArea.left(), y, plotArea.right(), y);
    }

    // Bars
    const int n = static_cast<int>(m_samples.size());
    const double barWidth = static_cast<double>(plotArea.width()) / kMaxSamples;

    p.setPen(Qt::NoPen);
    const QColor barColor(220, 60, 60);
    const QColor barColorDark(180, 40, 40);

    for (int i = 0; i < n; ++i) {
        const int val = m_samples[static_cast<size_t>(i)];
        const int barH = yMax > 0 ? (val * plotArea.height() / yMax) : 0;
        const int x = plotArea.left() + static_cast<int>(i * barWidth);
        const int w = std::max(1, static_cast<int>(barWidth) - 1);
        const QRect barRect(x, plotArea.bottom() - barH, w, barH);

        p.fillRect(barRect, barColor);
        // Darker left edge for 3D effect
        if (w > 2)
            p.fillRect(QRect(x, plotArea.bottom() - barH, 1, barH), barColorDark);
    }

    // Bottom axis line
    p.setPen(QPen(Qt::darkGray, 1));
    p.drawLine(plotArea.left(), plotArea.bottom(), plotArea.right(), plotArea.bottom());
    p.drawLine(plotArea.left(), plotArea.top(), plotArea.left(), plotArea.bottom());
}

} // namespace eMule
