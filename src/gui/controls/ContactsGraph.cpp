#include "pch.h"
#include "controls/ContactsGraph.h"
#include "utils/Appearance.h"

#include <algorithm>

#include <QFontMetrics>
#include <QPainter>
#include <QPaintEvent>


namespace eMule {

ContactsGraph::ContactsGraph(QWidget* parent)
    : QWidget(parent)
{
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
    // title + plot of a few label lines + x label: overlapped at large text sizes
    const Margins m = margins();
    return {std::max(140, m.labelW + 100), std::max(80, m.top + m.labelH + 4 * m.labelH)};
}

QSize ContactsGraph::sizeHint() const
{
    const QSize min = minimumSizeHint();
    return {std::max(180, min.width()), std::max(100, min.height() + 20)};
}

void ContactsGraph::paintEvent(QPaintEvent* /*event*/)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);

    const QRect r = rect();

    // Background: white in light mode as in MFC, follows the theme
    p.fillRect(r, palette().color(QPalette::Base));

    // Border
    p.setPen(QPen(themed(QColor(180, 180, 180), QColor(90, 90, 90)), 1));
    p.drawRect(r.adjusted(0, 0, -1, -1));

    // Leave margin for axis labels
    const auto [labelW, labelH, top] = margins();
    const QRect plotArea(r.left() + labelW, r.top() + top,
                         r.width() - labelW - 4, r.height() - labelH - top);

    if (plotArea.width() < 10 || plotArea.height() < 10)
        return;

    // Y-axis scale: round up peak to nearest 5
    const int yMax = ((m_peakValue + 4) / 5) * 5;

    // Draw Y-axis labels
    const QFont labelFont = this->labelFont();
    p.setFont(labelFont);
    p.setPen(Qt::darkGray);
    p.drawText(QRect(0, plotArea.top() - labelH / 2, labelW - 2, labelH),
               Qt::AlignRight | Qt::AlignVCenter, QString::number(yMax));
    p.drawText(QRect(0, plotArea.bottom() - labelH / 2, labelW - 2, labelH),
               Qt::AlignRight | Qt::AlignVCenter, QStringLiteral("0"));

    // Draw "Contacts" title
    p.setPen(palette().color(QPalette::Text));
    QFont titleFont = labelFont;
    titleFont.setBold(true);
    p.setFont(titleFont);
    p.drawText(QRect(plotArea.left(), r.top(), plotArea.width(), labelH),
               Qt::AlignRight, tr("Contacts"));

    // X-axis label
    p.setFont(labelFont);
    p.setPen(Qt::darkGray);
    p.drawText(QRect(plotArea.left(), plotArea.bottom() + 1, plotArea.width(), labelH),
               Qt::AlignCenter, tr("Time"));

    // Grid lines
    p.setPen(QPen(themed(QColor(220, 220, 220), QColor(70, 70, 70)), 1, Qt::DotLine));
    for (int i = 1; i < 4; ++i) {
        const int y = plotArea.bottom() - (plotArea.height() * i / 4);
        p.drawLine(plotArea.left(), y, plotArea.right(), y);
    }

    // Axes
    p.setPen(QPen(Qt::darkGray, 1));
    p.drawLine(plotArea.left(), plotArea.bottom(), plotArea.right(), plotArea.bottom());
    p.drawLine(plotArea.left(), plotArea.top(), plotArea.left(), plotArea.bottom());

    if (m_samples.empty())
        return;

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

}

// ---------------------------------------------------------------------------
// private
// ---------------------------------------------------------------------------

QFont ContactsGraph::labelFont()
{
    QFont font;
    font.setPointSize(scaledPointSize(7));
    return font;
}

ContactsGraph::Margins ContactsGraph::margins() const
{
    QFont bold = labelFont();
    bold.setBold(true);
    const QFontMetrics fm(labelFont());
    const int labelH = std::max(QFontMetrics(bold).height(), fm.height());
    // y labels up to 4 digits; plot starts below the title's upper half
    return {fm.horizontalAdvance(QStringLiteral("8888")) + 4, labelH, std::max(4, labelH / 2 + 2)};
}

} // namespace eMule
