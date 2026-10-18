#include "pch.h"
/// @file LogTextView.cpp
/// @brief Tail-following text pane — implementation.

#include "controls/LogTextView.h"

#include <QAction>
#include <QContextMenuEvent>
#include <QMenu>
#include <QAbstractTextDocumentLayout>
#include <QScrollBar>
#include <QTextBlock>
#include <QTimer>

#include <algorithm>
#include <memory>

namespace eMule {

LogTextView::LogTextView(QWidget* parent)
    : QTextBrowser(parent)
    , m_anchor(document())
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
    captureAnchor();
}

void LogTextView::scrollToBottom()
{
    m_follow = true;
    m_pendingValue = -1;
    m_holding = false;
    setBarValue(verticalScrollBar()->maximum());
    captureAnchor();
}

void LogTextView::restoreScrollValue(int value)
{
    m_follow = false;
    m_pendingValue = value;
    m_holding = false;
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
        if (max >= m_pendingValue) {
            m_pendingValue = -1;
            captureAnchor();
        }
    } else if (m_autoScroll && m_follow) {
        // The latch still holds the position from before the document changed, so
        // this is MFC's "was at the bottom before the insert" test.
        setBarValue(max);
    } else {
        // Keep the top line in place: lines trimmed or sorted in above it move it
        setBarValue(std::min(anchoredValue(), max));
        if (!m_autoScroll && !m_holding) {
            // QTextEdit::append() jumps to the new maximum right after this when it
            // sat at the bottom; hold until control is back in the event loop
            m_holding = true;
            QTimer::singleShot(0, this, [this] { m_holding = false; });
        }
    }
}

void LogTextView::onValueChanged(int value)
{
    if (m_settingValue)
        return;
    if (m_holding && !m_autoScroll && !verticalScrollBar()->isSliderDown()) {
        setBarValue(std::min(anchoredValue(), verticalScrollBar()->maximum()));
        return;
    }
    m_pendingValue = -1;
    m_follow = value >= verticalScrollBar()->maximum() - kFollowSlackPx;
    captureAnchor();
}

void LogTextView::setBarValue(int value)
{
    m_settingValue = true;
    verticalScrollBar()->setValue(value);
    m_settingValue = false;
}

void LogTextView::captureAnchor()
{
    const int value = verticalScrollBar()->value();
    if (m_anchor.document() != document())
        m_anchor = QTextCursor(document());
    QAbstractTextDocumentLayout* layout = document()->documentLayout();
    const int pos = std::max(0, layout->hitTest(QPointF(0, value), Qt::FuzzyHit));
    const QTextBlock block = document()->findBlock(pos);
    m_anchor.setPosition(block.isValid() ? block.position() : 0);
    m_anchorOffset = block.isValid()
        ? value - qRound(layout->blockBoundingRect(block).top())
        : value;
}

int LogTextView::anchoredValue() const
{
    if (m_anchor.document() != document())   // setDocument() swapped it
        return verticalScrollBar()->value();
    const QTextBlock block = m_anchor.block();
    if (!block.isValid())
        return m_anchorOffset;
    const QRectF rect = document()->documentLayout()->blockBoundingRect(block);
    return std::max(0, qRound(rect.top()) + m_anchorOffset);
}

} // namespace eMule
