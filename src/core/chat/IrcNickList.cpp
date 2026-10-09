#include "pch.h"
/// @file IrcNickList.cpp
/// @brief Channel nick list and MODE parsing — see IrcNickList.h.

#include "chat/IrcNickList.h"

#include <algorithm>

namespace eMule {

void IrcServerModes::applyIsupport(const QString& line)
{
    for (const QString& token : line.split(u' ', Qt::SkipEmptyParts)) {
        if (token.startsWith(u"PREFIX=(")) {
            // PREFIX=(modes)symbols
            const qsizetype close = token.indexOf(u')');
            if (close < 0)
                continue;
            const QString modes = token.mid(8, close - 8);
            const QString symbols = token.mid(close + 1);
            if (!modes.isEmpty() && modes.size() == symbols.size()) {
                userModes = modes;
                userSymbols = symbols;
            }
        } else if (token.startsWith(u"CHANMODES=")) {
            const QStringList types = token.mid(10).split(u',');
            chanModesA = types.value(0);
            chanModesB = types.value(1);
            chanModesC = types.value(2);
            chanModesD = types.value(3);
        }
    }
}

QList<IrcModeChange> parseModeChange(const QString& modes, const QStringList& params,
                                     const IrcServerModes& server)
{
    QList<IrcModeChange> changes;
    bool on = true;
    qsizetype next = 0;
    for (const QChar c : modes) {
        if (c == u'+' || c == u'-') {
            on = c == u'+';
            continue;
        }
        IrcModeChange change;
        change.mode = c;
        change.on = on;
        change.userMode = server.userModes.contains(c);
        const bool takesParam = change.userMode || server.chanModesA.contains(c)
                             || server.chanModesB.contains(c)
                             || (on && server.chanModesC.contains(c));
        if (takesParam)
            change.param = params.value(next++);
        changes.append(change);
    }
    return changes;
}

QString IrcNickList::bare(const QString& shown, const IrcServerModes& server)
{
    qsizetype i = 0;
    while (i < shown.size() && server.userSymbols.contains(shown.at(i)))
        ++i;
    return shown.mid(i);
}

bool IrcNickList::add(const QString& rawNick, const IrcServerModes& server)
{
    IrcNick entry;
    entry.nick = bare(rawNick, server);
    if (entry.nick.isEmpty() || contains(entry.nick))
        return false;
    entry.symbols = rawNick.left(rawNick.size() - entry.nick.size());
    entry.level = entry.symbols.isEmpty() ? -1 : static_cast<int>(server.userSymbols.indexOf(entry.symbols.at(0)));
    m_nicks.append(entry);
    return true;
}

bool IrcNickList::remove(const QString& nick)
{
    for (qsizetype i = 0; i < m_nicks.size(); ++i) {
        if (m_nicks.at(i).nick.compare(nick, Qt::CaseInsensitive) == 0) {
            m_nicks.removeAt(i);
            return true;
        }
    }
    return false;
}

bool IrcNickList::rename(const QString& oldNick, const QString& newNick)
{
    IrcNick* entry = findMutable(oldNick);
    if (!entry || newNick.isEmpty())
        return false;
    entry->nick = newNick;
    return true;
}

bool IrcNickList::changeMode(const QString& nick, QChar mode, bool on, const IrcServerModes& server)
{
    const qsizetype at = server.userModes.indexOf(mode);
    IrcNick* entry = findMutable(nick);
    if (at < 0 || !entry)
        return false;
    const QChar symbol = server.userSymbols.at(at);

    // Rebuilt in the server's order, so "@+" never becomes "+@"
    QString symbols;
    for (const QChar s : server.userSymbols) {
        const bool had = entry->symbols.contains(s);
        if (s == symbol ? on : had)
            symbols += s;
    }
    entry->symbols = symbols;
    entry->level = symbols.isEmpty() ? -1 : static_cast<int>(server.userSymbols.indexOf(symbols.at(0)));
    return true;
}

const IrcNick* IrcNickList::find(const QString& nick) const
{
    for (const IrcNick& entry : m_nicks) {
        if (entry.nick.compare(nick, Qt::CaseInsensitive) == 0)
            return &entry;
    }
    return nullptr;
}

IrcNick* IrcNickList::findMutable(const QString& nick)
{
    return const_cast<IrcNick*>(std::as_const(*this).find(nick));
}

QStringList IrcNickList::display() const
{
    QList<IrcNick> sorted = m_nicks;
    std::sort(sorted.begin(), sorted.end(), [](const IrcNick& a, const IrcNick& b) {
        // level -1 (no symbol) goes last; among symbols the smaller index is the higher rank
        const int la = a.level < 0 ? INT_MAX : a.level;
        const int lb = b.level < 0 ? INT_MAX : b.level;
        if (la != lb)
            return la < lb;
        return a.nick.compare(b.nick, Qt::CaseInsensitive) < 0;
    });
    QStringList out;
    out.reserve(sorted.size());
    for (const IrcNick& entry : std::as_const(sorted))
        out << entry.symbols + entry.nick;
    return out;
}

QString IrcNickList::decorated(const QString& nick) const
{
    const IrcNick* entry = find(nick);
    return entry ? entry->symbols + entry->nick : nick;
}

QStringList IrcNickList::bareNicks() const
{
    QStringList out;
    out.reserve(m_nicks.size());
    for (const IrcNick& entry : m_nicks)
        out << entry.nick;
    return out;
}

} // namespace eMule
