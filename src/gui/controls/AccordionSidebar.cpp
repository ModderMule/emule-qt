#include "pch.h"
/// @file AccordionSidebar.cpp
/// @brief Outlook-bar sidebar — see AccordionSidebar.h.

#include "controls/AccordionSidebar.h"
#include "utils/ColorUtils.h"

#include <QAbstractButton>
#include <QFontMetrics>
#include <QListWidget>
#include <QPainter>
#include <QStyle>
#include <QStyleOption>
#include <QVBoxLayout>

#include <algorithm>

namespace eMule {

namespace {

constexpr int kIconSize      = 16;
constexpr int kMinBarWidth   = 180;
constexpr int kMaxBarWidth   = 280;
constexpr int kHeaderPadding = 8;   // above + below the caption
constexpr int kArrowBox      = 12;
constexpr int kArrowInset    = 6;

/// One accordion header bar.
///
/// Not a QPushButton: QMacStyle draws PE_PanelButtonCommand as a rounded pill of its own
/// height, and a full-width bevelled band is not a shape it knows — forcing it with a
/// stylesheet would opt the control out of the native palette, dark mode included. So the
/// bar is painted from palette roles only, and the expanded/collapsed state is carried by
/// a disclosure arrow rather than by MorphXT's 3D bevel, which a flat band cannot show.
///
/// No Q_OBJECT: it adds no signals of its own and uses only QAbstractButton::clicked(),
/// so there is nothing for moc to generate and nothing to add to the project files.
class HeaderBar : public QAbstractButton {
public:
    HeaderBar(const QString& title, QWidget* parent)
        : QAbstractButton(parent)
    {
        setText(title);
        setCheckable(true);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
        setAttribute(Qt::WA_Hover);
        QFont f = font();
        f.setBold(true);
        setFont(f);
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    }

    /// 24 px is CSlideBar::m_iGroupHeight; a larger UI font wins over it.
    [[nodiscard]] QSize sizeHint() const override
    {
        const QFontMetrics fm(font());
        return {fm.horizontalAdvance(text()) + 2 * (kArrowInset + kArrowBox),
                std::max(24, fm.height() + kHeaderPadding)};
    }

    void setFirst(bool first) { m_first = first; }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter  p(this);
        const QPalette& pal = palette();

        // Mixed towards the text colour rather than taken from a role: QPalette::Window
        // and ::Button are both plain white under QMacStyle, so a band painted in either
        // is invisible against the item list -- and lighter()/darker() on white is still
        // white. Mixing with the foreground also inverts by itself in dark mode.
        // Expanded and collapsed share the fill; the arrow says which is which, as
        // MorphXT's 3D bevel used to.
        qreal weight = 0.09;
        if (isDown())
            weight = 0.24;
        else if (underMouse())
            weight = 0.15;
        p.fillRect(rect(), blend(pal.color(QPalette::Window),
                                 pal.color(QPalette::ButtonText), weight));

        // Hairline separators. Only the first bar draws a top edge; every other one sits
        // under a list or another bar that already drew its own bottom.
        p.setPen(pal.color(QPalette::Mid));
        if (m_first)
            p.drawLine(rect().topLeft(), rect().topRight());
        p.drawLine(rect().bottomLeft(), rect().bottomRight());

        QStyleOption arrow;
        arrow.initFrom(this);
        arrow.rect = QRect(kArrowInset, (height() - kArrowBox) / 2, kArrowBox, kArrowBox);
        style()->drawPrimitive(isChecked() ? QStyle::PE_IndicatorArrowDown
                                           : QStyle::PE_IndicatorArrowRight,
                               &arrow, &p, this);

        const int   inset = kArrowInset + kArrowBox;
        const QRect textRect = rect().adjusted(inset, 0, -inset, 0);
        p.setPen(pal.color(QPalette::ButtonText));
        p.drawText(textRect, Qt::AlignCenter,
                   fontMetrics().elidedText(text(), Qt::ElideRight, textRect.width()));
    }

private:
    bool m_first = false;
};

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

AccordionSidebar::AccordionSidebar(QWidget* parent)
    : QWidget(parent)
{
    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(0);

    // Fixed horizontally so the QHBoxLayout honours sizeHint() without a setFixedWidth;
    // the width follows the longest caption instead of a number picked for one language.
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    setBackgroundRole(QPalette::Base);
    setAutoFillBackground(true);
}

int AccordionSidebar::addGroup(const QString& title)
{
    const int index = int(m_groups.size());

    auto* header = new HeaderBar(title, this);
    static_cast<HeaderBar*>(header)->setFirst(index == 0);
    connect(header, &QAbstractButton::clicked, this,
            [this, index] { setCurrentGroup(index); });

    auto* list = new QListWidget(this);
    list->setFrameShape(QFrame::NoFrame);
    list->setIconSize(QSize(kIconSize, kIconSize));
    list->setUniformItemSizes(true);
    list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list->setFocusPolicy(Qt::StrongFocus);
    // A list that has lost focus paints its selection grey, which on a navigation bar
    // reads as "nothing is selected" the moment the user clicks into the page.
    list->setStyleSheet(QStringLiteral(
        "QListWidget::item:selected:!active { background: palette(highlight);"
        " color: palette(highlighted-text); }"));
    list->hide();
    connect(list, &QListWidget::currentRowChanged, this,
            [this, index](int row) { onListRowChanged(index, row); });

    m_layout->addWidget(header);
    m_layout->addWidget(list);

    m_groups.push_back({title, header, list});
    return index;
}

void AccordionSidebar::addItem(int group, const QIcon& icon, const QString& text, int id)
{
    if (group < 0 || group >= int(m_groups.size()))
        return;

    auto* item = new QListWidgetItem(icon, text);
    item->setData(Qt::UserRole, id);

    {
        const QSignalBlocker block(m_groups[size_t(group)].list);
        m_groups[size_t(group)].list->addItem(item);
    }
    ensureSomethingCurrent();
}

int AccordionSidebar::currentItemId() const
{
    const Group* group = groupAt(m_currentGroup);
    if (!group)
        return -1;
    const QListWidgetItem* item = group->list->currentItem();
    return item ? item->data(Qt::UserRole).toInt() : -1;
}

QString AccordionSidebar::currentItemText() const
{
    const Group* group = groupAt(m_currentGroup);
    if (!group)
        return {};
    const QListWidgetItem* item = group->list->currentItem();
    return item ? item->text() : QString();
}

int AccordionSidebar::currentGroup() const
{
    return m_currentGroup;
}

QString AccordionSidebar::groupTitle(int group) const
{
    const Group* g = groupAt(group);
    return g ? g->title : QString();
}

QString AccordionSidebar::currentGroupTitle() const
{
    return groupTitle(m_currentGroup);
}

int AccordionSidebar::itemCount() const
{
    int total = 0;
    for (const Group& group : m_groups)
        total += group.list->count();
    return total;
}

int AccordionSidebar::currentOrdinal() const
{
    const Group* current = groupAt(m_currentGroup);
    if (!current || current->list->currentRow() < 0)
        return -1;

    int ordinal = 0;
    for (int i = 0; i < m_currentGroup; ++i)
        ordinal += m_groups[size_t(i)].list->count();
    return ordinal + current->list->currentRow();
}

bool AccordionSidebar::setCurrentItemId(int id)
{
    for (size_t g = 0; g < m_groups.size(); ++g) {
        QListWidget* list = m_groups[g].list;
        for (int row = 0; row < list->count(); ++row) {
            if (list->item(row)->data(Qt::UserRole).toInt() != id)
                continue;

            // Set the row first, then expand: expanding emits currentItemChanged for
            // whatever the group already had selected, and a caller asking for a
            // specific page would see that page flash past first.
            {
                const QSignalBlocker block(list);
                list->setCurrentRow(row);
            }
            if (int(g) == m_currentGroup)
                emit currentItemChanged(id);
            else
                setCurrentGroup(int(g));
            return true;
        }
    }
    return false;
}

void AccordionSidebar::setCurrentGroup(int group)
{
    if (group < 0 || group >= int(m_groups.size()))
        return;

    const bool groupChanged = (group != m_currentGroup);
    if (groupChanged && m_currentGroup >= 0) {
        Group& previous = m_groups[size_t(m_currentGroup)];
        previous.list->hide();
        static_cast<QAbstractButton*>(previous.header)->setChecked(false);
    }

    m_currentGroup = group;
    Group& current = m_groups[size_t(group)];
    static_cast<QAbstractButton*>(current.header)->setChecked(true);
    current.list->show();
    // Only the visible list may stretch; the hidden ones take no space at all, which is
    // what pins every header after this one to the bottom edge.
    for (size_t i = 0; i < m_groups.size(); ++i)
        m_layout->setStretchFactor(m_groups[i].list, int(i) == group ? 1 : 0);

    // A group with nothing selected yet opens on its first item, so expanding a header
    // always lands somewhere — CSlideBar posts WM_SBN_SELCHANGED for the same reason.
    if (current.list->currentRow() < 0 && current.list->count() > 0) {
        const QSignalBlocker block(current.list);
        current.list->setCurrentRow(0);
    }

    // Or the arrow keys land nowhere after the list the user was in went away.
    if (current.list->isVisible())
        current.list->setFocus();

    if (groupChanged)
        emit currentGroupChanged(group);
    if (currentItemId() >= 0)
        emit currentItemChanged(currentItemId());
}

QSize AccordionSidebar::sizeHint() const
{
    // CSlideBar::GetGreaterStringWidth(): the widest caption in the bold header font or
    // item text in the item font decides the bar, not a number picked for one language.
    int widest = 0;
    for (const Group& group : m_groups) {
        widest = std::max(widest, group.header->sizeHint().width());

        const QFontMetrics fm(group.list->font());
        for (int row = 0; row < group.list->count(); ++row) {
            widest = std::max(widest,
                              fm.horizontalAdvance(group.list->item(row)->text())
                                  + kIconSize + 24);
        }
    }

    return {std::clamp(widest, kMinBarWidth, kMaxBarWidth), QWidget::sizeHint().height()};
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

void AccordionSidebar::onListRowChanged(int group, int row)
{
    if (m_updating || group != m_currentGroup || row < 0)
        return;

    m_updating = true;
    emit currentItemChanged(currentItemId());
    m_updating = false;
}

void AccordionSidebar::ensureSomethingCurrent()
{
    if (m_currentGroup >= 0)
        return;

    // Silent: this runs while the caller is still filling the bar, before it has had a
    // chance to connect to us, and a page change emitted from inside addItem() would be
    // both surprising and unheard.
    for (size_t g = 0; g < m_groups.size(); ++g) {
        if (m_groups[g].list->count() == 0)
            continue;
        const QSignalBlocker block(this);
        setCurrentGroup(int(g));
        return;
    }
}

const AccordionSidebar::Group* AccordionSidebar::groupAt(int index) const
{
    if (index < 0 || index >= int(m_groups.size()))
        return nullptr;
    return &m_groups[size_t(index)];
}

} // namespace eMule
