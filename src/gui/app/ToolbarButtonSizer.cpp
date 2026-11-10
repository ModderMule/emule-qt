#include "app/ToolbarButtonSizer.h"

#include <QAction>
#include <QEvent>
#include <QLayout>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>

#include <algorithm>
#include <functional>

namespace eMule {

ToolbarButtonSizer::ToolbarButtonSizer(QToolBar* bar)
    : QObject(bar)
    , m_bar(bar)
{
    bar->installEventFilter(this);
}

void ToolbarButtonSizer::update()
{
    m_pending = false;
    if (!m_bar)
        return;

    QList<QToolButton*> buttons;
    QList<int> naturals;
    int others = 0;   // separators and fixed widgets (speed graph)
    int items = 0;
    for (QAction* action : m_bar->actions()) {
        if (!action->isVisible())
            continue;
        QWidget* widget = m_bar->widgetForAction(action);
        if (!widget)
            continue;
        ++items;
        if (auto* button = qobject_cast<QToolButton*>(widget)) {
            buttons << button;
            naturals << button->sizeHint().width();
        } else if (widget->sizePolicy().horizontalPolicy() != QSizePolicy::Expanding) {
            others += widget->sizeHint().width();
        }
    }

    // Only labels under icons: beside or without text, MFC sizes each button alone
    const bool even = m_bar->toolButtonStyle() == Qt::ToolButtonTextUnderIcon
                      && m_bar->orientation() == Qt::Horizontal;
    int width = 0;
    if (even && !buttons.isEmpty()) {
        const QLayout* layout = m_bar->layout();
        const int spacing = layout ? std::max(0, layout->spacing()) : 0;
        const QMargins margins = layout ? layout->contentsMargins() : QMargins{};
        const int available = m_bar->contentsRect().width() - margins.left() - margins.right()
                              - others - spacing * std::max(0, items - 1);
        width = uniformWidth(available, naturals);
    }

    m_applying = true;
    for (QToolButton* button : buttons)
        button->setMinimumWidth(width);   // 0: back to the label's own width
    m_applying = false;
}

int ToolbarButtonSizer::uniformWidth(int available, QList<int> naturals)
{
    if (naturals.isEmpty() || available <= 0)
        return 0;
    std::ranges::sort(naturals, std::greater{});
    const int widest = naturals.front();
    // Widest first: one wider than the share of what is left keeps its own width
    qsizetype rest = naturals.size();
    for (int natural : naturals) {
        if (natural * rest <= available)
            break;
        available -= natural;
        --rest;
    }
    if (rest == 0 || available <= 0)
        return 0;   // even own widths do not fit: the » menu takes over
    return std::min<int>(widest, static_cast<int>(available / rest));
}

bool ToolbarButtonSizer::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_bar && !m_applying) {
        switch (event->type()) {
        case QEvent::Resize:
        case QEvent::LayoutRequest:   // a label changed (Connect/Disconnect), buttons rebuilt
        case QEvent::FontChange:
        case QEvent::StyleChange:
            scheduleUpdate();
            break;
        default:
            break;
        }
    }
    return QObject::eventFilter(watched, event);
}

void ToolbarButtonSizer::scheduleUpdate()
{
    // Once per event-loop turn: a rebuild sends a burst of layout requests
    if (m_pending)
        return;
    m_pending = true;
    QTimer::singleShot(0, this, &ToolbarButtonSizer::update);
}

} // namespace eMule
