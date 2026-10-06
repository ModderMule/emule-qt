#pragma once

/// @file LogTextView.h
/// @brief Append-only text pane that follows its tail — port of the scrolling
/// rules of MFC CHTRichEditCtrl (srchybrid/HTRichEditCtrl.cpp).
///
/// QTextEdit::append() follows new text only when the scrollbar sat exactly at
/// its maximum beforehand, and nothing ever re-pins: text added while hidden, a
/// resize, a font change or a trim of the oldest lines leaves the view stranded.
/// This pane keeps one "following" latch instead and re-pins whenever the
/// document's height changes, whatever changed it.

#include <QTextBrowser>

namespace eMule {

class LogTextView : public QTextBrowser {
    Q_OBJECT

public:
    /// Distance from the bottom that still counts as "at the bottom". MFC uses
    /// the same 20 px (CHTRichEditCtrl::AddLine — srchybrid/HTRichEditCtrl.cpp:273).
    static constexpr int kFollowSlackPx = 20;

    explicit LogTextView(QWidget* parent = nullptr);

    /// MFC m_bAutoScroll: on by default, per pane, not persisted. As there, it
    /// does not gate following new lines — that is decided by the view position
    /// alone — only whether selecting a pane with unseen text jumps to its end.
    [[nodiscard]] bool autoScroll() const { return m_autoScroll; }
    void setAutoScroll(bool on) { m_autoScroll = on; }

    /// True while the view is at (or within kFollowSlackPx of) the bottom, so
    /// new text will be followed.
    [[nodiscard]] bool isFollowing() const { return m_follow; }

    /// Jump to the last line and keep following.
    void scrollToBottom();

    /// Put the view back at @p value and stop following. For panes that rebuild
    /// their whole content: the position is re-applied as the layout catches up.
    void restoreScrollValue(int value);

protected:
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    void onRangeChanged(int min, int max);
    void onValueChanged(int value);
    void setBarValue(int value);

    bool m_autoScroll = true;
    bool m_follow = true;
    bool m_settingValue = false;   ///< our own setValue, not the user's
    int m_pendingValue = -1;       ///< restoreScrollValue target not yet reachable
};

} // namespace eMule
