/// @file KadContactHistogram.cpp
/// @brief Qt port of MFC CKadContactHistogramCtrl::OnPaint().

#include "controls/KadContactHistogram.h"
#include "controls/KadContactsModel.h"

#include <QPainter>
#include <QPaintEvent>

#include <algorithm>

namespace eMule {

KadContactHistogram::KadContactHistogram(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(140, 50);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_buckets.fill(0);
}

void KadContactHistogram::setContacts(const std::vector<KadContactRow>& contacts)
{
    m_buckets.fill(0);
    m_contactCount = static_cast<uint32_t>(contacts.size());

    for (const auto& c : contacts) {
        // MFC: DWORD dwHighestWord = *(DWORD*)KadUint128.GetData();
        //      UINT uHistSlot = dwHighestWord >> 20;
        // The clientId is a 32-char hex string representing 128 bits.
        // First 8 hex chars = highest 32-bit word.
        if (c.clientId.size() >= 8) {
            bool ok = false;
            const uint32_t highWord = c.clientId.left(8).toUInt(&ok, 16);
            if (ok) {
                const uint32_t slot = highWord >> 20;  // top 12 bits → 0..4095
                m_buckets[slot]++;
            }
        }
    }

    update();
}

void KadContactHistogram::clear()
{
    m_buckets.fill(0);
    m_contactCount = 0;
    update();
}

QSize KadContactHistogram::minimumSizeHint() const
{
    return {140, 50};
}

QSize KadContactHistogram::sizeHint() const
{
    return {280, 80};
}

// ---------------------------------------------------------------------------
// Painting — line-for-line match of MFC CKadContactHistogramCtrl::OnPaint()
// ---------------------------------------------------------------------------

void KadContactHistogram::paintEvent(QPaintEvent* /*event*/)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);

    const QRect rc = rect();

    // White background
    p.fillRect(rc, Qt::white);

    // Gray border
    p.setPen(QPen(QColor(128, 128, 128), 1));
    p.drawRect(rc.adjusted(0, 0, -1, -1));

    // -- Fonts ---
    QFont labelFont;
    labelFont.setPixelSize(10);
    const QFontMetrics fmLabel(labelFont);

    // MFC: measures "888" to get max numeric label width
    const int maxLabelHeight = fmLabel.height();
    const int maxNumLabelWidth = fmLabel.horizontalAdvance(QStringLiteral("888"));

    // -- Layout borders matching MFC ---
    // MFC: iLeftBorder  = 1 + m_iMaxNumLabelWidth + 3
    //      iRightBorder = 8
    //      iTopBorder   = m_iMaxLabelHeight
    //      iBottomBorder= m_iMaxLabelHeight
    const int axisWidth = 1;
    const int leftBorder = 1 + maxNumLabelWidth + 3;
    const int rightBorder = 8;
    const int topBorder = maxLabelHeight;
    const int bottomBorder = maxLabelHeight;

    const int plotW = rc.width() - leftBorder - rightBorder;
    const int plotH = rc.height() - topBorder - bottomBorder;

    if (plotW < 10 || plotH < 10)
        return;

    // -- Y-axis scaling (MFC logic) ---
    // MFC averages: uMax = total / kHistSize, minimum 15
    uint32_t uMax = m_contactCount > 0 ? (m_contactCount / kHistSize) : 0;
    if (uMax < 15)
        uMax = 15;

    p.setFont(labelFont);

    // -- Colors matching MFC ---
    const QColor axisColor(128, 128, 128);
    const QColor auxColor(192, 192, 192);
    const QColor barColor(255, 32, 32);

    // -- Y-axis step labels (MFC algorithm) ---
    // MFC: uLabels = plotH / (fontHeight + fontHeight/2)
    //      uStep = ((uMax / uLabels + 5) / 10) * 10, minimum 5
    const uint32_t uLabels = std::max(1u,
        static_cast<uint32_t>(plotH) / static_cast<uint32_t>(maxLabelHeight + maxLabelHeight / 2));
    uint32_t uStep = ((uMax / uLabels + 5) / 10) * 10;
    if (uStep < 5)
        uStep = 5;

    // Draw Y-axis numeric labels and grid lines together
    for (uint32_t y = uStep; y <= uMax; y += uStep) {
        const int yPos = rc.top() + topBorder + plotH
                         - static_cast<int>(static_cast<uint64_t>(y) * static_cast<uint32_t>(plotH) / uMax);

        // MFC: label rect = { 1, yPos, 1 + numLabelWidth, yPos + labelHeight }
        // Right-aligned in left margin
        p.setPen(axisColor);
        const QRect labelRect(rc.left() + 1, yPos - maxLabelHeight / 2,
                              maxNumLabelWidth, maxLabelHeight);
        p.drawText(labelRect, Qt::AlignRight | Qt::AlignVCenter, QString::number(y));

        // Dotted auxiliary grid line
        p.setPen(QPen(auxColor, 1, Qt::DotLine));
        p.drawLine(rc.left() + leftBorder, yPos,
                   rc.left() + leftBorder + plotW - 1, yPos);
    }

    // -- Draw bars ---
    // MFC: each pixel column accumulates buckets mapping to it.
    for (int x = 0; x < plotW; ++x) {
        const int bucketStart = static_cast<int>(
            static_cast<int64_t>(x) * kHistSize / plotW);
        const int bucketEnd = static_cast<int>(
            static_cast<int64_t>(x + 1) * kHistSize / plotW);

        uint32_t val = 0;
        for (int b = bucketStart; b < bucketEnd; ++b)
            val += m_buckets[static_cast<size_t>(b)];

        if (val == 0)
            continue;

        int barH = static_cast<int>(static_cast<uint64_t>(val) * static_cast<uint32_t>(plotH) / uMax);
        barH = std::min(barH, plotH);

        // MFC: bars exceeding uMax drawn in axis color (gray) instead of red
        const QColor& color = (val > uMax) ? axisColor : barColor;

        const int xPos = rc.left() + leftBorder + x;
        const int yBottom = rc.top() + topBorder + plotH - 1;
        p.setPen(color);
        p.drawLine(xPos, yBottom - barH + 1, xPos, yBottom);
    }

    // -- Axes ---
    p.setPen(QPen(axisColor, axisWidth));
    // Left axis (vertical)
    p.drawLine(rc.left() + leftBorder, rc.top() + topBorder,
               rc.left() + leftBorder, rc.top() + topBorder + plotH);
    // Bottom axis (horizontal)
    p.drawLine(rc.left() + leftBorder, rc.top() + topBorder + plotH,
               rc.left() + leftBorder + plotW, rc.top() + topBorder + plotH);

    // -- Axis labels (bold, matching MFC) ---
    QFont boldFont(labelFont);
    boldFont.setBold(true);
    p.setFont(boldFont);
    p.setPen(axisColor);

    // MFC: "Contacts" at (iBaseLineX + 1, iTopBorder + 1) with DT_LEFT | DT_TOP | DT_NOCLIP
    p.drawText(rc.left() + leftBorder + 1, rc.top() + topBorder + 1 + fmLabel.ascent(),
               tr("Contacts"));

    // MFC: "Kademlia Network" at rcClient with DT_RIGHT | DT_BOTTOM | DT_NOCLIP
    const QFontMetrics fmBold(boldFont);
    const QString xLabel = tr("Kademlia Network"); // by KadID UInt128, not XOR
    const int xLabelW = fmBold.horizontalAdvance(xLabel);
    p.drawText(rc.right() - xLabelW - 1,
               rc.bottom() - fmBold.descent() - 1,
               xLabel);
}

} // namespace eMule
