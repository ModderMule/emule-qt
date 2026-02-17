#pragma once

/// @file IrcMessage.h
/// @brief Parsed IRC message — RFC 2812 compliant.
///
/// Parses raw IRC protocol lines into structured fields:
/// prefix (nick!user@host or servername), command, and params.

#include <QString>
#include <QStringList>

namespace eMule {

// ---------------------------------------------------------------------------
// IrcMessage — one parsed IRC protocol line
// ---------------------------------------------------------------------------

struct IrcMessage {
    QString prefix;     // Raw prefix (without leading ':')
    QString nickname;   // Parsed from prefix: nick!user@host → nick
    QString user;       // Parsed from prefix: nick!user@host → user
    QString host;       // Parsed from prefix: nick!user@host → host
    QString command;    // Command string (e.g. "PRIVMSG", "001", "PING")
    QStringList params; // Parameters (trailing after ':' is last param)

    [[nodiscard]] bool isNumeric() const
    {
        return command.size() == 3
            && command[0].isDigit()
            && command[1].isDigit()
            && command[2].isDigit();
    }

    [[nodiscard]] int numericCode() const
    {
        return isNumeric() ? command.toInt() : -1;
    }

    [[nodiscard]] bool isValid() const { return !command.isEmpty(); }

    /// Parse a raw IRC protocol line (without trailing \r\n).
    /// Format per RFC 2812:
    ///   [":" prefix SPACE] command [ params ] crlf
    [[nodiscard]] static IrcMessage parse(const QString& rawLine);
};

// ---------------------------------------------------------------------------
// Implementation (inline for header-only convenience)
// ---------------------------------------------------------------------------

inline IrcMessage IrcMessage::parse(const QString& rawLine)
{
    IrcMessage msg;
    if (rawLine.isEmpty())
        return msg;

    qsizetype pos = 0;

    // 1. Parse optional prefix
    if (rawLine[0] == u':') {
        const auto spaceIdx = rawLine.indexOf(u' ', 1);
        if (spaceIdx < 0)
            return msg; // malformed
        msg.prefix = rawLine.mid(1, spaceIdx - 1);
        pos = spaceIdx + 1;

        // Decompose prefix: nick!user@host
        const auto bangIdx = msg.prefix.indexOf(u'!');
        if (bangIdx >= 0) {
            msg.nickname = msg.prefix.left(bangIdx);
            const auto atIdx = msg.prefix.indexOf(u'@', bangIdx + 1);
            if (atIdx >= 0) {
                msg.user = msg.prefix.mid(bangIdx + 1, atIdx - bangIdx - 1);
                msg.host = msg.prefix.mid(atIdx + 1);
            }
        }
        // If no '!', prefix is a servername — leave nickname/user/host empty
    }

    // Skip leading spaces
    while (pos < rawLine.size() && rawLine[pos] == u' ')
        ++pos;

    // 2. Parse command
    {
        const auto spaceIdx = rawLine.indexOf(u' ', pos);
        if (spaceIdx < 0) {
            msg.command = rawLine.mid(pos);
            return msg; // command only, no params
        }
        msg.command = rawLine.mid(pos, spaceIdx - pos);
        pos = spaceIdx + 1;
    }

    // 3. Parse params
    while (pos < rawLine.size()) {
        // Skip spaces
        while (pos < rawLine.size() && rawLine[pos] == u' ')
            ++pos;

        if (pos >= rawLine.size())
            break;

        if (rawLine[pos] == u':') {
            // Trailing parameter: everything after ':' is one param
            msg.params.append(rawLine.mid(pos + 1));
            break;
        }

        // Normal parameter (space-delimited)
        const auto spaceIdx = rawLine.indexOf(u' ', pos);
        if (spaceIdx < 0) {
            msg.params.append(rawLine.mid(pos));
            break;
        }
        msg.params.append(rawLine.mid(pos, spaceIdx - pos));
        pos = spaceIdx + 1;
    }

    return msg;
}

} // namespace eMule
