#pragma once

/// @file ChatChannelView.h
/// @brief What a chat tab of the IRC panel shows: the log, and for a channel its topic pane
/// above it behind a splitter (MFC IrcChannelTabCtrl.cpp:186-230). IRC channels and private
/// chats use it as it is; an ed3k room (Ed3kRoomTab) adds its room on top.

#include <QFont>
#include <QSplitter>

#include <functional>

namespace eMule {

class LogTextView;

class ChatChannelView : public QSplitter {
    Q_OBJECT

public:
    /// @p withTopic: a channel (topic pane over the log); else the log alone.
    /// @p onLink: a clicked link, as plain text (TextLinks.h).
    ChatChannelView(bool withTopic, std::function<void(const QString&)> onLink, QWidget* parent = nullptr);
    ~ChatChannelView() override;

    [[nodiscard]] LogTextView* log() const { return m_log; }
    /// The topic pane; null without one.
    [[nodiscard]] LogTextView* topicView() const { return m_topic; }

    void setTopicHtml(const QString& html);
    void append(const QString& html);
    /// A font for both panes; an empty family keeps the default.
    void applyFont(const QFont& font);

private:
    LogTextView* m_log = nullptr;
    LogTextView* m_topic = nullptr;
};

} // namespace eMule
