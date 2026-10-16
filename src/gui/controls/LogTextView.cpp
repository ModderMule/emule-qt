#include "pch.h"
/// @file LogTextView.cpp
/// @brief Tail-following text pane — implementation.

#include "controls/LogTextView.h"

#include <QAction>
#include <QContextMenuEvent>
#include <QMenu>
#include <QScrollBar>
#include <QTimer>

#include <algorithm>
#include <memory>

namespace eMule {

LogTextView::LogTextView(QWidget* parent)
    : QTextBrowser(parent)
{
    connect(verticalScrollBar(), &QScrollBar::rangeChanged, this, &LogTextView::onRangeChanged);
    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, &LogTextView::onValueChanged);
}

void LogTextView::setAutoScroll(bool on)
{
    m_autoScroll = on;
    // Back on: follow again if the view sits at the bottom now
    const QScrollBar* bar = verticalScrollBar();
    m_follow = bar->value() >= bar->maximum() - kFollowSlackPx;
}

void LogTextView::scrollToBottom()
{
    m_follow = true;
    m_pendingValue = -1;
    m_holdValue = -1;
    setBarValue(verticalScrollBar()->maximum());
}

void LogTextView::restoreScrollValue(int value)
{
    m_follow = false;
    m_pendingValue = value;
    m_holdValue = -1;
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
    // An explicit restore wins over both following and holding
    if (m_pendingValue >= 0) {
        setBarValue(std::min(m_pendingValue, max));
        if (max >= m_pendingValue)
            m_pendingValue = -1;
    } else if (!m_autoScroll) {
        // QTextEdit::append() jumps to the new maximum right after this when it sat
        // at the bottom; hold the position until control is back in the event loop
        if (m_holdValue < 0) {
            m_holdValue = verticalScrollBar()->value();
            QTimer::singleShot(0, this, [this] { m_holdValue = -1; });
        }
        setBarValue(std::min(m_holdValue, max));
    } else if (m_follow) {
        // The latch still holds the position from before the document changed, so
        // this is MFC's "was at the bottom before the insert" test.
        setBarValue(max);
    }
}

void LogTextView::onValueChanged(int value)
{
    if (m_settingValue)
        return;
    if (m_holdValue >= 0 && !m_autoScroll && !verticalScrollBar()->isSliderDown()) {
        setBarValue(std::min(m_holdValue, verticalScrollBar()->maximum()));
        return;
    }
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
