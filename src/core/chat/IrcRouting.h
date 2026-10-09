#pragma once

/// @file IrcRouting.h
/// @brief Where an IRC line is shown and how it is coloured — the decisions of MFC
///        CIrcWnd (AddStatus, AddCurrent, AddInfoMessage, NoticeMessage) and
///        CIrcChannelTabCtrl::ChatSend, without the window.

#include <QString>
#include <QStringList>

namespace eMule::IrcRouting {

// ---------------------------------------------------------------------------
// Colours (MFC IrcWnd.cpp:40-42, 599-610)
// ---------------------------------------------------------------------------

enum class LineColor {
    Default,
    Info,     ///< a line starting with '*': dark green (0,147,0)
    Notice,   ///< "-source- text" and errors: dark red (127,0,0)
    Quit,     ///< quits and "* Disconnected": blue (0,0,127)
    Action    ///< "/me": purple (156,0,156)
};

/// The colour AddInfoMessage picks from the text alone.
[[nodiscard]] LineColor infoLineColor(const QString& line);
/// "#RRGGBB", empty for Default.
[[nodiscard]] QString colorName(LineColor color);

// ---------------------------------------------------------------------------
// Server numerics
// ---------------------------------------------------------------------------

enum class NumericRoute {
    Status,    ///< the Status tab
    Current,   ///< whois replies: the tab being read, or Status when that is no chat
    Error      ///< 400 and up: Status with "-Error- ", repeated in the tab being read
};

[[nodiscard]] NumericRoute numericRoute(int code);

/// RPL_WHOISIDLE as MFC words it: "nick 5mins 03secs idle, signed on Mon Oct 05 12:00:00".
/// @param payload the numeric's parameters after our own nick: "<nick> <idle> [<signon>] ..."
[[nodiscard]] QString whoisIdleText(const QString& payload);

/// The text of an error line: "-Error- <line>", or the line as it is when it already
/// starts with '-'.
[[nodiscard]] QString errorLine(const QString& line);

// ---------------------------------------------------------------------------
// Notices (MFC CIrcWnd::NoticeMessage, IrcWnd.cpp:913-942)
// ---------------------------------------------------------------------------

struct NoticeRoute {
    QStringList channels;   ///< tabs (by name) that show it
    bool status = false;    ///< or the Status tab
    QString text;           ///< "-source- message" or "-source:target- message"
};

/// @param currentChat      name of the tab being read when it is a live channel or
///                         private chat; empty otherwise
/// @param openTabs         names of all channel and private tabs
/// @param tabsWithSource   names of the channels whose nick list holds @p source
[[nodiscard]] NoticeRoute routeNotice(const QString& source, const QString& target, const QString& message,
                                      const QString& ownNick, const QString& currentChat,
                                      const QStringList& openTabs, const QStringList& tabsWithSource);

// ---------------------------------------------------------------------------
// Input (MFC CIrcChannelTabCtrl::ChatSend, IrcChannelTabCtrl.cpp:531-628)
// ---------------------------------------------------------------------------

struct InputContext {
    QString tabName;         ///< channel or nick of the current tab; empty on Status / Channels
    bool isChannel = false;  ///< a channel (not a private chat)
    bool live = false;       ///< a channel or private chat that is not detached
    QString ownNick;
};

struct InputResult {
    QStringList raw;         ///< lines to send, in order
    enum class Echo { None, Tab, Status } echoTo = Echo::None;
    /// What to show: an info line ("* nick ...", " -> *nick* ..."), or — with
    /// @p ownMessage — the text of our own message, shown as "<nick> text".
    QString echo;
    bool ownMessage = false;
    LineColor echoColor = LineColor::Default;
    QString sound;           ///< file to play (name only), from "/sound"
    QString newNick;         ///< "/nick": asked for; stored once the server confirms
};

[[nodiscard]] InputResult interpretInput(const QString& input, const InputContext& ctx);

// ---------------------------------------------------------------------------
// Text helpers
// ---------------------------------------------------------------------------

/// Remove mIRC formatting (bold, colour with its digits, underline, ...), as the
/// channel list shows topics (MFC StripMessageOfFontCodes, IrcWnd.cpp:944-983).
[[nodiscard]] QString stripMircCodes(const QString& text);

/// The file a CTCP SOUND names, made safe: no path separators, lower case, and only
/// .wav / .mp3 (MFC IrcMain.cpp:214-225). Empty when it is not acceptable.
[[nodiscard]] QString soundFileName(const QString& ctcpParams);

} // namespace eMule::IrcRouting
