#pragma once

/// @file IrcEmuleProto.h
/// @brief eMule's CTCP extensions on IRC: add-as-friend and send-link.
///
/// Port of the RQSFRIEND / REPFRIEND / SENDLINK handling in MFC CIrcMain
/// (srchybrid/IrcMain.cpp:267-342) and CIrcNickListCtrl (IrcNickListCtrl.cpp:409-423).
/// The bodies here are what goes between the \001 marks of a PRIVMSG.

#include "utils/Types.h"

#include <QString>

#include <optional>

namespace eMule::IrcEmuleProto {

/// True when @p body is one of the three messages (case-insensitive, as MFC).
[[nodiscard]] bool isEmuleProto(const QString& body);

// -- Building ----------------------------------------------------------------

/// "RQSFRIEND|<verify>|" — ask a nick for what is needed to add it as a friend.
[[nodiscard]] QString friendRequest(uint32 verify);

/// The answer to a request. @p clientId is 0 when we are firewalled; @p serverIp is
/// the server's address as a number, or 0 when not on a server.
[[nodiscard]] QString friendReply(const QString& version, const QString& verify,
                                  uint32 clientId, uint16 port,
                                  uint32 serverIp, uint16 serverPort,
                                  const QString& userHashHex);

/// "SENDLINK|<own user hash>|<link>".
[[nodiscard]] QString sendLink(const QString& userHashHex, const QString& link);

// -- Parsing -----------------------------------------------------------------

/// The verify token of a request, echoed back unchanged.
[[nodiscard]] std::optional<QString> parseFriendRequest(const QString& body);

struct FriendReply {
    uint32  verify = 0;
    uint32  clientId = 0;
    uint16  port = 0;
    QString userHashHex;    ///< 32 hex digits
};
/// Empty when the reply is malformed or its hash is not a user hash.
[[nodiscard]] std::optional<FriendReply> parseFriendReply(const QString& body);

struct Link {
    QString userHashHex;    ///< the sender's, for the friends-only check
    QString link;
};
[[nodiscard]] std::optional<Link> parseSendLink(const QString& body);

} // namespace eMule::IrcEmuleProto
