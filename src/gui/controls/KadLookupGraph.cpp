#include "pch.h"
#include "controls/KadLookupGraph.h"

#include <QContextMenuEvent>
#include <QCursor>
#include <QHelpEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QToolTip>

#include <algorithm>
#include <cmath>


namespace eMule {

namespace {

// MFC KadLookupGraph.cpp:38-39
constexpr int kNodeWidth  = 26;
constexpr int kNodeHeight = 20;
constexpr int kIconSize   = 16;
constexpr int64_t kContactTimeoutMs = 3000;   // MFC SEC2MS(3)
constexpr int64_t kResultTimeoutMs  = 5000;   // MFC SEC2MS(5)

// kad::SearchType values (KadSearchDefs.h)
enum : int { TypeFile = 2, TypeKeyword = 3, TypeNotes = 4, TypeStoreFile = 5,
             TypeStoreKeyword = 6, TypeStoreNotes = 7, TypeFindSource = 9 };

using Dist = std::array<uint32_t, 4>;

/// 3 * d, saturating (MFC adds the distance to itself twice).
Dist times3(const Dist& d)
{
    Dist r{};
    uint64_t carry = 0;
    for (int c = 3; c >= 0; --c) {
        const uint64_t v = uint64_t{d[static_cast<size_t>(c)]} * 3 + carry;
        r[static_cast<size_t>(c)] = static_cast<uint32_t>(v);
        carry = v >> 32;
    }
    if (carry)
        r.fill(UINT32_MAX);
    return r;
}

uint64_t chunk64(const Dist& d, int start)
{
    return (uint64_t{d[static_cast<size_t>(start)]} << 32) | d[static_cast<size_t>(start + 1)];
}

void drawArrow(QPainter& p, QPointF from, QPointF to, const QColor& color)
{
    p.setPen(QPen(color, 0.8));
    p.drawLine(from, to);
    const QPointF vec = to - from;
    const double len = std::hypot(vec.x(), vec.y());
    if (len < 1.0)
        return;
    // Arrow head like GDI+ AdjustableArrowCap(6, 4) on a 0.8 px pen
    const QPointF unit = vec / len;
    const QPointF normal(-unit.y(), unit.x());
    const QPointF base = to - unit * 4.8;
    QPainterPath head;
    head.moveTo(to);
    head.lineTo(base + normal * 1.6);
    head.lineTo(base - normal * 1.6);
    head.closeSubpath();
    p.fillPath(head, color);
}

} // anonymous namespace

KadLookupGraph::KadLookupGraph(QWidget* parent)
    : QWidget(parent)
    , m_nodeIcons{QIcon(QStringLiteral(":/icons/Contact0.ico")),
                  QIcon(QStringLiteral(":/icons/Contact1.ico")),
                  QIcon(QStringLiteral(":/icons/Contact2.ico")),
                  QIcon(QStringLiteral(":/icons/Contact4.ico"))}
    , m_iconSearch(QStringLiteral(":/icons/emuleCollSearch.ico"))
    , m_iconStoreFile(QStringLiteral(":/icons/KadStoreFile.ico"))
    , m_iconStoreWord(QStringLiteral(":/icons/KadStoreWord.ico"))
    , m_ovlResponded(QStringLiteral(":/icons/ClientSecureHashOvl.ico"))
    , m_ovlNoResponse(QStringLiteral(":/icons/NoAccessFolderOvl.ico"))
{
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_snapshotAge.start();
}

void KadLookupGraph::setLookup(std::vector<LookupEntry> entries, int searchType)
{
    m_entries = std::move(entries);
    m_searchType = searchType;
    m_snapshotAge.restart();
    layoutNodes();
    // MFC recomputes the hot item and its tip on every repaint (KadLookupGraph.cpp:300-311)
    refreshToolTip();
    update();
}

void KadLookupGraph::clear()
{
    m_entries.clear();
    m_nodeRects.clear();
    m_hotId.clear();
    m_searchType = -1;
    if (QToolTip::isVisible() && underMouse())
        QToolTip::hideText();
    update();
}

// ---------------------------------------------------------------------------
// Events
// ---------------------------------------------------------------------------

bool KadLookupGraph::event(QEvent* event)
{
    if (event->type() == QEvent::ToolTip) {
        // Built on demand, so a re-hover always shows the latest snapshot
        const auto* help = static_cast<QHelpEvent*>(event);
        const int idx = hitTest(help->pos());
        if (idx >= 0)
            QToolTip::showText(help->globalPos(), toolTipText(idx), this,
                               m_nodeRects[static_cast<size_t>(idx)]);
        else
            QToolTip::hideText();
        return true;
    }
    return QWidget::event(event);
}

void KadLookupGraph::paintEvent(QPaintEvent* /*event*/)
{
    layoutNodes();

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    // MFC: themed list-view frame, COLOR_WINDOW inside
    const QRect rc = rect();
    p.fillRect(rc, palette().color(QPalette::Base));
    p.setPen(palette().color(QPalette::Mid));
    p.drawRect(rc.adjusted(0, 0, -1, -1));

    if (m_histWidth <= 0)
        return;

    // Axes and labels are drawn even without a lookup (MFC)
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setPen(QColor(128, 128, 128));
    p.drawLine(m_baseLineX, m_topY, m_baseLineX, m_baseLineY);
    p.drawLine(m_baseLineX, m_baseLineY, m_baseLineX + m_histWidth, m_baseLineY);
    p.setPen(palette().color(QPalette::Text));
    p.setFont(font());
    p.drawText(QRect(m_baseLineX, m_client.top(), m_client.width(), m_labelHeight),
               Qt::AlignLeft | Qt::AlignTop | Qt::TextSingleLine, tr("Distance"));
    p.drawText(QRect(m_client.left(), m_client.bottom() - m_labelHeight + 1, m_client.width(), m_labelHeight),
               Qt::AlignRight | Qt::AlignBottom | Qt::TextSingleLine, tr("Time"));
    p.setRenderHint(QPainter::Antialiasing, true);

    const auto visible = static_cast<int>(m_nodeRects.size());
    if (visible == 0)
        return;
    const auto hecount = static_cast<int>(m_entries.size());
    const int hotIdx = hotDrawIdx();

    std::vector<bool> hotConnected(static_cast<size_t>(visible), false);
    if (hotIdx >= 0)
        hotConnected[static_cast<size_t>(hotIdx)] = true;

    // Arrows from the reporting node to each contact (MFC KadLookupGraph.cpp:336-357)
    const QColor gray(192, 192, 192), darkGray(100, 100, 100), red(255, 32, 32);
    for (int i = 0; i < visible; ++i) {
        const auto& entry = m_entries[static_cast<size_t>(hecount - (i + 1))];
        for (const int fromIdx : entry.receivedFromIdx) {
            if (fromIdx < hecount - visible || fromIdx >= hecount)
                continue;
            const int fromDraw = hecount - (fromIdx + 1);
            const QColor* color = &gray;
            if (fromDraw == hotIdx) {
                hotConnected[static_cast<size_t>(i)] = true;
                color = &red;
            } else if (i == hotIdx) {
                hotConnected[static_cast<size_t>(fromDraw)] = true;
                color = &darkGray;
            }
            drawArrow(p, QRectF(m_nodeRects[static_cast<size_t>(fromDraw)]).center(),
                      QRectF(m_nodeRects[static_cast<size_t>(i)]).center(), *color);
        }
    }

    // Node icons, plus the "asked for results" indicator (MFC KadLookupGraph.cpp:415-477)
    for (int i = 0; i < visible; ++i) {
        const auto& entry = m_entries[static_cast<size_t>(hecount - (i + 1))];
        const QPoint pt = m_nodeRects[static_cast<size_t>(i)].center() - QPoint(kIconSize / 2, kIconSize / 2);
        const QRect iconRect(pt, QSize(kIconSize, kIconSize));

        p.setOpacity(hotIdx >= 0 && !hotConnected[static_cast<size_t>(i)] ? 0.5 : 1.0);
        m_nodeIcons[static_cast<size_t>(nodeIconIdx(entry))].paint(&p, iconRect);
        p.setOpacity(1.0);

        if (entry.askedSearchItemAge < 0)
            continue;
        QRect ind = iconRect;
        ind.translate(0, pt.y() - kIconSize - 4 >= m_topY ? -20 : 20);
        switch (m_searchType) {
        case TypeFile:
        case TypeKeyword:
        case TypeNotes:
        case TypeFindSource:
            m_iconSearch.paint(&p, ind);
            if (entry.respondedSearchItem > 0)
                m_ovlResponded.paint(&p, ind);
            else if (ageNow(entry.askedSearchItemAge) >= kResultTimeoutMs)
                m_ovlNoResponse.paint(&p, ind);
            break;
        case TypeStoreFile:
            m_iconStoreFile.paint(&p, ind);
            break;
        case TypeStoreKeyword:
        case TypeStoreNotes:
            m_iconStoreWord.paint(&p, ind);
            break;
        default:   // node lookups and buddy search: nothing to show
            break;
        }
    }
}

void KadLookupGraph::mouseMoveEvent(QMouseEvent* event)
{
    const int idx = hitTest(event->pos());
    const QString id = idx >= 0 ? m_entries[m_entries.size() - 1 - static_cast<size_t>(idx)].contactID
                                : QString();
    if (id != m_hotId) {
        m_hotId = id;
        update();
        // A tip already up follows the pointer to the next node at once
        if (QToolTip::isVisible()) {
            if (idx >= 0)
                QToolTip::showText(event->globalPosition().toPoint(), toolTipText(idx), this,
                                   m_nodeRects[static_cast<size_t>(idx)]);
            else
                QToolTip::hideText();
        }
    }
    QWidget::mouseMoveEvent(event);
}

void KadLookupGraph::leaveEvent(QEvent* event)
{
    if (!m_hotId.isEmpty()) {
        m_hotId.clear();
        update();
    }
    QWidget::leaveEvent(event);
}

void KadLookupGraph::contextMenuEvent(QContextMenuEvent* event)
{
    QMenu menu(this);
    QAction* autoShow = menu.addAction(tr("Automatically show active searches"));
    autoShow->setCheckable(true);
    autoShow->setChecked(m_autoShow);
    if (menu.exec(event->globalPos()) == autoShow) {
        m_autoShow = autoShow->isChecked();
        emit autoShowLookupsToggled(m_autoShow);
    }
}

// ---------------------------------------------------------------------------
// Private methods
// ---------------------------------------------------------------------------

void KadLookupGraph::layoutNodes()
{
    m_nodeRects.clear();
    m_histWidth = 0;

    m_client = rect().adjusted(2, 2, -2, -2);
    m_labelHeight = std::max(8, fontMetrics().height());
    constexpr int leftBorder = 3;
    constexpr int rightBorder = 8;
    m_topY = m_client.top() + m_labelHeight;
    m_baseLineX = m_client.left() + leftBorder;
    m_baseLineY = m_client.bottom() - m_labelHeight;
    const int histWidth = m_client.width() - leftBorder - rightBorder;
    const int histHeight = m_baseLineY - m_topY;
    if (histWidth < kNodeWidth || histHeight <= kNodeHeight)
        return;
    m_histWidth = histWidth;

    const auto hecount = static_cast<int>(m_entries.size());
    if (hecount == 0)
        return;

    // MFC KadLookupGraph.cpp:213-220
    const int maxNodes = histWidth / kNodeWidth;
    const int nodeEntryWidth = hecount > maxNodes ? kNodeWidth : histWidth / hecount;
    const int visible = std::min(maxNodes, hecount);

    // Scale: 3x the largest distance among the closest third (MFC KadLookupGraph.cpp:222-273)
    const auto k = static_cast<size_t>(std::max(1, visible / 3));
    std::vector<Dist> closest;
    closest.reserve(k);
    for (int i = 1; i <= visible; ++i) {
        const Dist& d = m_entries[static_cast<size_t>(hecount - i)].dist;
        if (closest.size() < k) {
            closest.push_back(d);
        } else {
            auto farthest = std::max_element(closest.begin(), closest.end());
            if (*farthest > d)
                *farthest = d;
        }
    }
    const Dist tmpScaling = *std::max_element(closest.begin(), closest.end());

    uint64_t scaling = 0;
    int startChunk = 0;
    for (; startChunk < 3; ++startChunk) {
        if (tmpScaling[static_cast<size_t>(startChunk)] > 0) {
            scaling = chunk64(tmpScaling, startChunk);
            break;
        }
    }
    if (scaling == 0) {
        startChunk = 2;
        scaling = tmpScaling[3];
    }
    scaling = scaling / static_cast<uint64_t>(histHeight - kNodeHeight) * 3;
    if (scaling == 0)
        scaling = 1;
    const Dist maxScaling = times3(tmpScaling);

    const int lowestY = m_baseLineY - kNodeHeight;
    m_nodeRects.reserve(static_cast<size_t>(visible));
    for (int i = 1; i <= visible; ++i) {
        const Dist& d = m_entries[static_cast<size_t>(hecount - i)].dist;
        int y = m_topY;
        if (!(d > maxScaling)) {
            const uint64_t drawY = chunk64(d, startChunk) / scaling;
            y = lowestY - static_cast<int>(std::min<uint64_t>(drawY, static_cast<uint64_t>(lowestY - m_topY)));
        }
        const int x = m_baseLineX + histWidth - i * nodeEntryWidth;
        m_nodeRects.emplace_back(x, y, kNodeWidth, kNodeHeight);
    }

    // Hot item follows the pointer as nodes move between snapshots
    if (underMouse()) {
        const int idx = hitTest(mapFromGlobal(QCursor::pos()));
        m_hotId = idx >= 0 ? m_entries[static_cast<size_t>(hecount - 1 - idx)].contactID : QString();
    }
}

int KadLookupGraph::hitTest(const QPoint& pos) const
{
    for (auto i = static_cast<int>(m_nodeRects.size()); --i >= 0;)
        if (m_nodeRects[static_cast<size_t>(i)].contains(pos))
            return i;
    return -1;
}

int KadLookupGraph::hotDrawIdx() const
{
    if (m_hotId.isEmpty())
        return -1;
    const auto hecount = static_cast<int>(m_entries.size());
    for (int i = 0; i < static_cast<int>(m_nodeRects.size()); ++i)
        if (m_entries[static_cast<size_t>(hecount - 1 - i)].contactID == m_hotId)
            return i;
    return -1;
}

int64_t KadLookupGraph::ageNow(int64_t age) const
{
    return age < 0 ? -1 : age + m_snapshotAge.elapsed();
}

int KadLookupGraph::nodeIconIdx(const LookupEntry& entry) const
{
    // MFC KadLookupGraph.cpp:422-432: 0 green, 1 blue, 2 yellow, 3 red
    if (entry.askedContactsAge >= 0) {
        if (entry.respondedContact > 0)
            return entry.providedCloser ? 0 : 1;
        return ageNow(entry.askedContactsAge) >= kContactTimeoutMs ? 3 : 2;
    }
    return entry.forcedInteresting ? 2 : 0;
}

QString KadLookupGraph::toolTipText(int drawIdx) const
{
    // MFC KadLookupGraph.cpp:500-560
    const auto hecount = static_cast<int>(m_entries.size());
    const int realIdx = hecount - (drawIdx + 1);
    const LookupEntry& e = m_entries[static_cast<size_t>(realIdx)];

    const QString discovered = e.receivedFromIdx.empty()
        ? tr("Routing Table")
        : tr("%1 Node(s)").arg(e.receivedFromIdx.size());

    QString foundNodes;
    if (e.askedContactsAge >= 0) {
        if (e.respondedContact == 0) {
            foundNodes = ageNow(e.askedContactsAge) >= kContactTimeoutMs ? tr("No Response") : tr("Asking");
        } else {
            unsigned useful = 0;
            for (const auto& other : m_entries)
                useful += static_cast<unsigned>(std::ranges::count(other.receivedFromIdx, realIdx));
            foundNodes = QStringLiteral("%1 (%2)").arg(e.respondedContact).arg(useful);
        }
    } else {
        foundNodes = tr("Not Asked");
    }

    QString results;
    if (e.askedSearchItemAge >= 0) {
        if (e.respondedSearchItem == 0)
            results = ageNow(e.askedSearchItemAge) >= kResultTimeoutMs ? tr("None") : tr("Asking");
        else
            results = QString::number(e.respondedSearchItem);
    } else {
        results = tr("Not Asked");
    }

    const auto row = [](const QString& label, const QString& value) {
        return QStringLiteral("<tr><td><b>%1:</b></td><td>&nbsp;%2</td></tr>")
            .arg(label.toHtmlEscaped(), value.toHtmlEscaped());
    };
    return QStringLiteral("<qt><b>%1</b><table cellspacing=0>").arg(e.contactID)
        + row(tr("Distance"), e.distance)
        + row(tr("Protocol Version"), QString::number(e.contactVersion))
        + QStringLiteral("<tr><td colspan=2><hr></td></tr>")
        + row(tr("Discovered by"), discovered)
        + row(tr("Found Nodes (useful)"), foundNodes)
        + QStringLiteral("<tr><td colspan=2></td></tr>")
        + row(tr("Search Results"), results)
        + QStringLiteral("</table></qt>");
}

void KadLookupGraph::refreshToolTip()
{
    if (!QToolTip::isVisible() || !underMouse())
        return;
    const int idx = hotDrawIdx();
    if (idx >= 0)
        QToolTip::showText(QCursor::pos(), toolTipText(idx), this, m_nodeRects[static_cast<size_t>(idx)]);
    else
        QToolTip::hideText();
}

} // namespace eMule
