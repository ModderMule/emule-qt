#include "controls/KadLookupGraph.h"

#include <QPainter>
#include <QPaintEvent>
#include <QMouseEvent>
#include <QRadialGradient>
#include <QToolTip>

#include <algorithm>
#include <cmath>

namespace eMule {

namespace {

constexpr int kNodeWidth  = 26;
constexpr int kNodeHeight = 20;
constexpr int kIconSize   = 14;
constexpr int kSelfHotIdx = -2;  // special hover index for the self node

} // anonymous namespace

KadLookupGraph::KadLookupGraph(QWidget* parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void KadLookupGraph::setEntries(std::vector<LookupEntry> entries)
{
    m_entries = std::move(entries);
    m_hotIdx = -1;
    update();
}

void KadLookupGraph::clear()
{
    m_entries.clear();
    m_nodeRects.clear();
    m_selfRect = {};
    m_hotIdx = -1;
    update();
}

void KadLookupGraph::setTitle(const QString& title)
{
    m_title = title;
}

void KadLookupGraph::paintEvent(QPaintEvent* /*event*/)
{
    m_nodeRects.clear();

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRect rc = rect();
    p.fillRect(rc, palette().color(QPalette::Window));

    // Inner area with border
    p.setPen(QPen(QColor(180, 180, 180), 1));
    p.drawRect(rc.adjusted(0, 0, -1, -1));

    const QRect client = rc.adjusted(2, 2, -2, -2);
    p.fillRect(client, Qt::white);

    if (m_entries.empty()) {
        p.setPen(Qt::darkGray);
        p.drawText(client, Qt::AlignCenter, tr("No search selected"));
        return;
    }

    // Font metrics for labels
    QFont labelFont;
    labelFont.setPointSize(8);
    p.setFont(labelFont);
    const QFontMetrics fm(labelFont);
    const int labelH = fm.height();

    const int leftBorder = 3;
    const int rightBorder = 8;
    const int topBorder = labelH + 2;
    const int bottomBorder = labelH + 2;

    const int baseLineX = client.left() + leftBorder;
    const int baseLineY = client.bottom() - bottomBorder;
    const int histWidth  = client.width() - leftBorder - rightBorder;
    const int histHeight = client.height() - topBorder - bottomBorder;

    if (histWidth < 20 || histHeight < 20)
        return;

    // Draw axes
    p.setPen(QPen(QColor(128, 128, 128), 1));
    p.drawLine(baseLineX, client.top() + topBorder, baseLineX, baseLineY);
    p.drawLine(baseLineX, baseLineY, baseLineX + histWidth, baseLineY);

    // Axis labels
    p.setPen(Qt::darkGray);
    p.drawText(QRect(baseLineX, client.top(), histWidth, labelH),
               Qt::AlignLeft | Qt::TextSingleLine, tr("Distance"));
    p.drawText(QRect(baseLineX, baseLineY + 2, histWidth, labelH),
               Qt::AlignRight | Qt::TextSingleLine, tr("Time"));

    const auto hecount = static_cast<int>(m_entries.size());

    // How many nodes fit without scrolling?
    const int maxNodes = histWidth / kNodeWidth;
    const int visibleNodes = std::min(maxNodes, hecount);
    if (visibleNodes == 0)
        return;

    // Node entry width: fixed while search running, spread when finished
    int nodeEntryWidth;
    if (hecount > maxNodes)
        nodeEntryWidth = kNodeWidth;
    else
        nodeEntryWidth = histWidth / hecount;

    // --- Scaling: find the 1/3 closest nodes' max distance, multiply by 3 ---
    // Collect distances of the visible nodes (from the end of the history - most recent)
    const int k = std::max(1, visibleNodes / 3);
    std::vector<int> closestIndices;
    closestIndices.reserve(static_cast<size_t>(k));

    // Helper to get a uint64 representation from the most significant non-zero 32-bit chunks
    auto getScaledDist = [this](int entryIdx, int startChunk) -> uint64_t {
        const auto& e = m_entries[static_cast<size_t>(entryIdx)];
        return (static_cast<uint64_t>(e.dist[startChunk]) << 32)
               | static_cast<uint64_t>(e.dist[startChunk + 1]);
    };

    auto distGreater = [this](int a, int b) {
        for (int c = 0; c < 4; ++c) {
            const auto& ea = m_entries[static_cast<size_t>(a)];
            const auto& eb = m_entries[static_cast<size_t>(b)];
            if (ea.dist[c] != eb.dist[c])
                return ea.dist[c] > eb.dist[c];
        }
        return false;
    };

    // Collect the k closest (smallest distance) among visible nodes
    for (int i = 1; i <= visibleNodes; ++i) {
        const int idx = hecount - i;
        if (static_cast<int>(closestIndices.size()) < k) {
            closestIndices.push_back(idx);
        } else {
            // Find the farthest among our k closest and replace if this is closer
            int farthestPos = 0;
            for (int j = 1; j < static_cast<int>(closestIndices.size()); ++j) {
                if (distGreater(closestIndices[static_cast<size_t>(j)],
                                closestIndices[static_cast<size_t>(farthestPos)]))
                    farthestPos = j;
            }
            if (distGreater(closestIndices[static_cast<size_t>(farthestPos)], idx))
                closestIndices[static_cast<size_t>(farthestPos)] = idx;
        }
    }

    // Find the maximum distance among the k closest
    int maxClosestIdx = closestIndices[0];
    for (size_t j = 1; j < closestIndices.size(); ++j) {
        if (distGreater(static_cast<int>(closestIndices[j]), maxClosestIdx))
            maxClosestIdx = static_cast<int>(closestIndices[j]);
    }

    // Find the first non-zero 32-bit chunk for scaling
    const auto& maxEntry = m_entries[static_cast<size_t>(maxClosestIdx)];
    int startChunk = 0;
    for (startChunk = 0; startChunk < 3; ++startChunk) {
        if (maxEntry.dist[startChunk] > 0)
            break;
    }
    if (startChunk >= 3)
        startChunk = 2;

    uint64_t scalingDist = getScaledDist(maxClosestIdx, startChunk);
    if (scalingDist == 0)
        scalingDist = maxEntry.dist[3];

    // Scale to fit: distance / (plotHeight - nodeHeight) * 3
    const int plotNodeArea = histHeight - kNodeHeight;
    if (plotNodeArea <= 0)
        return;

    scalingDist = (scalingDist / static_cast<uint64_t>(plotNodeArea)) * 3;
    if (scalingDist == 0)
        scalingDist = 1;

    // Maximum displayable distance = 3x the 1/3-closest max
    uint64_t maxScalingDist = getScaledDist(maxClosestIdx, startChunk) * 3;

    // --- Compute node positions ---
    m_nodeRects.resize(static_cast<size_t>(visibleNodes));

    for (int i = 0; i < visibleNodes; ++i) {
        const int entryIdx = hecount - (i + 1);
        const auto& e = m_entries[static_cast<size_t>(entryIdx)];

        uint64_t entryDist = (static_cast<uint64_t>(e.dist[startChunk]) << 32)
                             | static_cast<uint64_t>(e.dist[startChunk + 1]);

        int yPos;
        if (entryDist > maxScalingDist) {
            yPos = client.top() + topBorder;
        } else {
            uint64_t drawY = (entryDist > 0) ? (entryDist / scalingDist) : 0;
            yPos = static_cast<int>((baseLineY - kNodeHeight) - static_cast<int>(drawY));
        }

        const int xPos = histWidth - ((i + 1) * nodeEntryWidth);

        QRect nodeRect(baseLineX + xPos, yPos, kNodeWidth, kNodeHeight);
        m_nodeRects[static_cast<size_t>(i)] = { nodeRect, entryIdx };
    }

    // --- "Self" node: green sphere at top-left (MFC style) ---
    // Represents our own node — maximum XOR distance from target.
    const QPoint selfCenter(baseLineX + kNodeWidth / 2,
                            client.top() + topBorder + kNodeHeight / 2);
    m_selfRect = QRect(selfCenter.x() - kIconSize / 2, selfCenter.y() - kIconSize / 2,
                       kIconSize, kIconSize);

    // Determine hot-item connected set
    // Index -1 represents the self node for hover purposes
    std::vector<bool> hotConnected(static_cast<size_t>(visibleNodes), false);
    bool selfHot = false;
    if (m_hotIdx >= 0 && m_hotIdx < visibleNodes)
        hotConnected[static_cast<size_t>(m_hotIdx)] = true;
    if (m_hotIdx == kSelfHotIdx)
        selfHot = true;

    // --- Draw connection lines ---
    QPen penGray(QColor(192, 192, 192), 0.8);
    QPen penDarkGray(QColor(100, 100, 100), 0.8);
    QPen penRed(QColor(255, 32, 32), 0.8);

    // Lines from self node to initial contacts (entries with empty receivedFromIdx)
    for (int i = 0; i < visibleNodes; ++i) {
        const int entryIdx = hecount - (i + 1);
        const auto& entry = m_entries[static_cast<size_t>(entryIdx)];

        if (entry.receivedFromIdx.empty()) {
            QPoint pTo = m_nodeRects[static_cast<size_t>(i)].rect.center();

            QPen* pen;
            if (selfHot) {
                hotConnected[static_cast<size_t>(i)] = true;
                pen = &penRed;
            } else if (i == m_hotIdx) {
                selfHot = true;
                pen = &penDarkGray;
            } else {
                pen = &penGray;
            }
            p.setPen(*pen);
            p.drawLine(selfCenter, pTo);
        }
    }

    // Lines between discovered nodes (receivedFromIdx connections)
    for (int i = 0; i < visibleNodes; ++i) {
        const int entryIdx = hecount - (i + 1);
        const auto& entry = m_entries[static_cast<size_t>(entryIdx)];

        for (int fromIdx : entry.receivedFromIdx) {
            if (fromIdx >= hecount - visibleNodes) {
                const int fromDrawIdx = hecount - (fromIdx + 1);
                if (fromDrawIdx < 0 || fromDrawIdx >= visibleNodes)
                    continue;

                QPoint pFrom = m_nodeRects[static_cast<size_t>(fromDrawIdx)].rect.center();
                QPoint pTo   = m_nodeRects[static_cast<size_t>(i)].rect.center();

                QPen* pen;
                if (fromDrawIdx == m_hotIdx) {
                    hotConnected[static_cast<size_t>(i)] = true;
                    pen = &penRed;
                } else if (i == m_hotIdx) {
                    hotConnected[static_cast<size_t>(fromDrawIdx)] = true;
                    pen = &penDarkGray;
                } else {
                    pen = &penGray;
                }

                p.setPen(*pen);
                p.drawLine(pFrom, pTo);
            }
        }
    }

    // --- Draw node spheres (MFC-style with 3D gradient) ---
    auto drawSphere = [&p](const QPoint& center, int radius, QColor color) {
        QRadialGradient grad(center.x() - radius / 3, center.y() - radius / 3, radius * 1.2,
                             center.x() - radius / 3, center.y() - radius / 3);
        grad.setColorAt(0.0, color.lighter(160));
        grad.setColorAt(0.7, color);
        grad.setColorAt(1.0, color.darker(150));

        p.setBrush(QBrush(grad));
        p.setPen(QPen(color.darker(160), 0.8));
        p.drawEllipse(center, radius, radius);
    };

    // Draw self node (green, like MFC)
    {
        QColor selfColor(0, 180, 0);
        if (m_hotIdx >= 0 && !selfHot)
            selfColor.setAlpha(80);
        drawSphere(selfCenter, kIconSize / 2, selfColor);
    }

    // Draw lookup history nodes (yellow/gold)
    for (int i = 0; i < visibleNodes; ++i) {
        const auto& nr = m_nodeRects[static_cast<size_t>(i)];
        const auto& entry = m_entries[static_cast<size_t>(nr.entryIdx)];

        QColor color = nodeColor(entry);

        // Dim non-connected nodes when hovering
        if (m_hotIdx >= 0 && !hotConnected[static_cast<size_t>(i)])
            color.setAlpha(80);

        drawSphere(nr.rect.center(), kIconSize / 2, color);
    }
}

void KadLookupGraph::mouseMoveEvent(QMouseEvent* event)
{
    const int newHot = hitTest(event->pos());
    if (newHot != m_hotIdx) {
        m_hotIdx = newHot;
        update();

        if (m_hotIdx == kSelfHotIdx) {
            QToolTip::showText(event->globalPosition().toPoint(),
                               tr("Our node (search initiator)"), this);
        } else if (m_hotIdx >= 0 && m_hotIdx < static_cast<int>(m_nodeRects.size())) {
            const auto& nr = m_nodeRects[static_cast<size_t>(m_hotIdx)];
            const auto& e = m_entries[static_cast<size_t>(nr.entryIdx)];
            QString tip = QStringLiteral("%1\nDistance: %2\nVersion: %3")
                              .arg(e.contactID, e.distance)
                              .arg(e.contactVersion);
            QToolTip::showText(event->globalPosition().toPoint(), tip, this);
        } else {
            QToolTip::hideText();
        }
    }
    QWidget::mouseMoveEvent(event);
}

// ---------------------------------------------------------------------------
// Private methods
// ---------------------------------------------------------------------------

int KadLookupGraph::hitTest(const QPoint& pos) const
{
    // Check self node first (drawn on top)
    if (m_selfRect.isValid() && m_selfRect.contains(pos))
        return kSelfHotIdx;

    for (int i = static_cast<int>(m_nodeRects.size()) - 1; i >= 0; --i) {
        if (m_nodeRects[static_cast<size_t>(i)].rect.contains(pos))
            return i;
    }
    return -1;
}

QColor KadLookupGraph::nodeColor(const LookupEntry& entry) const
{
    // MFC-style colors: gold for responded, red for timed out / no response
    if (entry.askedContactsTime > 0) {
        if (entry.respondedContact > 0)
            return entry.providedCloser ? QColor(220, 190, 30)   // gold - responded with closer
                                        : QColor(200, 180, 50);  // muted gold - responded
        else
            return QColor(200, 50, 50); // red - asked, no response (offline/timed out)
    }
    if (entry.forcedInteresting)
        return QColor(210, 150, 30); // darker gold - forced interesting
    return QColor(220, 190, 30); // default gold - not yet asked
}

} // namespace eMule
