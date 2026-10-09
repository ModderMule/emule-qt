#pragma once

/// @file IrcNickList.h
/// @brief A channel's nick list and the mode changes that move nicks in it —
///        MFC CIrcNickListCtrl (NewNick, RemoveNick, ChangeNick, ChangeNickMode)
///        and CIrcWnd::ParseChangeMode, without the list control.

#include <QList>
#include <QString>
#include <QStringList>

namespace eMule {

/// What the server said about its modes in RPL_ISUPPORT (005):
/// "PREFIX=(qaohv)~&@%+" and "CHANMODES=A,B,C,D" (MFC IrcMain.cpp:525-565).
struct IrcServerModes {
    QString userModes = QStringLiteral("ov");     ///< letters, most powerful first
    QString userSymbols = QStringLiteral("@+");   ///< their symbols, same order
    QString chanModesA;   ///< lists (ban...): always a parameter
    QString chanModesB;   ///< always a parameter
    QString chanModesC;   ///< a parameter only when being set
    QString chanModesD;   ///< never a parameter

    /// Take PREFIX and CHANMODES out of one 005 line; other tokens are ignored.
    void applyIsupport(const QString& line);
};

struct IrcNick {
    QString nick;      ///< bare, without mode symbols
    QString symbols;   ///< e.g. "@+", in the server's order
    int level = -1;    ///< index of the first symbol in the server's list; -1 none
};

/// One step of a MODE line.
struct IrcModeChange {
    QChar mode;
    bool on = true;
    QString param;          ///< the nick for a user mode
    bool userMode = false;  ///< gives or takes a symbol in the nick list
};

/// Split "+ov-b alice bob *!*@x" into its steps, each with the parameter it consumes
/// (MFC CIrcWnd::ParseChangeMode, IrcWnd.cpp:1039-1097).
[[nodiscard]] QList<IrcModeChange> parseModeChange(const QString& modes, const QStringList& params,
                                                   const IrcServerModes& server);

class IrcNickList {
public:
    /// Add a nick as the server names it ("@alice"): leading mode symbols are split
    /// off. False when it is already listed (MFC NewNick refuses duplicates).
    bool add(const QString& rawNick, const IrcServerModes& server);
    bool remove(const QString& nick);
    bool rename(const QString& oldNick, const QString& newNick);
    /// Give or take the symbol of @p mode. False when the nick is unknown or the
    /// mode has no symbol.
    bool changeMode(const QString& nick, QChar mode, bool on, const IrcServerModes& server);
    void clear() { m_nicks.clear(); }

    [[nodiscard]] bool contains(const QString& nick) const { return find(nick) != nullptr; }
    [[nodiscard]] const IrcNick* find(const QString& nick) const;
    [[nodiscard]] qsizetype size() const { return m_nicks.size(); }
    [[nodiscard]] bool isEmpty() const { return m_nicks.isEmpty(); }

    /// "@alice", "+bob", "carol": by rank, then by name without regard to case
    /// (MFC SortProc, IrcNickListCtrl.cpp:57-73). Kept sorted here at all times;
    /// MFC sorts on a header click only — deliberate, 2026-10.
    [[nodiscard]] QStringList display() const;
    /// The nick with its symbols, for "<@alice>" in the log; the bare nick when unknown.
    [[nodiscard]] QString decorated(const QString& nick) const;
    [[nodiscard]] QStringList bareNicks() const;

    /// Strip the leading mode symbols off a list entry.
    [[nodiscard]] static QString bare(const QString& shown, const IrcServerModes& server);

private:
    [[nodiscard]] IrcNick* findMutable(const QString& nick);
    QList<IrcNick> m_nicks;
};

} // namespace eMule
