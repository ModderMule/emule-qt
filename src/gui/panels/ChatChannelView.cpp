#include "pch.h"
/// @file ChatChannelView.cpp
/// @brief A chat tab's log and topic panes — implementation.

#include "panels/ChatChannelView.h"

#include "controls/LogTextView.h"
#include "utils/TextLinks.h"

namespace eMule {

ChatChannelView::ChatChannelView(bool withTopic, std::function<void(const QString&)> onLink, QWidget* parent)
    : QSplitter(Qt::Vertical, parent)
{
    // Channels are full of ed2k:// links; those belong to the importer, not the OS.
    m_log = new LogTextView(this);
    m_log->setReadOnly(true);
    TextLinks::wireLinkClicks(m_log, this, onLink);

    if (withTopic) {
        // 18 px by default, up to 108 (MFC IrcChannelTabCtrl.cpp:186-230).
        // Theme colours rather than MFC's white on black — deliberate, 2026-10.
        m_topic = new LogTextView(this);
        m_topic->setReadOnly(true);
        m_topic->setObjectName(QStringLiteral("ircTopic"));
        m_topic->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        m_topic->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_topic->setMinimumHeight(18);
        m_topic->setMaximumHeight(108);
        TextLinks::wireLinkClicks(m_topic, this, onLink);
        addWidget(m_topic);
    }
    addWidget(m_log);
    setChildrenCollapsible(false);
    if (m_topic) {
        setStretchFactor(0, 0);
        setStretchFactor(1, 1);
        setSizes({m_topic->fontMetrics().height() + 8, 400});
    }
    // no splitter handle without a topic: the log alone fills the tab
    setHandleWidth(m_topic ? handleWidth() : 0);
}

ChatChannelView::~ChatChannelView() = default;

void ChatChannelView::setTopicHtml(const QString& html)
{
    if (m_topic)
        m_topic->setHtml(html);
}

void ChatChannelView::append(const QString& html)
{
    m_log->append(html);
}

void ChatChannelView::applyFont(const QFont& font)
{
    if (font.family().isEmpty())
        return;
    m_log->setFont(font);
    if (m_topic)
        m_topic->setFont(font);
}

} // namespace eMule
