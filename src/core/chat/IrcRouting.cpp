#include "pch.h"
/// @file IrcRouting.cpp
/// @brief IRC display and input decisions — see IrcRouting.h.

#include "chat/IrcRouting.h"

#include <QDateTime>
#include <QLocale>

namespace eMule::IrcRouting {

LineColor infoLineColor(const QString& line)
{
    if (line.startsWith(u'*'))
        return LineColor::Info;
    if (line.startsWith(u'-') && line.indexOf(u'-', 1) >= 0)
        return LineColor::Notice;
    return LineColor::Default;
}

QString colorName(LineColor color)
{
    switch (color) {
    case LineColor::Info:   return QStringLiteral("#009300");
    case LineColor::Notice: return QStringLiteral("#7F0000");
    case LineColor::Quit:   return QStringLiteral("#00007F");
    case LineColor::Action: return QStringLiteral("#9C009C");
    case LineColor::Default: break;
    }
    return {};
}

NumericRoute numericRoute(int code)
{
    switch (code) {
    case 311: case 312: case 313: case 317: case 318: case 319:   // WHOIS
    case 314: case 369:                                           // WHOWAS
        return NumericRoute::Current;
    default:
        return code >= 400 ? NumericRoute::Error : NumericRoute::Status;
    }
}

QString whoisIdleText(const QString& payload)
{
    // MFC IrcMain.cpp:654-669
    const QStringList parts = payload.split(u' ', Qt::SkipEmptyParts);
    const qint64 idle = parts.value(1).toLongLong();
    const qint64 hours = idle / 3600;
    const qint64 mins = (idle % 3600) / 60;
    const qint64 secs = idle % 60;
    const auto two = [](qint64 v) { return QStringLiteral("%1").arg(v, 2, 10, QLatin1Char('0')); };

    QString text = parts.value(0) + u' ';
    if (hours > 0)
        text += QStringLiteral("%1hrs %2mins %3secs").arg(two(hours), two(mins), two(secs));
    else if (mins > 0)
        text += QStringLiteral("%1mins %2secs").arg(two(mins), two(secs));
    else
        text += QStringLiteral("%1secs").arg(two(secs));
    text += QStringLiteral(" idle");

    if (const qint64 signon = parts.value(2).toLongLong(); signon != 0) {
        text += QStringLiteral(", signed on ")
              + QLocale::c().toString(QDateTime::fromSecsSinceEpoch(signon), QStringLiteral("ddd MMM dd HH:mm:ss"));
    }
    return text;
}

QString errorLine(const QString& line)
{
    return line.startsWith(u'-') ? line : QStringLiteral("-Error- ") + line;
}

NoticeRoute routeNotice(const QString& source, const QString& target, const QString& message,
                        const QString& ownNick, const QString& currentChat,
                        const QStringList& openTabs, const QStringList& tabsWithSource)
{
    NoticeRoute route;
    const auto holds = [](const QStringList& names, const QString& name) {
        return names.contains(name, Qt::CaseInsensitive);
    };

    // To us: shown where the user is reading
    if (target.compare(ownNick, Qt::CaseInsensitive) == 0) {
        route.text = QStringLiteral("-%1- %2").arg(source, message);
        if (currentChat.isEmpty())
            route.status = true;
        else
            route.channels << currentChat;
        return route;
    }

    route.text = QStringLiteral("-%1:%2- %3").arg(source, target, message);
    if (holds(openTabs, target)) {
        route.channels << target;
    } else if (!tabsWithSource.isEmpty()) {
        route.channels = tabsWithSource;
    } else {
        route.text = QStringLiteral("-%1- %2").arg(source, message);
        route.status = true;
    }
    return route;
}

InputResult interpretInput(const QString& input, const InputContext& ctx)
{
    InputResult result;
    if (input.isEmpty())
        return result;

    const auto infoEcho = [&](const QString& text, LineColor color = LineColor::Default) {
        result.echo = text;
        result.echoColor = color == LineColor::Default ? infoLineColor(text) : color;
        result.echoTo = ctx.live ? InputResult::Echo::Tab : InputResult::Echo::Status;
    };

    if (input.startsWith(u'/')) {
        const qsizetype space = input.indexOf(u' ');
        const QString cmd = (space < 0 ? input.mid(1) : input.mid(1, space - 1)).toLower();
        const QString args = space < 0 ? QString() : input.mid(space + 1);
        const QString firstArg = args.section(u' ', 0, 0);
        const QString restArgs = args.section(u' ', 1);

        if (cmd == u"hop") {
            // leave and re-enter the channel being read
            if (ctx.isChannel && !ctx.tabName.isEmpty())
                result.raw << QStringLiteral("PART ") + ctx.tabName << QStringLiteral("JOIN ") + ctx.tabName;
            return result;
        }
        if (cmd == u"me") {
            if (!ctx.live || args.isEmpty())
                return result;
            result.raw << QStringLiteral("PRIVMSG %1 :\001ACTION %2\001").arg(ctx.tabName, args);
            infoEcho(QStringLiteral("* %1 %2").arg(ctx.ownNick, args), LineColor::Action);
            return result;
        }
        if (cmd == u"sound") {
            if (!ctx.live || firstArg.isEmpty())
                return result;
            result.raw << QStringLiteral("PRIVMSG %1 :\001SOUND %2\001").arg(ctx.tabName, args);
            result.sound = QString(firstArg).remove(u'\\').remove(u'/');
            infoEcho(QStringLiteral("* %1 %2").arg(ctx.ownNick, restArgs.isEmpty() ? QStringLiteral("[SOUND]") : restArgs));
            return result;
        }
        if (cmd == u"msg" || cmd == u"notice") {
            if (firstArg.isEmpty() || restArgs.isEmpty())
                return result;
            result.raw << QStringLiteral("%1 %2 :%3").arg(cmd == u"msg" ? QStringLiteral("PRIVMSG") : QStringLiteral("NOTICE"),
                                                         firstArg, restArgs);
            infoEcho(QStringLiteral(" -> *%1* %2").arg(firstArg, restArgs));
            return result;
        }
        if (cmd == u"privmsg") {
            if (firstArg.compare(u"nickserv", Qt::CaseInsensitive) == 0)
                result.raw << QStringLiteral("ns ") + restArgs;
            else if (firstArg.compare(u"chanserv", Qt::CaseInsensitive) == 0)
                result.raw << QStringLiteral("cs ") + restArgs;
            else if (!firstArg.isEmpty())
                result.raw << QStringLiteral("PRIVMSG %1 :%2").arg(firstArg, restArgs);
            return result;
        }
        if (cmd == u"part" || cmd == u"leave") {
            const QString channel = args.trimmed().isEmpty() ? (ctx.isChannel ? ctx.tabName : QString())
                                                             : args.trimmed();
            if (!channel.isEmpty())
                result.raw << QStringLiteral("PART ") + channel;
            return result;
        }
        if (cmd == u"topic") {
            // "/topic #chan text" names the channel; without one it is the tab's
            if (firstArg.startsWith(u'#'))
                result.raw << (restArgs.isEmpty() ? QStringLiteral("TOPIC ") + firstArg
                                                  : QStringLiteral("TOPIC %1 :%2").arg(firstArg, restArgs));
            else if (ctx.isChannel && !ctx.tabName.isEmpty())
                result.raw << (args.isEmpty() ? QStringLiteral("TOPIC ") + ctx.tabName
                                              : QStringLiteral("TOPIC %1 :%2").arg(ctx.tabName, args));
            return result;
        }
        if (cmd == u"nick") {
            if (!firstArg.isEmpty()) {
                result.raw << QStringLiteral("NICK ") + firstArg;
                result.newNick = firstArg;
            }
            return result;
        }
        // anything else goes to the server as typed, without the slash
        result.raw << (args.isEmpty() ? cmd.toUpper() : cmd.toUpper() + u' ' + args);
        return result;
    }

    // Plain text. Outside a live chat it is a raw command line.
    if (!ctx.live) {
        result.raw << input;
        return result;
    }
    result.raw << QStringLiteral("PRIVMSG %1 :%2").arg(ctx.tabName, input);
    result.echo = input;
    result.ownMessage = true;
    result.echoTo = InputResult::Echo::Tab;
    return result;
}

QString stripMircCodes(const QString& text)
{
    QString out;
    out.reserve(text.size());
    for (qsizetype i = 0; i < text.size(); ++i) {
        const char16_t c = text.at(i).unicode();
        switch (c) {
        case 0x02:   // bold
        case 0x0F:   // reset
        case 0x16:   // reverse
        case 0x1D:   // italic
        case 0x1F:   // underline
            break;
        case 0x03: { // colour: up to two digits, optionally ",bg" with up to two more
            int digits = 0;
            while (digits < 2 && i + 1 < text.size() && text.at(i + 1).isDigit()) {
                ++i;
                ++digits;
            }
            if (digits > 0 && i + 2 < text.size() && text.at(i + 1) == u',' && text.at(i + 2).isDigit()) {
                i += 2;
                if (i + 1 < text.size() && text.at(i + 1).isDigit())
                    ++i;
            }
            break;
        }
        default:
            out += text.at(i);
        }
    }
    return out;
}

QString soundFileName(const QString& ctcpParams)
{
    QString name = ctcpParams.section(u' ', 0, 0);
    name.remove(u'\001').remove(u'\\').remove(u'/');
    name = name.toLower();
    if (name.size() <= 4 || !(name.endsWith(u".wav") || name.endsWith(u".mp3")))
        return {};
    return name;
}

} // namespace eMule::IrcRouting
