#include "nntp/NntpError.h"

#include <QCoreApplication>

namespace eMule::usenet {

QString describeNntpError(NntpError e)
{
    switch (e) {
    case NntpError::None:              return QCoreApplication::translate("Usenet", "No error");
    case NntpError::ConnectFailed:     return QCoreApplication::translate("Usenet", "Connection failed");
    case NntpError::TlsFailed:         return QCoreApplication::translate("Usenet", "TLS handshake failed");
    case NntpError::Timeout:           return QCoreApplication::translate("Usenet", "Server did not respond");
    case NntpError::Disconnected:      return QCoreApplication::translate("Usenet", "Server closed the connection");
    case NntpError::ServerUnavailable: return QCoreApplication::translate("Usenet", "Server unavailable");
    case NntpError::AuthFailed:        return QCoreApplication::translate("Usenet", "Authentication failed");
    case NntpError::ArticleNotFound:   return QCoreApplication::translate("Usenet", "Article not found");
    case NntpError::GroupNotFound:     return QCoreApplication::translate("Usenet", "Newsgroup not found");
    case NntpError::ArticleCorrupt:    return QCoreApplication::translate("Usenet", "Damaged article");
    case NntpError::ProtocolError:     return QCoreApplication::translate("Usenet", "Protocol error");
    }
    return QCoreApplication::translate("Usenet", "Unknown error");
}

} // namespace eMule::usenet
