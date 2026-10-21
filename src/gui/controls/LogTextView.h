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
///
/// When not following, the view is anchored to the line at its top rather than a
/// pixel offset, so trimming the oldest lines or a late line sorted in above the
/// view does not slide the text (issue #24; MFC restores the raw scroll position
/// there and drifts once its buffer is full).

#include <QTextBrowser>
#include <QTextCursor>

namespace eMule {

class LogTextView : public QTextBrowser {
    Q_OBJECT

public:
    /// Distance from the bottom that still counts as "at the bottom". MFC uses
    /// the same 20 px (CHTRichEditCtrl::AddLine — srchybrid/HTRichEditCtrl.cpp:273).
    static constexpr int kFollowSlackPx = 20;

    explicit LogTextView(QWidget* parent = nullptr);

    /// MFC m_bAutoScroll: on by default, per pane, not persisted. Off, new lines
    /// never move the view, even at the bottom (MFC lets the position override it,
    /// so its toggle did nothing — issue #14). Also gates whether selecting a pane
    /// with unseen text jumps to its end.
    [[nodiscard]] bool autoScroll() const { return m_autoScroll; }
    void setAutoScroll(bool on);

    /// True while the view is at (or within kFollowSlackPx of) the bottom, so
    /// new text will be followed while autoScroll() is on.
    [[nodiscard]] bool isFollowing() const { return m_follow; }

    /// Jump to the last line and keep following.
    void scrollToBottom();

    /// Drop the oldest lines down to @p maxBlocks; returns how many went. A view
    /// that is not following keeps the line at its top and everything below it,
    /// up to kHeldCapFactor x @p maxBlocks — trimming that line would leave the
    /// view on the oldest line, where each trim slides the text up (issue #40).
    int trimTo(int maxBlocks);

    /// Ceiling for trimTo() while the view holds its position.
    static constexpr int kHeldCapFactor = 4;

    /// Put the view back at @p value and stop following. For panes that rebuild
    /// their whole content: the position is re-applied as the layout catches up.
    void restoreScrollValue(int value);

protected:
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    void onRangeChanged(int min, int max);
    void onValueChanged(int value);
    void setBarValue(int value);
    void captureAnchor();
    [[nodiscard]] int anchoredValue() const;

    bool m_autoScroll = true;
    bool m_follow = true;
    bool m_settingValue = false;   ///< our own setValue, not the user's
    bool m_holding = false;        ///< anchor held against append() while Autoscroll is off
    int m_pendingValue = -1;       ///< restoreScrollValue target not yet reachable
    QTextCursor m_anchor;          ///< start of the top visible block; moves with edits
    int m_anchorOffset = 0;        ///< view top minus that block's top, px
};

} // namespace eMule
