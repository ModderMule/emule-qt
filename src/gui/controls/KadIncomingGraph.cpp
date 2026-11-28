#include "pch.h"
#include "controls/KadIncomingGraph.h"
#include "app/SkinIcons.h"
#include "utils/ArgStr.h"

#include <QCborArray>
#include <QHelpEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <bit>
#include <cmath>

namespace eMule {

namespace {

constexpr int kIconSize = 16;
constexpr int kNodeBox = 20;
constexpr int kAxisWidth = 30;

using Dist = std::array<std::uint32_t, 4>;

/// 32 hex digits to 4 chunks, most significant first; false: not an id.
bool parseId(const QString& hex, Dist& out)
{
    if (hex.size() != 32)
        return false;
    for (int c = 0; c < 4; ++c) {
        bool ok = false;
        out[static_cast<size_t>(c)] = hex.mid(c * 8, 8).toUInt(&ok, 16);
        if (!ok)
            return false;
    }
    return true;
}

int leadingZeros(const Dist& d)
{
    int n = 0;
    for (const std::uint32_t c : d) {
        if (c != 0)
            return n + std::countl_zero(c);
        n += 32;
    }
    return 128;
}

double log2Of(const Dist& d)
{
    double v = 0;
    for (const std::uint32_t c : d)
        v = v * 4294967296.0 + c;
    return v > 0 ? std::log2(v) : -1;
}

QString hexOf(const Dist& d)
{
    QString s;
    for (const std::uint32_t c : d)
        s += QStringLiteral("%1").arg(c, 8, 16, QLatin1Char('0'));
    return s.toUpper();
}

void drawArrow(QPainter& p, QPointF from, QPointF to, const QColor& color, double width)
{
    // stop at the icon's edge
    const QPointF vec = to - from;
    const double len = std::hypot(vec.x(), vec.y());
    if (len < kNodeBox)
        return;
    const QPointF unit = vec / len;
    from += unit * (kNodeBox / 2.0);
    to -= unit * (kNodeBox / 2.0);
    p.setPen(QPen(color, width));
    p.drawLine(from, to);
    const QPointF normal(-unit.y(), unit.x());
    const QPointF base = to - unit * 6.0;
    QPainterPath head;
    head.moveTo(to);
    head.lineTo(base + normal * 2.5);
    head.lineTo(base - normal * 2.5);
    head.closeSubpath();
    p.fillPath(head, color);
}

} // namespace

KadIncomingGraph::KadIncomingGraph(QWidget* parent)
    : KadGraphBase(parent)
    , m_icons{skinIcon(QStringLiteral(":/icons/Contact0.ico")), skinIcon(QStringLiteral(":/icons/Contact1.ico")),
              skinIcon(QStringLiteral(":/icons/Contact2.ico")), skinIcon(QStringLiteral(":/icons/Contact3.ico")),
              skinIcon(QStringLiteral(":/icons/Kad.ico"))}
{
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    clear();
}

void KadIncomingGraph::setRequest(const QCborMap& row, const QCborMap& graph, const QString& title,
                                  const QString& answer)
{
    m_requestId = static_cast<quint64>(row.value(QStringLiteral("id")).toInteger());
    m_title = title;
    m_answer = answer;
    m_target = row.value(QStringLiteral("target")).toString().toUpper();
    m_wanted = static_cast<int>(graph.value(QStringLiteral("wanted")).toInteger(-1));
    m_nodes.clear();
    m_hint.clear();
    m_extra.clear();

    const qint64 results = row.value(QStringLiteral("results")).toInteger(-1);
    const qint64 records = row.value(QStringLiteral("records")).toInteger(-1);
    if (results >= 0)
        m_extra = tr("%n result(s) sent", nullptr, int(results));
    else if (records >= 0)
        m_extra = tr("%1 of %2 stored").arg(argStr(row.value(QStringLiteral("stored")).toInteger()), argStr(records));

    Dist target{};
    if (!parseId(m_target, target)) {
        m_hint = tr("This request has no target: its contacts are not a step towards anything.");
        m_handedCount = 0;
        setLoading(false);   // and the tip under the pointer follows
        update();
        return;
    }

    const auto add = [&](Role role, const QCborMap& m) {
        Node n;
        n.role = role;
        n.id = m.value(QStringLiteral("id")).toString().toUpper();
        n.nodeId = m.value(QStringLiteral("node")).toString().toUpper();
        if (n.nodeId == n.id)
            n.nodeId.clear();
        n.addr = m.value(QStringLiteral("addr")).toString();
        n.cc = m.value(QStringLiteral("cc")).toString();
        n.version = static_cast<int>(m.value(QStringLiteral("version")).toInteger(-1));
        Dist id{};
        n.known = parseId(n.id, id);
        if (n.known) {
            for (size_t c = 0; c < 4; ++c)
                n.dist[c] = id[c] ^ target[c];
            const double l = log2Of(n.dist);
            n.closeness = l < 0 ? 128.0 : 128.0 - l;
            n.prefixBits = leadingZeros(n.dist);
        }
        m_nodes.push_back(std::move(n));
    };
    QCborMap asker = graph.value(QStringLiteral("asker")).toMap();
    if (!asker.contains(QStringLiteral("addr")))
        asker.insert(QStringLiteral("addr"), row.value(QStringLiteral("addr")));
    if (!asker.contains(QStringLiteral("cc")))
        asker.insert(QStringLiteral("cc"), row.value(QStringLiteral("cc")));
    add(Role::Asker, asker);
    add(Role::Own, graph.value(QStringLiteral("own")).toMap());
    const QCborArray handed = graph.value(QStringLiteral("handed")).toArray();
    for (const auto& v : handed)
        add(Role::Handed, v.toMap());
    m_handedCount = static_cast<int>(handed.size());
    // farther left, closer right: the step the lookup takes reads left to right
    std::stable_sort(m_nodes.begin() + 2, m_nodes.end(),
                     [](const Node& a, const Node& b) { return a.closeness < b.closeness; });
    m_hot = -1;
    layoutNodes();
    setLoading(false);   // and the tip under the pointer follows the new nodes
    update();
}

void KadIncomingGraph::clear()
{
    m_nodes.clear();
    m_requestId = 0;
    m_title.clear();
    m_answer.clear();
    m_target.clear();
    m_extra.clear();
    m_handedCount = 0;
    m_wanted = -1;
    m_hot = -1;
    m_hint = tr("Select an incoming search to see who asked us and which closer contacts we handed out.");
    setLoading(false);   // and no tip under the pointer
    update();
}

// ---------------------------------------------------------------------------
// Events
// ---------------------------------------------------------------------------

void KadIncomingGraph::paintEvent(QPaintEvent* /*event*/)
{
    layoutNodes();
    QPainter p(this);
    p.fillRect(rect(), palette().color(QPalette::Base));
    p.setPen(palette().color(QPalette::Mid));
    p.drawRect(rect().adjusted(0, 0, -1, -1));

    const QRect client = rect().adjusted(6, 4, -6, -4);
    const QColor text = palette().color(QPalette::Text);
    QColor dim = text;
    dim.setAlphaF(0.55f);

    // header: what was asked, and what we answered
    QFont bold = font();
    bold.setBold(true);
    p.setFont(bold);
    p.setPen(text);
    const QRect head(client.left(), client.top(), client.width(), m_labelHeight);
    if (!m_title.isEmpty())
        p.drawText(head, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine,
                   fontMetrics().elidedText(m_title, Qt::ElideRight, client.width() * 2 / 3));
    p.setFont(font());
    if (!m_answer.isEmpty())
        p.drawText(head, Qt::AlignRight | Qt::AlignVCenter | Qt::TextSingleLine, m_answer);

    if (!m_hint.isEmpty() || m_nodes.size() < 2 || m_plot.width() <= kNodeBox || m_plot.height() <= kNodeBox) {
        p.setPen(dim);
        p.drawText(client.adjusted(20, m_labelHeight, -20, 0), Qt::AlignCenter | Qt::TextWordWrap,
                   m_hint.isEmpty() ? tr("Too small to draw") : m_hint);
        return;
    }

    paintAxis(p);
    p.setRenderHint(QPainter::Antialiasing, true);

    const Node& asker = m_nodes[0];
    const Node& me = m_nodes[1];
    const QPointF meC = QRectF(me.rect).center();

    // our distance: contacts below the line are the step forward we gave
    if (me.known) {
        QPen dash(QColor(0, 120, 215), 1, Qt::DashLine);
        p.setPen(dash);
        const int y = yOf(me.closeness);
        p.drawLine(QPointF(meC.x() + kNodeBox / 2.0, y), QPointF(m_plot.right(), y));
        p.setPen(QColor(0, 120, 215));
        p.drawText(QRect(m_plot.right() - 200, y - m_labelHeight - 1, 200, m_labelHeight),
                   Qt::AlignRight | Qt::AlignBottom | Qt::TextSingleLine, tr("our distance"));
    }

    const QColor gray(150, 150, 150), green(0, 150, 0), red(230, 30, 30), blue(0, 90, 200);
    // requester -> us
    drawArrow(p, QRectF(asker.rect).center(), meC, m_hot == 0 || m_hot == 1 ? red : blue, 1.5);
    // us -> each contact handed out
    for (size_t i = 2; i < m_nodes.size(); ++i) {
        const Node& n = m_nodes[i];
        const bool closer = me.known && n.known && n.closeness > me.closeness;
        const QColor c = m_hot == static_cast<int>(i) ? red : closer ? green : gray;
        drawArrow(p, meC, QRectF(n.rect).center(), c, m_hot == static_cast<int>(i) ? 1.5 : 1.0);
    }

    for (size_t i = 0; i < m_nodes.size(); ++i) {
        const Node& n = m_nodes[i];
        const QRect icon(n.rect.center() - QPoint(kIconSize / 2 - 1, kIconSize / 2 - 1), QSize(kIconSize, kIconSize));
        p.setOpacity(n.known ? 1.0 : 0.5);
        iconOf(n).paint(&p, icon);
        p.setOpacity(1.0);
        if (!n.known) {
            p.setPen(text);
            p.drawText(QRect(icon.right() + 2, icon.top(), 16, icon.height()), Qt::AlignLeft | Qt::AlignVCenter,
                       QStringLiteral("?"));
        }
    }

    // what the answer held besides contacts
    if (!m_extra.isEmpty()) {
        p.setPen(text);
        p.drawText(QRect(me.rect.center().x() - 100, me.rect.bottom() + 2, 200, m_labelHeight),
                   Qt::AlignHCenter | Qt::AlignTop | Qt::TextSingleLine, m_extra);
    }

    // column captions
    p.setPen(dim);
    const int capY = m_plot.bottom() + 4;
    const auto caption = [&](int cx, const QString& s) {
        p.drawText(QRect(cx - 120, capY, 240, m_labelHeight), Qt::AlignHCenter | Qt::AlignTop | Qt::TextSingleLine, s);
    };
    caption(asker.rect.center().x(), tr("Requester"));
    caption(me.rect.center().x(), tr("This node"));
    const int handedX = m_plot.left() + m_plot.width() * 3 / 4;
    caption(handedX, m_handedCount == 0 ? tr("No contacts handed out")
                     : m_wanted > 0      ? tr("Handed out %1 of %2 asked").arg(m_handedCount).arg(m_wanted)
                                         : tr("Handed out (%1)").arg(m_handedCount));
}

void KadIncomingGraph::mouseMoveEvent(QMouseEvent* event)
{
    const int idx = hitTest(event->pos());
    if (idx != m_hot) {
        m_hot = idx;
        update();
    }
    KadGraphBase::mouseMoveEvent(event);
}

void KadIncomingGraph::leaveEvent(QEvent* event)
{
    if (m_hot >= 0) {
        m_hot = -1;
        update();
    }
    KadGraphBase::leaveEvent(event);
}

// ---------------------------------------------------------------------------
// Private methods
// ---------------------------------------------------------------------------

void KadIncomingGraph::layoutNodes()
{
    m_labelHeight = std::max(12, fontMetrics().height());
    const QRect client = rect().adjusted(6, 4, -6, -4);
    // header, axis title above; captions and the answer note below
    m_plot = QRect(client.left() + kAxisWidth, client.top() + 2 * m_labelHeight + 6,
                   client.width() - kAxisWidth - kNodeBox / 2,
                   client.height() - 4 * m_labelHeight - 12);
    if (m_nodes.size() < 2 || m_plot.width() <= kNodeBox || m_plot.height() <= kNodeBox)
        return;

    // zoom on the bits that matter: a big network shares many leading bits everywhere
    double lo = 128, hi = 0;
    for (const Node& n : m_nodes) {
        if (!n.known)
            continue;
        lo = std::min(lo, n.closeness);
        hi = std::max(hi, n.closeness);
    }
    if (lo > hi) {
        lo = 0;
        hi = 128;
    }
    m_lo = std::max(0, static_cast<int>(std::floor(lo)) - 1);
    m_hi = std::min(128, static_cast<int>(std::ceil(hi)) + 1);
    if (m_hi - m_lo < 4) {
        m_hi = std::min(128, m_lo + 4);
        m_lo = std::max(0, m_hi - 4);
    }

    const auto place = [&](Node& n, int cx) {
        const int cy = n.known ? yOf(n.closeness) : m_plot.top();
        n.rect = QRect(cx - kNodeBox / 2, cy - kNodeBox / 2, kNodeBox, kNodeBox);
    };
    place(m_nodes[0], m_plot.left() + m_plot.width() / 10);
    place(m_nodes[1], m_plot.left() + m_plot.width() * 3 / 10);
    const auto handed = static_cast<int>(m_nodes.size()) - 2;
    const int left = m_plot.left() + m_plot.width() / 2;
    const int span = m_plot.right() - kNodeBox - left;
    for (int i = 0; i < handed; ++i) {
        const int cx = handed == 1 ? left + span / 2 : left + span * i / (handed - 1);
        place(m_nodes[static_cast<size_t>(i + 2)], cx);
    }
}

int KadIncomingGraph::yOf(double closeness) const
{
    const double f = (closeness - m_lo) / std::max(1, m_hi - m_lo);
    const int inner = m_plot.height() - kNodeBox;
    return m_plot.top() + kNodeBox / 2 + static_cast<int>(std::clamp(f, 0.0, 1.0) * inner);
}

int KadIncomingGraph::hitTest(const QPoint& pos) const
{
    if (!m_hint.isEmpty())
        return -1;
    for (auto i = static_cast<int>(m_nodes.size()); --i >= 0;)
        if (m_nodes[static_cast<size_t>(i)].rect.contains(pos))
            return i;
    return -1;
}

QRect KadIncomingGraph::nodeRect(int idx) const
{
    return m_nodes[static_cast<size_t>(idx)].rect;
}

QString KadIncomingGraph::toolTipText(int idx) const
{
    const Node& n = m_nodes[static_cast<size_t>(idx)];
    const Node* me = own();
    const auto row = &KadGraphBase::tipRow;
    QString role;
    switch (n.role) {
    case Role::Asker: role = tr("Requester"); break;
    case Role::Own: role = tr("This node"); break;
    case Role::Handed: role = tr("Handed-out contact"); break;
    }
    QString html = QStringLiteral("<qt><b>%1</b><table cellspacing=0>").arg(role.toHtmlEscaped());
    // eD2K: the Kad ID is the node ID; ed3k: a routing ID besides the node ID
    const QString unknown = n.role == Role::Asker ? tr("unknown (not in our routing table)") : tr("unknown");
    if (!n.nodeId.isEmpty()) {
        html += row(tr("Node ID"), n.nodeId);
        html += row(tr("Routing ID"), n.known ? n.id : unknown);
    } else {
        html += row(tr("Node ID"), n.known ? n.id : unknown);
    }
    html += countryRow(n.cc);
    if (n.version >= 0)
        html += row(tr("Protocol Version"), QString::number(n.version));
    if (n.known) {
        html += QStringLiteral("<tr><td colspan=2><hr></td></tr>");
        html += row(tr("Target"), m_target);
        html += row(tr("Distance"), hexOf(n.dist));
        html += row(tr("Common prefix"), tr("%n bit(s)", nullptr, n.prefixBits));
        if (me && me->known && n.role != Role::Own) {
            const int diff = n.prefixBits - me->prefixBits;
            const bool closer = n.closeness > me->closeness;
            html += row(tr("Compared to us"),
                        closer ? (diff > 0 ? tr("closer by %n bit(s)", nullptr, diff) : tr("closer"))
                               : (diff < 0 ? tr("farther by %n bit(s)", nullptr, -diff) : tr("not closer")));
        }
    }
    return html + QStringLiteral("</table></qt>");
}

const QIcon& KadIncomingGraph::iconOf(const Node& n) const
{
    switch (n.role) {
    case Role::Own: return m_icons[4];
    case Role::Asker: return n.known ? m_icons[1] : m_icons[3];
    case Role::Handed: break;
    }
    const Node* me = own();
    return me && me->known && n.closeness > me->closeness ? m_icons[0] : m_icons[2];
}

const KadIncomingGraph::Node* KadIncomingGraph::own() const
{
    return m_nodes.size() >= 2 ? &m_nodes[1] : nullptr;
}

void KadIncomingGraph::paintAxis(QPainter& p) const
{
    // Y: common prefix bits with the target, closer down
    p.setRenderHint(QPainter::Antialiasing, false);
    const QColor axis(128, 128, 128);
    QColor grid = palette().color(QPalette::Mid);
    grid.setAlphaF(0.35f);
    const int span = m_hi - m_lo;
    const int step = span <= 8 ? 1 : span <= 16 ? 2 : span <= 40 ? 5 : span <= 80 ? 10 : 20;
    const int labelW = kAxisWidth - 4;
    for (int b = (m_lo + step - 1) / step * step; b <= m_hi; b += step) {
        const int y = yOf(b);
        p.setPen(grid);
        p.drawLine(m_plot.left(), y, m_plot.right(), y);
        p.setPen(axis);
        p.drawText(QRect(m_plot.left() - labelW - 3, y - m_labelHeight / 2, labelW, m_labelHeight),
                   Qt::AlignRight | Qt::AlignVCenter, QString::number(b));
    }
    p.setPen(axis);
    p.drawLine(m_plot.left(), m_plot.top(), m_plot.left(), m_plot.bottom());
    p.drawLine(m_plot.left(), m_plot.bottom(), m_plot.right(), m_plot.bottom());
    p.setPen(palette().color(QPalette::Text));
    p.drawText(QRect(m_plot.left() - kAxisWidth, m_plot.top() - m_labelHeight - 4, m_plot.width(), m_labelHeight),
               Qt::AlignLeft | Qt::AlignBottom | Qt::TextSingleLine,
               tr("Bits shared with the target (closer downwards)"));
}

} // namespace eMule
