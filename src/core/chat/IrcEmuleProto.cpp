#include "pch.h"
/// @file IrcEmuleProto.cpp
/// @brief eMule's CTCP extensions on IRC — implementation.

#include "chat/IrcEmuleProto.h"

#include <QStringList>

namespace eMule::IrcEmuleProto {

namespace {

constexpr QLatin1String kRequest("RQSFRIEND");
constexpr QLatin1String kReply("REPFRIEND");
constexpr QLatin1String kLink("SENDLINK");

bool isHexHash(const QString& s)
{
    if (s.size() != 32)
        return false;
    for (const QChar c : s)
        if (!c.isDigit() && (c.toLower() < u'a' || c.toLower() > u'f'))
            return false;
    return true;
}

} // namespace

bool isEmuleProto(const QString& body)
{
    return body.startsWith(kRequest, Qt::CaseInsensitive)
        || body.startsWith(kReply, Qt::CaseInsensitive)
        || body.startsWith(kLink, Qt::CaseInsensitive);
}

QString friendRequest(uint32 verify)
{
    return QStringLiteral("RQSFRIEND|%1|").arg(verify);
}

QString friendReply(const QString& version, const QString& verify,
                    uint32 clientId, uint16 port,
                    uint32 serverIp, uint16 serverPort, const QString& userHashHex)
{
    // MFC prints the server address as a number, "0.0.0.0" when on no server
    const QString server = serverIp != 0 ? QString::number(serverIp) : QStringLiteral("0.0.0.0");
    return QStringLiteral("REPFRIEND eMule%1|%2|%3:%4|%5:%6|%7|")
        .arg(version, verify)
        .arg(clientId).arg(port)
        .arg(server).arg(serverIp != 0 ? serverPort : 0)
        .arg(userHashHex);
}

QString sendLink(const QString& userHashHex, const QString& link)
{
    return QStringLiteral("SENDLINK|%1|%2").arg(userHashHex, link);
}

std::optional<QString> parseFriendRequest(const QString& body)
{
    if (!body.startsWith(kRequest, Qt::CaseInsensitive))
        return std::nullopt;
    // "RQSFRIEND|<verify>|"
    const QStringList parts = body.mid(kRequest.size()).split(u'|');
    if (parts.size() < 2 || parts[1].isEmpty())
        return std::nullopt;
    return parts[1];
}

std::optional<FriendReply> parseFriendReply(const QString& body)
{
    if (!body.startsWith(kReply, Qt::CaseInsensitive))
        return std::nullopt;
    // "REPFRIEND <version>|<verify>|<id>:<port>|<serverIp>:<serverPort>|<hash>|"
    const QStringList parts = body.mid(kReply.size()).split(u'|');
    if (parts.size() < 5)
        return std::nullopt;

    FriendReply reply;
    bool ok = false;
    reply.verify = parts[1].toUInt(&ok);
    if (!ok)
        return std::nullopt;
    const QStringList client = parts[2].split(u':');
    if (client.size() != 2)
        return std::nullopt;
    reply.clientId = client[0].toUInt();
    reply.port = client[1].toUShort();
    reply.userHashHex = parts[4];
    if (!isHexHash(reply.userHashHex))
        return std::nullopt;
    return reply;
}

std::optional<Link> parseSendLink(const QString& body)
{
    if (!body.startsWith(kLink, Qt::CaseInsensitive))
        return std::nullopt;
    // "SENDLINK|<hash>|<link>" — the link has bars of its own
    const QString rest = body.mid(kLink.size());
    if (!rest.startsWith(u'|'))
        return std::nullopt;
    const qsizetype bar = rest.indexOf(u'|', 1);
    if (bar < 0)
        return std::nullopt;
    Link out;
    out.userHashHex = rest.mid(1, bar - 1);
    out.link = rest.mid(bar + 1).trimmed();
    if (out.link.isEmpty())
        return std::nullopt;
    return out;
}

} // namespace eMule::IrcEmuleProto
