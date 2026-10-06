#include "pch.h"
/// @file LogTextView.cpp
/// @brief Tail-following text pane — implementation.

#include "controls/LogTextView.h"

#include <QAction>
#include <QContextMenuEvent>
#include <QMenu>
#include <QScrollBar>

#include <algorithm>
#include <memory>

namespace eMule {

LogTextView::LogTextView(QWidget* parent)
    : QTextBrowser(parent)
{
    connect(verticalScrollBar(), &QScrollBar::rangeChanged, this, &LogTextView::onRangeChanged);
    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, &LogTextView::onValueChanged);
}

void LogTextView::scrollToBottom()
{
    m_follow = true;
    m_pendingValue = -1;
    setBarValue(verticalScrollBar()->maximum());
}

void LogTextView::restoreScrollValue(int value)
{
    m_follow = false;
    m_pendingValue = value;
    onRangeChanged(verticalScrollBar()->minimum(), verticalScrollBar()->maximum());
}

void LogTextView::contextMenuEvent(QContextMenuEvent* event)
{
    // MFC: CHTRichEditCtrl::OnContextMenu — srchybrid/HTRichEditCtrl.cpp:537.
    const std::unique_ptr<QMenu> menu(createStandardContextMenu(event->pos()));
    menu->addSeparator();
    QAction* autoScroll = menu->addAction(tr("Autoscroll"));
    autoScroll->setCheckable(true);
    autoScroll->setChecked(m_autoScroll);
    // Toggles the flag only, as there (OnCommand, :554) — no scrolling.
    connect(autoScroll, &QAction::toggled, this, &LogTextView::setAutoScroll);
    menu->exec(event->globalPos());
}

void LogTextView::onRangeChanged(int /*min*/, int max)
{
    // The latch still holds the position from before the document changed, so
    // this is MFC's "was at the bottom before the insert" test.
    if (m_follow) {
        setBarValue(max);
    } else if (m_pendingValue >= 0) {
        setBarValue(std::min(m_pendingValue, max));
        if (max >= m_pendingValue)
            m_pendingValue = -1;
    }
}

void LogTextView::onValueChanged(int value)
{
    if (m_settingValue)
        return;
    m_pendingValue = -1;
    m_follow = value >= verticalScrollBar()->maximum() - kFollowSlackPx;
}

void LogTextView::setBarValue(int value)
{
    m_settingValue = true;
    verticalScrollBar()->setValue(value);
    m_settingValue = false;
}

} // namespace eMule
